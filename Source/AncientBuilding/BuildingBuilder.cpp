#include "AncientBuilding/BuildingBuilder.h"

#include "AncientBuilding/TileSkin.h"

#include <algorithm>
#include <cmath>

using namespace BuildingGen;

namespace
{
	const float BUILD_PI = 3.14159265358979323846f;
	const float BUILD_TAU = 2.0f * BUILD_PI;
	const float BUILD_EPSILON = 1e-6f;

	/** Ridge tile: flat soffit, shoulders, rounded crown. */
	std::vector<Vector2> MakeRidgeContour(float Scale)
	{
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.5f, -0.10f) * Scale);
		Contour.push_back(Vector2(0.5f, -0.10f) * Scale);
		Contour.push_back(Vector2(0.5f, 0.30f) * Scale);
		Contour.push_back(Vector2(0.30f, 0.62f) * Scale);
		Contour.push_back(Vector2(0.0f, 0.75f) * Scale);
		Contour.push_back(Vector2(-0.30f, 0.62f) * Scale);
		Contour.push_back(Vector2(-0.5f, 0.30f) * Scale);

		return Contour;
	}

	/**
	 * 连檐: the timber board the tile courses die onto at the eave.
	 *
	 * This used to be a fat bar standing in for the whole 滴水 course. Now that the tile skin hangs
	 * a real 滴水 from every channel and caps every barrel with a 瓦当, the bar's only remaining job
	 * is to be the board behind them — so it is slim, and TuckedEaveKnot drops it clear of the
	 * tiles instead of leaving it coincident with them, where it hid the lot.
	 */
	std::vector<Vector2> MakeEaveContour(float Scale)
	{
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.5f, -0.16f) * Scale);
		Contour.push_back(Vector2(0.5f, -0.16f) * Scale);
		Contour.push_back(Vector2(0.5f, 0.16f) * Scale);
		Contour.push_back(Vector2(-0.5f, 0.16f) * Scale);

		return Contour;
	}

	/** Scale for the 连檐 board, slim enough to sit behind the eave tiles rather than over them. */
	float EaveBoardScale(const BuildingSpec& Spec)
	{
		return Spec.Module * 0.42f * Spec.RidgeScale;
	}

	/**
	 * Moves an eave knot down and back up the slope, so the board lands under the tile ends.
	 * OutwardZ is the sign of the eave's outward direction along Z, or 0 for an eave running along Z.
	 */
	Vector3 TuckedEaveKnot(const BuildingSpec& Spec, const Vector3& Knot, float OutwardZ, float OutwardX)
	{
		const float Drop = Spec.Module * 0.20f;
		const float Back = Spec.Module * 0.26f;

		return Knot + Vector3(-OutwardX * Back, -Drop, -OutwardZ * Back);
	}

	/** Even bay boundary positions across a span, inclusive of both ends. */
	std::vector<float> BayPositions(float HalfSpan, int32_t Bays)
	{
		const int32_t Count = std::max(Bays, 1);
		std::vector<float> Result;
		for (int32_t Index = 0; Index <= Count; ++Index)
		{
			Result.push_back(-HalfSpan + (2.0f * HalfSpan) * float(Index) / float(Count));
		}

		return Result;
	}

	/**
	 * Directions the balustrade is broken and stairs are placed, per Table 1's lambda.
	 * lambda = 0 gives the front only, 1 front and back, 2 all four sides.
	 */
	std::vector<float> CullingAngles(int32_t RunCount)
	{
		std::vector<float> Angles;
		for (int32_t Index = 0; Index < std::max(RunCount, 1); ++Index)
		{
			Angles.push_back(BUILD_TAU * float(Index) / float(std::max(RunCount, 1)));
		}

		return Angles;
	}
} // namespace

// ==================== MeshAccumulator ====================

void MeshAccumulator::AddTriangle(const Vector3& A, const Vector3& B, const Vector3& C, const Color& Tint)
{
	const Vector3 Edge0 = B - A;
	const Vector3 Edge1 = C - A;
	Vector3 Normal = Edge0.cross(Edge1);
	if (Normal.length_squared() < 1e-14f)
	{
		return;
	}
	// Godot's front face is the side the cross product points away from.
	Normal = -Normal.normalized();

	const int32_t Base = int32_t(Vertices.size());
	const Vector3 Points[3] = { A, B, C };
	const Vector2 Coords[3] = { Vector2(0, 0), Vector2(1, 0), Vector2(1, 1) };

	for (int32_t Index = 0; Index < 3; ++Index)
	{
		Vertices.push_back(Points[Index]);
		Normals.push_back(Normal);
		UVs.push_back(Coords[Index]);
		Colors.push_back(Tint);
	}

	Indices.push_back(Base);
	Indices.push_back(Base + 1);
	Indices.push_back(Base + 2);
}

void MeshAccumulator::AddQuad(
	const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, const Color& Tint)
{
	AddTriangle(A, B, C, Tint);
	AddTriangle(A, C, D, Tint);
}

void MeshAccumulator::AddBox(const Vector3& Centre, const Vector3& HalfExtents, const Color& Tint)
{
	const Vector3& H = HalfExtents;
	if (H.x <= 0.0f || H.y <= 0.0f || H.z <= 0.0f)
	{
		return;
	}

	const Vector3 P000 = Centre + Vector3(-H.x, -H.y, -H.z);
	const Vector3 P100 = Centre + Vector3(H.x, -H.y, -H.z);
	const Vector3 P110 = Centre + Vector3(H.x, H.y, -H.z);
	const Vector3 P010 = Centre + Vector3(-H.x, H.y, -H.z);
	const Vector3 P001 = Centre + Vector3(-H.x, -H.y, H.z);
	const Vector3 P101 = Centre + Vector3(H.x, -H.y, H.z);
	const Vector3 P111 = Centre + Vector3(H.x, H.y, H.z);
	const Vector3 P011 = Centre + Vector3(-H.x, H.y, H.z);

	AddQuad(P001, P101, P111, P011, Tint);   // +Z
	AddQuad(P100, P000, P010, P110, Tint);   // -Z
	AddQuad(P101, P100, P110, P111, Tint);   // +X
	AddQuad(P000, P001, P011, P010, Tint);   // -X
	AddQuad(P010, P011, P111, P110, Tint);   // +Y
	AddQuad(P000, P100, P101, P001, Tint);   // -Y
}

void MeshAccumulator::AddPolygon(const std::vector<Vector3>& Points, const Vector3& Normal, const Color& Tint)
{
	if (Points.size() < 3)
	{
		return;
	}

	const int32_t Base = int32_t(Vertices.size());
	for (const Vector3& Point : Points)
	{
		Vertices.push_back(Point);
		Normals.push_back(Normal);
		UVs.push_back(Vector2(Point.x, Point.y));
		Colors.push_back(Tint);
	}

	// Fan, with each triangle wound against the supplied normal individually.
	//
	// Taking one winding decision for the whole fan — from the first three points — is only valid
	// for a convex outline. 山花 is not convex: the 举架 curve caves in, so once the fan passes the
	// ridge apex the triangles' own orientation reverses and half the tympanum was being culled.
	// That showed up as a solid missing triangle beside the gable, which reads as a hole in the
	// side of the roof.
	for (size_t Index = 1; Index + 1 < Points.size(); ++Index)
	{
		const Vector3 Reference = (Points[Index] - Points[0]).cross(Points[Index + 1] - Points[0]);
		if (Reference.length_squared() < 1e-14f)
		{
			// Degenerate sliver, which the duplicated apex vertex produces. Nothing to draw, and
			// its cross product carries no usable orientation.
			continue;
		}

		Indices.push_back(Base);
		if (Reference.dot(Normal) > 0.0f)
		{
			Indices.push_back(Base + int32_t(Index + 1));
			Indices.push_back(Base + int32_t(Index));
		}
		else
		{
			Indices.push_back(Base + int32_t(Index));
			Indices.push_back(Base + int32_t(Index + 1));
		}
	}
}

void MeshAccumulator::AddSweep(const SweepResult& Sweep, const Color& Tint)
{
	const int32_t Base = int32_t(Vertices.size());

	Vertices.insert(Vertices.end(), Sweep.Vertices.begin(), Sweep.Vertices.end());
	Normals.insert(Normals.end(), Sweep.Normals.begin(), Sweep.Normals.end());
	UVs.insert(UVs.end(), Sweep.UVs.begin(), Sweep.UVs.end());
	Colors.insert(Colors.end(), Sweep.Vertices.size(), Tint);

	for (const int32_t Index : Sweep.Indices)
	{
		Indices.push_back(Base + Index);
	}
}

void MeshAccumulator::AddColumn(
	const Vector3& Base,
	float Height,
	float BottomRadius,
	float TopRadius,
	int32_t Sides,
	const Color& Tint)
{
	const int32_t SideCount = std::max(Sides, 3);
	if (Height <= 0.0f || BottomRadius <= 0.0f)
	{
		return;
	}

	for (int32_t Index = 0; Index < SideCount; ++Index)
	{
		const float Angle0 = BUILD_TAU * float(Index) / float(SideCount);
		const float Angle1 = BUILD_TAU * float(Index + 1) / float(SideCount);

		const Vector3 B0 = Base + Vector3(BottomRadius * std::cos(Angle0), 0.0f, BottomRadius * std::sin(Angle0));
		const Vector3 B1 = Base + Vector3(BottomRadius * std::cos(Angle1), 0.0f, BottomRadius * std::sin(Angle1));
		const Vector3 T0 = Base + Vector3(TopRadius * std::cos(Angle0), Height, TopRadius * std::sin(Angle0));
		const Vector3 T1 = Base + Vector3(TopRadius * std::cos(Angle1), Height, TopRadius * std::sin(Angle1));

		AddQuad(B0, B1, T1, T0, Tint);
	}
}

