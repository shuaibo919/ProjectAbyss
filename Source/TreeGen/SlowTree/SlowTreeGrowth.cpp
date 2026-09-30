#include "SlowTreeGrowth.h"

#include "CylinderSegment.h"
#include "NodeGraph.h"
#include "Nodes.h"
#include "SlowTreeFoliage.h"
#include "SlowTreeMeshData.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <unordered_map>

using namespace godot;

namespace
{
constexpr float Pi = 3.14159265359f;
constexpr float Tau = 2.0f * Pi;
constexpr float MinimumWoodRadius = 0.006f;

uint32_t Mix(uint32_t Value)
{
	Value ^= Value >> 16;
	Value *= 0x7feb352du;
	Value ^= Value >> 15;
	Value *= 0x846ca68bu;
	return Value ^ (Value >> 16);
}

float Unit(uint32_t Seed)
{
	return float(Mix(Seed) & 0xffffffu) / 16777216.0f;
}
float Signed(uint32_t Seed)
{
	return Unit(Seed) * 2.0f - 1.0f;
}
float Smooth(float Start, float End, float Value)
{
	const float T = std::clamp((Value - Start) / std::max(0.0001f, End - Start), 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

Vector3 SafeNormal(const Vector3& Value, const Vector3& Fallback = Vector3(0, 1, 0))
{
	return Value.length_squared() > 0.0000001f ? Value.normalized() : Fallback;
}

Vector3 Perpendicular(const Vector3& Direction)
{
	return SafeNormal(Direction.cross(std::abs(Direction.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(0, 0, 1)));
}

float Profile(const std::array<float, 5>& Values, float Position)
{
	const float Coordinate = std::clamp(Position, 0.0f, 1.0f) * 4.0f;
	const size_t Index = std::min(size_t(Coordinate), size_t(3));
	const float Blend = Smooth(0.0f, 1.0f, Coordinate - float(Index));
	return Values[Index] + (Values[Index + 1] - Values[Index]) * Blend;
}

enum class EGrowthForm : int32_t
{
	Broadleaf,
	Willow,
	Pine,
	Ginkgo,
	Bamboo,
	Metasequoia,
	Peach
};

enum class EStemRole : int32_t
{
	Leader,
	Lateral,
	Fork,
	ShortShoot,
	Pendant,
	Root,
	Shoot
};

struct Stem
{
	int32_t Parent = -1;
	int32_t Attachment = 0;
	int32_t Order = 0;
	int32_t ForkIndex = -1;
	EStemRole Role = EStemRole::Lateral;
	uint32_t Seed = 0;
	float Length = 0.0f;
	float Height = 0.0f;
	Vector3 ForkDirection;
	std::vector<BranchRing> Rings;
	// Only culms use unequal internode lengths. Other species retain their uniform sampling.
	std::vector<float> ArcLengths;
	std::vector<float> Demand;
};

struct MeshStem
{
	int32_t Sides = 6;
	bool bVisible = false;
	std::vector<BranchRing> Rings;
	std::vector<float> ArcLengths;
	std::vector<uint32_t> Vertices;
	std::vector<uint8_t> Holes;
	std::vector<uint32_t> Port;
};

/** A connected indexed surface. Normals are accumulated across branch junctions. */
struct WoodSurface
{
	bool bHasColors = false;
	bool bHasTangents = false;
	bool bHasJoinWeights = false;
	bool bAccumulateNormals = true;
	std::vector<Vector3> Tangents;
	std::vector<Vector3> Positions;
	std::vector<Vector3> Normals;
	std::vector<Vector2> UVs;
	std::vector<Vector3> Colors;
	std::vector<uint32_t> Indices;
	std::vector<float> JoinWeights;

	uint32_t AddVertex(const Vector3& Position, const Vector2& UV)
	{
		const uint32_t Index = uint32_t(Positions.size());
		Positions.push_back(Position);
		Normals.push_back(Vector3());
		UVs.push_back(UV);
		if (bHasJoinWeights)
		{
			JoinWeights.push_back(0.0f);
		}
		if (bHasTangents)
		{
			Tangents.push_back(Vector3(1, 0, 0));
		}
		if (bHasColors)
		{
			Colors.push_back(Vector3(1, 1, 1));
		}
		return Index;
	}

	void AddTriangle(uint32_t A, uint32_t B, uint32_t C)
	{
		const Vector3 Normal = (Positions[B] - Positions[A]).cross(Positions[C] - Positions[A]);
		if (Normal.length_squared() < 1e-18f)
		{
			return;
		}
		// The construction loops are counterclockwise; Godot front faces are clockwise.
		Indices.insert(Indices.end(), {A, C, B});
		if (!bAccumulateNormals)
		{
			return;
		}
		const Vector3 UnitNormal = Normal.normalized();
		// Angle weighting prevents a long parent face from dominating a small junction face.
		for (const std::array<uint32_t, 3>& Corner : {std::array<uint32_t, 3>{A, B, C}, {B, C, A}, {C, A, B}})
		{
			const Vector3 First = SafeNormal(Positions[Corner[1]] - Positions[Corner[0]]);
			const Vector3 Second = SafeNormal(Positions[Corner[2]] - Positions[Corner[0]]);
			Normals[Corner[0]] += UnitNormal * std::acos(std::clamp(First.dot(Second), -1.0f, 1.0f));
		}
	}

	void Bridge(const std::vector<uint32_t>& Lower, const std::vector<uint32_t>& Upper)
	{
		// Zipper two complete loops without introducing a T-junction when tessellation differs.
		size_t L = 0;
		size_t U = 0;
		while (L < Lower.size() || U < Upper.size())
		{
			if (U == Upper.size() || (L < Lower.size() && (L + 1) * Upper.size() <= (U + 1) * Lower.size()))
			{
				AddTriangle(Lower[L % Lower.size()], Lower[(L + 1) % Lower.size()], Upper[U % Upper.size()]);
				++L;
			}
			else
			{
				AddTriangle(Lower[L % Lower.size()], Upper[(U + 1) % Upper.size()], Upper[U % Upper.size()]);
				++U;
			}
		}
	}

	/** Refine shared edges once, then relax only the weighted collar region. */
	bool RefineJunctions(const std::atomic<bool>* Cancelled)
	{
		const auto IsCancelled = [Cancelled]()
		{
			return Cancelled && Cancelled->load(std::memory_order_relaxed);
		};
		const auto EdgeKey = [](uint32_t A, uint32_t B)
		{
			return (uint64_t(std::min(A, B)) << 32) | std::max(A, B);
		};
		std::unordered_map<uint64_t, uint32_t> Midpoints;
		for (size_t Face = 0; Face < Indices.size(); Face += 3)
		{
			if (Face % 3072 == 0 && IsCancelled())
			{
				return false;
			}
			for (size_t Corner = 0; Corner < 3; ++Corner)
			{
				const uint32_t A = Indices[Face + Corner];
				const uint32_t B = Indices[Face + (Corner + 1) % 3];
				const uint64_t Key = EdgeKey(A, B);
				if (JoinWeights[A] + JoinWeights[B] <= 0.0f || Midpoints.find(Key) != Midpoints.end())
				{
					continue;
				}
				Vector2 OtherUV = UVs[B];
				OtherUV.x += std::round(UVs[A].x - OtherUV.x);
				const uint32_t Middle = AddVertex(Positions[A].lerp(Positions[B], 0.5f), UVs[A].lerp(OtherUV, 0.5f));
				JoinWeights[Middle] = (JoinWeights[A] + JoinWeights[B]) * 0.5f;
				Colors[Middle] = Colors[A].lerp(Colors[B], 0.5f);
				Tangents[Middle] = SafeNormal(Tangents[A].lerp(Tangents[B], 0.5f));
				Midpoints.emplace(Key, Middle);
			}
		}
		std::vector<uint32_t> Refined;
		Refined.reserve(Indices.size() + Midpoints.size() * 6);
		const auto Face = [&Refined](uint32_t A, uint32_t B, uint32_t C)
		{
			Refined.insert(Refined.end(), {A, B, C});
		};
		for (size_t Triangle = 0; Triangle < Indices.size(); Triangle += 3)
		{
			if (Triangle % 3072 == 0 && IsCancelled())
			{
				return false;
			}
			std::array<uint32_t, 3> V = {Indices[Triangle], Indices[Triangle + 1], Indices[Triangle + 2]};
			std::array<uint32_t, 3> M{};
			std::array<bool, 3> Split{};
			int32_t Count = 0;
			for (size_t Corner = 0; Corner < 3; ++Corner)
			{
				const auto Found = Midpoints.find(EdgeKey(V[Corner], V[(Corner + 1) % 3]));
				Split[Corner] = Found != Midpoints.end();
				if (Split[Corner])
				{
					M[Corner] = Found->second;
					++Count;
				}
			}
			if (Count == 0)
			{
				Face(V[0], V[1], V[2]);
			}
			else if (Count == 3)
			{
				Face(V[0], M[0], M[2]);
				Face(V[1], M[1], M[0]);
				Face(V[2], M[2], M[1]);
				Face(M[0], M[1], M[2]);
			}
			else
			{
				size_t Start = 0;
				while (Split[Start] != (Count == 1))
				{
					++Start;
				}
				std::rotate(V.begin(), V.begin() + Start, V.end());
				std::rotate(M.begin(), M.begin() + Start, M.end());
				if (Count == 1)
				{
					Face(V[0], M[0], V[2]);
					Face(M[0], V[1], V[2]);
				}
				else
				{
					Face(V[2], M[2], M[1]);
					Face(V[0], V[1], M[1]);
					Face(V[0], M[1], M[2]);
				}
			}
		}
		Indices = std::move(Refined);
		std::vector<std::vector<uint32_t>> Neighbors(Positions.size());
		for (size_t Triangle = 0; Triangle < Indices.size(); Triangle += 3)
		{
			for (size_t Corner = 0; Corner < 3; ++Corner)
			{
				const uint32_t Vertex = Indices[Triangle + Corner];
				if (JoinWeights[Vertex] > 0.0f)
				{
					Neighbors[Vertex].push_back(Indices[Triangle + (Corner + 1) % 3]);
					Neighbors[Vertex].push_back(Indices[Triangle + (Corner + 2) % 3]);
				}
			}
		}
		std::vector<uint32_t> Active;
		for (uint32_t Vertex = 0; Vertex < Neighbors.size(); ++Vertex)
		{
			std::vector<uint32_t>& Adjacent = Neighbors[Vertex];
			if (Adjacent.empty())
			{
				continue;
			}
			std::sort(Adjacent.begin(), Adjacent.end());
			Adjacent.erase(std::unique(Adjacent.begin(), Adjacent.end()), Adjacent.end());
			Active.push_back(Vertex);
		}
		std::vector<Vector3> Next(Positions.size());
		// Initial relaxation opens the cramped port corners; paired passes then round the
		// crotch without continuing to shrink it. Fixed outer rings preserve the branch paths.
		for (int32_t Pass = 0; Pass < 24; ++Pass)
		{
			if (IsCancelled())
			{
				return false;
			}
			const float Strength = Pass < 8 ? 0.42f : (Pass % 2 == 0 ? 0.50f : -0.53f);
			for (const uint32_t Vertex : Active)
			{
				Next[Vertex] = Positions[Vertex];
				Vector3 Average;
				for (const uint32_t Neighbor : Neighbors[Vertex])
				{
					Average += Positions[Neighbor];
				}
				Average /= float(Neighbors[Vertex].size());
				Next[Vertex] += (Average - Positions[Vertex]) * (Strength * JoinWeights[Vertex]);
			}
			for (const uint32_t Vertex : Active)
			{
				Positions[Vertex] = Next[Vertex];
			}
		}
		Normals.assign(Positions.size(), Vector3());
		bAccumulateNormals = true;
		const std::vector<uint32_t> Faces = std::move(Indices);
		Indices.clear();
		for (size_t Face = 0; Face < Faces.size(); Face += 3)
		{
			if (Face % 3072 == 0 && IsCancelled())
			{
				return false;
			}
			AddTriangle(Faces[Face], Faces[Face + 2], Faces[Face + 1]);
		}
		return true;
	}

	void FinalizeNormals()
	{
		for (Vector3& Normal : Normals)
		{
			Normal = SafeNormal(Normal);
		}
		// A very acute crotch needs a shading crease. Keep positions identical so the surface
		// remains welded, and split only corners whose smooth normal points below that face.
		constexpr float MinimumSmoothCosine = 0.25f;
		for (size_t Triangle = 0; Triangle < Indices.size(); Triangle += 3)
		{
			const Vector3 Face = (Positions[Indices[Triangle + 2]] - Positions[Indices[Triangle]])
									 .cross(Positions[Indices[Triangle + 1]] - Positions[Indices[Triangle]])
									 .normalized();
			for (size_t Corner = 0; Corner < 3; ++Corner)
			{
				const uint32_t Original = Indices[Triangle + Corner];
				if (Normals[Original].dot(Face) < MinimumSmoothCosine)
				{
					const Vector3 Position = Positions[Original];
					const Vector2 UV = UVs[Original];
					const uint32_t Duplicate = AddVertex(Position, UV);
					Normals[Duplicate] = Face;
					if (bHasTangents)
					{
						Tangents[Duplicate] = Tangents[Original];
					}
					if (bHasColors)
					{
						Colors[Duplicate] = Colors[Original];
					}
					Indices[Triangle + Corner] = Duplicate;
				}
			}
		}
	}
};

class GrowthBuilder
{
public:
	GrowthBuilder(const NodeGraph& Graph, const TrunkNode& Root, const TreeFoliageOptions& InOptions,
				  TreeMeshData& InOutput)
		: Options(InOptions), Settings(InOptions.Growth), Output(InOutput),
		  Form(static_cast<EGrowthForm>(InOptions.Preset)), bGinkgo(Form == EGrowthForm::Ginkgo)
	{
		Trunk = Root.params;
		ReadChildren(Graph, Root.id);
		if (Form == EGrowthForm::Broadleaf || Form == EGrowthForm::Metasequoia)
		{
			const float Slenderness = Form == EGrowthForm::Broadleaf ? 0.66f : 0.56f;
			Trunk.startRadius *= Slenderness;
			Trunk.endRadius *= Slenderness;
		}
		if (Form == EGrowthForm::Bamboo)
		{
			// Medium Phyllostachys-like culms: roughly 8 cm across at 8.5 m, with a fine leafy tip.
			Trunk.startRadius *= 0.28f;
			Trunk.endRadius *= 0.065f;
			Trunk.jointCount = std::clamp(int32_t(std::round(Trunk.length / Settings.BambooInternodeLength)), 8, 48);
			CulmNodes.push_back(0.0f);
			for (int32_t Node = 0; Node < Trunk.jointCount; ++Node)
			{
				const float Along = (Node + 0.5f) / Trunk.jointCount;
				const float Internode = Profile({0.38f, 1.0f, 1.0f, 0.78f, 0.30f}, Along) *
										(0.90f + 0.20f * Unit(uint32_t(Trunk.seed) + Node * 37u));
				CulmNodes.push_back(CulmNodes.back() + Internode);
			}
			const float Scale = Trunk.length / CulmNodes.back();
			for (float& Distance : CulmNodes)
			{
				Distance *= Scale;
			}
		}
		Stems.reserve(4096);
	}

	void Build()
	{
		const Vector3 Origin(Trunk.posX, 0.0f, Trunk.posZ);
		// Ornamental peach spreads from a short bole; its upper crown is carried by ascending flower shoots.
		const float LeaderLength =
			Trunk.length * (Form == EGrowthForm::Peach ? 0.24f : (Form == EGrowthForm::Willow ? 0.72f : 1.0f));
		AddStem(-1, 0, EStemRole::Leader, 0, LeaderLength, Vector3(0, 1, 0), Mix(uint32_t(Trunk.seed)), Origin);
		if (Stems.empty())
		{
			return;
		}
		if (Form == EGrowthForm::Willow || bGinkgo)
		{
			GrowBranches();
		}
		else
		{
			GrowSpeciesBranches();
		}
		ResolveRadii();
		BuildWood();
		CollectFoliage();
		const int32_t StemOffset = int32_t(Output.Growth.Stems);
		++Output.Growth.Trunks;
		Output.Growth.Stems += uint32_t(Stems.size());
		Output.Growth.Segments += uint32_t(Segments);
		if (Settings.bDebug)
		{
			if (!Output.Growth.Offsets.empty())
			{
				Output.Growth.Offsets.pop_back();
			}
			for (const Stem& Item : Stems)
			{
				Output.Growth.Offsets.push_back(int32_t(Output.Growth.Points.size()));
				Output.Growth.Parents.push_back(Item.Parent < 0 ? -1 : Item.Parent + StemOffset);
				Output.Growth.Attachments.push_back(Item.Attachment);
				Output.Growth.Roles.push_back(int32_t(Item.Role));
				for (const BranchRing& Ring : Item.Rings)
				{
					Output.Growth.Points.push_back(Ring.center);
					Output.Growth.Radii.push_back(Ring.radius);
				}
			}
			Output.Growth.Offsets.push_back(int32_t(Output.Growth.Points.size()));
		}
	}

private:
	const TreeFoliageOptions& Options;
	const TreeGrowthSettings& Settings;
	TreeMeshData& Output;
	const EGrowthForm Form;
	const bool bGinkgo;
	bool bHasRoots = false;
	bool bHasSpines = false;
	TrunkParams Trunk;
	RootsParams Roots;
	TwigParams Twigs;
	SpineParams Spines;
	std::vector<LeafClusterParams> Foliage;
	std::vector<BranchParams> Levels;
	std::vector<Stem> Stems;
	std::vector<float> CulmNodes;
	int32_t Segments = 0;

	static float GetRingDistance(const Stem& Item, size_t Index)
	{
		return Item.ArcLengths.empty() ? float(Index) * Item.Length / float(Item.Rings.size() - 1)
									   : Item.ArcLengths[Index];
	}

	float GetMinimumRadius(const Stem& Item) const
	{
		if (Form == EGrowthForm::Peach)
		{
			// Flowers need a continuous physical bearing shoot, including during winter and leaf thinning.
			return 0.00065f;
		}
		// Bamboo's slender supporting axes must remain visible behind the much smaller leaf fans.
		if (Form == EGrowthForm::Bamboo)
		{
			return Item.Role != EStemRole::Shoot ? 0.00055f : MinimumWoodRadius;
		}
		// Keep the actual supporting twigs. Only the terminal organ's rachis lives in the mask.
		return Item.Role == EStemRole::Shoot || Item.Role == EStemRole::ShortShoot ? MinimumWoodRadius : 0.0015f;
	}

	bool IsCancelled() const
	{
		return Options.Cancelled && Options.Cancelled->load(std::memory_order_relaxed);
	}

	void ReadChildren(const NodeGraph& Graph, NodeId Parent)
	{
		for (const TreeNode* Child : Graph.childrenOf(Parent))
		{
			switch (Child->getType())
			{
			case NodeType::Roots:
				Roots = static_cast<const RootsNode*>(Child)->params;
				bHasRoots = true;
				break;
			case NodeType::Branch:
				Levels.push_back(static_cast<const BranchNode*>(Child)->params);
				break;
			case NodeType::Twig:
				Twigs = static_cast<const TwigNode*>(Child)->params;
				break;
			case NodeType::LeafCluster:
				Foliage.push_back(static_cast<const LeafClusterNode*>(Child)->params);
				break;
			case NodeType::Spine:
				Spines = static_cast<const SpineNode*>(Child)->params;
				bHasSpines = true;
				break;
			case NodeType::Frond:
			{
				const FrondParams& Frond = static_cast<const FrondNode*>(Child)->params;
				LeafClusterParams Spray;
				Spray.material = Frond.material;
				Spray.leafCount = 12;
				Spray.leafSize = Frond.width;
				Foliage.push_back(Spray);
				break;
			}
			default:
				break;
			}
			ReadChildren(Graph, Child->id);
		}
	}

	int32_t AddStem(int32_t Parent, int32_t Attachment, EStemRole Role, int32_t Order, float Length,
					const Vector3& InitialDirection, uint32_t Seed, const Vector3& RootPosition = Vector3())
	{
		if (IsCancelled() || Length < 0.035f)
		{
			return -1;
		}
		Stem Item;
		Item.Parent = Parent;
		Item.Attachment = Attachment;
		Item.Role = Role;
		Item.Order = Order;
		Item.Seed = Seed;
		Vector3 Position = Parent >= 0 ? Stems[Parent].Rings[Attachment].center : RootPosition;
		Item.Height =
			Parent == 0 ? (Form == EGrowthForm::Bamboo ? GetRingDistance(Stems[0], size_t(Attachment)) / Stems[0].Length
													   : float(Attachment) / float(Stems[0].Rings.size() - 1))
						: (Parent > 0 ? Stems[Parent].Height : 0.0f);
		if (Role == EStemRole::Pendant)
		{
			Length = std::min(Length, std::max(0.05f, Position.y - 0.45f));
		}
		Item.Length = Length;
		int32_t Count = Role == EStemRole::ShortShoot
							? 2
							: std::clamp(int32_t(std::ceil(Length / (Order == 0 ? 0.20f : 0.22f))), 6, 80);
		if (Role == EStemRole::Pendant)
		{
			Count = std::clamp(int32_t(std::ceil(Length / 0.55f)), 4, 9);
		}
		if (Role == EStemRole::Shoot)
		{
			Count = std::clamp(int32_t(std::ceil(Length / 0.18f)), 2, 16);
		}
		if (Form == EGrowthForm::Peach && Role != EStemRole::Root)
		{
			Count = Role == EStemRole::Shoot ? std::clamp(int32_t(std::ceil(Length / 0.18f)), 3, 10)
											 : std::clamp(int32_t(std::ceil(Length / 0.12f)), 9, 40);
		}
		if (Form == EGrowthForm::Bamboo && Role == EStemRole::Leader)
		{
			// Every third sample is an actual culm node; paired branches attach to these samples.
			Count = std::clamp(Trunk.jointCount, 1, 48) * 3;
		}
		if (Form == EGrowthForm::Bamboo && Role == EStemRole::Lateral)
		{
			Count = std::clamp(int32_t(std::ceil(Length / (Order == 1 ? 0.16f : 0.12f))), Order == 1 ? 5 : 3, 40);
		}
		if (Segments + Count + int32_t(Output.Growth.Segments) > Options.MaxSegments)
		{
			Output.bSegmentBudgetApplied = true;
			return -1;
		}
		Segments += Count;
		Vector3 Direction = SafeNormal(InitialDirection);
		Vector3 Right = Perpendicular(Direction);
		const Vector3 Side = Right;
		const Vector3 OtherSide = Direction.cross(Side);
		const Vector3 Flat = SafeNormal(Vector3(Direction.x, 0.0f, Direction.z), Side);
		Vector3 Goal = Direction;
		float Bend = Settings.BranchBend;
		if (Role == EStemRole::Leader)
		{
			Bend = Settings.TrunkBend;
			if (Form == EGrowthForm::Bamboo)
			{
				const Vector3 Lean = SafeNormal(
					Vector3(Trunk.posX + Signed(Seed + 8) * 0.25f, 0, Trunk.posZ + Signed(Seed + 24) * 0.25f), Side);
				Goal = SafeNormal(Vector3(0, 1, 0) + Lean * (0.36f + 0.26f * Unit(Seed + 11)));
			}
		}
		else if (Role == EStemRole::Pendant)
		{
			Goal = Vector3(0, -1, 0);
		}
		else if (Role == EStemRole::Root)
		{
			Goal = SafeNormal(Flat + Vector3(0, -0.55f, 0));
		}
		else
		{
			switch (Form)
			{
			case EGrowthForm::Pine:
				Goal = SafeNormal(Flat + Vector3(0, Order == 1 ? -0.08f + Item.Height * 0.45f : 0.55f, 0));
				break;
			case EGrowthForm::Bamboo:
				Goal = SafeNormal(Flat + Vector3(0, Order == 1 ? -0.28f : -0.55f, 0));
				break;
			case EGrowthForm::Metasequoia:
				Goal = SafeNormal(Flat + Vector3(0,
												 Order == 1 ? -0.12f + Item.Height * 0.30f
															: (Role == EStemRole::Shoot ? -0.24f : -0.08f),
												 0));
				break;
			case EGrowthForm::Peach:
				Goal = SafeNormal(Flat * (Order == 1 ? 0.85f : 0.60f) +
								  Vector3(0, (Order == 1 ? 1.10f : 1.35f) + Signed(Seed + 51) * 0.22f, 0));
				break;
			case EGrowthForm::Broadleaf:
				Goal = SafeNormal(Flat * 0.85f + Vector3(0, 0.50f, 0));
				break;
			default:
				Goal = SafeNormal(Flat * (bGinkgo ? 0.60f : 0.9f) +
								  Vector3(0, bGinkgo ? 0.85f : (Order < 3 ? 0.10f : -0.5f), 0));
				break;
			}
		}
		Item.ForkDirection = SafeNormal(Side * Signed(Seed + 31) + OtherSide * Signed(Seed + 83));
		const float SpeciesForkChance =
			Form == EGrowthForm::Willow
				? 0.70f
				: (bGinkgo ? 0.35f
						   : (Form == EGrowthForm::Peach ? 0.85f : (Form == EGrowthForm::Broadleaf ? 0.55f : 0.0f)));
		const float ForkChance = SpeciesForkChance * Settings.Forking;
		const bool bForkLevel = Order == 1 || (Form == EGrowthForm::Peach && Order == 2);
		const float LevelForkChance = Form == EGrowthForm::Peach && Order == 2 ? ForkChance * 0.60f : ForkChance;
		if (Role == EStemRole::Lateral && bForkLevel && Unit(Seed + 99) < LevelForkChance)
		{
			Item.ForkIndex = std::clamp(int32_t(Count * (0.55f + Unit(Seed + 25) * 0.16f)), 2, Count - 3);
		}
		const float PhaseA = Unit(Seed + 15) * Tau;
		const float PhaseB = Unit(Seed + 39) * Tau;
		const bool bRegularAxis =
			Form == EGrowthForm::Pine || Form == EGrowthForm::Bamboo || Form == EGrowthForm::Metasequoia;
		const float Noise =
			(Role == EStemRole::Leader ? (bRegularAxis ? 0.065f : (bGinkgo ? 0.18f : 0.37f))
									   : (Form == EGrowthForm::Metasequoia ? 0.20f : (bRegularAxis ? 0.10f : 0.28f))) *
			Bend;
		Item.Rings.reserve(size_t(Count + 1));
		Item.Rings.push_back({Position, 0.0f, Direction, Right});
		if (Form == EGrowthForm::Bamboo && Role == EStemRole::Leader)
		{
			Item.ArcLengths.push_back(0.0f);
		}
		for (int32_t Index = 1; Index <= Count; ++Index)
		{
			float Distance = Length * float(Index) / float(Count);
			float StepLength = Length / float(Count);
			if (!Item.ArcLengths.empty())
			{
				const size_t Node = size_t((Index - 1) / 3);
				Distance = CulmNodes[Node] + (CulmNodes[Node + 1] - CulmNodes[Node]) * ((Index - 1) % 3 + 1) / 3.0f;
				StepLength = Distance - Item.ArcLengths.back();
				Item.ArcLengths.push_back(Distance);
			}
			const float T = Item.ArcLengths.empty() ? float(Index) / float(Count) : Distance / Length;
			const float DefaultBend = Role == EStemRole::Pendant ? Smooth(0.02f, 0.68f, T) : Smooth(0.10f, 0.95f, T);
			const float Influence = std::clamp(Settings.BendAlongBranch.Evaluate(T, DefaultBend) * Bend, 0.0f, 1.0f);
			Vector3 Desired = InitialDirection.lerp(Goal, Influence);
			const float WaveA = std::sin(T * Tau * 1.15f + PhaseA) - std::sin(PhaseA);
			const float WaveB = std::sin(T * Tau * 0.65f + PhaseB) - std::sin(PhaseB);
			Desired += (Side * WaveA + OtherSide * WaveB * 0.7f) * Noise * Smooth(0.0f, 0.25f, T);
			if (Form != EGrowthForm::Bamboo && Role != EStemRole::Root)
			{
				const float FineBend = (std::sin(T * Tau * 2.8f + PhaseB) - std::sin(PhaseB)) * 0.065f * Bend;
				Desired += Side * FineBend * Smooth(0.0f, 0.2f, T);
				if (Form == EGrowthForm::Willow && Role == EStemRole::Lateral)
				{
					Desired.y -= std::sin(Pi * T) * (Order == 1 ? 0.10f : 0.35f) * Bend;
				}
			}
			if (Item.ForkIndex > 0)
			{
				Desired += Item.ForkDirection * Smooth(float(Item.ForkIndex) / Count, 1.0f, T) * 0.50f;
			}
			Direction = SafeNormal(Desired);
			Right = SafeNormal(Right - Direction * Right.dot(Direction), Perpendicular(Direction));
			Position += Direction * StepLength;
			Item.Rings.push_back({Position, 0.0f, Direction, Right});
		}
		const int32_t Result = int32_t(Stems.size());
		Stems.push_back(std::move(Item));
		return Result;
	}

	bool ShouldGrow(float Height, uint32_t Seed) const
	{
		return Unit(Seed + 14) <= std::clamp(Settings.DensityByHeight.Evaluate(Height, 1.0f), 0.0f, 1.0f);
	}

	int32_t AddLateral(size_t Parent, float Along, float Azimuth, float Degrees, float Length, uint32_t Seed,
					   EStemRole Role = EStemRole::Lateral)
	{
		if (!ShouldGrow(Parent == 0 ? Along : Stems[Parent].Height, Seed))
		{
			return -1;
		}
		const int32_t LastRing = int32_t(Stems[Parent].Rings.size()) - 1;
		const int32_t Attachment = std::clamp(int32_t(std::round(Along * LastRing)), 1, LastRing - 1);
		const BranchRing Anchor = Stems[Parent].Rings[Attachment];
		const float Angle = Degrees * Pi / 180.0f;
		const Vector3 Radial = Anchor.right.rotated(Anchor.up, Azimuth);
		Vector3 Direction = SafeNormal(Anchor.up * std::cos(Angle) + Radial * std::sin(Angle));
		if (Form == EGrowthForm::Peach && Parent > 0)
		{
			Direction.y = std::max(Direction.y, Role == EStemRole::Shoot ? 0.45f : 0.12f);
			Direction = SafeNormal(Direction);
		}
		return AddStem(int32_t(Parent), Attachment, Role, Stems[Parent].Order + 1, Length, Direction, Seed);
	}

	void GrowForks(size_t FirstParent = 1)
	{
		const size_t ParentCount = Stems.size();
		for (size_t Parent = FirstParent; Parent < ParentCount; ++Parent)
		{
			const int32_t ForkIndex = Stems[Parent].ForkIndex;
			if (ForkIndex < 0)
			{
				continue;
			}
			Vector3 Direction = SafeNormal(Stems[Parent].Rings[ForkIndex].up - Stems[Parent].ForkDirection * 0.95f);
			if (Form == EGrowthForm::Peach)
			{
				Direction.y = std::max(Direction.y, 0.18f);
				Direction = SafeNormal(Direction);
			}
			const int32_t ForkOrder = Form == EGrowthForm::Peach ? Stems[Parent].Order : 1;
			const int32_t Fork = AddStem(int32_t(Parent), ForkIndex, EStemRole::Fork, ForkOrder,
										 Stems[Parent].Length * 0.58f, Direction, Mix(Stems[Parent].Seed + 377));
			if (Fork >= 0)
			{
				++Output.Growth.Forks;
			}
		}
	}

	void GrowConiferScaffolds()
	{
		const BranchParams& Params = Levels.front();
		const bool bPine = Form == EGrowthForm::Pine;
		const int32_t Tiers = std::clamp(
			int32_t(std::ceil((Params.regionEnd - Params.regionStart) / std::max(0.04f, Params.intervalSpacing))), 2,
			20);
		const int32_t PerTier = std::clamp(int32_t(std::round(Params.branchesPerNode * (bPine ? 1.0f : 1.5f))), 1, 10);
		for (int32_t Tier = 0; Tier < Tiers && !IsCancelled(); ++Tier)
		{
			const uint32_t TierSeed = Mix(uint32_t(Trunk.seed) + uint32_t(Tier + 1) * 747796405u);
			const float Height = Params.regionStart + (Params.regionEnd - Params.regionStart) *
														  (Tier + 0.20f + Unit(TierSeed) * 0.35f) / Tiers;
			const float Phase = Unit(TierSeed + 19) * Tau;
			for (int32_t Branch = 0; Branch < PerTier; ++Branch)
			{
				const uint32_t Seed = Mix(TierSeed + uint32_t(Branch + 1) * 2891336453u);
				// Primary Metasequoia branches form irregular tiers; the fine lateral axes below are opposite.
				if (Branch > 1 && Unit(Seed + 83) < (bPine ? 0.16f : 0.09f))
				{
					continue;
				}
				const float Along = std::clamp(Height + Signed(Seed) * (bPine ? 0.024f : 0.038f), 0.06f, 0.97f);
				const float Envelope = Profile(bPine ? std::array<float, 5>{0.65f, 1.0f, 0.94f, 0.64f, 0.07f}
													 : std::array<float, 5>{0.90f, 1.0f, 0.72f, 0.36f, 0.035f},
											   Along);
				const float Length = Trunk.length * Params.lengthRatio * (bPine ? 1.0f : 0.70f) *
									 Settings.LengthByHeight.Evaluate(Along, Envelope) *
									 (0.66f + 0.57f * Unit(Seed + 5));
				AddLateral(0, Along, Phase + Branch * Tau / PerTier + Signed(Seed + 3) * 0.24f,
						   (bPine ? 99.0f : 91.0f) - Along * 35.0f + Signed(Seed + 7) * 10.0f, Length, Seed);
			}
		}
	}

	void GrowCulmBranches()
	{
		const BranchParams& Params = Levels.front();
		const int32_t Nodes = std::clamp(Trunk.jointCount, 1, 48);
		for (int32_t Node = 1; Node < Nodes && !IsCancelled(); ++Node)
		{
			const uint32_t NodeSeed = Mix(uint32_t(Trunk.seed) + uint32_t(Node) * 747796405u);
			const float Height = CulmNodes[Node] / Trunk.length;
			if (Height < Params.regionStart || Height > 0.985f || !ShouldGrow(Height, NodeSeed))
			{
				continue;
			}
			const float Phase = Node * Pi + Unit(uint32_t(Trunk.seed)) * Tau + Signed(NodeSeed) * 0.32f;
			const float Envelope = Profile({0.35f, 0.75f, 1.0f, 0.63f, 0.025f}, Height);
			const float Length = Trunk.length * Params.lengthRatio * 0.62f *
								 Settings.LengthByHeight.Evaluate(Height, Envelope) *
								 (0.78f + 0.34f * Unit(NodeSeed + 5));
			// Phyllostachys-like paired branches share one side of a node and have unequal lengths.
			for (int32_t Side = 0; Side < 2; ++Side)
			{
				const uint32_t Seed = Mix(NodeSeed + uint32_t(Side + 1) * 2891336453u);
				const BranchRing& Anchor = Stems[0].Rings[Node * 3];
				const float Angle = (Params.spreadAngle - Height * 18.0f + Side * 9.0f) * Pi / 180.0f;
				const Vector3 Radial = Anchor.right.rotated(Anchor.up, Phase + (Side == 0 ? -0.48f : 0.48f));
				AddStem(0, Node * 3, EStemRole::Lateral, 1, std::max(0.04f, Length * (Side == 0 ? 1.0f : 0.57f)),
						SafeNormal(Anchor.up * std::cos(Angle) + Radial * std::sin(Angle)), Seed);
			}
		}
		const size_t PrimaryCount = Stems.size();
		for (size_t Parent = 1; Parent < PrimaryCount && !IsCancelled(); ++Parent)
		{
			const float ParentLength = Stems[Parent].Length;
			const uint32_t ParentSeed = Stems[Parent].Seed;
			if (ParentLength < 0.18f)
			{
				AddLateral(Parent, 0.7f, Unit(ParentSeed) * Tau, 20.0f, 0.08f, Mix(ParentSeed), EStemRole::Shoot);
				continue;
			}
			const int32_t Count = std::clamp(int32_t(std::round(Twigs.twigCount * ParentLength / 1.1f)), 3, 12);
			for (int32_t Index = 0; Index < Count; ++Index)
			{
				const uint32_t Seed = Mix(ParentSeed + uint32_t(Index + 1) * 2891336453u);
				const float Along = 0.22f + 0.73f * (Index + 0.35f + Unit(Seed) * 0.3f) / Count;
				const size_t Ring = std::min(size_t(std::round(Along * (Stems[Parent].Rings.size() - 1))),
											 Stems[Parent].Rings.size() - 2);
				const float Phase =
					GetFanPhase(Stems[Parent].Rings[Ring]) + (Index % 2) * Pi + Signed(Seed + 2) * 0.48f;
				const float Length = ParentLength * Twigs.lengthRatio * 0.9f *
									 Profile({0.35f, 0.85f, 1.0f, 0.8f, 0.20f}, Along) *
									 (0.75f + 0.5f * Unit(Seed + 5));
				AddLateral(Parent, Along, Phase, Twigs.spreadAngle + Signed(Seed + 3) * 12.0f, Length, Seed);
			}
		}
		const size_t LeafParents = Stems.size();
		for (size_t Parent = 1; Parent < LeafParents && !IsCancelled(); ++Parent)
		{
			if (Stems[Parent].Order != 2 || Stems[Parent].Role != EStemRole::Lateral)
			{
				continue;
			}
			const uint32_t ParentSeed = Stems[Parent].Seed;
			const float ParentLength = Stems[Parent].Length;
			const int32_t Count = std::clamp(int32_t(std::ceil(ParentLength / 0.075f)), 3, 8);
			for (int32_t Index = 0; Index < Count; ++Index)
			{
				const uint32_t Seed = Mix(ParentSeed + uint32_t(Index + 1) * 277803737u);
				const float Along = 0.18f + 0.79f * (Index + 0.4f + 0.3f * Unit(Seed)) / Count;
				const size_t Ring = std::min(size_t(std::round(Along * (Stems[Parent].Rings.size() - 1))),
											 Stems[Parent].Rings.size() - 2);
				const float Phase =
					GetFanPhase(Stems[Parent].Rings[Ring]) + (Index % 2) * Pi + Signed(Seed + 8) * 0.70f;
				AddLateral(Parent, Along, Phase, 26.0f + Unit(Seed + 3) * 24.0f, 0.065f + 0.055f * Unit(Seed + 5), Seed,
						   EStemRole::Shoot);
			}
		}
	}

	void GrowBroadleafScaffolds()
	{
		const BranchParams& Params = Levels.front();
		const bool bPeach = Form == EGrowthForm::Peach;
		const int32_t Count = std::clamp(int32_t(std::round(Params.branchCount * (bPeach ? 4.0f / 7.0f : 0.75f))),
										 bPeach ? 3 : 2, bPeach ? 8 : 24);
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			const uint32_t Seed = Mix(uint32_t(Trunk.seed) + uint32_t(Index + 1) * 747796405u);
			const float U = (Index + 0.35f + Unit(Seed) * 0.3f) / Count;
			const float Along = bPeach ? 0.48f + U * 0.45f : 0.24f + U * 0.69f;
			const float Envelope = bPeach ? 1.0f : Profile({0.68f, 1.0f, 0.94f, 0.62f, 0.12f}, Along);
			const float Length = Trunk.length * Params.lengthRatio * Settings.LengthByHeight.Evaluate(Along, Envelope) *
								 (0.86f + 0.30f * Unit(Seed + 5));
			const float Azimuth = (bPeach ? Index * Tau / Count : Index * 2.399963f) +
								  Unit(uint32_t(Trunk.seed) + 41) * Tau + Signed(Seed + 11) * 0.28f;
			AddLateral(0, Along, Azimuth, (bPeach ? 62.0f : 58.0f - Along * 18.0f) + Signed(Seed + 3) * 9.0f, Length,
					   Seed);
		}
		GrowForks();
	}

	float GetFanPhase(const BranchRing& Anchor) const
	{
		const Vector3 Side = SafeNormal(Anchor.up.cross(Vector3(0, 1, 0)), Anchor.right);
		return std::atan2(Side.dot(Anchor.up.cross(Anchor.right)), Side.dot(Anchor.right));
	}

	void GrowPeachBranches()
	{
		const BranchParams& Scaffold = Levels.front();
		const int32_t ScaffoldCount = std::clamp(int32_t(std::round(Scaffold.branchCount * 0.85f)), 3, 8);
		const float CrownPhase = Unit(uint32_t(Trunk.seed) + 41) * Tau;
		for (int32_t Index = 0; Index < ScaffoldCount && !IsCancelled(); ++Index)
		{
			const uint32_t Seed = Mix(uint32_t(Trunk.seed) + uint32_t(Index + 1) * 747796405u);
			const float Along = 0.38f + 0.55f * (Index + 0.35f) / ScaffoldCount;
			const float Azimuth = CrownPhase + Index * 2.399963f + Signed(Seed + 11) * 0.26f;
			const float Length = Trunk.length * Scaffold.lengthRatio * (0.80f + 0.43f * Unit(Seed + 5)) *
								 Settings.LengthByHeight.Evaluate(Along, 1.0f);
			AddLateral(0, Along, Azimuth, 55.0f + Signed(Seed + 3) * 14.0f, Length, Seed);
		}
		GrowForks();
		const size_t FirstSecondary = Stems.size();
		const BranchParams& Secondary = Levels[std::min(size_t(1), Levels.size() - 1)];
		for (size_t Parent = 1; Parent < FirstSecondary && !IsCancelled(); ++Parent)
		{
			const float ParentLength = Stems[Parent].Length;
			const uint32_t ParentSeed = Stems[Parent].Seed;
			const int32_t Count = std::clamp(
				int32_t(std::round(Secondary.branchCount * std::clamp(ParentLength / 1.6f, 0.55f, 1.0f))), 3, 9);
			for (int32_t Index = 0; Index < Count; ++Index)
			{
				const uint32_t Seed = Mix(ParentSeed + uint32_t(Index + 1) * 2891336453u);
				const float Along = 0.25f + 0.68f * (Index + 0.3f + Unit(Seed + 7) * 0.25f) / Count;
				const float Length = ParentLength * Secondary.lengthRatio *
									 Profile({0.72f, 1.18f, 1.04f, 0.83f, 0.38f}, Along) *
									 (0.76f + 0.48f * Unit(Seed + 5));
				const float Azimuth = Index * 2.399963f + Unit(ParentSeed + 6) * Tau;
				AddLateral(Parent, Along, Azimuth, 43.0f + Signed(Seed + 3) * 12.0f, Length, Seed);
			}
		}
		GrowForks(FirstSecondary);
		const size_t FirstShoot = Stems.size();
		for (size_t Parent = FirstSecondary; Parent < FirstShoot && !IsCancelled(); ++Parent)
		{
			const float ParentLength = Stems[Parent].Length;
			const uint32_t ParentSeed = Stems[Parent].Seed;
			const int32_t Count = std::clamp(int32_t(std::round(Twigs.twigCount * Settings.PeachTwigDensity *
																std::clamp(ParentLength / 0.65f, 0.5f, 1.0f))),
											 2, 16);
			for (int32_t Index = 0; Index < Count; ++Index)
			{
				const uint32_t Seed = Mix(ParentSeed + uint32_t(Index + 1) * 277803737u);
				const float Along = 0.22f + 0.73f * (Index + 0.25f + Unit(Seed + 7) * 0.35f) / Count;
				const float Length =
					std::clamp(ParentLength * Twigs.lengthRatio * Profile({0.68f, 0.95f, 1.20f, 1.15f, 0.85f}, Along) *
								   (0.80f + 0.55f * Unit(Seed + 5)),
							   0.16f, Trunk.length * 0.40f);
				AddLateral(Parent, Along, Index * 2.399963f + Unit(ParentSeed) * Tau, 31.0f + Signed(Seed + 3) * 13.0f,
						   Length, Seed, EStemRole::Shoot);
			}
		}
		if (bHasRoots)
		{
			for (int32_t Index = 0; Index < std::clamp(Roots.rootCount, 0, 8); ++Index)
			{
				const float Angle = Index * Tau / Roots.rootCount + Unit(uint32_t(Trunk.seed)) * Tau;
				AddStem(0, 1, EStemRole::Root, 1, Roots.length * 0.65f * (0.7f + 0.4f * Unit(Index + 17)),
						SafeNormal(Vector3(std::cos(Angle), -0.40f, std::sin(Angle))),
						Mix(uint32_t(Trunk.seed) + Index + 90));
			}
		}
	}

	void GrowSpeciesBranches()
	{
		if (Levels.empty())
		{
			return;
		}
		if (Form == EGrowthForm::Peach)
		{
			GrowPeachBranches();
			return;
		}
		if (Form == EGrowthForm::Bamboo)
		{
			GrowCulmBranches();
			return;
		}
		else if (Form == EGrowthForm::Pine || Form == EGrowthForm::Metasequoia)
		{
			GrowConiferScaffolds();
		}
		else
		{
			GrowBroadleafScaffolds();
		}
		const bool bPlanar =
			Form == EGrowthForm::Pine || Form == EGrowthForm::Bamboo || Form == EGrowthForm::Metasequoia;
		const bool bPaired = Form == EGrowthForm::Metasequoia;
		const size_t LevelCount = std::min(Levels.size(), size_t(3));
		for (size_t Level = 1; Level < LevelCount && !IsCancelled(); ++Level)
		{
			const size_t ParentCount = Stems.size();
			const BranchParams& Params = Levels[Level];
			for (size_t Parent = 1; Parent < ParentCount; ++Parent)
			{
				if (Stems[Parent].Order != int32_t(Level))
				{
					continue;
				}
				const float ParentLength = Stems[Parent].Length;
				const uint32_t ParentSeed = Stems[Parent].Seed;
				int32_t Count =
					std::clamp(int32_t(Params.branchCount * std::clamp(ParentLength / 1.2f, 0.45f, 1.0f)), 2, 16);
				if (Form == EGrowthForm::Broadleaf)
				{
					Count = std::max(3, int32_t(std::round(Count * 1.05f)));
				}
				if (bPaired)
				{
					Count += Count % 2;
				}
				for (int32_t Index = 0; Index < Count; ++Index)
				{
					const int32_t Node = bPaired ? Index / 2 : Index;
					const int32_t NodeCount = bPaired ? Count / 2 : Count;
					const uint32_t Seed = Mix(ParentSeed + uint32_t(Index + 1) * 2891336453u);
					const float Along =
						(Form == EGrowthForm::Pine ? 0.30f : 0.20f) +
						(Form == EGrowthForm::Pine ? 0.65f : 0.75f) *
							(Node + 0.22f +
							 0.35f * Unit(bPaired ? Mix(ParentSeed + uint32_t(Node) * 37u) : Seed + 19)) /
							NodeCount;
					const int32_t Ring = std::clamp(int32_t(std::round(Along * (Stems[Parent].Rings.size() - 1))), 1,
													int32_t(Stems[Parent].Rings.size()) - 2);
					const float Phase =
						bPlanar ? GetFanPhase(Stems[Parent].Rings[Ring]) + (Index % 2) * Pi +
									  (bPaired ? Signed(Mix(ParentSeed + uint32_t(Node) * 39u)) * 0.36f : 0.0f)
								: Index * 2.399963f + Unit(ParentSeed) * Tau;
					const float Length = ParentLength * Params.lengthRatio *
										 Profile({0.60f, 1.0f, 0.92f, 0.70f, 0.22f}, Along) *
										 (0.82f + 0.35f * Unit(Seed + 5));
					AddLateral(Parent, Along, Phase, Params.spreadAngle + Signed(Seed + 3) * (bPaired ? 3.0f : 9.0f),
							   Length, Seed);
				}
			}
		}
		const size_t ShootParents = Stems.size();
		for (size_t Parent = 1; Parent < ShootParents && !IsCancelled(); ++Parent)
		{
			if (Stems[Parent].Order != int32_t(LevelCount))
			{
				continue;
			}
			const float ParentLength = Stems[Parent].Length;
			const uint32_t ParentSeed = Stems[Parent].Seed;
			int32_t Count = std::clamp(bHasSpines ? Spines.spineCount : Twigs.twigCount, 2, 24);
			if (Form == EGrowthForm::Broadleaf)
			{
				Count = std::max(3, int32_t(std::round(Count * 1.05f)));
			}
			if (bPaired)
			{
				Count += Count % 2;
			}
			for (int32_t Index = 0; Index < Count; ++Index)
			{
				const int32_t Node = bPaired ? Index / 2 : Index;
				const int32_t NodeCount = bPaired ? Count / 2 : Count;
				const uint32_t Seed = Mix(ParentSeed + uint32_t(Index + 1) * 277803737u);
				const float Along = 0.14f + 0.82f * (Node + 0.5f) / NodeCount;
				const int32_t Ring = std::clamp(int32_t(std::round(Along * (Stems[Parent].Rings.size() - 1))), 1,
												int32_t(Stems[Parent].Rings.size()) - 2);
				const float Phase = bPlanar
										? GetFanPhase(Stems[Parent].Rings[Ring]) + (Index % 2) * Pi +
											  (bPaired ? Signed(Mix(ParentSeed + uint32_t(Node) * 39u)) * 0.28f : 0.0f)
										: Index * 2.399963f + Unit(ParentSeed) * Tau;
				const float Ratio = bHasSpines ? Spines.lengthRatio : Twigs.lengthRatio;
				const float Angle = bHasSpines ? Spines.spreadAngle : Twigs.spreadAngle;
				const float Length = ParentLength * Ratio * Profile({0.55f, 0.92f, 1.0f, 0.72f, 0.30f}, Along) *
									 (0.8f + 0.4f * Unit(Seed + 5));
				AddLateral(Parent, Along, Phase, Angle + Signed(Seed + 3) * 5.0f, std::max(0.05f, Length), Seed,
						   EStemRole::Shoot);
			}
		}
		if (bHasRoots && Form != EGrowthForm::Bamboo)
		{
			for (int32_t Index = 0; Index < std::clamp(Roots.rootCount, 0, 8); ++Index)
			{
				const float Angle = Index * Tau / Roots.rootCount + Unit(uint32_t(Trunk.seed)) * Tau;
				AddStem(0, 1, EStemRole::Root, 1, Roots.length * 0.65f * (0.7f + 0.4f * Unit(Index + 17)),
						SafeNormal(Vector3(std::cos(Angle), -0.40f, std::sin(Angle))),
						Mix(uint32_t(Trunk.seed) + Index + 90));
			}
		}
	}

	void GrowBranches()
	{
		if (Levels.empty())
		{
			return;
		}
		const size_t LevelCount = std::min(Levels.size(), size_t(bGinkgo ? 2 : 3));
		for (size_t Level = 0; Level < LevelCount && !IsCancelled(); ++Level)
		{
			const size_t ParentCount = Stems.size();
			const BranchParams& Params = Levels[Level];
			for (size_t Parent = 0; Parent < ParentCount; ++Parent)
			{
				if (Stems[Parent].Order != int32_t(Level))
				{
					continue;
				}
				const float ParentLength = Stems[Parent].Length;
				const uint32_t ParentSeed = Stems[Parent].Seed;
				const int32_t RingCount = int32_t(Stems[Parent].Rings.size()) - 1;
				const int32_t Count =
					std::clamp(int32_t(Params.branchCount * (Level == 0 ? 1.0f : std::min(1.0f, ParentLength / 1.1f))),
							   1, std::min(120, RingCount - 2));
				for (int32_t Index = 0; Index < Count; ++Index)
				{
					const uint32_t Seed =
						Mix(ParentSeed + uint32_t(Index + 1) * 747796405u + uint32_t(Level) * 277803737u);
					const float U = (Index + 0.35f + Unit(Seed) * 0.45f) / float(Count);
					const float Along =
						Level == 0 ? (bGinkgo ? 0.13f + 0.82f * U : 0.34f + 0.60f * U) : 0.12f + 0.83f * U;
					const float Height = Level == 0 ? Along : Stems[Parent].Height;
					if (Unit(Seed + 14) > std::clamp(Settings.DensityByHeight.Evaluate(Height, 1.0f), 0.0f, 1.0f))
					{
						continue;
					}
					const int32_t Attachment = std::clamp(int32_t(std::round(Along * RingCount)), 1, RingCount - 1);
					const BranchRing Anchor = Stems[Parent].Rings[Attachment];
					const float Azimuth = Index * 2.399963f + Unit(ParentSeed + 41) * Tau + Signed(Seed + 11) * 0.40f;
					const Vector3 Radial = Anchor.right.rotated(Anchor.up, Azimuth);
					const float Degrees =
						Level == 0 ? (bGinkgo ? 64.0f - Height * 28.0f : 45.0f + Height * 24.0f) : Params.spreadAngle;
					const float Angle = (Degrees + Signed(Seed + 3) * 12.0f) * Pi / 180.0f;
					const Vector3 Direction = SafeNormal(Anchor.up * std::cos(Angle) + Radial * std::sin(Angle));
					float Length = ParentLength * Params.lengthRatio * (0.78f + Unit(Seed + 5) * 0.44f);
					if (Level == 0)
					{
						const float Envelope = bGinkgo ? Profile({0.80f, 1.0f, 0.95f, 0.70f, 0.18f}, Height)
													   : Profile({0.65f, 1.0f, 0.96f, 0.75f, 0.28f}, Height);
						Length *= Settings.LengthByHeight.Evaluate(Height, Envelope) * (bGinkgo ? 1.02f : 0.95f);
					}
					else
					{
						Length *= Profile({0.65f, 1.0f, 0.95f, 0.70f, 0.28f}, Along);
					}
					AddStem(int32_t(Parent), Attachment, EStemRole::Lateral, int32_t(Level + 1), Length, Direction,
							Seed);
					if (Output.bSegmentBudgetApplied)
					{
						return;
					}
				}
			}
			if (Level == 0)
			{
				const size_t ForkParents = Stems.size();
				for (size_t Parent = 1; Parent < ForkParents; ++Parent)
				{
					const int32_t ForkIndex = Stems[Parent].ForkIndex;
					if (ForkIndex < 0)
					{
						continue;
					}
					const Vector3 Direction =
						SafeNormal(Stems[Parent].Rings[ForkIndex].up - Stems[Parent].ForkDirection * 0.95f);
					const int32_t Fork =
						AddStem(int32_t(Parent), ForkIndex, EStemRole::Fork, 1, Stems[Parent].Length * 0.58f, Direction,
								Mix(Stems[Parent].Seed + 377));
					if (Fork >= 0)
					{
						++Output.Growth.Forks;
					}
				}
			}
		}
		const size_t ShootParents = Stems.size();
		for (size_t Parent = 1; Parent < ShootParents && !IsCancelled(); ++Parent)
		{
			if (Stems[Parent].Order != int32_t(LevelCount) && !(bGinkgo && Stems[Parent].Order == 1) &&
				!(Form == EGrowthForm::Willow && Stems[Parent].Order == 2))
			{
				continue;
			}
			const float Length = Stems[Parent].Length;
			const int32_t LastRing = int32_t(Stems[Parent].Rings.size()) - 1;
			const uint32_t ParentSeed = Stems[Parent].Seed;
			const bool bInnerWillow = Form == EGrowthForm::Willow && Stems[Parent].Order == 2;
			const int32_t Count =
				bInnerWillow
					? 3
					: std::clamp(int32_t(Twigs.twigCount * (bGinkgo ? std::min(1.5f, Length / 0.68f) : 1.0f)), 2, 80);
			for (int32_t Index = 0; Index < Count; ++Index)
			{
				const uint32_t Seed = Mix(ParentSeed + uint32_t(Index + 1) * 2891336453u);
				const int32_t Attachment = std::clamp(
					int32_t((0.10f + 0.85f * (Index + 0.2f + 0.6f * Unit(Seed)) / Count) * LastRing), 1, LastRing - 1);
				const BranchRing Anchor = Stems[Parent].Rings[Attachment];
				const Vector3 Radial = Anchor.right.rotated(Anchor.up, Index * 2.399963f + Unit(ParentSeed) * Tau);
				const Vector3 Direction = SafeNormal(Anchor.up * (bGinkgo ? 0.40f : 0.72f) + Radial * 0.55f +
													 Vector3(0, bGinkgo ? 0.35f : -0.20f, 0));
				const float ShootLength = bInnerWillow ? (0.55f + Unit(Seed + 5) * 1.0f)
										  : bGinkgo	   ? 0.07f + 0.13f * Unit(Seed + 5)
													: (1.1f + Unit(Seed + 5) * 2.4f) * std::clamp(Length, 0.7f, 1.3f);
				AddStem(int32_t(Parent), Attachment, bGinkgo ? EStemRole::ShortShoot : EStemRole::Pendant,
						int32_t(LevelCount + 1), ShootLength, Direction, Seed);
				if (Output.bSegmentBudgetApplied)
				{
					return;
				}
			}
		}
		for (int32_t Index = 0; Index < 5; ++Index)
		{
			const float Angle = float(Index) * Tau / 5.0f + Unit(uint32_t(Trunk.seed)) * Tau;
			AddStem(0, 1, EStemRole::Root, 1, Roots.length * (0.7f + 0.4f * Unit(Index + 17)),
					SafeNormal(Vector3(std::cos(Angle), -0.32f, std::sin(Angle))),
					Mix(uint32_t(Trunk.seed) + Index + 90));
		}
	}

	void ResolveRadii()
	{
		// Accumulate support demand from distal shoots toward the trunk. Foliage LOD never enters this pass.
		for (Stem& Item : Stems)
		{
			Item.Demand.assign(Item.Rings.size(), 0.0f);
		}
		for (size_t Reverse = Stems.size(); Reverse > 0; --Reverse)
		{
			Stem& Item = Stems[Reverse - 1];
			float Demand = Item.Role == EStemRole::ShortShoot ? 0.18f : 0.40f;
			const float PerLength = Item.Order == 0 ? 2.5f : (Item.Order == 1 ? 1.2f : 0.25f);
			for (size_t Ring = Item.Rings.size(); Ring > 0; --Ring)
			{
				Demand += Item.Demand[Ring - 1] + Item.Length * PerLength / float(Item.Rings.size());
				Item.Demand[Ring - 1] = Demand;
			}
			if (Item.Parent >= 0 && Item.Role != EStemRole::Root)
			{
				Stems[Item.Parent].Demand[Item.Attachment] += Demand;
			}
		}
		const float Exponent = 1.0f / Settings.RadiusPower;
		const float Scale = Trunk.startRadius / std::pow(Stems[0].Demand[0], Exponent);
		const float ReferenceRadius =
			(bGinkgo || Form == EGrowthForm::Metasequoia) ? 0.48f : (Form == EGrowthForm::Willow ? 0.58f : 0.68f);
		const float BranchScale =
			std::clamp(Levels.empty() ? 1.0f : Levels[0].radiusScale / ReferenceRadius, 0.1f, 5.0f);
		for (Stem& Item : Stems)
		{
			const float ParentRadius =
				Item.Parent >= 0 ? Stems[Item.Parent].Rings[Item.Attachment].radius : Trunk.startRadius;
			const float RadiusMultiplier = Item.Parent < 0 ? 1.0f : BranchScale;
			for (size_t Index = 0; Index < Item.Rings.size(); ++Index)
			{
				const float Along = Item.ArcLengths.empty() ? float(Index) / float(Item.Rings.size() - 1)
															: Item.ArcLengths[Index] / Item.Length;
				// Smooth the discrete loss of support at a branch departure over neighboring samples.
				const size_t Previous = Index > 0 ? Index - 1 : 0;
				const size_t Next = std::min(Index + 1, Item.Rings.size() - 1);
				const float Demand = (Item.Demand[Previous] + Item.Demand[Index] * 2.0f + Item.Demand[Next]) * 0.25f;
				float Radius = Scale * std::pow(Demand, Exponent) * RadiusMultiplier;
				if ((Form == EGrowthForm::Broadleaf || Form == EGrowthForm::Metasequoia) && Item.Order > 0)
				{
					Radius *= Item.Order == 1 ? 0.88f : 0.72f;
				}
				Radius *= std::max(
					0.03f, Settings.RadiusAlongBranch.Evaluate(Along, 1.0f - 0.80f * Smooth(0.86f, 1.0f, Along)));
				if (Item.Parent >= 0)
				{
					Radius = std::min(Radius, ParentRadius * 0.90f);
				}
				else
				{
					Radius *= 1.0f + 0.38f * std::exp(-Along * 35.0f);
				}
				if (Form == EGrowthForm::Bamboo)
				{
					// Culm internodes do not obey the woody tree's accumulated-support taper.
					if (Item.Parent < 0)
					{
						Radius = GetCulmRadius(Along);
					}
					else
					{
						Radius = std::min(Radius, ParentRadius * std::clamp(0.22f * BranchScale, 0.02f, 0.65f) *
													  (1.0f - Along * 0.96f));
					}
				}
				if (Item.Role == EStemRole::Root)
				{
					Radius =
						Trunk.startRadius * std::clamp(Roots.radiusScale, 0.05f, 1.0f) * 2.0f * (1.0f - Along * 0.97f);
				}
				// Short spurs and willow whips stay in the skeleton for foliage placement.
				// Their sub-centimeter wood is represented by the masked cluster at this mesh LOD.
				if (Item.Role == EStemRole::ShortShoot || Item.Role == EStemRole::Pendant)
				{
					Radius = std::min(Radius,
									  (Item.Role == EStemRole::ShortShoot ? 0.003f : 0.004f) * (1.0f - Along * 0.9f));
				}
				if (Item.Role == EStemRole::Shoot)
				{
					const float ShootRadius =
						Form == EGrowthForm::Peach ? 0.0038f + Unit(Item.Seed + 41) * 0.0015f : 0.004f;
					Radius = std::min(Radius, ShootRadius * (1.0f - Along * 0.95f));
				}
				Item.Rings[Index].radius = std::max(0.0006f, Radius);
			}
		}
	}

	float GetCulmRadius(float Along) const
	{
		const float Distance = Along * Trunk.length;
		const size_t Node = GetCulmNode(Distance);
		const float Local = (Distance - CulmNodes[Node]) / (CulmNodes[Node + 1] - CulmNodes[Node]);
		const float Nearest = Local < 0.5f ? Distance - CulmNodes[Node] : Distance - CulmNodes[Node + 1];
		const float Collar = 1.0f - Smooth(0.002f, 0.012f, std::abs(Nearest));
		const float Sheath = 1.0f - Smooth(0.001f, 0.004f, std::abs(Nearest + 0.012f));
		const float Taper = Trunk.startRadius + (Trunk.endRadius - Trunk.startRadius) * std::pow(Along, 1.25f);
		const float Waist = 1.0f - 0.028f * std::sin(Pi * Local);
		return Taper * Waist *
			   (1.0f + Trunk.jointBulge * Settings.BambooNodeDefinition * (Collar * 0.45f + Sheath * 0.25f)) *
			   (1.0f + 0.10f * std::exp(-Along * 25.0f)) * (1.0f - 0.65f * Smooth(0.94f, 1.0f, Along)) *
			   std::max(0.03f, Settings.RadiusAlongBranch.Evaluate(Along, 1.0f));
	}

	size_t GetCulmNode(float Distance) const
	{
		const size_t Upper = size_t(std::upper_bound(CulmNodes.begin(), CulmNodes.end(), Distance) - CulmNodes.begin());
		return std::min(Upper > 0 ? Upper - 1 : 0, CulmNodes.size() - 2);
	}

	Vector3 GetBambooColor(const Stem& Item, float Distance, float Angle) const
	{
		const Vector3 Base = Trunk.material.albedo * Vector3(0.80f, 0.76f, 0.80f);
		if (Item.Parent >= 0)
		{
			return Base.lerp(Vector3(0.30f, 0.29f, 0.13f), 0.30f + 0.20f * Unit(Item.Seed));
		}
		const size_t Node = GetCulmNode(Distance);
		const float Local = (Distance - CulmNodes[Node]) / (CulmNodes[Node + 1] - CulmNodes[Node]);
		const float Nearest = Local < 0.5f ? Distance - CulmNodes[Node] : Distance - CulmNodes[Node + 1];
		const float Variation =
			0.87f + 0.13f * Unit(uint32_t(Trunk.seed)) + 0.035f * std::cos(Angle * 5.0f + Node * 0.7f);
		Vector3 Color = Base * Variation;
		// The powdery zone sits above the sheath scar; it must follow real, unequal nodes.
		const float Wax = (1.0f - Smooth(0.014f, 0.055f, Nearest)) * Smooth(0.002f, 0.01f, Nearest);
		Color = Color.lerp(Vector3(0.37f, 0.43f, 0.28f), Wax * 0.32f * Settings.BambooNodeDefinition);
		const float Scar = 1.0f - Smooth(0.001f, 0.0035f, std::abs(Nearest + 0.012f));
		return Color.lerp(Vector3(0.31f, 0.29f, 0.15f), std::min(1.0f, Scar * 0.75f * Settings.BambooNodeDefinition));
	}

	Vector3 GetBarkColor(const Stem& Item, float Distance, float Angle) const
	{
		const std::array<Vector3, 7> Palettes = {Vector3(0.25f, 0.23f, 0.19f),
												 Vector3(0.27f, 0.25f, 0.20f),
												 Vector3(0.31f, 0.22f, 0.15f),
												 Vector3(0.30f, 0.28f, 0.24f),
												 Vector3(),
												 Vector3(0.32f, 0.22f, 0.16f),
												 Vector3(0.27f, 0.23f, 0.21f)};
		Vector3 Color = Palettes[size_t(Form)];
		const float Young = Smooth(0.0f, 3.0f, float(Item.Order));
		Color = Color.lerp(Form == EGrowthForm::Willow ? Vector3(0.30f, 0.32f, 0.12f) : Vector3(0.34f, 0.29f, 0.18f),
						   Young * 0.55f);
		const float Variation =
			0.89f + 0.13f * Unit(Item.Seed) +
			0.065f * std::sin(Distance * 2.1f + std::cos(Angle * 3.0f) + Unit(Item.Seed + 13) * Tau);
		return Color * Variation;
	}

	static BranchRing InterpolateRing(const BranchRing& A, const BranchRing& B, float Blend)
	{
		return {A.center.lerp(B.center, Blend), A.radius + (B.radius - A.radius) * Blend,
				SafeNormal(A.up.lerp(B.up, Blend)), SafeNormal(A.right.lerp(B.right, Blend))};
	}

	float GetExitDistance(const Stem& Child) const
	{
		const Stem& Parent = Stems[Child.Parent];
		const BranchRing& Anchor = Parent.Rings[Child.Attachment];
		const Vector3 Direction = Child.Rings.front().up;
		const float Departure = std::max(0.25f, Direction.cross(Anchor.up).length());
		const float Origin = GetRingDistance(Parent, size_t(Child.Attachment));
		return std::clamp(Origin + Direction.dot(Anchor.up) * Anchor.radius / Departure, 0.0f, Parent.Length);
	}

	bool ClaimPeachCollar(MeshStem& Mesh, MeshStem& Parent, const Stem& Item, float ExitDistance, float Angle) const
	{
		if (Form != EGrowthForm::Peach || Item.Role == EStemRole::Root || Parent.Sides < 6 || Item.Parent != 0)
		{
			return false;
		}
		const BranchRing& Anchor = Stems[Item.Parent].Rings[Item.Attachment];
		const float Departure = std::max(0.3f, Item.Rings.front().up.cross(Anchor.up).length());
		const float HalfLength = Item.Rings.front().radius * 0.95f / Departure;
		const float Ratio = std::clamp(Item.Rings.front().radius * 1.25f / Anchor.radius, 0.0f, 0.94f);
		const int32_t DesiredWidth =
			std::clamp(int32_t(std::ceil(std::asin(Ratio) * Parent.Sides / Pi)), 1, Parent.Sides / 2);
		const int32_t RowCount = int32_t(Parent.Rings.size());
		const int32_t FirstRow = std::clamp(
			int32_t(std::lower_bound(Parent.ArcLengths.begin(), Parent.ArcLengths.end(), ExitDistance - HalfLength) -
					Parent.ArcLengths.begin()),
			0, RowCount - 2);
		const int32_t LastRow = std::clamp(
			int32_t(std::lower_bound(Parent.ArcLengths.begin(), Parent.ArcLengths.end(), ExitDistance + HalfLength) -
					Parent.ArcLengths.begin()),
			FirstRow + 1, RowCount - 1);
		// A thick branch needs a footprint on the trunk, not a one-face slit. Reserve every face
		// inside that footprint so neighbouring departures cannot bridge through one another.
		for (int32_t Width = DesiredWidth; Width >= 1; --Width)
		{
			const int32_t Sector = int32_t(std::round(Angle / Tau * Parent.Sides - Width * 0.5f));
			const int32_t Column = (Sector + Parent.Sides * 3) % Parent.Sides;
			for (int32_t Height = std::min(LastRow - FirstRow, 8); Height >= 1; --Height)
			{
				for (int32_t Shift : {0, 1, -1, 2, -2})
				{
					const int32_t Start = FirstRow + (LastRow - FirstRow - Height) / 2 + Shift;
					const int32_t End = Start + Height;
					if (Start < 0 || End >= RowCount)
					{
						continue;
					}
					bool bAvailable = true;
					for (int32_t Row = Start; Row < End; ++Row)
					{
						for (int32_t Offset = 0; Offset < Width; ++Offset)
						{
							bAvailable =
								bAvailable && !Parent.Holes[Row * Parent.Sides + (Column + Offset) % Parent.Sides];
						}
					}
					if (!bAvailable)
					{
						continue;
					}
					const auto Vertex = [&Parent](int32_t Row, int32_t Col)
					{
						return Parent.Vertices[Row * Parent.Sides + Col % Parent.Sides];
					};
					for (int32_t Offset = 0; Offset <= Width; ++Offset)
					{
						Mesh.Port.push_back(Vertex(Start, Column + Offset));
					}
					for (int32_t Row = Start + 1; Row <= End; ++Row)
					{
						Mesh.Port.push_back(Vertex(Row, Column + Width));
					}
					for (int32_t Offset = Width - 1; Offset >= 0; --Offset)
					{
						Mesh.Port.push_back(Vertex(End, Column + Offset));
					}
					for (int32_t Row = End - 1; Row > Start; --Row)
					{
						Mesh.Port.push_back(Vertex(Row, Column));
					}
					for (int32_t Row = Start; Row < End; ++Row)
					{
						for (int32_t Offset = 0; Offset < Width; ++Offset)
						{
							Parent.Holes[Row * Parent.Sides + (Column + Offset) % Parent.Sides] = 1;
						}
					}
					return true;
				}
			}
		}
		return false;
	}

	void BuildWood()
	{
		WoodSurface Surface;
		Surface.bHasColors = true;
		Surface.bHasTangents = Form != EGrowthForm::Bamboo;
		Surface.bHasJoinWeights = Form == EGrowthForm::Peach;
		// A refined surface computes its normals once, after positions and connectivity settle.
		Surface.bAccumulateNormals = !Surface.bHasJoinWeights;
		std::vector<MeshStem> Meshes(Stems.size());
		std::vector<std::vector<size_t>> Children(Stems.size());
		for (size_t Index = 1; Index < Stems.size(); ++Index)
		{
			Children[Stems[Index].Parent].push_back(Index);
		}
		for (size_t Index = 0; Index < Stems.size() && !IsCancelled(); ++Index)
		{
			const Stem& Item = Stems[Index];
			MeshStem& Mesh = Meshes[Index];
			if (Item.Rings.front().radius < GetMinimumRadius(Item) ||
				(Item.Parent >= 0 && !Meshes[Item.Parent].bVisible))
			{
				continue;
			}
			Mesh.bVisible = true;
			Mesh.Sides =
				std::min(Options.RadialSegments, Item.Order == 0 ? 12 : (Item.Rings.front().radius > 0.045f ? 8 : 6));
			if (Form == EGrowthForm::Bamboo && Item.Parent < 0)
			{
				Mesh.Sides = std::min(Options.RadialSegments, 16);
			}
			else if (Form == EGrowthForm::Bamboo && Item.Order >= 2)
			{
				Mesh.Sides = std::min(Options.RadialSegments, 4);
			}
			if (Form != EGrowthForm::Bamboo && Item.Rings.front().radius < 0.035f)
			{
				const bool bFineAxis =
					Item.Role == EStemRole::Pendant || (Form == EGrowthForm::Peach && Item.Role == EStemRole::Shoot);
				Mesh.Sides = std::min(Options.RadialSegments, bFineAxis ? 3 : 4);
			}
			float Trim = 0.0f;
			if (Item.Parent >= 0)
			{
				const BranchRing& ParentRing = Stems[Item.Parent].Rings[Item.Attachment];
				const float Departure = std::max(0.25f, Item.Rings.front().up.cross(ParentRing.up).length());
				Trim = std::min(Item.Length * 0.45f, (ParentRing.radius + Item.Rings.front().radius) / Departure);
			}
			const float Step = Item.Length / float(Item.Rings.size() - 1);
			std::vector<Vector2> Sockets;
			for (const size_t ChildIndex : Children[Index])
			{
				const Stem& Child = Stems[ChildIndex];
				if (Child.Rings.front().radius < GetMinimumRadius(Child))
				{
					continue;
				}
				const float Departure =
					std::max(0.3f, Child.Rings.front().up.cross(Item.Rings[Child.Attachment].up).length());
				const float HalfWidth = std::clamp(Child.Rings.front().radius / Departure, Step * 0.08f, Step * 0.45f);
				const float Center = GetExitDistance(Child);
				Sockets.push_back(
					Vector2(std::max(Trim, Center - HalfWidth), std::min(Item.Length, Center + HalfWidth)));
			}
			// Place local rings around each socket instead of stretching a small twig across two long faces.
			std::vector<float> Samples{Trim, Item.Length};
			if (Form == EGrowthForm::Bamboo && Item.Parent < 0)
			{
				for (const float NodeDistance : CulmNodes)
				{
					for (const float Offset : {-0.018f, -0.012f, -0.006f, 0.0f, 0.012f, 0.045f})
					{
						Samples.push_back(std::clamp(NodeDistance + Offset, Trim, Item.Length));
					}
				}
			}
			for (const Vector2& Socket : Sockets)
			{
				if (Socket.x > Trim && Socket.x < Item.Length)
				{
					Samples.push_back(Socket.x);
				}
				if (Socket.y > Trim && Socket.y < Item.Length)
				{
					Samples.push_back(Socket.y);
				}
			}
			for (size_t Ring = 1; Ring + 1 < Item.Rings.size(); ++Ring)
			{
				if (!Item.ArcLengths.empty() && Ring % 3 != 1)
				{
					continue;
				}
				const float Distance = Item.ArcLengths.empty() ? float(Ring) * Step : Item.ArcLengths[Ring];
				bool bInSocket = false;
				for (const Vector2& Socket : Sockets)
				{
					bInSocket = bInSocket || (Distance >= Socket.x && Distance <= Socket.y);
				}
				if (!bInSocket && Distance > Trim)
				{
					Samples.push_back(Distance);
				}
			}
			std::sort(Samples.begin(), Samples.end());
			for (const float Distance : Samples)
			{
				const float Coordinate = Distance / Step;
				size_t Low = std::min(size_t(Coordinate), Item.Rings.size() - 2);
				float Blend = Coordinate - float(Low);
				if (!Item.ArcLengths.empty())
				{
					const size_t Upper =
						size_t(std::upper_bound(Item.ArcLengths.begin(), Item.ArcLengths.end(), Distance) -
							   Item.ArcLengths.begin());
					Low = std::min(Upper > 0 ? Upper - 1 : 0, Item.Rings.size() - 2);
					Blend = (Distance - Item.ArcLengths[Low]) / (Item.ArcLengths[Low + 1] - Item.ArcLengths[Low]);
				}
				BranchRing Ring = InterpolateRing(Item.Rings[Low], Item.Rings[Low + 1], Blend);
				if (Form == EGrowthForm::Bamboo && Item.Parent < 0)
				{
					Ring.radius = GetCulmRadius(Distance / Item.Length);
				}
				if (Mesh.Rings.empty() || Mesh.Rings.back().center.distance_squared_to(Ring.center) > 1e-10f)
				{
					Mesh.Rings.push_back(Ring);
					Mesh.ArcLengths.push_back(Distance);
				}
			}
			Mesh.Holes.assign((Mesh.Rings.size() - 1) * size_t(Mesh.Sides), 0);
			float Phase = 0.0f;
			if (Item.Parent >= 0)
			{
				MeshStem& Parent = Meshes[Item.Parent];
				const Vector3 Anchor = Item.Rings.front().center;
				const float ExitDistance = GetExitDistance(Item);
				size_t Nearest = 0;
				float Distance = std::numeric_limits<float>::max();
				for (size_t Ring = 0; Ring + 1 < Parent.Rings.size(); ++Ring)
				{
					const float Candidate =
						std::abs((Parent.ArcLengths[Ring] + Parent.ArcLengths[Ring + 1]) * 0.5f - ExitDistance);
					if (Candidate < Distance)
					{
						Distance = Candidate;
						Nearest = Ring;
					}
				}
				if (Parent.Rings.size() < 3)
				{
					Mesh.bVisible = false;
					continue;
				}
				const BranchRing& ParentRing = Parent.Rings[Nearest];
				const Vector3 Outward = Item.Rings[1].center - Anchor;
				const float Angle =
					std::atan2(Outward.dot(ParentRing.up.cross(ParentRing.right)), Outward.dot(ParentRing.right));
				const int32_t Width =
					Item.Rings.front().radius > ParentRing.radius * 0.65f && Parent.Sides >= 6 ? 2 : 1;
				const int32_t Sector = int32_t(std::round(Angle / Tau * Parent.Sides - float(Width) * 0.5f));
				bool bFound = ClaimPeachCollar(Mesh, Parent, Item, ExitDistance, Angle);
				if (bFound)
				{
					++Output.Growth.Junctions;
				}
				for (int32_t Shift : {0, 1, -1, 2, -2})
				{
					if (bFound)
					{
						break;
					}
					const int32_t Center = int32_t(Nearest) + Shift;
					if (Center < 0 || Center + 1 >= int32_t(Parent.Rings.size()))
					{
						continue;
					}
					const int32_t Column = (Sector + Parent.Sides * 3) % Parent.Sides;
					bool bAvailable = true;
					for (int32_t Offset = 0; Offset < Width; ++Offset)
					{
						if (Parent.Holes[Center * Parent.Sides + (Column + Offset) % Parent.Sides])
						{
							bAvailable = false;
						}
					}
					if (!bAvailable)
					{
						continue;
					}
					const auto Vertex = [&Parent](int32_t Row, int32_t Col)
					{
						return Parent.Vertices[Row * Parent.Sides + Col % Parent.Sides];
					};
					for (int32_t Offset = 0; Offset <= Width; ++Offset)
					{
						Mesh.Port.push_back(Vertex(Center, Column + Offset));
					}
					for (int32_t Offset = Width; Offset >= 0; --Offset)
					{
						Mesh.Port.push_back(Vertex(Center + 1, Column + Offset));
					}
					for (int32_t Offset = 0; Offset < Width; ++Offset)
					{
						Parent.Holes[Center * Parent.Sides + (Column + Offset) % Parent.Sides] = 1;
					}
					bFound = true;
					++Output.Growth.Junctions;
					break;
				}
				if (!bFound)
				{
					Mesh.bVisible = false;
					++Output.Growth.OmittedJunctions;
					continue;
				}
				const BranchRing& First = Mesh.Rings.front();
				Vector3 PortCenter = First.center;
				if (Form == EGrowthForm::Peach && Item.Parent == 0)
				{
					PortCenter = Vector3();
					for (const uint32_t Vertex : Mesh.Port)
					{
						PortCenter += Surface.Positions[Vertex];
					}
					PortCenter /= float(Mesh.Port.size());
				}
				const Vector3 ToPort = Surface.Positions[Mesh.Port[0]] - PortCenter;
				Phase = std::atan2(ToPort.dot(First.up.cross(First.right)), ToPort.dot(First.right));
			}
			for (size_t RingIndex = 0; RingIndex < Mesh.Rings.size(); ++RingIndex)
			{
				BranchRing& Ring = Mesh.Rings[RingIndex];
				Ring.right = Ring.right.rotated(Ring.up, Phase);
				const float Along = float(RingIndex) / float(Mesh.Rings.size() - 1);
				for (int32_t SideIndex = 0; SideIndex < Mesh.Sides; ++SideIndex)
				{
					const float Angle = float(SideIndex) * Tau / float(Mesh.Sides);
					const Vector3 Radial = Ring.right * std::cos(Angle) + Ring.up.cross(Ring.right) * std::sin(Angle);
					float Lobes = Item.Order == 0 && Form != EGrowthForm::Bamboo
									  ? 1.0f + 0.045f * std::cos(Angle * 3.0f + Along * 3.0f) * (1.0f - Along)
									  : 1.0f;
					const float Distance = Mesh.ArcLengths[RingIndex];
					if (Form == EGrowthForm::Bamboo && Item.Parent < 0)
					{
						const size_t Node = GetCulmNode(Distance);
						const float Local = (Distance - CulmNodes[Node]) / (CulmNodes[Node + 1] - CulmNodes[Node]);
						const float GrooveAngle = Node * Pi + Unit(uint32_t(Trunk.seed)) * Tau;
						// A shallow sulcus above each branch-bearing node, fading before the next collar.
						const float Groove = std::pow(std::max(0.0f, std::cos(Angle - GrooveAngle)), 18.0f);
						Lobes -= Groove * 0.10f * std::sin(Pi * Local) * Smooth(0.35f, 0.50f, Distance / Item.Length);
					}
					const uint32_t Vertex = Surface.AddVertex(Ring.center + Radial * (Ring.radius * Lobes),
															  Vector2(float(SideIndex) / Mesh.Sides, Distance));
					Mesh.Vertices.push_back(Vertex);
					if (Form == EGrowthForm::Peach)
					{
						if (Item.Parent < 0)
						{
							Surface.JoinWeights[Vertex] = Smooth(0.0f, Item.Length * 0.15f, Distance);
						}
						else if (Item.Parent == 0 || Item.Rings.front().radius > 0.045f)
						{
							Surface.JoinWeights[Vertex] =
								1.0f - Smooth(Trim, Trim + Item.Rings.front().radius * 4.0f, Distance);
						}
					}
					if (Form == EGrowthForm::Bamboo)
					{
						Surface.Colors[Vertex] = GetBambooColor(Item, Distance, Angle);
					}
					else
					{
						Surface.Colors[Vertex] = GetBarkColor(Item, Distance, Angle);
						Surface.Tangents[Vertex] =
							-Ring.right * std::sin(Angle) + Ring.up.cross(Ring.right) * std::cos(Angle);
					}
				}
			}
		}
		for (size_t Index = 0; Index < Meshes.size() && !IsCancelled(); ++Index)
		{
			const MeshStem& Mesh = Meshes[Index];
			if (!Mesh.bVisible)
			{
				continue;
			}
			for (size_t Ring = 0; Ring + 1 < Mesh.Rings.size(); ++Ring)
			{
				for (int32_t Side = 0; Side < Mesh.Sides; ++Side)
				{
					if (Mesh.Holes[Ring * Mesh.Sides + Side])
					{
						continue;
					}
					const uint32_t A = Mesh.Vertices[Ring * Mesh.Sides + Side];
					const uint32_t B = Mesh.Vertices[Ring * Mesh.Sides + (Side + 1) % Mesh.Sides];
					const uint32_t C = Mesh.Vertices[(Ring + 1) * Mesh.Sides + Side];
					const uint32_t D = Mesh.Vertices[(Ring + 1) * Mesh.Sides + (Side + 1) % Mesh.Sides];
					Surface.AddTriangle(A, B, C);
					Surface.AddTriangle(B, D, C);
				}
			}
			const std::vector<uint32_t> First(Mesh.Vertices.begin(), Mesh.Vertices.begin() + Mesh.Sides);
			if (!Mesh.Port.empty())
			{
				const bool bRoundJoin = Form == EGrowthForm::Peach &&
										(Stems[Index].Parent == 0 || Stems[Index].Rings.front().radius > 0.045f);
				if (bRoundJoin)
				{
					for (const uint32_t PortVertex : Mesh.Port)
					{
						Surface.JoinWeights[PortVertex] = 1.0f;
					}
				}
				std::vector<uint32_t> Middle;
				for (size_t Corner = 0; Corner < Mesh.Port.size(); ++Corner)
				{
					const float Coordinate = float(Corner) * Mesh.Sides / float(Mesh.Port.size());
					const size_t Low = size_t(Coordinate);
					const Vector3 Target = Surface.Positions[First[Low]].lerp(
						Surface.Positions[First[(Low + 1) % First.size()]], Coordinate - float(Low));
					const Vector3 Base = Surface.Positions[Mesh.Port[Corner]];
					const Vector3 Direction = SafeNormal(Target - Mesh.Rings.front().center);
					const uint32_t Vertex =
						Surface.AddVertex(Base.lerp(Target, 0.55f) +
											  Direction * Mesh.Rings.front().radius * Settings.JunctionShape * 0.10f,
										  Vector2(float(Corner) / Mesh.Port.size(), 0.0f));
					if (Surface.bHasColors)
					{
						Surface.Colors[Vertex] =
							Surface.Colors[Mesh.Port[Corner]].lerp(Surface.Colors[First[Low]], 0.55f);
					}
					Middle.push_back(Vertex);
					if (bRoundJoin)
					{
						Surface.JoinWeights[Vertex] = 1.0f;
					}
				}
				Surface.Bridge(Mesh.Port, Middle);
				Surface.Bridge(Middle, First);
			}
			else
			{
				const uint32_t Center = Surface.AddVertex(Mesh.Rings.front().center, Vector2());
				if (Surface.bHasColors)
				{
					Surface.Colors[Center] = Surface.Colors[First.front()];
				}
				for (int32_t Side = 0; Side < Mesh.Sides; ++Side)
				{
					Surface.AddTriangle(Center, First[(Side + 1) % Mesh.Sides], First[Side]);
				}
			}
			const uint32_t Tip = Surface.AddVertex(Mesh.Rings.back().center, Vector2(0.5f, Stems[Index].Length));
			if (Surface.bHasColors)
			{
				Surface.Colors[Tip] = Surface.Colors[Mesh.Vertices.back()];
			}
			const size_t Last = Mesh.Vertices.size() - Mesh.Sides;
			for (int32_t Side = 0; Side < Mesh.Sides; ++Side)
			{
				Surface.AddTriangle(Tip, Mesh.Vertices[Last + Side], Mesh.Vertices[Last + (Side + 1) % Mesh.Sides]);
			}
		}
		if (Form == EGrowthForm::Peach && !Surface.RefineJunctions(Options.Cancelled.get()))
		{
			return;
		}
		Surface.FinalizeNormals();
		if (Surface.bHasTangents)
		{
			// Split only the texture seam, preserving welded positions and angle-weighted shading.
			constexpr uint32_t NoSeamVertex = std::numeric_limits<uint32_t>::max();
			std::vector<uint32_t> SeamVertices(Surface.Positions.size(), NoSeamVertex);
			for (size_t Triangle = 0; Triangle < Surface.Indices.size(); Triangle += 3)
			{
				const float A = Surface.UVs[Surface.Indices[Triangle]].x;
				const float B = Surface.UVs[Surface.Indices[Triangle + 1]].x;
				const float C = Surface.UVs[Surface.Indices[Triangle + 2]].x;
				if (std::max({A, B, C}) - std::min({A, B, C}) <= 0.5f)
				{
					continue;
				}
				for (size_t Corner = 0; Corner < 3; ++Corner)
				{
					const uint32_t Original = Surface.Indices[Triangle + Corner];
					if (Surface.UVs[Original].x >= 0.5f)
					{
						continue;
					}
					if (SeamVertices[Original] != NoSeamVertex)
					{
						Surface.Indices[Triangle + Corner] = SeamVertices[Original];
						continue;
					}
					const Vector3 Position = Surface.Positions[Original];
					const Vector2 UV = Surface.UVs[Original] + Vector2(1, 0);
					const uint32_t Duplicate = Surface.AddVertex(Position, UV);
					Surface.Normals[Duplicate] = Surface.Normals[Original];
					Surface.Colors[Duplicate] = Surface.Colors[Original];
					Surface.Tangents[Duplicate] = Surface.Tangents[Original];
					SeamVertices[Original] = Duplicate;
					Surface.Indices[Triangle + Corner] = Duplicate;
				}
			}
		}
		MeshBatch Batch;
		Batch.material = Trunk.material;
		Batch.WoodColors = std::move(Surface.Colors);
		Batch.bBambooCulm = Form == EGrowthForm::Bamboo;
		Batch.BarkPreset = Form == EGrowthForm::Bamboo ? -1 : int32_t(Form);
		Batch.vertices.reserve(Surface.Positions.size() * 10);
		if (Surface.bHasTangents)
		{
			Batch.WoodTangents.reserve(Surface.Positions.size());
		}
		for (size_t Index = 0; Index < Surface.Positions.size(); ++Index)
		{
			const Vector3& P = Surface.Positions[Index];
			const Vector3 N = SafeNormal(Surface.Normals[Index]);
			const Vector2& UV = Surface.UVs[Index];
			if (Surface.bHasTangents)
			{
				const Vector3 Tangent =
					SafeNormal(Surface.Tangents[Index] - N * N.dot(Surface.Tangents[Index]), Perpendicular(N));
				Batch.WoodTangents.push_back(Vector4(Tangent.x, Tangent.y, Tangent.z, 1.0f));
			}
			Batch.vertices.insert(Batch.vertices.end(), {P.x, P.y, P.z, N.x, N.y, N.z, UV.x, UV.y, 0.0f, 0.0f});
		}
		Batch.indices = std::move(Surface.Indices);
		Output.Growth.WoodVertices += uint32_t(Surface.Positions.size());
		Output.batches.push_back(std::move(Batch));
	}

	void CollectFoliage()
	{
		if (!Options.bGenerateLeaves)
		{
			return;
		}
		for (const Stem& Item : Stems)
		{
			if (IsCancelled())
			{
				return;
			}
			if (Item.Role == EStemRole::ShortShoot || Item.Role == EStemRole::Pendant || Item.Role == EStemRole::Shoot)
			{
				for (const LeafClusterParams& Leaves : Foliage)
				{
					SlowTreeFoliage::CollectCards(Output, Item.Rings, Leaves.material, Options, Leaves.leafCount,
												  Leaves.leafSize, int32_t(Item.Seed & 0x7fffffffu),
												  Unit(Item.Seed) * Tau);
				}
			}
		}
	}
};
} // namespace

float TreeGrowthCurve::Evaluate(float Position, float Fallback) const
{
	if (!bOverride)
	{
		return Fallback;
	}
	const float Coordinate = std::clamp(Position, 0.0f, 1.0f) * float(Samples.size() - 1);
	const size_t Index = std::min(size_t(Coordinate), Samples.size() - 2);
	return Samples[Index] + (Samples[Index + 1] - Samples[Index]) * (Coordinate - float(Index));
}

bool SlowTreeGrowth::IsEnabled(const TreeFoliageOptions& Options)
{
	return Options.Growth.bEnabled && Options.bSpeciesRules && Options.bCrossedCards && Options.Preset >= 0 &&
		   Options.Preset <= 6;
}

void SlowTreeGrowth::Generate(const NodeGraph& Graph, const TreeFoliageOptions& Options, TreeMeshData& Output)
{
	// A bamboo clump has several independent roots in the graph. Stable ordering also makes seed
	// and budget behaviour independent of unordered_map iteration.
	std::vector<const TrunkNode*> Trunks;
	for (const auto& Entry : Graph.nodes())
	{
		if (Entry.second->getType() == NodeType::Trunk)
		{
			Trunks.push_back(static_cast<const TrunkNode*>(Entry.second.get()));
		}
	}
	std::sort(Trunks.begin(), Trunks.end(),
			  [](const TrunkNode* Left, const TrunkNode* Right)
			  {
				  return Left->id < Right->id;
			  });
	for (const TrunkNode* Trunk : Trunks)
	{
		GrowthBuilder Builder(Graph, *Trunk, Options, Output);
		Builder.Build();
	}
}
