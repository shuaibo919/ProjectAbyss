#include "TreeGen/TreeMeshBuilder.h"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>

using namespace TreeGen;
using godot::Array;
using godot::ArrayMesh;
using godot::Mesh;
using godot::PackedColorArray;
using godot::PackedInt32Array;
using godot::PackedVector2Array;
using godot::PackedVector3Array;
using godot::Ref;
using godot::Vector2;

namespace
{
	/** Step used for the finite-difference surface normals. */
	const float NORMAL_EPSILON = 0.002f;

	/** Radius below which bark counts as a green shoot rather than woody. */
	const float BARK_SMALL_RADIUS = 0.001f;
	/** Radius above which bark is fully woody. */
	const float BARK_BIG_RADIUS = 0.025f;

	/** Octaves of cloud noise baked into bark. The GPU mesh shader also used two. */
	const int32_t BARK_CLOUD_OCTAVES = 2;

	/** Copies a std::vector into the matching Godot packed array in one shot. */
	template <typename TPacked, typename TElement>
	TPacked ToPacked(const std::vector<TElement>& Source)
	{
		TPacked Result;
		if (Source.empty())
		{
			return Result;
		}

		Result.resize(int64_t(Source.size()));
		std::copy(Source.begin(), Source.end(), Result.ptrw());

		return Result;
	}

	/** Appends a triangle, skipping degenerate ones so clipped lobes cost nothing. */
	void AddTriangle(
		std::vector<int32_t>& Indices,
		const std::vector<Vector3>& Vertices,
		int32_t I0,
		int32_t I1,
		int32_t I2)
	{
		if (I0 == I1 || I1 == I2 || I0 == I2)
		{
			return;
		}

		const Vector3 Edge0 = Vertices[I1] - Vertices[I0];
		const Vector3 Edge1 = Vertices[I2] - Vertices[I0];
		if (Edge0.cross(Edge1).length_squared() <= 1e-16f)
		{
			return;
		}

		Indices.push_back(I0);
		Indices.push_back(I1);
		Indices.push_back(I2);
	}

	/**
	 * The nine outline points of one leaf half. The GPU built these as a mix of on-curve
	 * points and quadratic Bezier control points so a pixel shader could evaluate the
	 * silhouette analytically; indices 0/2/4/6/8 lie on the outline, 1/3/5/7 are controls.
	 */
	void ComputeLeafOutlinePoints(const LeafParams& P, uint32_t Seed, Vector2 OutPoints[9])
	{
		const float BotAngle = ToRadians(P.BotAngle);
		const float MidAngle = ToRadians(P.MidAngle);
		const float TopAngle = ToRadians(P.TopAngle) + 0.2f * Random::SignedValue(Seed, 222);

		const Vector2 Waypoint[3] = {
			Vector2(0.0f, 0.0f),
			Vector2(-0.5f + 0.1f * Random::SignedValue(Seed, 92), P.SideOffset + 0.1f * Random::SignedValue(Seed, 29)),
			Vector2(0.0f, 1.0f)
		};
		const Vector2 Tangent[3] = {
			Vector2(std::sin(BotAngle), std::cos(BotAngle)),
			Vector2(std::sin(MidAngle), std::cos(MidAngle)),
			Vector2(std::sin(TopAngle), std::cos(TopAngle))
		};

		// Rows 0 and 2 evaluate the Hermite curve at t = 0 and t = 0.5; rows 1 and 3 are the
		// equivalent Bezier handles.
		static const float BlendRow[4][4] = {
			{ 1.0f, 0.0f, 0.0f, 0.0f },
			{ 1.0f, 0.25f, 0.0f, 0.0f },
			{ 0.5f, 0.125f, 0.5f, 0.125f },
			{ 0.0f, 0.0f, 1.0f, 0.25f }
		};

		for (int32_t InHalf = 0; InHalf < 9; ++InHalf)
		{
			int32_t IsSecond = (InHalf > 3) ? 1 : 0;
			const float ChordLength = IsSecond
				? Waypoint[1].distance_to(Waypoint[2])
				: Waypoint[0].distance_to(Waypoint[1]);
			IsSecond += (InHalf > 7) ? 1 : 0;

			const int32_t Start = std::min(IsSecond, 2);
			const int32_t End = std::min(IsSecond + 1, 2);

			const Vector2 Control[4] = {
				Waypoint[Start],
				Tangent[Start] * ChordLength,
				Waypoint[End],
				Tangent[End] * -ChordLength
			};

			const float* Row = BlendRow[InHalf % 4];
			OutPoints[InHalf] = Control[0] * Row[0] + Control[1] * Row[1] + Control[2] * Row[2] + Control[3] * Row[3];
		}
	}