// ==================== Roof profile ====================

std::vector<Vector2> BuildingGen::BuildRoofProfile(const BuildingSpec& Spec, float HalfSpan)
{
	const int32_t Courses = std::max(Spec.RafterCourses, 3);
	const float Run = HalfSpan / float(Courses);

	// Raw rises first, then scale the total to the requested roof height. That keeps the
	// *shape* of the curve independent of how tall the roof ends up.
	std::vector<float> Rises(static_cast<size_t>(Courses), 0.0f);
	float Total = 0.0f;
	for (int32_t Index = 0; Index < Courses; ++Index)
	{
		const float T = (float(Index) + 0.5f) / float(Courses);
		const float Ratio = Spec.EaveRiseRatio + (Spec.RidgeRiseRatio - Spec.EaveRiseRatio) * T;
		Rises[size_t(Index)] = Run * Ratio;
		Total += Rises[size_t(Index)];
	}

	const float Scale = (Total > BUILD_EPSILON) ? (Spec.RoofHeight / Total) : 0.0f;

	std::vector<Vector2> Profile;
	Profile.reserve(size_t(Courses) + 1);

	float Height = 0.0f;
	float Distance = HalfSpan;
	Profile.push_back(Vector2(Distance, Height));

	for (int32_t Index = 0; Index < Courses; ++Index)
	{
		Height += Rises[size_t(Index)] * Scale;
		Distance -= Run;
		Profile.push_back(Vector2(std::fmax(Distance, 0.0f), Height));
	}

	return Profile;
}

void MeshAccumulator::AddQuadOriented(
	const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
	const Vector3& DesiredNormal, const Color& Tint)
{
	// AddTriangle negates the cross product, so the winding that yields DesiredNormal is the
	// one whose cross product opposes it.
	const Vector3 Cross = (B - A).cross(C - A);
	if (Cross.dot(DesiredNormal) > 0.0f)
	{
		AddQuad(A, D, C, B, Tint);
	}
	else
	{
		AddQuad(A, B, C, D, Tint);
	}
}

void MeshAccumulator::AddQuadSmooth(
	const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
	const Vector3& NormalA, const Vector3& NormalB, const Vector3& NormalC, const Vector3& NormalD,
	const Color& Tint)
{
	// Geometric normal decides the winding; the supplied normals only shade. AddTriangle negates
	// the cross product, so the quad faces the way the negated cross points.
	const Vector3 Geometric = -(B - A).cross(C - A);
	if (Geometric.length_squared() < 1e-14f)
	{
		// Coincident corners: a section crease, or a course pinched out at a hip. Nothing to draw.
		return;
	}

	const bool bFlip = Geometric.dot(NormalA + NormalB + NormalC + NormalD) < 0.0f;

	const int32_t Base = int32_t(Vertices.size());
	const Vector3 Points[4] = { A, B, C, D };
	const Vector3 Corners[4] = { NormalA, NormalB, NormalC, NormalD };
	const Vector2 Coords[4] = { Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1) };

	for (int32_t Index = 0; Index < 4; ++Index)
	{
		Vertices.push_back(Points[Index]);
		Normals.push_back(Corners[Index]);
		UVs.push_back(Coords[Index]);
		Colors.push_back(Tint);
	}

	// Two triangles, wound consistently with the shading normals so backface culling keeps them.
	const int32_t Order[6] = { 0, 1, 2, 0, 2, 3 };
	for (int32_t Step = 0; Step < 6; Step += 3)
	{
		if (bFlip)
		{
			Indices.push_back(Base + Order[Step + 2]);
			Indices.push_back(Base + Order[Step + 1]);
			Indices.push_back(Base + Order[Step]);
		}
		else
		{
			Indices.push_back(Base + Order[Step]);
			Indices.push_back(Base + Order[Step + 1]);
			Indices.push_back(Base + Order[Step + 2]);
		}
	}
}

// ==================== Corner flip (翼角起翘) ====================
namespace
{
	float FlipRound(float Value)
	{
		return std::floor(Value + 0.5f);
	}
} // namespace

float CornerFlip::Weight(float X, float Z) const
{
	if (Rise <= 0.0f && Extend <= 0.0f)
	{
		return 0.0f;
	}

	float Distance;
	if (bPolygonal)
	{
		// Corners sit at the vertex angles, so the distance from one is an arc length. Half a
		// facet is the furthest any point can be from its nearest corner.
		const int32_t Count = std::max(Sides, 3);
		const float Step = BUILD_TAU / float(Count);
		const float Offset = Step * 0.5f;
		const float Angle = std::atan2(Z, X) - Offset;
		// Signed distance to the nearest multiple of Step.
		const float Wrapped = Angle - Step * FlipRound(Angle / Step);
		Distance = std::abs(Wrapped) * std::fmax(Radius, BUILD_EPSILON);
	}
	else
	{
		// Distance inward from each eave edge. On the +Z edge only the X term is non-zero, so
		// the larger of the two is the distance travelled away from the corner along an edge.
		const float InsetX = std::fmax(HalfWidth - std::abs(X), 0.0f);
		const float InsetZ = std::fmax(HalfDepth - std::abs(Z), 0.0f);
		Distance = std::fmax(InsetX, InsetZ);
	}

	const float T = 1.0f - std::fmin(Distance / std::fmax(Span, BUILD_EPSILON), 1.0f);

	// Squared falloff: the lift stays flat along most of the eave and turns up sharply near the
	// corner, which is what the real 角梁 geometry does.
	return T * T;
}

Vector3 CornerFlip::Apply(const Vector3& Point) const
{
	const float W = Weight(Point.x, Point.z);
	if (W <= 0.0f)
	{
		return Point;
	}

	if (bPolygonal)
	{
		const Vector3 Radial(Point.x, 0.0f, Point.z);
		const Vector3 Outward = (Radial.length_squared() > BUILD_EPSILON)
			? Radial.normalized()
			: Vector3(0, 0, 0);

		return Point + Outward * (Extend * W) + Vector3(0.0f, Rise * W, 0.0f);
	}

	const float SignX = (Point.x >= 0.0f) ? 1.0f : -1.0f;
	const float SignZ = (Point.z >= 0.0f) ? 1.0f : -1.0f;

	return Point + Vector3(SignX * Extend * W, Rise * W, SignZ * Extend * W);
}

std::vector<Vector2> BuildingGen::BuildRoofProfileScaled(
	const BuildingSpec& Spec, float HalfSpan, float TargetRise)
{
	BuildingSpec Scaled = Spec;
	Scaled.RoofHeight = TargetRise;

	return BuildRoofProfile(Scaled, HalfSpan);
}

float BuildingGen::GetBoardThickness(const BuildingSpec& Spec)
{
	// Thin enough to read as boarding on rafters rather than as a slab, thick enough to stay
	// visible at the eave from ground level.
	return Spec.Module * 0.30f;
}

void BuildingGen::AddRoofPanel(
	MeshAccumulator& Mesh,
	const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
	const Vector3& Normal,
	float Thickness,
	ERoofPanelEdges OpenEdges,
	const Color& Tint,
	const Color& SoffitTint)
{
	Mesh.AddQuadOriented(A, B, C, D, Normal, Tint);

	if (Thickness <= 0.0f)
	{
		return;
	}

	// Straight down, deliberately not along the normal. Offsetting each panel along its own normal
	// splits the soffit at every seam where the slope changes — adjacent panels push their shared
	// corners in different directions — and the roof ends up laced with hairline cracks. Dropping
	// vertically gives every panel the same displacement, so shared corners stay shared and the
	// soffit is watertight. It also matches how 举架 depths are reckoned, which are vertical.
	const Vector3 Drop(0.0f, -Thickness, 0.0f);
	const Vector3 UnderA = A + Drop;
	const Vector3 UnderB = B + Drop;
	const Vector3 UnderC = C + Drop;
	const Vector3 UnderD = D + Drop;

	Mesh.AddQuadOriented(UnderA, UnderB, UnderC, UnderD, -Normal, SoffitTint);

	// Closing an open edge is also what stops the eave reading as a knife edge.
	if (HasEdge(OpenEdges, ERoofPanelEdges::Lower))
	{
		const Vector3 Outward = ((A + B) - (D + C)).normalized();
		Mesh.AddQuadOriented(A, B, UnderB, UnderA, Outward, SoffitTint);
	}

	if (HasEdge(OpenEdges, ERoofPanelEdges::Upper))
	{
		const Vector3 Outward = ((D + C) - (A + B)).normalized();
		Mesh.AddQuadOriented(D, C, UnderC, UnderD, Outward, SoffitTint);
	}
}

// ==================== Parts ====================

namespace
{
	/**
	 * One wall bay: either a door or a 槛墙 dado with a lattice window above it. Axis-aligned,
	 * with bAlongX selecting whether the bay runs along X (a front/back wall) or Z (an end wall).
	 *
	 * The lattice is a grid of thin bars rather than a texture, which is the only option that
	 * stays consistent with this module being asset-free.
	 */
	void AddWallBay(
		const BuildingSpec& Spec,
		const Vector3& Centre,
		bool bAlongX,
		float HalfLength,
		float HalfThickness,
		float Height,
		bool bIsDoor,
		MeshAccumulator& Mesh)
	{
		if (HalfLength <= 0.0f || Height <= 0.0f)
		{
			return;
		}

		// Extent helper: swaps which axis carries the bay length.
		const auto Extent = [bAlongX, HalfThickness](float Along, float Up) -> Vector3
		{
			return bAlongX ? Vector3(Along, Up, HalfThickness) : Vector3(HalfThickness, Up, Along);
		};
		const auto Offset = [bAlongX](float Along, float Up) -> Vector3
		{
			return bAlongX ? Vector3(Along, Up, 0.0f) : Vector3(0.0f, Up, Along);
		};

		const float FrameHalf = Spec.Module * 0.13f;

		if (bIsDoor)
		{
			// 板门: two leaves recessed behind a frame, with a threshold and lintel.
			const float LeafHalf = (HalfLength - FrameHalf * 2.0f) * 0.5f;
			if (LeafHalf <= 0.0f)
			{
				return;
			}

			// Jambs.
			for (int32_t Side = -1; Side <= 1; Side += 2)
			{
				Mesh.AddBox(
					Centre + Offset((HalfLength - FrameHalf) * float(Side), Height * 0.5f),
					Extent(FrameHalf, Height * 0.5f),
					Spec.TimberColor);
			}
			// Lintel.
			Mesh.AddBox(
				Centre + Offset(0.0f, Height - FrameHalf),
				Extent(HalfLength, FrameHalf),
				Spec.TimberColor);
			// Two leaves, set back so the frame reads as a frame.
			for (int32_t Side = -1; Side <= 1; Side += 2)
			{
				Mesh.AddBox(
					Centre + Offset(LeafHalf * float(Side), (Height - FrameHalf) * 0.5f),
					Extent(LeafHalf * 0.94f, (Height - FrameHalf) * 0.5f) * Vector3(1, 1, 0.55f)
						+ Vector3(bAlongX ? 0.0f : 0.0f, 0.0f, 0.0f),
					Spec.TimberColor * 0.82f);
			}

			return;
		}

		// 槛墙 dado, then the window opening above it.
		const float DadoHeight = Height * 0.46f;
		Mesh.AddBox(
			Centre + Offset(0.0f, DadoHeight * 0.5f),
			Extent(HalfLength, DadoHeight * 0.5f),
			Spec.PlasterColor);

		const float SillY = DadoHeight;
		const float WindowHeight = Height - DadoHeight;

		// Frame around the opening.
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			Mesh.AddBox(
				Centre + Offset((HalfLength - FrameHalf) * float(Side), SillY + WindowHeight * 0.5f),
				Extent(FrameHalf, WindowHeight * 0.5f),
				Spec.TimberColor);
		}
		Mesh.AddBox(
			Centre + Offset(0.0f, SillY + FrameHalf),
			Extent(HalfLength, FrameHalf),
			Spec.TimberColor);
		Mesh.AddBox(
			Centre + Offset(0.0f, Height - FrameHalf),
			Extent(HalfLength, FrameHalf),
			Spec.TimberColor);

		// 棂条 lattice: a bar grid spanning the opening.
		const float InnerHalf = HalfLength - FrameHalf * 2.0f;
		const float InnerLow = SillY + FrameHalf * 2.0f;
		const float InnerHigh = Height - FrameHalf * 2.0f;
		if (InnerHalf <= 0.0f || InnerHigh <= InnerLow)
		{
			return;
		}

		const float BarHalf = Spec.Module * 0.022f;
		const float BarSpacing = Spec.Module * 0.95f;
		const float LatticeThickness = HalfThickness * 0.45f;

		const int32_t Verticals = std::max(int32_t(InnerHalf * 2.0f / BarSpacing), 1);
		for (int32_t Index = 0; Index <= Verticals; ++Index)
		{
			const float Along = -InnerHalf + (InnerHalf * 2.0f) * float(Index) / float(Verticals);
			Mesh.AddBox(
				Centre + Offset(Along, (InnerLow + InnerHigh) * 0.5f),
				bAlongX
					? Vector3(BarHalf, (InnerHigh - InnerLow) * 0.5f, LatticeThickness)
					: Vector3(LatticeThickness, (InnerHigh - InnerLow) * 0.5f, BarHalf),
				Spec.TimberColor * 0.9f);
		}