	/** One transformed leaf vertex, ready to append to the foliage surface. */
	struct LeafVertex
	{
		Vector3 Position;
		Vector3 Normal;
		Vector2 UV;
	};

	/**
	 * Applies the leaf vertex pipeline from the sample's mesh shader: mirror, scale, fold
	 * for the season, rotate into the lobe's fan position, then clip side lobes to a half.
	 *
	 * The normal is built in leaf-local space and carried through the same rotations, so it
	 * can be cupped across the blade. The original got that curvature from a per-pixel vein
	 * bump, which a baked mesh has no equivalent for.
	 */
	LeafVertex TransformLeafPoint(
		const Vector2& Local,
		bool bIsLeft,
		int32_t Lobe,
		const LeafParams& P,
		const LeafInstance& Instance,
		float Season)
	{
		Vector3 Position(Local.x, 0.0f, Local.y);
		if (bIsLeft)
		{
			Position.x = -Position.x;
		}

		// Blade coordinates in [-1, 1] x [0, 1], captured before any scaling.
		const float BladeX = Position.x;

		const int32_t LobeCount = std::max(1, P.Lobes);
		const float CenterLobeF = float(LobeCount - 1) * 0.5f;
		const int32_t CenterLobe = int32_t(CenterLobeF);
		const float LobeScale = 1.0f - P.LobeFalloff * std::abs(float(Lobe) - CenterLobeF);

		Vector2 LobeTexCoord(Position.x, Position.z);
		LobeTexCoord.x *= P.ScaleX;

		Position *= Instance.Scale * LobeScale;
		Position.x *= P.ScaleX;

		// Narrowing x by ScaleX steepens the blade, so the normal's x scales inversely.
		Vector3 LocalNormal(-P.Curl * BladeX / std::fmax(P.ScaleX, 0.01f), 1.0f, 0.0f);
		LocalNormal = LocalNormal.normalized();

		Vector3 Normal;
		if (Instance.bIsBlossom)
		{
			// Petals fan out around the flower's axis and tilt up by LobeAngle.
			const float PetalStep = TREE_TAU / float(LobeCount);
			const float PetalAngle = float(Lobe) * PetalStep;

			float Cup = PetalStep * 0.25f;
			Cup = (Position.x > 0.0f) ? Cup : -Cup;

			const Quaternion PetalRotation =
				QRotateZ(PetalAngle) * (QRotateX(ToRadians(P.LobeAngle)) * QRotateZ(Cup));

			Position = QTransform(PetalRotation, Position);
			Normal = QTransform(Instance.Rotation * PetalRotation, LocalNormal);
		}
		else
		{
			// Leaves curl along their midrib as autumn progresses.
			float Fold = std::fmax(0.0f, Season - 2.0f) * ToRadians(20.0f);
			Fold = (Position.x > 0.0f) ? Fold : -Fold;

			const Vector2 Folded = Rotate2D(Vector2(Position.x, Position.y), Fold);
			Position.x = Folded.x;
			Position.y = Folded.y;

			const Vector2 FoldedNormal = Rotate2D(Vector2(LocalNormal.x, LocalNormal.y), Fold);
			LocalNormal.x = FoldedNormal.x;
			LocalNormal.y = FoldedNormal.y;

			const float LobeRotation = ToRadians(-P.LobeAngle * CenterLobeF + P.LobeAngle * float(Lobe));
			const Vector2 Rotated = Rotate2D(Vector2(Position.x, Position.z), LobeRotation);
			Position.x = Rotated.x;
			Position.z = Rotated.y;

			// The lobe fan turns about the blade normal, so only x and z move.
			const Vector2 RotatedNormal = Rotate2D(Vector2(LocalNormal.x, LocalNormal.z), LobeRotation);
			LocalNormal.x = RotatedNormal.x;
			LocalNormal.z = RotatedNormal.y;

			Normal = QTransform(Instance.Rotation, LocalNormal);

			// Side lobes are half-leaves clipped against the midrib.
			if (Lobe < CenterLobe)
			{
				Position.x = std::fmax(0.0f, Position.x);
			}
			else if (Lobe > CenterLobe)
			{
				Position.x = std::fmin(0.0f, Position.x);
			}
		}

		// A hair of thickness so overlapping lobes do not z-fight.
		if (std::abs(LobeScale) > 1e-6f)
		{
			Position.y -= std::abs(LobeTexCoord.x / LobeScale * 0.001f);
		}

		LeafVertex Result;
		Result.Position = Instance.Position + QTransform(Instance.Rotation, Position);
		Result.Normal = Normal.normalized();
		// Local x spans [-1, 1] and z spans [0, 1], which maps cleanly onto a leaf atlas.
		Result.UV = Vector2(Local.x * 0.5f + 0.5f, Local.y);

		return Result;
	}
} // namespace