		const int32_t Horizontals = std::max(int32_t((InnerHigh - InnerLow) / BarSpacing), 1);
		for (int32_t Index = 0; Index <= Horizontals; ++Index)
		{
			const float Up = InnerLow + (InnerHigh - InnerLow) * float(Index) / float(Horizontals);
			Mesh.AddBox(
				Centre + Offset(0.0f, Up),
				bAlongX
					? Vector3(InnerHalf, BarHalf, LatticeThickness)
					: Vector3(LatticeThickness, BarHalf, InnerHalf),
				Spec.TimberColor * 0.9f);
		}
	}

	/**
	 * A 斗拱 bracket set: a 座斗 block carrying tiers of crossing 拱 arms, each tier stepping
	 * wider and higher, with small 升 blocks at the arm ends. Boxes only — the real joinery is
	 * far beyond what a greybox needs, but the stepped corbelling is the silhouette that reads.
	 */
	void AddBracketSet(
		const BuildingSpec& Spec,
		const Vector3& Base,
		bool bAlongX,
		MeshAccumulator& Mesh)
	{
		const float Height = Spec.BracketHeight;
		if (Height <= 0.0f)
		{
			return;
		}

		const float Unit = Spec.ColumnRadius;
		const int32_t Tiers = 2;
		const float TierHeight = Height / float(Tiers + 1);

		// 座斗, the block the whole set stands on.
		Mesh.AddBox(
			Base + Vector3(0.0f, TierHeight * 0.5f, 0.0f),
			Vector3(Unit * 0.85f, TierHeight * 0.5f, Unit * 0.85f),
			Spec.BracketColor);

		for (int32_t Tier = 0; Tier < Tiers; ++Tier)
		{
			const float Y = TierHeight * (float(Tier) + 1.0f);
			const float Reach = Unit * (1.15f + 0.5f * float(Tier));
			const float ArmHalf = TierHeight * 0.32f;

			// Crossing arms: one along the wall, one projecting out from it.
			Mesh.AddBox(
				Base + Vector3(0.0f, Y + ArmHalf, 0.0f),
				bAlongX ? Vector3(Reach, ArmHalf, Unit * 0.4f) : Vector3(Unit * 0.4f, ArmHalf, Reach),
				Spec.BracketColor * (1.0f + 0.06f * float(Tier)));
			Mesh.AddBox(
				Base + Vector3(0.0f, Y + ArmHalf, 0.0f),
				bAlongX ? Vector3(Unit * 0.4f, ArmHalf, Reach) : Vector3(Reach, ArmHalf, Unit * 0.4f),
				Spec.BracketColor * (1.0f + 0.06f * float(Tier)));

			// 升 blocks capping the arm ends.
			for (int32_t Side = -1; Side <= 1; Side += 2)
			{
				const Vector3 Along = bAlongX
					? Vector3(Reach * float(Side), 0.0f, 0.0f)
					: Vector3(0.0f, 0.0f, Reach * float(Side));
				const Vector3 Across = bAlongX
					? Vector3(0.0f, 0.0f, Reach * float(Side))
					: Vector3(Reach * float(Side), 0.0f, 0.0f);

				for (const Vector3& At : { Along, Across })
				{
					Mesh.AddBox(
						Base + At + Vector3(0.0f, Y + ArmHalf * 2.0f + TierHeight * 0.18f, 0.0f),
						Vector3(Unit * 0.32f, TierHeight * 0.16f, Unit * 0.32f),
						Spec.BracketColor * 1.15f);
				}
			}
		}
	}

	void BuildPlatform(const BuildingSpec& Spec, MeshAccumulator& Mesh)
	{
		if (Spec.PlatformHeight <= 0.0f)
		{
			return;
		}

		// 阶条石 cap: a slightly wider slab on top of the body, which reads as dressed stone.
		const float CapHeight = std::fmin(Spec.PlatformHeight * 0.22f, Spec.Module * 0.5f);
		const float BodyHeight = Spec.PlatformHeight - CapHeight;
		const float Inset = Spec.Module * 0.12f;

		Mesh.AddBox(
			Vector3(0.0f, BodyHeight * 0.5f, 0.0f),
			Vector3(Spec.PlatformHalfWidth - Inset, BodyHeight * 0.5f, Spec.PlatformHalfDepth - Inset),
			Spec.StoneColor);

		Mesh.AddBox(
			Vector3(0.0f, BodyHeight + CapHeight * 0.5f, 0.0f),
			Vector3(Spec.PlatformHalfWidth, CapHeight * 0.5f, Spec.PlatformHalfDepth),
			Spec.StoneColor * 1.06f);
	}

	/**
	 * One stair run, built the way the paper intends: a swept block whose *top* is stepped by
	 * the direction constraint of equations 3-5. delta is derived from the block's own
	 * proportions so the two upper contour corners are selected and the lower two are not —
	 * the paper leaves delta to the user, which is fragile for a block this flat.
	 */
	void BuildStepRun(const BuildingSpec& Spec, float Angle, MeshAccumulator& Mesh)
	{
		const int32_t Steps = std::max(Spec.StepCount, 1);
		const float RunWidth = Spec.FenceGapWidth;
		const float BlockHeight = Spec.PlatformHeight;
		if (RunWidth <= 0.0f || BlockHeight <= 0.0f || Spec.StepRunDepth <= 0.0f)
		{
			return;
		}

		// Outward direction for this run, and the platform edge it starts from.
		const Vector3 Outward(std::sin(Angle), 0.0f, std::cos(Angle));
		const float EdgeDistance = (std::abs(Outward.z) > 0.5f) ? Spec.PlatformHalfDepth : Spec.PlatformHalfWidth;

		const float TreadDepth = Spec.StepRunDepth / float(Steps);
		const float RiserOffset = TreadDepth * 0.04f;

		// Two knots per tread: flat across the tread, then a near-vertical jump to the next.
		std::vector<Vector3> Knots;
		std::vector<float> Samples;
		for (int32_t Index = 0; Index < Steps; ++Index)
		{
			const float Drop = -BlockHeight * float(Index) / float(Steps);
			const float Near = EdgeDistance + float(Index) * TreadDepth + RiserOffset;
			const float Far = EdgeDistance + float(Index + 1) * TreadDepth;

			Knots.push_back(Outward * Near + Vector3(0.0f, BlockHeight * 0.5f, 0.0f));
			Samples.push_back(Drop);
			Knots.push_back(Outward * Far + Vector3(0.0f, BlockHeight * 0.5f, 0.0f));
			Samples.push_back(Drop);
		}

		SweepSettings Settings;
		Settings.Contour.push_back(Vector2(-RunWidth * 0.5f, -BlockHeight * 0.5f));
		Settings.Contour.push_back(Vector2(RunWidth * 0.5f, -BlockHeight * 0.5f));
		Settings.Contour.push_back(Vector2(RunWidth * 0.5f, BlockHeight * 0.5f));
		Settings.Contour.push_back(Vector2(-RunWidth * 0.5f, BlockHeight * 0.5f));
		Settings.bClosedContour = true;
		Settings.bGenerateCaps = true;
		Settings.DisplacementScale = 1.0f;
		Settings.DisplacementSamples = Samples;

		// Corner half-angle from the bitangent, plus a margin, keeps the selection unambiguous.
		const float CornerAngle = std::atan2(RunWidth * 0.5f, BlockHeight * 0.5f) * (180.0f / BUILD_PI);
		Settings.ConstraintAngleDegrees = std::fmin(CornerAngle + 6.0f, 89.0f);

		SweepResult Sweep;
		if (BuildSweep(Knots, Settings, Sweep))
		{
			Mesh.AddSweep(Sweep, Spec.StoneColor);
		}
	}

	/** Balustrade posts, panels and a swept rail, broken by a gap at each culling angle. */
	void BuildFence(const BuildingSpec& Spec, MeshAccumulator& Mesh)
	{
		const float PostHalf = Spec.Module * 0.16f;
		const float RailHeight = Spec.PlatformHeight + Spec.FenceHeight;
		const float HalfGap = Spec.FenceGapWidth * 0.5f;
		const std::vector<float> Angles = CullingAngles(Spec.StepRunCount);

		// Walk the platform rim as four runs so gaps can be punched per side.
		struct Side
		{
			Vector3 From;
			Vector3 To;
			float Angle;
		};

		const float HW = Spec.PlatformHalfWidth;
		const float HD = Spec.PlatformHalfDepth;
		const Side Sides[4] = {
			{ Vector3(-HW, 0, HD), Vector3(HW, 0, HD), 0.0f },                 // front, +Z
			{ Vector3(HW, 0, HD), Vector3(HW, 0, -HD), BUILD_PI * 0.5f },      // +X
			{ Vector3(HW, 0, -HD), Vector3(-HW, 0, -HD), BUILD_PI },           // back, -Z
			{ Vector3(-HW, 0, -HD), Vector3(-HW, 0, HD), BUILD_PI * 1.5f },    // -X
		};

		for (const Side& Run : Sides)
		{
			// Radial culling: this side is broken only if one of the culling angles points along it.
			bool bHasGap = false;
			for (const float Angle : Angles)
			{
				if (std::abs(std::cos(Angle - Run.Angle)) > 0.99f && std::cos(Angle - Run.Angle) > 0.0f)
				{
					bHasGap = true;
					break;
				}
			}

			const Vector3 Delta = Run.To - Run.From;
			const float Length = Delta.length();
			if (Length < BUILD_EPSILON)
			{
				continue;
			}
			const Vector3 Direction = Delta / Length;

			// Two sub-runs when broken, one otherwise. Each becomes its own swept rail.
			struct SubRun
			{
				float Start;
				float End;
			};
			std::vector<SubRun> SubRuns;
			if (bHasGap && HalfGap > 0.0f && HalfGap < Length * 0.5f)
			{
				SubRuns.push_back({ 0.0f, Length * 0.5f - HalfGap });
				SubRuns.push_back({ Length * 0.5f + HalfGap, Length });
			}
			else
			{
				SubRuns.push_back({ 0.0f, Length });
			}

			for (const SubRun& Part : SubRuns)
			{
				const float Span = Part.End - Part.Start;
				if (Span < Spec.Module * 0.5f)
				{
					continue;
				}

				const Vector3 Start = Run.From + Direction * Part.Start;
				const Vector3 End = Run.From + Direction * Part.End;

				// Rail, swept so it miters correctly if the rim is ever made curved.
				std::vector<Vector3> Knots;
				Knots.push_back(Start + Vector3(0.0f, RailHeight, 0.0f));
				Knots.push_back(End + Vector3(0.0f, RailHeight, 0.0f));

				SweepSettings Settings;
				const float RailHalf = Spec.Module * 0.2f;
				Settings.Contour.push_back(Vector2(-RailHalf, -RailHalf * 0.5f));
				Settings.Contour.push_back(Vector2(RailHalf, -RailHalf * 0.5f));
				Settings.Contour.push_back(Vector2(RailHalf, RailHalf * 0.5f));
				Settings.Contour.push_back(Vector2(-RailHalf, RailHalf * 0.5f));
				Settings.bClosedContour = true;

				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					Mesh.AddSweep(Sweep, Spec.StoneColor * 1.04f);
				}

				// Posts at both ends and every module or so between.
				const int32_t PostCount = std::max(int32_t(Span / (Spec.Module * 2.4f)), 1);
				for (int32_t Index = 0; Index <= PostCount; ++Index)
				{
					const Vector3 At = Start + (End - Start) * (float(Index) / float(PostCount));
					Mesh.AddBox(
						At + Vector3(0.0f, Spec.PlatformHeight + Spec.FenceHeight * 0.5f, 0.0f),
						Vector3(PostHalf, Spec.FenceHeight * 0.5f, PostHalf),
						Spec.StoneColor * 0.96f);
				}

				// Panel below the rail.
				const Vector3 Mid = (Start + End) * 0.5f;
				const Vector3 PanelHalf = Vector3(
					std::abs(Direction.x) > 0.5f ? Span * 0.5f - PostHalf : PostHalf * 0.45f,
					Spec.FenceHeight * 0.28f,
					std::abs(Direction.z) > 0.5f ? Span * 0.5f - PostHalf : PostHalf * 0.45f);
				Mesh.AddBox(
					Mid + Vector3(0.0f, Spec.PlatformHeight + Spec.FenceHeight * 0.45f, 0.0f),
					PanelHalf,
					Spec.StoneColor * 0.92f);
			}
		}
	}

	void BuildBody(const BuildingSpec& Spec, MeshAccumulator& Mesh)
	{
		const float HalfWidth = Spec.Width * 0.5f;
		const float HalfDepth = Spec.Depth * 0.5f;
		const float Base = Spec.PlatformHeight;
		const float ColumnTop = Base + Spec.ColumnHeight;

		const std::vector<float> XPositions = BayPositions(HalfWidth, Spec.BaysX);
		const std::vector<float> ZPositions = BayPositions(HalfDepth, Spec.BaysZ);

		// Columns stand on the bay grid's perimeter — the paper's "connection line of the
		// column's pivot" used as the body frame.
		if (Spec.bGenerateColumns)
		{
			for (const float X : XPositions)
			{
				for (const float Z : ZPositions)
				{
					const bool bOnPerimeter =
						std::abs(std::abs(X) - HalfWidth) < BUILD_EPSILON ||
						std::abs(std::abs(Z) - HalfDepth) < BUILD_EPSILON;
					if (!bOnPerimeter)
					{
						continue;
					}

					Mesh.AddColumn(
						Vector3(X, Base, Z),
						Spec.ColumnHeight,
						Spec.ColumnRadius,
						// 收分: columns taper slightly towards the top.
						Spec.ColumnRadius * 0.88f,
						Spec.ColumnSides,
						Spec.TimberColor);
				}
			}
		}

		// Walls fill the perimeter bays, except the bay a stair run arrives at.
		if (Spec.bGenerateWalls)
		{
			const float WallHalf = Spec.ColumnRadius * 0.72f;
			const float WallHeight = Spec.ColumnHeight * 0.94f;
			const std::vector<float> Angles = CullingAngles(Spec.StepRunCount);

			const auto HasOpening = [&Angles](float Angle) -> bool
			{
				for (const float Culling : Angles)
				{
					if (std::abs(std::cos(Culling - Angle)) > 0.99f && std::cos(Culling - Angle) > 0.0f)
					{
						return true;
					}
				}

				return false;
			};

			// Front (+Z) and back (-Z) runs, split by bay.
			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				const float Z = HalfDepth * float(Sign);
				const float Angle = (Sign > 0) ? 0.0f : BUILD_PI;
				const bool bOpen = HasOpening(Angle);
				const int32_t Centre = Spec.BaysX / 2;

				for (int32_t Bay = 0; Bay < Spec.BaysX; ++Bay)
				{
					const float From = XPositions[size_t(Bay)];
					const float To = XPositions[size_t(Bay) + 1];
					// The centre bay of an approached side is the doorway; the rest get windows.
					const bool bIsDoor = bOpen && Bay == Centre;

					AddWallBay(
						Spec,
						Vector3((From + To) * 0.5f, Base, Z),
						true,
						std::abs(To - From) * 0.5f - Spec.ColumnRadius,
						WallHalf,
						WallHeight,
						bIsDoor,
						Mesh);
				}
			}

			// Side (+/-X) runs.
			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				const float X = HalfWidth * float(Sign);
				const float Angle = (Sign > 0) ? BUILD_PI * 0.5f : BUILD_PI * 1.5f;
				const bool bOpen = HasOpening(Angle);
				const int32_t Centre = Spec.BaysZ / 2;

				for (int32_t Bay = 0; Bay < Spec.BaysZ; ++Bay)
				{
					const float From = ZPositions[size_t(Bay)];
					const float To = ZPositions[size_t(Bay) + 1];
					const bool bIsDoor = bOpen && Bay == Centre;

					AddWallBay(
						Spec,
						Vector3(X, Base, (From + To) * 0.5f),
						false,
						std::abs(To - From) * 0.5f - Spec.ColumnRadius,
						WallHalf,
						WallHeight,
						bIsDoor,
						Mesh);
				}
			}
		}

		// Bracket band: 阑额 architrave plus a 斗 block over each column. Stands in for a real
		// 斗拱 set until phase 3.
		if (Spec.BracketHeight > 0.0f)
		{
			const float BandHalf = Spec.BracketHeight * 0.42f;
			const float Overhang = Spec.ColumnRadius * 1.5f;

			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				Mesh.AddBox(
					Vector3(0.0f, ColumnTop + BandHalf, HalfDepth * float(Sign)),
					Vector3(HalfWidth + Overhang, BandHalf, Overhang * 0.6f),
					Spec.BracketColor);
				Mesh.AddBox(
					Vector3(HalfWidth * float(Sign), ColumnTop + BandHalf, 0.0f),
					Vector3(Overhang * 0.6f, BandHalf, HalfDepth),
					Spec.BracketColor);
			}

			// 柱头铺作 over each column, and one 补间铺作 between them — the alternation is what
			// makes a bracket band read as 斗拱 rather than as a cornice.
			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				for (size_t Index = 0; Index < XPositions.size(); ++Index)
				{
					AddBracketSet(Spec, Vector3(XPositions[Index], ColumnTop, HalfDepth * float(Sign)), true, Mesh);
				}

				for (size_t Index = 0; Index + 1 < ZPositions.size(); ++Index)
				{
					const float Mid = (ZPositions[Index] + ZPositions[Index + 1]) * 0.5f;
					AddBracketSet(Spec, Vector3(HalfWidth * float(Sign), ColumnTop, Mid), false, Mesh);
				}
			}
		}
	}

	/**
	 * One flush-gable slope: boarding, tile courses swept along per-course sub-splines, and
	 * the eave drip course. Follows section 7's construction — a surface between ridge curves,
	 * resampled into sub-splines, with tiles instanced along each.
	 */
	void BuildGableSlope(
		const BuildingSpec& Spec,
		const std::vector<Vector2>& Profile,
		float Sign,
		float HalfWidth,
		MeshAccumulator& Mesh)
	{
		const float RoofBase = Spec.RoofBase;
		const float Thickness = GetBoardThickness(Spec);
		const Color BoardColor = Spec.TileColor * 0.7f;
		const Color SoffitColor = Spec.TimberColor * 1.15f;

		// Boarding, as one quad strip per rafter course, each a sandwich with its 望板 soffit.
		for (size_t Index = 0; Index + 1 < Profile.size(); ++Index)
		{
			const Vector2& Low = Profile[Index];
			const Vector2& High = Profile[Index + 1];

			const Vector3 A(-HalfWidth, RoofBase + Low.y, Sign * Low.x);
			const Vector3 B(HalfWidth, RoofBase + Low.y, Sign * Low.x);
			const Vector3 C(HalfWidth, RoofBase + High.y, Sign * High.x);
			const Vector3 D(-HalfWidth, RoofBase + High.y, Sign * High.x);

			// Up-slope means z decreases, so the surface normal is up and outward along Sign.
			const Vector3 Up = (Vector3(0.0f, High.y - Low.y, Sign * (High.x - Low.x)));
			Vector3 Normal = Vector3(1.0f, 0.0f, 0.0f).cross(Up).normalized();
			if (Normal.y < 0.0f)
			{
				Normal = -Normal;
			}

			AddRoofPanel(
				Mesh, A, B, C, D, Normal, Thickness,
				Index == 0 ? ERoofPanelEdges::Lower : ERoofPanelEdges::None,
				BoardColor, SoffitColor);
		}

		// Tile skin. Cr measures coverage down from the ridge, so drop the lower knots.
		const int32_t Courses = std::max(int32_t((HalfWidth * 2.0f) / std::fmax(Spec.TileCourseWidth, 0.02f)), 1);
		const float Pitch = (HalfWidth * 2.0f) / float(Courses);
		const size_t KeepFrom = size_t(
			std::floor(float(Profile.size() - 1) * (1.0f - std::fmin(std::fmax(Spec.TileCoverage, 0.0f), 1.0f))));

		{
			std::vector<TileSkinColumn> Columns;
			LayTileCourses(
				-HalfWidth,
				Pitch,
				Courses,
				[&Profile, RoofBase, Sign, KeepFrom](float X) -> std::vector<Vector3>
				{
					std::vector<Vector3> Points;
					Points.reserve(Profile.size() - KeepFrom);
					for (size_t Index = KeepFrom; Index < Profile.size(); ++Index)
					{
						Points.push_back(Vector3(X, RoofBase + Profile[Index].y, Sign * Profile[Index].x));
					}

					return Points;
				},
				Columns);

			// Cr below 1 bares the roof from the eave upward, which leaves column 0 partway up the
			// slope with no eave to dress.
			BuildTileSkin(
				Columns,
				ETileSkinLoop::Open,
				KeepFrom == 0 ? ETileEaves::AtStart : ETileEaves::None,
				Spec.TileColor,
				Mesh);
		}

		// 连檐 board along the bottom edge, tucked under the eave tiles.
		{
			const Vector2& Eave = Profile.front();
			std::vector<Vector3> Knots;
			Knots.push_back(TuckedEaveKnot(
				Spec, Vector3(-HalfWidth, RoofBase + Eave.y, Sign * Eave.x), Sign, 0.0f));
			Knots.push_back(TuckedEaveKnot(
				Spec, Vector3(HalfWidth, RoofBase + Eave.y, Sign * Eave.x), Sign, 0.0f));

			SweepSettings Settings;
			Settings.Contour = MakeEaveContour(EaveBoardScale(Spec));
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 垂脊 down the gable edge on this slope.
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			std::vector<Vector3> Knots;
			for (const Vector2& Point : Profile)
			{
				Knots.push_back(Vector3(HalfWidth * float(Side), RoofBase + Point.y, Sign * Point.x));
			}

			SweepSettings Settings;
			Settings.Contour = MakeRidgeContour(Spec.Module * 0.85f * Spec.RidgeScale);
			Settings.bClosedContour = true;
			Settings.UpReference = Vector3(0, 1, 0);

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}
	}

	/** How far the hipped shell has closed in at a profile distance: 0 at the eave, 1 at the break. */
	float SkirtInsetFraction(float Distance, float Inset)
	{
		return 1.0f - std::fmin(Distance / std::fmax(Inset, BUILD_EPSILON), 1.0f);
	}

	/**
	 * One face of a hipped skirt, and the source of that face's tile skin columns.
	 *
	 * A face narrows as the shell rises, so a column laid at a fixed offset along the eave
	 * eventually runs off the side and has to stop on the hip line. Holding the offset — rather
	 * than a fraction of the narrowing width — is what keeps the courses parallel and square to
	 * the eave, as real 瓦垄 are.
	 */
	struct SkirtFace
	{
		const std::vector<Vector2>* Profile = nullptr;
		const CornerFlip* Flip = nullptr;

		/** Whether the face's eave runs along X; otherwise along Z. */
		bool bAlongX = true;

		/** Which of the two opposing faces this is. */
		float Sign = 1.0f;

		/** Half-extent along the eave, and the distance out to the eave across it. */
		float Extent = 0.0f;
		float OppositeExtent = 0.0f;

		float Inset = 0.0f;
		float RoofBase = 0.0f;

		Vector3 PointAt(float Along, float Fraction, float Y) const
		{
			const float Out = OppositeExtent - Inset * Fraction;
			const Vector3 Point = bAlongX
				? Vector3(Along, Y, Sign * Out)
				: Vector3(Sign * Out, Y, Along);

			return Flip->Apply(Point);
		}

		std::vector<Vector3> Column(float Along) const
		{
			const std::vector<Vector2>& Steps = *Profile;

			std::vector<Vector3> Points;
			Points.reserve(Steps.size());

			for (size_t Step = 0; Step < Steps.size(); ++Step)
			{
				const float Fraction = SkirtInsetFraction(Steps[Step].x, Inset);
				// The face narrows as the shell rises. Once it passes this column, the column has
				// reached the hip.
				const float Cross = Extent - Inset * Fraction;
				if (std::abs(Along) > Cross)
				{
					// 翼角切瓦: land the column exactly on the hip line rather than stopping at the
					// previous ring, which left a serrated edge the 戗脊 could not fully hide.
					// Real corners use cut tiles for this.
					if (Step > 0)
					{
						const float PrevFraction = SkirtInsetFraction(Steps[Step - 1].x, Inset);
						const float PrevCross = Extent - Inset * PrevFraction;
						const float Span = std::fmax(PrevCross - Cross, BUILD_EPSILON);
						const float T = (PrevCross - std::abs(Along)) / Span;

						const float HitFraction = PrevFraction + (Fraction - PrevFraction) * T;
						const float HitY = RoofBase + Steps[Step - 1].y
							+ (Steps[Step].y - Steps[Step - 1].y) * T;
						Points.push_back(PointAt(Along, HitFraction, HitY));
					}
					break;
				}

				Points.push_back(PointAt(Along, Fraction, RoofBase + Steps[Step].y));
			}

			return Points;
		}
	};

	/**
	 * The hipped family, 歇山 and 庑殿 in one construction.
	 *
	 * A hipped shell is lofted between the eave rectangle and an inner rectangle inset by the
	 * same distance in X and Z, which is what puts the hip ridges at 45 degrees in plan and so
	 * makes all four slopes share one pitch. Lofting rings also yields the four faces *and* the
	 * four corner wedges in one pass, with no corner special-casing.
	 *
	 * Three terminations share it, which is why the whole family costs so little:
	 *  - 歇山 stops at the 收山 break and puts a gabled tier with 山花 above.
	 *  - 庑殿 takes the shell all the way in, so the inner rectangle collapses to a line, and
	 *    that line *is* the main ridge. 歇山's 戗脊 become 庑殿's 垂脊 unchanged.
	 *  - 盝顶 stops early and caps the opening with a flat platform ringed by a 围脊.
	 *
	 * A square plan taken to full hip degenerates the ridge to a point — correctly, since that
	 * is a 攒尖 pyramid — so the main ridge is skipped when it has no length.
	 */
	void BuildHippedRoof(const BuildingSpec& Spec, EHipTop Top, MeshAccumulator& Mesh)
	{
		const bool bFullHip = Top == HIP_TOP_RIDGE;
		const bool bFlatTop = Top == HIP_TOP_FLAT;
		const float RoofBase = Spec.RoofBase;
		const float HalfWidthEave = Spec.Width * 0.5f + Spec.EaveOverhang;
		const float HalfDepthEave = Spec.Depth * 0.5f + Spec.EaveOverhang;

		// How far the shell closes in: all the way for 庑殿, to the 收山 break for 歇山, and
		// only as far as the flat platform for 盝顶.
		const float TopRatio = bFlatTop
			? (1.0f - std::fmin(std::fmax(Spec.FlatTopRatio, 0.05f), 0.9f))
			: std::fmin(std::fmax(Spec.GableRatio, 0.05f), 0.9f);
		const float Inset = bFullHip
			? HalfDepthEave
			: std::fmin(
				HalfDepthEave * TopRatio,
				std::fmin(HalfWidthEave, HalfDepthEave) * 0.9f);
		const float HalfWidthBreak = HalfWidthEave - Inset;
		const float HalfDepthBreak = HalfDepthEave - Inset;

		CornerFlip Flip;
		Flip.Rise = Spec.CornerRise;
		Flip.Extend = Spec.CornerExtend;
		Flip.Span = Spec.CornerSpan;
		Flip.HalfWidth = HalfWidthEave;
		Flip.HalfDepth = HalfDepthEave;

		// One curve over the full depth, so the skirt and the tier stay continuous.
		const std::vector<Vector2> Full = BuildRoofProfile(Spec, HalfDepthEave);

		const auto HeightAt = [&Full](float Distance) -> float
		{
			for (size_t Index = 0; Index + 1 < Full.size(); ++Index)
			{
				const float High = Full[Index].x;
				const float Low = Full[Index + 1].x;
				if (Distance <= High && Distance >= Low)
				{
					const float Span = std::fmax(High - Low, BUILD_EPSILON);
					const float T = (High - Distance) / Span;
					return Full[Index].y + (Full[Index + 1].y - Full[Index].y) * T;
				}
			}

			return Full.back().y;
		};

		const float BreakHeight = HeightAt(HalfDepthBreak);
		const std::vector<Vector2> SkirtProfile = BuildRoofProfileScaled(Spec, Inset, BreakHeight);

		// Enough samples per side that the corner lift curves instead of kinking.
		const int32_t SamplesX = std::max(int32_t(HalfWidthEave * 2.0f / std::fmax(Spec.Module * 0.7f, 0.05f)), 4);
		const int32_t SamplesZ = std::max(int32_t(HalfDepthEave * 2.0f / std::fmax(Spec.Module * 0.7f, 0.05f)), 4);

		// A closed ring of plan positions, walking the +Z, +X, -Z then -X sides.
		const auto BuildRing = [&](float HalfW, float HalfD) -> std::vector<Vector2>
		{
			std::vector<Vector2> Ring;
			for (int32_t Index = 0; Index < SamplesX; ++Index)
			{
				const float T = float(Index) / float(SamplesX);
				Ring.push_back(Vector2(-HalfW + 2.0f * HalfW * T, HalfD));
			}
			for (int32_t Index = 0; Index < SamplesZ; ++Index)
			{
				const float T = float(Index) / float(SamplesZ);
				Ring.push_back(Vector2(HalfW, HalfD - 2.0f * HalfD * T));
			}
			for (int32_t Index = 0; Index < SamplesX; ++Index)
			{
				const float T = float(Index) / float(SamplesX);
				Ring.push_back(Vector2(HalfW - 2.0f * HalfW * T, -HalfD));
			}
			for (int32_t Index = 0; Index < SamplesZ; ++Index)
			{
				const float T = float(Index) / float(SamplesZ);
				Ring.push_back(Vector2(-HalfW, -HalfD + 2.0f * HalfD * T));
			}

			return Ring;
		};

		// Skirt.x runs from Inset down to 0, so this is 0 at the eave and 1 at the break.
		const auto InsetFraction = [Inset](float Distance) -> float
		{
			return SkirtInsetFraction(Distance, Inset);
		};

		// ---- Hipped skirt, lofted between the eave and break rectangles ----

		const float Thickness = GetBoardThickness(Spec);
		const Color BoardColor = Spec.TileColor * 0.7f;
		const Color SoffitColor = Spec.TimberColor * 1.15f;

		std::vector<std::vector<Vector3>> Rings;
		Rings.reserve(SkirtProfile.size());
		for (const Vector2& Step : SkirtProfile)
		{
			const float Fraction = InsetFraction(Step.x);
			const std::vector<Vector2> Plan = BuildRing(
				HalfWidthEave - Inset * Fraction, HalfDepthEave - Inset * Fraction);

			std::vector<Vector3> Ring;
			Ring.reserve(Plan.size());
			for (const Vector2& Point : Plan)
			{
				Ring.push_back(Flip.Apply(Vector3(Point.x, RoofBase + Step.y, Point.y)));
			}
			Rings.push_back(Ring);
		}

		for (size_t Level = 0; Level + 1 < Rings.size(); ++Level)
		{
			const std::vector<Vector3>& Low = Rings[Level];
			const std::vector<Vector3>& High = Rings[Level + 1];
			const size_t Count = Low.size();

			for (size_t Index = 0; Index < Count; ++Index)
			{
				const size_t Next = (Index + 1) % Count;

				Vector3 Normal = (Low[Next] - Low[Index]).cross(High[Index] - Low[Index]);
				if (Normal.length_squared() < 1e-12f)
				{
					continue;
				}
				Normal = Normal.normalized();
				// A roof surface always faces upward.
				if (Normal.y < 0.0f)
				{
					Normal = -Normal;
				}

				AddRoofPanel(
					Mesh,
					Low[Index], Low[Next], High[Next], High[Index],
					Normal, Thickness,
					Level == 0 ? ERoofPanelEdges::Lower : ERoofPanelEdges::None,
					BoardColor, SoffitColor);
			}
		}

		// Tile skin on the four skirt faces. The corner wedges keep boarding only and the
		// 戗脊 sits over that seam, which is how a real corner hides its fan of cut tiles.
		{
			struct Face
			{
				bool bAlongX;
				float Sign;
			};
			const Face Faces[4] = { { true, 1.0f }, { true, -1.0f }, { false, 1.0f }, { false, -1.0f } };

			for (const Face& Current : Faces)
			{
				// Space courses over the eave edge and let each column clip itself where it runs
				// off the face. That tiles the corner wedges too, and on 庑殿 it tiles the
				// triangular hip ends, which a constant top extent would have left bare.
				SkirtFace Skirt;
				Skirt.Profile = &SkirtProfile;
				Skirt.Flip = &Flip;
				Skirt.bAlongX = Current.bAlongX;
				Skirt.Sign = Current.Sign;
				Skirt.Extent = Current.bAlongX ? HalfWidthEave : HalfDepthEave;
				Skirt.OppositeExtent = Current.bAlongX ? HalfDepthEave : HalfWidthEave;
				Skirt.Inset = Inset;
				Skirt.RoofBase = RoofBase;

				const int32_t Courses = std::max(
					int32_t(Skirt.Extent * 2.0f / std::fmax(Spec.TileCourseWidth, 0.05f)), 1);
				const float Pitch = Skirt.Extent * 2.0f / float(Courses);

				std::vector<TileSkinColumn> Columns;
				LayTileCourses(
					-Skirt.Extent,
					Pitch,
					Courses,
					[&Skirt](float Along) -> std::vector<Vector3> { return Skirt.Column(Along); },
					Columns);

				BuildTileSkin(
					Columns, ETileSkinLoop::Open, ETileEaves::AtStart, Spec.TileColor, Mesh);
			}
		}

		// ---- Gabled tier above the break ----

		const float TierBase = RoofBase + BreakHeight;
		const std::vector<Vector2> TierProfile = (Top == HIP_TOP_GABLED_TIER)
			? BuildRoofProfileScaled(Spec, HalfDepthBreak, std::fmax(Spec.RoofHeight - BreakHeight, 0.01f))
			: std::vector<Vector2>();

		for (int32_t Sign = -1; Sign <= 1 && Top == HIP_TOP_GABLED_TIER; Sign += 2)
		{
			for (size_t Index = 0; Index + 1 < TierProfile.size(); ++Index)
			{
				const Vector2& Low = TierProfile[Index];
				const Vector2& High = TierProfile[Index + 1];

				const Vector3 A(-HalfWidthBreak, TierBase + Low.y, float(Sign) * Low.x);
				const Vector3 B(HalfWidthBreak, TierBase + Low.y, float(Sign) * Low.x);
				const Vector3 C(HalfWidthBreak, TierBase + High.y, float(Sign) * High.x);
				const Vector3 D(-HalfWidthBreak, TierBase + High.y, float(Sign) * High.x);

				// The tier's lower edge lands on the skirt below it, so there is no open eave to cap.
				AddRoofPanel(
					Mesh, A, B, C, D,
					Vector3(0.0f, 1.0f, float(Sign)).normalized(),
					Thickness, ERoofPanelEdges::None, BoardColor, SoffitColor);
			}

			const int32_t Courses = std::max(
				int32_t(HalfWidthBreak * 2.0f / std::fmax(Spec.TileCourseWidth, 0.05f)), 1);
			const float Pitch = HalfWidthBreak * 2.0f / float(Courses);
			const size_t KeepFrom = size_t(std::floor(
				float(TierProfile.size() - 1) * (1.0f - std::fmin(std::fmax(Spec.TileCoverage, 0.0f), 1.0f))));

			std::vector<TileSkinColumn> Columns;
			LayTileCourses(
				-HalfWidthBreak,
				Pitch,
				Courses,
				[&TierProfile, TierBase, Sign, KeepFrom](float X) -> std::vector<Vector3>
				{
					std::vector<Vector3> Points;
					Points.reserve(TierProfile.size() - KeepFrom);
					for (size_t Index = KeepFrom; Index < TierProfile.size(); ++Index)
					{
						Points.push_back(Vector3(
							X, TierBase + TierProfile[Index].y, float(Sign) * TierProfile[Index].x));
					}

					return Points;
				},
				Columns);

			// The tier's lower edge is the 收山 break sitting on the skirt below it, closed by a
			// 博脊 — not an eave, so it gets no 瓦当 or 滴水.
			BuildTileSkin(Columns, ETileSkinLoop::Open, ETileEaves::None, Spec.TileColor, Mesh);
		}

		// 山花, the vertical tympanum closing each end of the tier. Only 歇山 has one.
		for (int32_t Side = -1; Side <= 1 && Top == HIP_TOP_GABLED_TIER; Side += 2)
		{
			const float X = HalfWidthBreak * float(Side);
			std::vector<Vector3> Points;
			for (size_t Index = 0; Index < TierProfile.size(); ++Index)
			{
				Points.push_back(Vector3(X, TierBase + TierProfile[Index].y, TierProfile[Index].x));
			}
			for (size_t Index = TierProfile.size(); Index-- > 0;)
			{
				Points.push_back(Vector3(X, TierBase + TierProfile[Index].y, -TierProfile[Index].x));
			}
			Mesh.AddPolygon(Points, Vector3(float(Side), 0.0f, 0.0f), Spec.PlasterColor * 0.9f);
		}

		// ---- Ridges ----

		const float Apex = (Top == HIP_TOP_GABLED_TIER) ? (TierBase + TierProfile.back().y) : TierBase;

		// 盝顶: cap the opening with a flat platform and ring it with a 围脊.
		if (bFlatTop)
		{
			std::vector<Vector3> Cap;
			const std::vector<Vector2> Plan = BuildRing(HalfWidthBreak, HalfDepthBreak);
			for (const Vector2& Point : Plan)
			{
				Cap.push_back(Vector3(Point.x, Apex, Point.y));
			}
			Mesh.AddPolygon(Cap, Vector3(0, 1, 0), Spec.TileColor * 0.8f);

			std::vector<Vector3> Knots;
			for (const Vector2& Point : Plan)
			{
				Knots.push_back(Vector3(Point.x, Apex, Point.y));
			}
			Knots.push_back(Knots.front());

			SweepSettings Settings;
			Settings.Contour = MakeRidgeContour(Spec.Module * 1.05f * Spec.RidgeScale);
			Settings.bClosedContour = true;
			Settings.bGenerateCaps = false;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 正脊 along the apex. On a square 庑殿 plan the ridge has no length — that is a 攒尖
		// pyramid, and the four diagonal ridges already meet at the point.
		if (!bFlatTop && HalfWidthBreak > Spec.Module * 0.15f)
		{
			std::vector<Vector3> Knots;
			Knots.push_back(Vector3(-HalfWidthBreak, Apex, 0.0f));
			Knots.push_back(Vector3(HalfWidthBreak, Apex, 0.0f));

			SweepSettings Settings;
			Settings.Contour = MakeRidgeContour(Spec.Module * 1.35f * Spec.RidgeScale);
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 垂脊 down each edge of the gabled tier. Only 歇山 has one.
		for (int32_t Side = -1; Side <= 1 && Top == HIP_TOP_GABLED_TIER; Side += 2)
		{
			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				std::vector<Vector3> Knots;
				for (size_t Index = TierProfile.size(); Index-- > 0;)
				{
					Knots.push_back(Vector3(
						HalfWidthBreak * float(Side),
						TierBase + TierProfile[Index].y,
						float(Sign) * TierProfile[Index].x));
				}

				SweepSettings Settings;
				Settings.Contour = MakeRidgeContour(Spec.Module * 0.8f * Spec.RidgeScale);
				Settings.bClosedContour = true;
				Settings.UpReference = Vector3(0, 1, 0);

				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					Mesh.AddSweep(Sweep, Spec.RidgeColor);
				}
			}
		}

		// The diagonal ridges: 戗脊 on 歇山, 垂脊 on 庑殿. Same curve either way — from the inner
		// rectangle corner out and down to the flipped eave corner.
		for (int32_t SideX = -1; SideX <= 1; SideX += 2)
		{
			for (int32_t SideZ = -1; SideZ <= 1; SideZ += 2)
			{
				std::vector<Vector3> Knots;
				for (size_t Index = SkirtProfile.size(); Index-- > 0;)
				{
					const float Fraction = InsetFraction(SkirtProfile[Index].x);
					const Vector3 Point(
						float(SideX) * (HalfWidthEave - Inset * Fraction),
						RoofBase + SkirtProfile[Index].y,
						float(SideZ) * (HalfDepthEave - Inset * Fraction));
					Knots.push_back(Flip.Apply(Point));
				}

				SweepSettings Settings;
				Settings.Contour = MakeRidgeContour(Spec.Module * 0.85f * Spec.RidgeScale);
				Settings.bClosedContour = true;
				Settings.UpReference = Vector3(0, 1, 0);

				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					Mesh.AddSweep(Sweep, Spec.RidgeColor);
				}
			}
		}

		// 连檐 board all the way round the flipped eave, tucked under the eave tiles. Closing the
		// loop is what makes the last corner miter against the first side rather than butt-ending.
		{
			// Inset and dropped rather than sitting on the eave line, where at full size it used to
			// stand in front of the tile ends and hide every 瓦当 and 滴水 behind it.
			const float Back = Spec.Module * 0.26f;
			const std::vector<Vector2> Plan = BuildRing(HalfWidthEave - Back, HalfDepthEave - Back);
			std::vector<Vector3> Knots;
			for (const Vector2& Point : Plan)
			{
				Knots.push_back(Flip.Apply(Vector3(Point.x, RoofBase - Spec.Module * 0.20f, Point.y)));
			}
			Knots.push_back(Knots.front());

			SweepSettings Settings;
			Settings.Contour = MakeEaveContour(EaveBoardScale(Spec));
			Settings.bClosedContour = true;
			Settings.bGenerateCaps = false;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}
	}

	/**
	 * 卷棚. One continuous profile from the +Z eave, over a rolled ridge of radius RollRadius,
	 * down to the -Z eave, so a single tile course spans the whole roof and the ridge needs no
	 * separate 正脊. The slopes are built to the tangent point and joined by a semicircle.
	 */
	void BuildRolledGable(const BuildingSpec& Spec, float HalfWidth, float HalfSpan, MeshAccumulator& Mesh)
	{
		const float RoofBase = Spec.RoofBase;
		const float Roll = std::fmin(Spec.RollRadius, std::fmin(HalfSpan * 0.5f, Spec.RoofHeight * 0.6f));

		// Slope from the eave up to where the roll begins.
		const std::vector<Vector2> Slope = BuildRoofProfileScaled(
			Spec, HalfSpan - Roll, std::fmax(Spec.RoofHeight - Roll, 0.01f));

		// Continuous profile: +Z slope, the roll, then the mirrored -Z slope.
		std::vector<Vector2> Profile;
		for (const Vector2& Step : Slope)
		{
			Profile.push_back(Vector2(Step.x + Roll, Step.y));
		}

		const float RollCentre = Spec.RoofHeight - Roll;
		const int32_t RollSteps = 8;
		for (int32_t Index = 1; Index < RollSteps; ++Index)
		{
			const float Angle = BUILD_PI * float(Index) / float(RollSteps);
			Profile.push_back(Vector2(Roll * std::cos(Angle), RollCentre + Roll * std::sin(Angle)));
		}

		for (size_t Index = Slope.size(); Index-- > 0;)
		{
			Profile.push_back(Vector2(-(Slope[Index].x + Roll), Slope[Index].y));
		}

		// Boarding. The profile runs eave to eave over the roll, so both ends are open edges.
		const float Thickness = GetBoardThickness(Spec);
		const Color BoardColor = Spec.TileColor * 0.7f;
		const Color SoffitColor = Spec.TimberColor * 1.15f;

		for (size_t Index = 0; Index + 1 < Profile.size(); ++Index)
		{
			const Vector2& From = Profile[Index];
			const Vector2& To = Profile[Index + 1];

			const Vector3 A(-HalfWidth, RoofBase + From.y, From.x);
			const Vector3 B(HalfWidth, RoofBase + From.y, From.x);
			const Vector3 C(HalfWidth, RoofBase + To.y, To.x);
			const Vector3 D(-HalfWidth, RoofBase + To.y, To.x);

			// Outward is up and away from the centreline, which flips sign over the roll.
			const Vector3 Outward = Vector3(0.0f, 1.0f, (From.x + To.x) * 0.5f).normalized();

			ERoofPanelEdges Edges = ERoofPanelEdges::None;
			if (Index == 0)
			{
				Edges = Edges | ERoofPanelEdges::Lower;
			}
			if (Index + 2 == Profile.size())
			{
				Edges = Edges | ERoofPanelEdges::Upper;
			}

			AddRoofPanel(Mesh, A, B, C, D, Outward, Thickness, Edges, BoardColor, SoffitColor);
		}

		// Tile skin running the full span, eave to eave over the roll.
		{
			const int32_t Courses = std::max(int32_t(HalfWidth * 2.0f / std::fmax(Spec.TileCourseWidth, 0.05f)), 1);
			const float Pitch = HalfWidth * 2.0f / float(Courses);

			std::vector<TileSkinColumn> Columns;
			LayTileCourses(
				-HalfWidth,
				Pitch,
				Courses,
				[&Profile, RoofBase](float X) -> std::vector<Vector3>
				{
					std::vector<Vector3> Points;
					Points.reserve(Profile.size());
					for (const Vector2& Step : Profile)
					{
						Points.push_back(Vector3(X, RoofBase + Step.y, Step.x));
					}

					return Points;
				},
				Columns);

			// 卷棚's profile runs eave to eave over the roll, so both ends want dressing.
			BuildTileSkin(
				Columns, ETileSkinLoop::Open, ETileEaves::AtBothEnds, Spec.TileColor, Mesh);
		}

		// 垂脊 along both gable edges, following the whole rolled profile.
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			std::vector<Vector3> Knots;
			for (const Vector2& Step : Profile)
			{
				Knots.push_back(Vector3(HalfWidth * float(Side), RoofBase + Step.y, Step.x));
			}

			SweepSettings Settings;
			Settings.Contour = MakeRidgeContour(Spec.Module * 0.85f * Spec.RidgeScale);
			Settings.bClosedContour = true;
			Settings.UpReference = Vector3(0, 1, 0);

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 连檐 board on both sides, tucked under the eave tiles.
		for (int32_t Sign = -1; Sign <= 1; Sign += 2)
		{
			std::vector<Vector3> Knots;
			Knots.push_back(TuckedEaveKnot(
				Spec, Vector3(-HalfWidth, RoofBase, float(Sign) * HalfSpan), float(Sign), 0.0f));
			Knots.push_back(TuckedEaveKnot(
				Spec, Vector3(HalfWidth, RoofBase, float(Sign) * HalfSpan), float(Sign), 0.0f));

			SweepSettings Settings;
			Settings.Contour = MakeEaveContour(EaveBoardScale(Spec));
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 山墙 following the wall line, closing the rolled silhouette.
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			const float X = Spec.Width * 0.5f * float(Side);
			std::vector<Vector3> Points;
			for (const Vector2& Step : Profile)
			{
				Points.push_back(Vector3(X, RoofBase + Step.y, Step.x));
			}
			Mesh.AddPolygon(Points, Vector3(float(Side), 0.0f, 0.0f), Spec.PlasterColor * 0.94f);
		}
	}

	/**
	 * The gabled family: 硬山, 悬山 and 卷棚.
	 *
	 * 硬山 stops the roof at the end walls; 悬山 overhangs past them. 卷棚 replaces the sharp
	 * ridge with a roll, and because the tiles then run continuously from one eave over the top
	 * to the other, it is built from a single unbroken profile rather than two slopes — which is
	 * also physically what a 卷棚 roof does.
	 */
	void BuildGabledRoof(const BuildingSpec& Spec, bool bOverhang, bool bRolled, MeshAccumulator& Mesh)
	{
		const float HalfWidth = Spec.Width * 0.5f + (bOverhang ? Spec.GableOverhang : 0.0f);
		const float HalfSpan = Spec.Depth * 0.5f + Spec.EaveOverhang;

		if (bRolled)
		{
			BuildRolledGable(Spec, HalfWidth, HalfSpan, Mesh);
			return;
		}

		const std::vector<Vector2> Profile = BuildRoofProfile(Spec, HalfSpan);

		BuildGableSlope(Spec, Profile, 1.0f, HalfWidth, Mesh);
		BuildGableSlope(Spec, Profile, -1.0f, HalfWidth, Mesh);

		// 正脊 along the apex.
		{
			const float Apex = Spec.RoofBase + Profile.back().y;
			std::vector<Vector3> Knots;
			Knots.push_back(Vector3(-HalfWidth, Apex, 0.0f));
			Knots.push_back(Vector3(HalfWidth, Apex, 0.0f));

			SweepSettings Settings;
			Settings.Contour = MakeRidgeContour(Spec.Module * 1.35f * Spec.RidgeScale);
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 山墙 gable tympanum. It closes the wall line, not the roof edge, so on 悬山 the roof
		// correctly overhangs a wall that stops short of it.
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			const float X = Spec.Width * 0.5f * float(Side);
			std::vector<Vector3> Points;

			// Up the +Z slope, over the apex, back down the -Z slope.
			for (size_t Index = 0; Index < Profile.size(); ++Index)
			{
				Points.push_back(Vector3(X, Spec.RoofBase + Profile[Index].y, Profile[Index].x));
			}
			for (size_t Index = Profile.size(); Index-- > 0;)
			{
				Points.push_back(Vector3(X, Spec.RoofBase + Profile[Index].y, -Profile[Index].x));
			}

			Mesh.AddPolygon(Points, Vector3(float(Side), 0.0f, 0.0f), Spec.PlasterColor * 0.94f);
		}
	}
} // namespace