// ==================== Surface ====================

void TreeMeshBuilder::Surface::Clear()
{
	Vertices.clear();
	Normals.clear();
	UVs.clear();
	Colors.clear();
	Indices.clear();
}

void TreeMeshBuilder::Surface::Reserve(size_t VertexCount, size_t IndexCount)
{
	Vertices.reserve(VertexCount);
	Normals.reserve(VertexCount);
	UVs.reserve(VertexCount);
	Colors.reserve(VertexCount);
	Indices.reserve(IndexCount);
}

// ==================== Build ====================

void TreeMeshBuilder::Build(
	const TreeSkeleton& Skeleton,
	const TreeParams& InParams,
	const GenerationContext& InContext,
	const MeshQuality& InQuality)
{
	Params = InParams;
	Context = InContext;
	Quality = InQuality;
	Quality.RadialSegments = ClampInt(Quality.RadialSegments, 3, 64);
	Quality.RingsPerSegment = ClampInt(Quality.RingsPerSegment, 1, 16);
	Quality.LeafArcSegments = ClampInt(Quality.LeafArcSegments, 1, 8);
	Quality.FruitLongitudes = ClampInt(Quality.FruitLongitudes, 4, 32);
	Quality.FruitBands = ClampInt(Quality.FruitBands, 3, 24);

	Bark.Clear();
	Foliage.Clear();
	Fruit.Clear();
	SurfaceKinds.clear();

	BuildBark(Skeleton.GetSegments());

	if (Quality.bGenerateLeaves)
	{
		BuildFoliage(Skeleton.GetLeaves());
	}
	if (Quality.bGenerateFruit)
	{
		BuildFruit(Skeleton.GetFruits());
	}

	if (!Bark.IsEmpty())
	{
		SurfaceKinds.push_back(SURFACE_BARK);
	}
	if (!Foliage.IsEmpty())
	{
		SurfaceKinds.push_back(SURFACE_FOLIAGE);
	}
	if (!Fruit.IsEmpty())
	{
		SurfaceKinds.push_back(SURFACE_FRUIT);
	}
}

int32_t TreeMeshBuilder::GetVertexCount() const
{
	return int32_t(Bark.Vertices.size() + Foliage.Vertices.size() + Fruit.Vertices.size());
}

int32_t TreeMeshBuilder::GetTriangleCount() const
{
	return int32_t((Bark.Indices.size() + Foliage.Indices.size() + Fruit.Indices.size()) / 3);
}

// ==================== Bark ====================

TreeMeshBuilder::BarkSample TreeMeshBuilder::SampleBark(const StemSegment& Segment, float Theta, float V) const
{
	const SegmentInfo& Si = Segment.Si;

	BarkSample Sample;

	const Vector3 Center = StemSpline(
		Segment.FromPos, QGetZ(Segment.FromRot),
		Segment.ToPos, QGetZ(Segment.ToRot), V);
	const Quaternion Rot = QSlerp(Segment.FromRot, Segment.ToRot, V);
	const float Z = Lerp(Si.FromZ, Si.ToZ, V);

	float Radius = GetTaperedRadius(Si, Params.Taper[Si.Level], Params.Flare, Z);

	// Non-circular cross-section only exists at the very foot of the trunk.
	if (Si.Level == 0 && Si.FromZ == 0.0f && Params.Lobes > 0)
	{
		Radius *= GetLobeFactor(Params.Lobes, Params.LobeDepth, Theta, V);
	}

	Sample.RadialDirection = QGetX(Rot * QRotateZ(Theta));

	if (Quality.bBarkDetail && Params.StemBumpStrength > 0.0f)
	{
		const float SafeRadius = std::fmax(Si.Radius, 1e-4f);
		// Repeat count chosen so bark cells stay roughly square regardless of stem girth.
		const int32_t NoiseScale = std::max(1, int32_t(TREE_PI * Si.Radius + 1.3f));
		const Vector2 NormalizedUV(Theta / TREE_PI, (Si.Length * Z * 2.0f) / (4.0f * SafeRadius));
		const Vector2 UV = NormalizedUV * float(NoiseScale);

		const float Cloud = Bark::CloudNoiseWrapX(UV * Vector2(8, 2), NoiseScale * 8, BARK_CLOUD_OCTAVES);
		const float Warp = Bark::WrappingXPerlinNoise(UV * 2.0f, NoiseScale * 2);

		float Cracks = 0.0f;
		if (Si.Level < 2)
		{
			// Distance-to-cell-border noise reads as bark fissures; capped so plates stay flat.
			const Vector2 WarpedUV = (UV + Vector2(0.03f * Warp, 0.03f * Warp)) * Vector2(8, 1);
			Cracks = std::fmin(Bark::VoronoiDistanceWrapX(WarpedUV, NoiseScale * 8), Params.StemBumpGapSize) * 4.0f;
		}

		float Bump = Lerp(Cloud, Cracks, Params.StemBumpVoronoiWeight);
		Bump *= 1.0f + float(Si.Level);

		// Out of season, snow piles up on upward-facing bark.
		const float SeasonDistance = std::abs(Context.Season - 2.0f);
		const float Up = Sample.RadialDirection.y;
		if (Up > 0.5f && SeasonDistance > 1.75f)
		{
			Bump = Lerp(Bump, Up * 10.0f, (Up - 0.5f) * 2.0f * (SeasonDistance - 1.75f) / 0.25f);
		}

		Sample.Bump = Bump;
		Sample.Cloud = Cloud;
		Sample.NoiseUV = UV;

		Radius *= 1.0f + Bump * 0.12f * Params.StemBumpStrength;
	}

	Sample.Position = Center + Sample.RadialDirection * Radius;

	return Sample;
}

Color TreeMeshBuilder::ShadeBark(const StemSegment& Segment, const BarkSample& Sample, float V) const
{
	const SegmentInfo& Si = Segment.Si;

	const float SeasonDistance = std::abs(Context.Season - 2.0f);
	const bool bIsSnow = Sample.RadialDirection.y > 0.5f && SeasonDistance > 1.75f;

	Color BaseColor;
	if (bIsSnow)
	{
		BaseColor = Color(1, 1, 1, 1);
	}
	else
	{
		// Thin shoots are still green; thick stems are woody.
		const float Woodiness = Saturate((Si.Radius - BARK_SMALL_RADIUS) / (BARK_BIG_RADIUS - BARK_SMALL_RADIUS));
		BaseColor = Params.StemSmallColor.lerp(Params.StemBigColor, Woodiness);

		if (Quality.bBarkDetail)
		{
			const bool bHasLichen =
				(Bark::Voronoi(Sample.NoiseUV * Params.StemLichenFrequency) + 0.6f * Sample.Cloud)
					< (Params.StemLichenSize - 0.5f)
				&& Sample.Bump > 0.3f;

			if (bHasLichen)
			{
				BaseColor *= Color(1.5f, 1.65f, 1.5f, 1.0f);
			}

			if (Params.bStemBirchTexture)
			{
				const Vector2 BirchUV = Sample.NoiseUV * Vector2(4.0f, 1.25f);
				const int32_t NoiseScale = std::max(1, int32_t(TREE_PI * Si.Radius + 1.3f));
				const float Birch = Bark::CloudNoiseWrapX(BirchUV, NoiseScale * 4, BARK_CLOUD_OCTAVES);

				const float Z = Lerp(Si.FromZ, Si.ToZ, V);
				const float Threshold = (Si.Level == 0) ? (1.0f - Z - 0.6f) : -0.1f;
				if (Birch > Threshold)
				{
					BaseColor = Color(0.5f, 0.5f, 0.5f, 1.0f);
				}
			}
		}
	}

	const float Occlusion = FakeAOFromDistance(
		Segment.AoDistance + ((1.0f - V) * Si.Length) / float(std::max(1, Params.CurveRes[Si.Level])));

	return Color(BaseColor.r * Occlusion, BaseColor.g * Occlusion, BaseColor.b * Occlusion, 1.0f);
}