// ==================== Entry point ====================

void BuildingGen::BuildBuilding(const BuildingSpec& Spec, MeshAccumulator& OutMesh)
{
	const bool bCentralRoof = Spec.RoofType == ROOF_PYRAMIDAL
		|| Spec.RoofType == ROOF_ROUND
		|| Spec.RoofType == ROOF_HELMET;
	const ECentralProfile CentralProfile =
		(Spec.RoofType == ROOF_HELMET) ? CENTRAL_HELMET : CENTRAL_STRAIGHT;

	// Equation 8: a non-rectangular plan must be regular, and only a centralised roof can sit
	// on one. Both conditions route to the polygonal generator.
	if (Spec.Sides != 4)
	{
		BuildPolygonalBuilding(Spec, CentralProfile, OutMesh);
		return;
	}

	BuildPlatform(Spec, OutMesh);

	if (Spec.bGenerateFence)
	{
		BuildFence(Spec, OutMesh);
	}

	if (Spec.bGenerateSteps)
	{
		for (const float Angle : CullingAngles(Spec.StepRunCount))
		{
			BuildStepRun(Spec, Angle, OutMesh);
		}
	}

	BuildBody(Spec, OutMesh);

	// Only the ridged family is implemented so far; the centralised family (攒尖/盔顶/盝顶)
	// is a separate generator, per the Eq 8 split.
	// A centralised roof on a square plan is legal — that is exactly the 攒尖 a square 庑殿
	// already degenerates into, just built deliberately and with a finial.
	if (bCentralRoof)
	{
		BuildCentralisedRoof(Spec, CentralProfile, OutMesh);
		return;
	}

	switch (Spec.RoofType)
	{
		case ROOF_HIP:
			BuildHippedRoof(Spec, HIP_TOP_RIDGE, OutMesh);
			break;
		case ROOF_GABLE_AND_HIP:
			BuildHippedRoof(Spec, HIP_TOP_GABLED_TIER, OutMesh);
			break;
		case ROOF_HOLLOW:
			BuildHippedRoof(Spec, HIP_TOP_FLAT, OutMesh);
			break;
		case ROOF_OVERHANGING_GABLE:
			BuildGabledRoof(Spec, true, false, OutMesh);
			break;
		case ROOF_ROUND_RIDGE:
			BuildGabledRoof(Spec, true, true, OutMesh);
			break;
		default:
			BuildGabledRoof(Spec, false, false, OutMesh);
			break;
	}
}