void TreeMeshBuilder::BuildBark(const std::vector<StemSegment>& Segments)
{
	const int32_t Rings = Quality.RingsPerSegment;

	// The seam vertex is duplicated so UVs can wrap, hence Radial + 1 per ring.
	const size_t VerticesPerSegment = size_t(Quality.RadialSegments + 1) * size_t(Rings + 1);
	Bark.Reserve(
		Segments.size() * VerticesPerSegment,
		Segments.size() * size_t(Quality.RadialSegments) * size_t(Rings) * 6);

	for (const StemSegment& Segment : Segments)
	{
		const int32_t BaseIndex = int32_t(Bark.Vertices.size());
		const int32_t Radial = Quality.GetRadialSegmentsForLevel(Segment.Si.Level);

		for (int32_t Ring = 0; Ring <= Rings; ++Ring)
		{
			const float V = float(Ring) / float(Rings);

			for (int32_t Column = 0; Column <= Radial; ++Column)
			{
				const float Theta = (float(Column) / float(Radial)) * TREE_TAU;

				const BarkSample Sample = SampleBark(Segment, Theta, V);

				// Normals come from finite differences of the same surface function, which keeps
				// them continuous across both the circumferential seam and the segment joints.
				const Vector3 ThetaPlus = SampleBark(Segment, Theta + NORMAL_EPSILON, V).Position;
				const Vector3 ThetaMinus = SampleBark(Segment, Theta - NORMAL_EPSILON, V).Position;
				const Vector3 VPlus = SampleBark(Segment, Theta, std::fmin(V + NORMAL_EPSILON, 1.0f)).Position;
				const Vector3 VMinus = SampleBark(Segment, Theta, std::fmax(V - NORMAL_EPSILON, 0.0f)).Position;

				Vector3 Normal = (ThetaPlus - ThetaMinus).cross(VPlus - VMinus);
				if (Normal.length_squared() > 1e-16f)
				{
					Normal = Normal.normalized();
					if (Normal.dot(Sample.RadialDirection) < 0.0f)
					{
						Normal = -Normal;
					}
				}
				else
				{
					Normal = Sample.RadialDirection;
				}

				Bark.Vertices.push_back(Sample.Position);
				Bark.Normals.push_back(Normal);
				Bark.UVs.push_back(Vector2(
					float(Column) / float(Radial),
					Lerp(Segment.Si.FromZ, Segment.Si.ToZ, V) * Segment.Si.Length));
				Bark.Colors.push_back(ShadeBark(Segment, Sample, V));
			}
		}

		for (int32_t Ring = 0; Ring < Rings; ++Ring)
		{
			for (int32_t Column = 0; Column < Radial; ++Column)
			{
				const int32_t I00 = BaseIndex + Ring * (Radial + 1) + Column;
				const int32_t I01 = I00 + 1;
				const int32_t I10 = I00 + (Radial + 1);
				const int32_t I11 = I10 + 1;

				// Wound so that Godot sees the outward-facing side as the front face.
				AddTriangle(Bark.Indices, Bark.Vertices, I00, I10, I01);
				AddTriangle(Bark.Indices, Bark.Vertices, I01, I10, I11);
			}
		}

		// Only the trunk's very first ring is an open hole. Branches start on their parent's
		// axis rather than its surface, so their base rings are already enclosed, and stem
		// tips close to a point because the taper drives the radius to zero.
		if (Segment.Si.Level == 0 && Segment.Si.FromZ == 0.0f)
		{
			AddStemBaseCap(Segment, Radial);
		}
	}
}

void TreeMeshBuilder::AddStemBaseCap(const StemSegment& Segment, int32_t Radial)
{
	// The rim is duplicated rather than shared, so the cap can carry its own downward normal
	// instead of inheriting the tube's outward one.
	const Vector3 Normal = -QGetZ(Segment.FromRot);
	const int32_t CentreIndex = int32_t(Bark.Vertices.size());
	const Color CentreColor = ShadeBark(Segment, SampleBark(Segment, 0.0f, 0.0f), 0.0f);

	Bark.Vertices.push_back(Segment.FromPos);
	Bark.Normals.push_back(Normal);
	Bark.UVs.push_back(Vector2(0.5f, 0.5f));
	Bark.Colors.push_back(CentreColor);

	for (int32_t Column = 0; Column <= Radial; ++Column)
	{
		const float Theta = (float(Column) / float(Radial)) * TREE_TAU;
		const BarkSample Sample = SampleBark(Segment, Theta, 0.0f);

		Bark.Vertices.push_back(Sample.Position);
		Bark.Normals.push_back(Normal);
		Bark.UVs.push_back(Vector2(0.5f + 0.5f * std::cos(Theta), 0.5f + 0.5f * std::sin(Theta)));
		Bark.Colors.push_back(ShadeBark(Segment, Sample, 0.0f));
	}

	for (int32_t Column = 0; Column < Radial; ++Column)
	{
		AddTriangle(
			Bark.Indices, Bark.Vertices,
			CentreIndex, CentreIndex + 1 + Column, CentreIndex + 2 + Column);
	}
}

// ==================== Foliage ====================

void TreeMeshBuilder::BuildFoliage(const std::vector<LeafInstance>& Leaves)
{
	const size_t PointsPerSide = size_t(Quality.LeafArcSegments) * 4 + 1;
	Foliage.Reserve(Leaves.size() * PointsPerSide * 2, Leaves.size() * PointsPerSide * 6);

	for (const LeafInstance& Instance : Leaves)
	{
		if (Instance.Scale <= 0.0f)
		{
			continue;
		}

		const LeafParams& LeafP = Instance.bIsBlossom ? Params.Blossom : Params.Leaf;

		if (LeafP.bIsNeedle)
		{
			BuildNeedleLeaf(Instance, LeafP);
		}
		else
		{
			BuildLeaf(Instance, LeafP);
		}
	}
}

void TreeMeshBuilder::BuildLeaf(const LeafInstance& Instance, const LeafParams& LeafP)
{
	Vector2 Outline[9];
	ComputeLeafOutlinePoints(LeafP, Instance.Seed, Outline);

	// Four quadratic arcs run from the leaf base, around the widest point, to the tip.
	const int32_t ArcSegments = Quality.LeafArcSegments;
	const int32_t PointCount = ArcSegments * 4 + 1;

	std::vector<Vector2> Samples;
	Samples.reserve(size_t(PointCount));
	Samples.push_back(Outline[0]);
	for (int32_t Arc = 0; Arc < 4; ++Arc)
	{
		const Vector2& P0 = Outline[Arc * 2 + 0];
		const Vector2& P1 = Outline[Arc * 2 + 1];
		const Vector2& P2 = Outline[Arc * 2 + 2];

		for (int32_t Step = 1; Step <= ArcSegments; ++Step)
		{
			Samples.push_back(QuadraticBezier(P0, P1, P2, float(Step) / float(ArcSegments)));
		}
	}

	// Blossoms place one petal per lobe around the flower; leaves fan their lobes sideways.
	const int32_t LobeCount = ClampInt(LeafP.Lobes, 1, 5);

	// Occlusion also shifts the perceived season, so shaded leaves turn colour later.
	const float Occlusion = FakeAOFromDistance(Instance.AoDistance);
	const float ShadedSeason = GetNoisedLeafSeason(Instance.Seed, Context.Season)
		+ LeafP.SeasonOffset + 0.5f * -(Occlusion - 0.5f);
	const Color SeasonColor = GetSeasonLeafColor(
		LeafP.LeafColor, ShadedSeason, LeafP.bIsNeedle || LeafP.bEvergreen, Instance.bIsBlossom);

	// Per-leaf brightness spread; a canopy of one exact green reads as plastic.
	const float Tint = Occlusion
		* std::fmax(0.0f, 1.0f + LeafP.ColorJitter * Random::SignedValue(Instance.Seed, 0x1EAF));
	const Color LeafColor(
		SeasonColor.r * Tint, SeasonColor.g * Tint, SeasonColor.b * Tint, 1.0f);

	for (int32_t Lobe = 0; Lobe < LobeCount; ++Lobe)
	{
		const int32_t BaseIndex = int32_t(Foliage.Vertices.size());

		for (int32_t Point = 0; Point < PointCount; ++Point)
		{
			for (int32_t Side = 0; Side < 2; ++Side)
			{
				const LeafVertex Vertex = TransformLeafPoint(
					Samples[size_t(Point)], Side == 1, Lobe, LeafP, Instance, Context.Season);

				Foliage.Vertices.push_back(Vertex.Position);
				Foliage.Normals.push_back(Vertex.Normal);
				Foliage.UVs.push_back(Vertex.UV);
				Foliage.Colors.push_back(LeafColor);
			}
		}

		// Stitch the right and left outlines into a strip. The base and tip collapse to a
		// single point, so those quads degenerate into triangles and AddTriangle drops the rest.
		for (int32_t Point = 0; Point < PointCount - 1; ++Point)
		{
			const int32_t Right0 = BaseIndex + Point * 2;
			const int32_t Left0 = Right0 + 1;
			const int32_t Right1 = Right0 + 2;
			const int32_t Left1 = Right0 + 3;

			AddTriangle(Foliage.Indices, Foliage.Vertices, Right0, Right1, Left1);
			AddTriangle(Foliage.Indices, Foliage.Vertices, Right0, Left1, Left0);
		}
	}
}

void TreeMeshBuilder::BuildNeedleLeaf(const LeafInstance& Instance, const LeafParams& LeafP)
{
	// The GPU renders a needle fascicle as four flat blades whose pixel shader discards
	// everything but ~20 hair-thin needles per blade. Baking that many primitives is not
	// worth it, so each blade becomes one tapered spike and NeedleBlades supplies the volume.
	const int32_t BladeCount = ClampInt(LeafP.NeedleBlades, 2, 10);
	const int32_t Steps = std::max(2, Quality.LeafArcSegments + 1);
	const float HalfWidth = 0.25f * LeafP.ScaleX;

	const float Occlusion = FakeAOFromDistance(Instance.AoDistance);
	const Color NeedleColor = LeafP.LeafColor * 1.1f;
	const float Tint = Occlusion
		* std::fmax(0.0f, 1.0f + LeafP.ColorJitter * Random::SignedValue(Instance.Seed, 0x1EAF));
	const Color VertexColor(
		NeedleColor.r * Tint, NeedleColor.g * Tint, NeedleColor.b * Tint, 1.0f);

	for (int32_t Blade = 0; Blade < BladeCount; ++Blade)
	{
		const int32_t BaseIndex = int32_t(Foliage.Vertices.size());
		const float BladeAngle = 0.75f * TREE_PI * float(Blade);

		const Quaternion BladeRotation = QRotateZ(BladeAngle);
		const Vector3 Normal = QGetY(Instance.Rotation * BladeRotation);

		for (int32_t Step = 0; Step < Steps; ++Step)
		{
			const float Z = float(Step) / float(Steps - 1);
			// Taper follows the sample's silhouette test, |x| <= 0.1 * (1 - z)^(1/6).
			const float Width = HalfWidth * std::pow(std::fmax(0.0f, 1.0f - Z), 1.0f / 6.0f);

			for (int32_t Side = 0; Side < 2; ++Side)
			{
				Vector3 Position((Side == 0) ? -Width : Width, 0.0f, Z);
				Position *= Instance.Scale;

				const Vector2 Rotated = Rotate2D(Vector2(Position.x, Position.y), BladeAngle);
				Position.x = Rotated.x;
				Position.y = Rotated.y;

				Foliage.Vertices.push_back(Instance.Position + QTransform(Instance.Rotation, Position));
				Foliage.Normals.push_back(Normal);
				Foliage.UVs.push_back(Vector2(float(Side), Z));
				Foliage.Colors.push_back(VertexColor);
			}
		}

		for (int32_t Step = 0; Step < Steps - 1; ++Step)
		{
			const int32_t I00 = BaseIndex + Step * 2;
			const int32_t I01 = I00 + 1;
			const int32_t I10 = I00 + 2;
			const int32_t I11 = I00 + 3;

			AddTriangle(Foliage.Indices, Foliage.Vertices, I00, I10, I11);
			AddTriangle(Foliage.Indices, Foliage.Vertices, I00, I11, I01);
		}
	}
}

// ==================== Fruit ====================

void TreeMeshBuilder::BuildFruit(const std::vector<LeafInstance>& Fruits)
{
	if (Fruits.empty())
	{
		return;
	}

	const int32_t Longitudes = Quality.FruitLongitudes;
	// Bands include the two poles, matching the sample's baked 16 x 9 sphere.
	const int32_t Bands = Quality.FruitBands;

	const Vector2 ShapeStart(0.0f, 0.0f);
	const Vector2 ShapeMid0(Params.Fruit.Shape[0], Params.Fruit.Shape[1]);
	const Vector2 ShapeMid1(Params.Fruit.Shape[2], Params.Fruit.Shape[3]);
	const Vector2 ShapeEnd(0.0f, 1.0f);

	Fruit.Reserve(
		Fruits.size() * size_t(Longitudes + 1) * size_t(Bands + 1),
		Fruits.size() * size_t(Longitudes) * size_t(Bands) * 6);

	for (const LeafInstance& Instance : Fruits)
	{
		if (Instance.Scale <= 0.0f)
		{
			continue;
		}

		const int32_t BaseIndex = int32_t(Fruit.Vertices.size());
		const float Progress = GetSeasonFruitProgress(Instance.Seed, Context.Season);

		for (int32_t Band = 0; Band <= Bands; ++Band)
		{
			// A latitude sweep gives the same z distribution the sample's vertex table had.
			const float Latitude = TREE_PI * float(Band) / float(Bands);
			const float T = Saturate(0.5f + 0.5f * std::cos(Latitude));

			// Revolve the cubic Bezier profile: x is the radius, y the height along the axis.
			const Vector2 Profile = CubicBezier(ShapeStart, ShapeMid0, ShapeMid1, ShapeEnd, T);

			for (int32_t Longitude = 0; Longitude <= Longitudes; ++Longitude)
			{
				const float Phi = TREE_TAU * float(Longitude) / float(Longitudes);
				const Vector3 Direction(std::cos(Phi), std::sin(Phi), 0.0f);

				const Vector3 Local = Vector3(
					Profile.x * Direction.x,
					Profile.x * Direction.y,
					Profile.y) * Instance.Scale;

				// The profile's slope gives the exact surface normal of the revolution.
				const Vector2 Slope = CubicBezier(ShapeStart, ShapeMid0, ShapeMid1, ShapeEnd,
					Saturate(T + 0.001f)) - Profile;
				Vector3 LocalNormal(Slope.y * Direction.x, Slope.y * Direction.y, -Slope.x);
				if (LocalNormal.length_squared() <= 1e-16f)
				{
					LocalNormal = Vector3(Direction.x, Direction.y, 0.0f);
				}
				LocalNormal = LocalNormal.normalized();

				// Unripe fruit keeps the leaf's colour and shifts towards its own as it ripens.
				const float VertexProgress = Saturate(Progress + (1.0f - T) * 0.5f);
				const Color Blended = Params.Leaf.LeafColor.lerp(
					Params.Fruit.FruitColor, Saturate(0.3f + VertexProgress * VertexProgress));

				Fruit.Vertices.push_back(Instance.Position + QTransform(Instance.Rotation, Local));
				Fruit.Normals.push_back(QTransform(Instance.Rotation, LocalNormal));
				Fruit.UVs.push_back(Vector2(float(Longitude) / float(Longitudes), T));
				Fruit.Colors.push_back(Color(Blended.r, Blended.g, Blended.b, 1.0f));
			}
		}

		for (int32_t Band = 0; Band < Bands; ++Band)
		{
			for (int32_t Longitude = 0; Longitude < Longitudes; ++Longitude)
			{
				const int32_t I00 = BaseIndex + Band * (Longitudes + 1) + Longitude;
				const int32_t I01 = I00 + 1;
				const int32_t I10 = I00 + (Longitudes + 1);
				const int32_t I11 = I10 + 1;

				AddTriangle(Fruit.Indices, Fruit.Vertices, I00, I01, I10);
				AddTriangle(Fruit.Indices, Fruit.Vertices, I01, I11, I10);
			}
		}
	}
}

// ==================== Mesh assembly ====================

Array TreeMeshBuilder::ToArrays(const Surface& InSurface) const
{
	Array Arrays;
	Arrays.resize(Mesh::ARRAY_MAX);
	Arrays[Mesh::ARRAY_VERTEX] = ToPacked<PackedVector3Array>(InSurface.Vertices);
	Arrays[Mesh::ARRAY_NORMAL] = ToPacked<PackedVector3Array>(InSurface.Normals);
	Arrays[Mesh::ARRAY_TEX_UV] = ToPacked<PackedVector2Array>(InSurface.UVs);
	Arrays[Mesh::ARRAY_COLOR] = ToPacked<PackedColorArray>(InSurface.Colors);
	Arrays[Mesh::ARRAY_INDEX] = ToPacked<PackedInt32Array>(InSurface.Indices);

	return Arrays;
}

Ref<ArrayMesh> TreeMeshBuilder::CreateMesh() const
{
	Ref<ArrayMesh> Result(memnew(ArrayMesh));

	for (const ESurfaceKind Kind : SurfaceKinds)
	{
		switch (Kind)
		{
			case SURFACE_BARK:
				Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, ToArrays(Bark));
				break;
			case SURFACE_FOLIAGE:
				Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, ToArrays(Foliage));
				break;
			case SURFACE_FRUIT:
				Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, ToArrays(Fruit));
				break;
		}
	}

	return Result;
}
