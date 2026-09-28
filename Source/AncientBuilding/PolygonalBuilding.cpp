#include "AncientBuilding/BuildingBuilder.h"

#include "AncientBuilding/Masonry.h"
#include "AncientBuilding/RoofCurve.h"
#include "AncientBuilding/TileSkin.h"

// The centralised roof family (攒尖 / 圆攒尖 / 盔顶) and the polygonal plan it needs.
//
// Equation 8 of Hu & Qin 2020 ties the two together: `sides != 4` forces the aspect ratio to 1,
// so a polygonal plan is always regular. That is why this lives apart from the rectangular
// generator in BuildingBuilder.cpp — it is a different plan topology, not a different roof.
//
// All three roof types are one loft from the eave polygon to the apex; only the profile and the
// resolution differ. 圆攒尖 is 攒尖 resolved finely enough to read as a cone, and 盔顶 is 攒尖
// with a bulged profile — the case the paper singles out as impossible for the method it
// replaces, because the old frame coupled ridge shape to tile coverage.

#include <algorithm>
#include <cmath>

using namespace BuildingGen;

namespace
{
	const float POLY_PI = 3.14159265358979323846f;
	const float POLY_TAU = 2.0f * POLY_PI;
	const float POLY_EPSILON = 1e-6f;

	// The ridge section is shared with the rectangular roofs (BuildingGen::RidgeContourFor), so a
	// polygonal plan gets the same 脊断面档 — same soffit, same crown, same 分层 bands — as a 硬山.

	std::vector<Vector2> MakeEaveSection(float Scale)
	{
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.5f, -0.16f) * Scale);
		Contour.push_back(Vector2(0.5f, -0.16f) * Scale);
		Contour.push_back(Vector2(0.5f, 0.16f) * Scale);
		Contour.push_back(Vector2(-0.5f, 0.16f) * Scale);

		return Contour;
	}

	/** Effective side count: 圆攒尖 needs enough facets to stop reading as a polygon. */
	int32_t EffectiveSides(const BuildingSpec& Spec)
	{
		const int32_t Base = std::max(Spec.Sides, 3);

		return (Spec.RoofType == ROOF_ROUND) ? std::max(Base, 24) : Base;
	}

	/**
	 * Centralised slope profile as (radius fraction from the apex, height fraction). Index 0 is
	 * the eave, the last entry the apex.
	 *
	 * 攒尖 reuses the 举架 idea directly: shallow at the eave, steep at the ridge. 盔顶 adds a
	 * bulge that pushes the lower slope *outside* the straight line, which is what gives the
	 * helmet its swollen shoulder.
	 */
	std::vector<Vector2> BuildCentralProfile(const BuildingSpec& Spec, ECentralProfile Kind)
	{
		const int32_t Courses = std::max(Spec.RafterCourses, 3) * 2;

		std::vector<Vector2> Profile;
		Profile.reserve(size_t(Courses) + 1);

		for (int32_t Index = 0; Index <= Courses; ++Index)
		{
			// T runs 0 at the eave to 1 at the apex.
			const float T = float(Index) / float(Courses);

			float RadiusFraction = 1.0f - T;
			float HeightFraction;

			if (Kind == CENTRAL_HELMET)
			{
				// Bulge outward low down, then draw in sharply: sin gives the shoulder, the
				// power term keeps the apex steep.
				RadiusFraction = (1.0f - T) + Spec.HelmetBulge * std::sin(POLY_PI * T) * (1.0f - T);
				HeightFraction = std::pow(T, 1.35f);
			}
			else
			{
				// Concave 举架 curve: the exponent above 1 keeps the eave shallow.
				HeightFraction = std::pow(T, 1.0f / 1.45f);
			}

			Profile.push_back(Vector2(std::fmax(RadiusFraction, 0.0f), HeightFraction));
		}

		// Force an exact apex so the ridges and the finial agree with the surface.
		Profile.back() = Vector2(0.0f, 1.0f);

		return Profile;
	}

	/**
	 * 腰檐 profile for a polygonal storey, in the same (radius fraction, height fraction) layout:
	 * from the eave (1, 0) in to the break (OpenFraction, 1). The same concave 举架 curve as 攒尖,
	 * just stopped at the upper wall; the caller scales height by the skirt's own rise.
	 */
	std::vector<Vector2> BuildWaistProfile(const BuildingSpec& Spec, float OpenFraction)
	{
		const int32_t Courses = std::max(Spec.RafterCourses, 3);

		std::vector<Vector2> Profile;
		Profile.reserve(size_t(Courses) + 1);
		for (int32_t Index = 0; Index <= Courses; ++Index)
		{
			const float T = float(Index) / float(Courses);
			Profile.push_back(Vector2(1.0f - (1.0f - OpenFraction) * T, std::pow(T, 1.0f / 1.45f)));
		}

		return Profile;
	}

	/**
	 * Where the tile skin stops, as a fraction of the eave apothem.
	 *
	 * On a centralised roof every course converges on the apex, so its pitch shrinks with the plan
	 * radius. Taken all the way in, the barrels end up narrower than a pixel and the whole crown
	 * shimmers. Real 攒尖 roofs do not tile to a point either: the courses die into the masonry
	 * base under the 宝顶, which is what BuildFinialBase covers this with.
	 */
	const float TILE_APEX_CUTOFF = 0.22f;

	/** 宝顶 base: the drum the converging courses and 垂脊 die into. */
	void BuildFinialBase(const BuildingSpec& Spec, const Vector3& Apex, float Radius, MeshAccumulator& Mesh)
	{
		if (Radius <= 0.0f)
		{
			return;
		}

		// Sunk slightly so its rim overlaps the last ring of tiles rather than meeting it exactly.
		const float Drop = Radius * 0.35f;
		Mesh.AddColumn(
			Apex - Vector3(0.0f, Drop, 0.0f), Drop * 1.45f,
			Radius, Radius * 0.62f, 12, Spec.RidgeColor);
	}

	/** 宝顶: a small stack of blocks at the apex, so the converging ridges have something to die into. */
	void BuildFinial(const BuildingSpec& Spec, const Vector3& Apex, MeshAccumulator& Mesh)
	{
		const float Size = Spec.FinialSize;
		if (Size <= 0.0f)
		{
			return;
		}

		Mesh.AddColumn(Apex, Size * 0.35f, Size * 0.42f, Size * 0.30f, 8, Spec.RidgeColor);
		Mesh.AddColumn(
			Apex + Vector3(0.0f, Size * 0.35f, 0.0f), Size * 0.55f,
			Size * 0.30f, Size * 0.10f, 8, Spec.RidgeColor * 1.15f);
		Mesh.AddBox(
			Apex + Vector3(0.0f, Size * 0.95f, 0.0f),
			Vector3(Size * 0.11f, Size * 0.11f, Size * 0.11f),
			Spec.RidgeColor * 1.3f);
	}
} // namespace

std::vector<Vector2> BuildingGen::PlanPolygon(float Apothem, int32_t Sides)
{
	const int32_t Count = std::max(Sides, 3);
	// Circumradius from the apothem, and a half-step rotation so edges face the axes.
	const float Radius = Apothem / std::cos(POLY_PI / float(Count));

	std::vector<Vector2> Result;
	Result.reserve(size_t(Count));
	for (int32_t Index = 0; Index < Count; ++Index)
	{
		const float Angle = POLY_TAU * float(Index) / float(Count) + POLY_PI / float(Count);
		Result.push_back(Vector2(Radius * std::cos(Angle), Radius * std::sin(Angle)));
	}

	return Result;
}

void BuildingGen::BuildCentralisedRoof(
	const BuildingSpec& Spec, ECentralProfile Profile, MeshAccumulator& OutMesh)
{
	BuildCentralisedShell(Spec, Profile, 0.0f, OutMesh);
}

void BuildingGen::BuildCentralisedShell(
	const BuildingSpec& Spec, ECentralProfile Profile, float OpenFraction, MeshAccumulator& OutMesh)
{
	// OpenFraction > 0: a 腰檐 round a polygonal storey. The loft stops at that fraction of the
	// eave apothem — the upper storey's wall — and Spec.RoofHeight is the skirt's own rise.
	const bool bOpen = OpenFraction > 0.0f;
	const int32_t Sides = EffectiveSides(Spec);
	const float EaveApothem = Spec.PlanApothem + Spec.EaveOverhang;
	const Vector3 Apex(0.0f, Spec.RoofBase + Spec.RoofHeight, 0.0f);

	const std::vector<Vector2> Shape = bOpen
		? BuildWaistProfile(Spec, OpenFraction)
		: BuildCentralProfile(Spec, Profile);

	const float Circumradius = EaveApothem / std::cos(POLY_PI / float(Sides));
	// Facet chord length, which is the natural scale for how far a corner lift may reach. Using
	// the rectangular CornerSpan here would swallow a whole facet on an octagon.
	const float FacetLength = 2.0f * Circumradius * std::sin(POLY_PI / float(Sides));

	CornerFlip Flip;
	Flip.Rise = Spec.CornerRise;
	Flip.Extend = Spec.CornerExtend;
	// Half a facet is the most a corner can own; the ratio trims it further.
	Flip.Span = FacetLength * 0.5f * std::fmin(std::fmax(Spec.CornerSpan / std::fmax(Spec.PlanApothem, 0.01f), 0.15f), 1.0f);
	Flip.bPolygonal = true;
	Flip.Sides = Sides;
	Flip.Radius = Circumradius;

	// 圆攒尖 has no corners to lift and no ridges: it is a cone.
	const bool bRound = Spec.RoofType == ROOF_ROUND;
	if (bRound)
	{
		Flip.Rise = 0.0f;
		Flip.Extend = 0.0f;
	}

	// Sample the eave polygon densely so the corner lift curves instead of kinking.
	const int32_t PerSide = std::max(int32_t(
		(EaveApothem * POLY_TAU / float(Sides)) / std::fmax(Spec.Module * 0.7f, 0.05f)), 2);

	const auto RingAt = [&](float RadiusFraction, float Height) -> std::vector<Vector3>
	{
		const std::vector<Vector2> Corners = PlanPolygon(EaveApothem * RadiusFraction, Sides);
		std::vector<Vector3> Ring;
		Ring.reserve(size_t(Sides * PerSide));

		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			const Vector2& From = Corners[size_t(Side)];
			const Vector2& To = Corners[size_t((Side + 1) % Sides)];
			for (int32_t Step = 0; Step < PerSide; ++Step)
			{
				const float T = float(Step) / float(PerSide);
				const Vector2 Plan = From + (To - From) * T;
				Ring.push_back(Flip.Apply(Vector3(Plan.x, Height, Plan.y)));
			}
		}

		return Ring;
	};

	// ---- Boarding, lofted from the eave up to the apex ----
	std::vector<std::vector<Vector3>> Rings;
	Rings.reserve(Shape.size());
	for (const Vector2& Step : Shape)
	{
		Rings.push_back(RingAt(Step.x, Spec.RoofBase + Step.y * Spec.RoofHeight));
	}

	const float BoardThickness = GetBoardThickness(Spec);
	const Color BoardColor = Spec.TileColor * 0.7f;
	const Color SoffitColor = Spec.TimberColor * 1.15f;

	// 望板 lofted from the eave up to the apex.
	OutMesh.SetSlot(EMaterialSlot::Timber);
	for (size_t Level = 0; Level + 1 < Rings.size(); ++Level)
	{
		const std::vector<Vector3>& Low = Rings[Level];
		const std::vector<Vector3>& High = Rings[Level + 1];
		const size_t Count = Low.size();

		for (size_t Index = 0; Index < Count; ++Index)
		{
			const size_t Next = (Index + 1) % Count;

			Vector3 Normal;
			Vector3 VertexNormals[4];
			if (Spec.RoofCurveMode == 1)
			{
				// Central-difference slope of the dense central profile at each ring's span,
				// scaled into world units and tilted outward by each corner's own azimuth.
				// The profile is already an analytic curve sampled densely, so this carries
				// the curve's smooth shading onto the loft and adjacent rings share their
				// boundary values; the legacy path kept the exact planar normal of each
				// quad, which is what faceted the cone.
				const auto LevelNormal = [&](size_t L) -> Vector2
				{
					const size_t Prev = (L > 0) ? L - 1 : 0;
					const size_t NextLevel = std::min(L + 1, Shape.size() - 1);
					const float DR = Shape[Prev].x - Shape[NextLevel].x;
					const float DH = Shape[NextLevel].y - Shape[Prev].y;
					return (DR > 1e-9f)
						? Vector2(DH * Spec.RoofHeight, DR * EaveApothem).normalized()
						: Vector2(0.0f, 1.0f);
				};
				const Vector2 N2Low = LevelNormal(Level);
				const Vector2 N2High = LevelNormal(Level + 1);
				const Vector3 Corners[4] = { Low[Index], Low[Next], High[Next], High[Index] };
				const Vector2* LevelNormals[4] = { &N2Low, &N2Low, &N2High, &N2High };
				for (int32_t K = 0; K < 4; ++K)
				{
					const Vector2 P(Corners[K].x, Corners[K].z);
					if (P.length_squared() > 1e-12f)
					{
						const Vector2 Dir = P.normalized();
						VertexNormals[K] = Vector3(
							Dir.x * LevelNormals[K]->x, LevelNormals[K]->y, Dir.y * LevelNormals[K]->x);
					}
					else
					{
						VertexNormals[K] = Vector3(0.0f, 1.0f, 0.0f);
					}
				}
				Normal = (VertexNormals[0] + VertexNormals[1] + VertexNormals[2] + VertexNormals[3]).normalized();
			}
			else
			{
				Normal = (Low[Next] - Low[Index]).cross(High[Index] - Low[Index]);
				if (Normal.length_squared() < 1e-12f)
				{
					continue;
				}
				Normal = Normal.normalized();
				if (Normal.y < 0.0f)
				{
					Normal = -Normal;
				}
			}

			AddRoofPanel(
				OutMesh,
				Low[Index], Low[Next], High[Next], High[Index],
				Normal, BoardThickness,
				Level == 0 ? ERoofPanelEdges::Lower : ERoofPanelEdges::None,
				BoardColor, SoffitColor,
				Spec.RoofCurveMode == 1 ? VertexNormals : nullptr);
		}
	}

	// ---- Tile skin running up each facet, from the eave towards the apex ----
	{
		const std::vector<Vector2> Corners = PlanPolygon(EaveApothem, Sides);
		const float SideLength = Corners[0].distance_to(Corners[size_t(1 % Sides)]);
		const int32_t Courses = std::max(
			int32_t(SideLength / std::fmax(Spec.TileCourseWidth, 0.05f)), 1);
		const float Pitch = SideLength / float(Courses);

		// Shape.x is already a radius fraction, so the coverage boundary sits at x = coverage.
		const size_t KeepFrom = RoofCoverageStart(Spec, Shape, 1.0f);
		Vector2 CoverageBoundary;
		const bool bCoverageBoundary = RoofCoverageBoundary(Spec, Shape, 1.0f, KeepFrom, CoverageBoundary);

		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			const Vector2& From = Corners[size_t(Side)];
			const Vector2& To = Corners[size_t((Side + 1) % Sides)];

			// Position across the facet, held constant as the column climbs. Because every ring is
			// the same polygon scaled about the centre, holding the *fraction* keeps the column on
			// the facet all the way to the apex — no clipping needed here, and the courses converge
			// on the finial the way a real 攒尖 roof's do.
			// 包络 (R14.1): the facet's across-slope domain is [0, SideLength] — a corner at either
			// end, each carrying a 垂脊 down from the apex. The courses are cut inside both.
			const float FacetRidge = Spec.Module * 0.85f * Spec.RidgeScale;
			const TileCourseLayout Band = TileBandFor(
				Spec, 0.0f, SideLength, Pitch, Courses, FacetRidge, FacetRidge);

			std::vector<TileSkinColumn> Columns;
			LayTileCourses(
				Spec.TileDetail,
				Band,
				Courses,
				[&Shape, &From, &To, &Flip, &Spec, SideLength, KeepFrom, bCoverageBoundary, &CoverageBoundary](float Across) -> std::vector<Vector3>
				{
					const float Fraction = Across / std::fmax(SideLength, 1e-6f);

					std::vector<Vector3> Points;
					Points.reserve(Shape.size() - KeepFrom + 1);
					// The exact coverage edge, unless it falls inside the finial's masonry cutoff.
					if (bCoverageBoundary && CoverageBoundary.x >= TILE_APEX_CUTOFF)
					{
						const Vector2 Plan = (From + (To - From) * Fraction) * CoverageBoundary.x;
						Points.push_back(Flip.Apply(Vector3(
							Plan.x, Spec.RoofBase + CoverageBoundary.y * Spec.RoofHeight, Plan.y)));
					}
					for (size_t Index = KeepFrom; Index < Shape.size(); ++Index)
					{
						const Vector2& Step = Shape[Index];
						if (Step.x < TILE_APEX_CUTOFF)
						{
							break;
						}

						const Vector2 Plan = (From + (To - From) * Fraction) * Step.x;
						Points.push_back(Flip.Apply(
							Vector3(Plan.x, Spec.RoofBase + Step.y * Spec.RoofHeight, Plan.y)));
					}

					return Points;
				},
				Columns);

			// Cr below 1 bares the roof from the eave upward, leaving column 0 partway up the facet.
			OutMesh.SetSlot(EMaterialSlot::Tile);
			BuildTileSkin(
				Columns,
				ETileSkinLoop::Open,
				KeepFrom == 0 ? ETileEaves::AtStart : ETileEaves::None,
				TileSkinSettingsFor(Spec),
				Spec.TileColor,
				OutMesh);
		}
	}

	// ---- 垂脊 from the apex down each corner, and the eave drip course ----
	// 高度链 (R17): the converging courses rise with the 泥背 layer, so everything that bears on
	// them — the 垂脊 and the 宝顶 drum — comes up by the same amount. 0 unless the tier beds the skin.
	const float Bedding = RoofBeddingLift(Spec);
	OutMesh.SetSlot(EMaterialSlot::Ridge);
	if (!bRound)
	{
		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			const Vector2 Corner = PlanPolygon(EaveApothem, Sides)[size_t(Side)];

			std::vector<Vector3> Knots;
			for (size_t Index = Shape.size(); Index-- > 0;)
			{
				// Stop short of the apex: every ridge converging on the same point overlaps into
				// a spiky crown. The 宝顶 finial covers the junction, which is its actual job.
				// A 腰檐's 角脊 run all the way in to the 围脊 instead.
				if (!bOpen && Shape[Index].y > 0.9f)
				{
					continue;
				}
				const Vector2 Plan = Corner * Shape[Index].x;
				Knots.push_back(Flip.Apply(
					Vector3(Plan.x, Spec.RoofBase + Shape[Index].y * Spec.RoofHeight, Plan.y)));
			}
			if (Knots.size() < 2)
			{
				continue;
			}
			LiftAlongKnotNormals(Knots, Bedding);

			const float CornerRidgeScale = Spec.Module * 0.85f * Spec.RidgeScale;

			SweepSettings Settings;
			ConfigureRidgeSweep(Settings, Spec, CornerRidgeScale, true);
			Settings.bClosedContour = true;
			Settings.UpReference = Vector3(0, 1, 0);

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				OutMesh.AddSweep(Sweep, Spec.RidgeColor);
			}

			// The profile is walked from the apex down, so the eave end is the last knot.
			SeatRidgeBeasts(OutMesh, Spec, Knots, CornerRidgeScale, 0x2);
		}
	}

	// 连檐 board round the eave polygon, inset and dropped so it sits under the eave tiles rather
	// than in front of them.
	{
		OutMesh.SetSlot(EMaterialSlot::Timber);
		const float Back = Spec.Module * 0.26f;
		const float Inset = 1.0f - Back / std::fmax(EaveApothem, 1e-6f);
		std::vector<Vector3> Knots = RingAt(Inset, Spec.RoofBase - Spec.Module * 0.20f);
		Knots.push_back(Knots.front());

		SweepSettings Settings;
		Settings.Contour = MakeEaveSection(Spec.Module * 0.42f * Spec.RidgeScale);
		Settings.bClosedContour = true;
		Settings.bGenerateCaps = false;

		SweepResult Sweep;
		if (BuildSweep(Knots, Settings, Sweep))
		{
			OutMesh.AddSweep(Sweep, Spec.RidgeColor);
		}
	}

	// Eave rafter heads under the eave polygon, fanning toward the apex like the courses: one
	// head per eave sample midpoint, from the same section as the rectangular roofs.
	{
		const std::vector<Vector2> Corners = PlanPolygon(EaveApothem, Sides);
		std::vector<Vector3> Ring;
		Ring.reserve(size_t(Sides * PerSide));
		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			const Vector2& From = Corners[size_t(Side)];
			const Vector2& To = Corners[size_t((Side + 1) % Sides)];
			for (int32_t Step = 0; Step < PerSide; ++Step)
			{
				const float T = float(Step) / float(PerSide);
				const Vector2 Plan = From + (To - From) * T;
				Ring.push_back(Vector3(Plan.x, Spec.RoofBase, Plan.y));
			}
		}

		// The central profile is sampled densely, so the first pair gives the eave tangent.
		const float TangentSlope = (Shape[1].y * Spec.RoofHeight)
			/ std::fmax((Shape[0].x - Shape[1].x) * EaveApothem, POLY_EPSILON);

		std::vector<Vector3> Points;
		std::vector<Vector2> Inward;
		Points.reserve(Ring.size());
		Inward.reserve(Ring.size());
		for (size_t Index = 0; Index < Ring.size(); ++Index)
		{
			const Vector3& A = Ring[Index];
			const Vector3& B = Ring[(Index + 1) % Ring.size()];
			const Vector2 Mid((A.x + B.x) * 0.5f, (A.z + B.z) * 0.5f);
			if (Mid.length_squared() < 1e-12f)
			{
				continue;
			}
			Points.push_back(Vector3(Mid.x, Spec.RoofBase, Mid.y));
			Inward.push_back(-Mid.normalized());
		}

		AddEaveRafterHeads(OutMesh, Spec, Points, Inward, TangentSlope, &Flip, Spec.TimberColor * 1.28f);
	}

	// 宝顶 and the masonry drum it sits on: the converging 垂脊 die into them, so both go on the
	// ridge slot rather than the stone one. Both ride the bedding lift with the courses they cover.
	OutMesh.SetSlot(EMaterialSlot::Ridge);
	if (bOpen)
	{
		// 腰檐: a 围脊 round the break instead, against the upper storey's wall.
		std::vector<Vector3> Knots;
		const std::vector<Vector2> Corners = PlanPolygon(EaveApothem * Shape.back().x, Sides);
		for (const Vector2& Corner : Corners)
		{
			Knots.push_back(Vector3(Corner.x, Apex.y + Bedding, Corner.y));
		}
		Knots.push_back(Knots.front());

		SweepSettings Settings;
		ConfigureRidgeSweep(Settings, Spec, Spec.Module * 1.05f * Spec.RidgeScale);
		Settings.bClosedContour = true;
		Settings.bGenerateCaps = false;

		SweepResult Sweep;
		if (BuildSweep(Knots, Settings, Sweep))
		{
			OutMesh.AddSweep(Sweep, Spec.RidgeColor);
		}
		return;
	}
	BuildFinialBase(Spec, Apex + Vector3(0.0f, Bedding, 0.0f), EaveApothem * TILE_APEX_CUTOFF, OutMesh);
	BuildFinial(Spec, Apex + Vector3(0.0f, Bedding, 0.0f), OutMesh);
}

namespace
{
	/** Which parts of a polygonal building one call emits; the storey stack builds them apart. */
	enum EPolyParts : uint32_t
	{
		POLY_NONE = 0,
		POLY_PLATFORM = 1u << 0,
		POLY_BODY = 1u << 1,
		POLY_STEPS_FENCE = 1u << 2,
		POLY_ROOF = 1u << 3,
		POLY_ALL = POLY_PLATFORM | POLY_BODY | POLY_STEPS_FENCE | POLY_ROOF,
	};

	void BuildPolygonalParts(
		const BuildingSpec& Spec, ECentralProfile Profile, uint32_t Parts, MeshAccumulator& OutMesh);
} // namespace

void BuildingGen::BuildPolygonalBuilding(
	const BuildingSpec& Spec, ECentralProfile Profile, MeshAccumulator& OutMesh)
{
	if (Spec.StoreyCount > 1)
	{
		BuildPolygonalStoreys(Spec, Profile, OutMesh);
		return;
	}

	if (Spec.BaseKind == 1)
	{
		// A pavilion on a 城台 (角楼, 台上亭): the terrace replaces the polygonal platform.
		BuildMasonryTerrace(Spec, OutMesh);
		BuildPolygonalParts(Spec, Profile, POLY_BODY | POLY_ROOF, OutMesh);
		return;
	}

	BuildPolygonalParts(Spec, Profile, POLY_ALL, OutMesh);
}

namespace
{
void BuildPolygonalParts(
	const BuildingSpec& Spec, ECentralProfile Profile, uint32_t Parts, MeshAccumulator& OutMesh)
{
	const int32_t Sides = std::max(Spec.Sides, 3);
	const float BodyApothem = Spec.PlanApothem;
	const float PlatformApothem = BodyApothem + (Spec.PlatformHalfWidth - Spec.Width * 0.5f);

	const std::vector<Vector2> PlatformPlan = PlanPolygon(PlatformApothem, Sides);
	const std::vector<Vector2> BodyPlan = PlanPolygon(BodyApothem, Sides);

	// ---- Platform: a prism with a slightly wider 阶条石 cap ----
	if ((Parts & POLY_PLATFORM) && Spec.BaseKind == 3)
	{
		AddStiltDeck(Spec, OutMesh, PlatformPlan);
	}
	else if ((Parts & POLY_PLATFORM) && Spec.PlatformHeight > 0.0f)
	{
		const float CapHeight = std::fmin(Spec.PlatformHeight * 0.22f, Spec.Module * 0.5f);
		const float BodyHeight = Spec.PlatformHeight - CapHeight;
		const std::vector<Vector2> Inner = PlanPolygon(PlatformApothem - Spec.Module * 0.12f, Sides);

		const auto AddPrism = [&](const std::vector<Vector2>& Plan, float Bottom, float Top, const Color& Tint)
		{
			for (int32_t Side = 0; Side < Sides; ++Side)
			{
				const Vector2& From = Plan[size_t(Side)];
				const Vector2& To = Plan[size_t((Side + 1) % Sides)];

				const Vector3 A(From.x, Bottom, From.y);
				const Vector3 B(To.x, Bottom, To.y);
				const Vector3 C(To.x, Top, To.y);
				const Vector3 D(From.x, Top, From.y);

				const Vector3 Outward = Vector3((From.x + To.x) * 0.5f, 0.0f, (From.y + To.y) * 0.5f).normalized();
				OutMesh.AddQuadOriented(A, B, C, D, Outward, Tint);
			}

			std::vector<Vector3> Cap;
			for (const Vector2& Point : Plan)
			{
				Cap.push_back(Vector3(Point.x, Top, Point.y));
			}
			OutMesh.AddPolygon(Cap, Vector3(0, 1, 0), Tint);
		};

		// 台基 body and 阶条石 cap: stone, as in the rectangular branch.
		OutMesh.SetSlot(EMaterialSlot::Stone);
		AddPrism(Inner, 0.0f, BodyHeight, Spec.StoneColor);
		AddPrism(PlatformPlan, BodyHeight, Spec.PlatformHeight, Spec.StoneColor * 1.06f);
	}

	// ---- Body: a column on every vertex, walls between them ----
	const float Base = Spec.PlatformHeight;

	// 通柱 from the storey below: shafts only; walls stay on this floor. Same as BuildBody.
	BuildingSpec DroppedColumns = Spec;
	DroppedColumns.ColumnHeight += Spec.ColumnFootDrop;

	if ((Parts & POLY_BODY) && Spec.bGenerateColumns)
	{
		for (size_t I = 0; I < BodyPlan.size(); ++I)
		{
			const Vector2& Point = BodyPlan[I];
			AddBuildingColumn(Spec.ColumnFootDrop > POLY_EPSILON ? DroppedColumns : Spec, OutMesh,
				Vector3(Point.x, Base - Spec.ColumnFootDrop, Point.y), 0x200000u + uint32_t(I));
		}
	}

	if ((Parts & POLY_BODY) && !Spec.bGenerateWalls && (Spec.RailingKind > 0 || Spec.bHangingFascia))
	{
		// 亭: bench railings / 美人靠 / 倒挂楣子 in every bay but the entrance (the +Z-facing
		// side, where the stair lands), exactly the side the walled path leaves open.
		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			const Vector2& From = BodyPlan[size_t(Side)];
			const Vector2& To = BodyPlan[size_t((Side + 1) % Sides)];
			const Vector2 Mid = (From + To) * 0.5f;
			AddOpenBayInfill(Spec, OutMesh, Vector3(From.x, Base, From.y), Vector3(To.x, Base, To.y),
				Vector3(Mid.x, 0.0f, Mid.y), Side == Sides - 1 && Spec.bGenerateSteps);
		}
	}

	if ((Parts & POLY_BODY) && Spec.bGenerateWalls)
	{
		// The polygonal wall is one plain plastered slab per edge — no 槛墙, no opening — so
		// unlike the rectangular bay it is all wall.
		OutMesh.SetSlot(EMaterialSlot::Wall);

		const float WallHeight = Spec.ColumnHeight * 0.94f;
		const float WallHalf = Spec.ColumnRadius * 0.72f;

		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			// Leave the side facing +Z open as the entrance, matching where the stair lands.
			if (Side == Sides - 1)
			{
				continue;
			}

			const Vector2& From = BodyPlan[size_t(Side)];
			const Vector2& To = BodyPlan[size_t((Side + 1) % Sides)];
			const Vector2 Mid = (From + To) * 0.5f;
			const Vector2 Along = To - From;
			const float Length = Along.length();
			if (Length < POLY_EPSILON)
			{
				continue;
			}

			// A thin slab spanning the edge, rotated to lie along it.
			const float Angle = std::atan2(Along.y, Along.x);
			const int32_t Steps = std::max(int32_t(Length / std::fmax(Spec.Module, 0.1f)), 1);
			const float SlabLength = Length / float(Steps);

			for (int32_t Step = 0; Step < Steps; ++Step)
			{
				const float T = (float(Step) + 0.5f) / float(Steps);
				const Vector2 At = From + Along * T;

				// Build the slab as four corners so it can follow the edge direction.
				const Vector2 Dir(std::cos(Angle), std::sin(Angle));
				const Vector2 Perp(-Dir.y, Dir.x);
				const Vector2 P0 = At - Dir * (SlabLength * 0.5f) - Perp * WallHalf;
				const Vector2 P1 = At + Dir * (SlabLength * 0.5f) - Perp * WallHalf;
				const Vector2 P2 = At + Dir * (SlabLength * 0.5f) + Perp * WallHalf;
				const Vector2 P3 = At - Dir * (SlabLength * 0.5f) + Perp * WallHalf;

				const float Top = Base + WallHeight;
				const Vector2 Quad[4] = { P0, P1, P2, P3 };
				for (int32_t Face = 0; Face < 4; ++Face)
				{
					const Vector2& A = Quad[Face];
					const Vector2& B = Quad[(Face + 1) % 4];
					const Vector3 Outward = Vector3(
						(A.x + B.x) * 0.5f - At.x, 0.0f, (A.y + B.y) * 0.5f - At.y).normalized();
					OutMesh.AddQuadOriented(
						Vector3(A.x, Base, A.y), Vector3(B.x, Base, B.y),
						Vector3(B.x, Top, B.y), Vector3(A.x, Top, A.y),
						Outward, Spec.PlasterColor);
				}

				std::vector<Vector3> Cap;
				for (const Vector2& Corner : Quad)
				{
					Cap.push_back(Vector3(Corner.x, Top, Corner.y));
				}
				OutMesh.AddPolygon(Cap, Vector3(0, 1, 0), Spec.PlasterColor);
			}
		}
	}

	// ---- Bracket band, following the polygon ----
	if ((Parts & POLY_BODY) && Spec.BracketHeight > 0.0f)
	{
		// 阑额 band and 斗 blocks: timber.
		OutMesh.SetSlot(EMaterialSlot::Timber);

		const float ColumnTop = Base + Spec.ColumnHeight;
		const float Overhang = Spec.ColumnRadius * 1.5f;
		const std::vector<Vector2> BandPlan = PlanPolygon(BodyApothem + Overhang * 0.6f, Sides);

		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			const Vector2& From = BandPlan[size_t(Side)];
			const Vector2& To = BandPlan[size_t((Side + 1) % Sides)];

			const Vector3 A(From.x, ColumnTop, From.y);
			const Vector3 B(To.x, ColumnTop, To.y);
			const Vector3 C(To.x, ColumnTop + Spec.BracketHeight * 0.84f, To.y);
			const Vector3 D(From.x, ColumnTop + Spec.BracketHeight * 0.84f, From.y);

			const Vector3 Outward = Vector3((From.x + To.x) * 0.5f, 0.0f, (From.y + To.y) * 0.5f).normalized();
			OutMesh.AddQuadOriented(A, B, C, D, Outward, Spec.BracketColor);
		}

		for (const Vector2& Point : BodyPlan)
		{
			OutMesh.AddBox(
				Vector3(Point.x, ColumnTop + Spec.BracketHeight * 0.78f, Point.y),
				Vector3(Spec.ColumnRadius * 1.5f, Spec.BracketHeight * 0.3f, Spec.ColumnRadius * 1.5f),
				Spec.BracketColor * 1.1f);
		}
	}

	// ---- A single stair run on the open side, plus a balustrade elsewhere ----
	if ((Parts & POLY_STEPS_FENCE) && Spec.bGenerateSteps && Spec.PlatformHeight > 0.0f && Spec.StepRunDepth > 0.0f)
	{
		const int32_t Steps = std::max(Spec.StepCount, 1);
		const float RunWidth = Spec.FenceGapWidth;
		const float TreadDepth = Spec.StepRunDepth / float(Steps);

		OutMesh.SetSlot(EMaterialSlot::Stone);

		for (int32_t Step = 0; Step < Steps; ++Step)
		{
			const float Top = Spec.PlatformHeight * float(Steps - Step) / float(Steps);
			const float Near = PlatformApothem + float(Step) * TreadDepth;

			OutMesh.AddBox(
				Vector3(0.0f, Top * 0.5f, Near + TreadDepth * 0.5f),
				Vector3(RunWidth * 0.5f, Top * 0.5f, TreadDepth * 0.5f),
				Spec.StoneColor);
		}
	}

	// 栏杆与台基同进退：没有台基时它坐在平地上，没有意义（与矩形分支的处置一致）。
	if ((Parts & POLY_STEPS_FENCE) && Spec.bGenerateFence && Spec.bGeneratePlatform)
	{
		// 栏杆: stone rail and posts, as in the rectangular branch.
		OutMesh.SetSlot(EMaterialSlot::Stone);

		const float RailHeight = Spec.PlatformHeight + Spec.FenceHeight;
		const float RailHalf = Spec.Module * 0.2f;

		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			// Skip the entrance side so the stair is not fenced off.
			if (Side == Sides - 1)
			{
				continue;
			}

			const Vector2& From = PlatformPlan[size_t(Side)];
			const Vector2& To = PlatformPlan[size_t((Side + 1) % Sides)];

			std::vector<Vector3> Knots;
			Knots.push_back(Vector3(From.x, RailHeight, From.y));
			Knots.push_back(Vector3(To.x, RailHeight, To.y));

			SweepSettings Settings;
			Settings.Contour.push_back(Vector2(-RailHalf, -RailHalf * 0.5f));
			Settings.Contour.push_back(Vector2(RailHalf, -RailHalf * 0.5f));
			Settings.Contour.push_back(Vector2(RailHalf, RailHalf * 0.5f));
			Settings.Contour.push_back(Vector2(-RailHalf, RailHalf * 0.5f));
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				OutMesh.AddSweep(Sweep, Spec.StoneColor * 1.04f);
			}

			OutMesh.AddBox(
				Vector3(From.x, Spec.PlatformHeight + Spec.FenceHeight * 0.5f, From.y),
				Vector3(Spec.Module * 0.16f, Spec.FenceHeight * 0.5f, Spec.Module * 0.16f),
				Spec.StoneColor * 0.96f);
		}
	}

	if (Parts & POLY_ROOF)
	{
		BuildCentralisedRoof(Spec, Profile, OutMesh);
	}
}

	/** Plan-polygon prism with a top cap, faces wound outward. */
	void AddPolygonSlab(MeshAccumulator& Mesh, const std::vector<Vector2>& Plan, float Bottom, float Top,
		const Color& Tint)
	{
		const size_t Count = Plan.size();
		for (size_t Side = 0; Side < Count; ++Side)
		{
			const Vector2& From = Plan[Side];
			const Vector2& To = Plan[(Side + 1) % Count];
			const Vector3 Outward = Vector3((From.x + To.x) * 0.5f, 0.0f, (From.y + To.y) * 0.5f).normalized();
			Mesh.AddQuadOriented(Vector3(From.x, Bottom, From.y), Vector3(To.x, Bottom, To.y),
				Vector3(To.x, Top, To.y), Vector3(From.x, Top, From.y), Outward, Tint);
		}

		std::vector<Vector3> Cap;
		std::vector<Vector3> Floor;
		for (const Vector2& Point : Plan)
		{
			Cap.push_back(Vector3(Point.x, Top, Point.y));
			Floor.push_back(Vector3(Point.x, Bottom, Point.y));
		}
		Mesh.AddPolygon(Cap, Vector3(0, 1, 0), Tint);
		Mesh.AddPolygon(Floor, Vector3(0, -1, 0), Tint);
	}

	/** Polygonal 平座: deck, 平座铺作 band and a 勾栏 round the edge. Spec is the upper storey's. */
	void BuildPolygonalBalcony(const BuildingSpec& Spec, float Floor, float BandBottom, MeshAccumulator& Mesh)
	{
		const int32_t Sides = std::max(Spec.Sides, 3);
		const float Apothem = Spec.PlanApothem + Spec.BalconyProjection;
		const float Deck = Spec.Module * 0.32f;

		Mesh.SetSlot(EMaterialSlot::Timber);
		AddPolygonSlab(Mesh, PlanPolygon(Apothem, Sides), Floor - Deck, Floor, Spec.TimberColor * 1.2f);
		if (Floor - Deck - BandBottom > POLY_EPSILON)
		{
			AddPolygonSlab(Mesh, PlanPolygon(Apothem - Spec.BalconyProjection * 0.35f, Sides),
				BandBottom, Floor - Deck, Spec.BracketColor);
		}

		const std::vector<Vector2> Rim = PlanPolygon(Apothem - Spec.Module * 0.16f, Sides);
		const float RailHeight = std::fmin(Spec.FenceHeight, Spec.Module * 1.4f);
		const float RailHalf = Spec.Module * 0.16f;
		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			const Vector2& From = Rim[size_t(Side)];
			const Vector2& To = Rim[size_t((Side + 1) % Sides)];

			std::vector<Vector3> Knots;
			Knots.push_back(Vector3(From.x, Floor + RailHeight, From.y));
			Knots.push_back(Vector3(To.x, Floor + RailHeight, To.y));

			SweepSettings Settings;
			Settings.Contour.push_back(Vector2(-RailHalf, -RailHalf * 0.5f));
			Settings.Contour.push_back(Vector2(RailHalf, -RailHalf * 0.5f));
			Settings.Contour.push_back(Vector2(RailHalf, RailHalf * 0.5f));
			Settings.Contour.push_back(Vector2(-RailHalf, RailHalf * 0.5f));
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.TimberColor * 1.1f);
			}

			// Posts at the vertices and one mid-span, plus a low 地栿 along the deck.
			for (const Vector2& At : { From, (From + To) * 0.5f })
			{
				Mesh.AddBox(Vector3(At.x, Floor + RailHeight * 0.5f, At.y),
					Vector3(RailHalf, RailHeight * 0.5f, RailHalf), Spec.TimberColor * 1.05f);
			}
			std::vector<Vector3> Sill;
			Sill.push_back(Vector3(From.x, Floor + RailHalf * 0.5f, From.y));
			Sill.push_back(Vector3(To.x, Floor + RailHalf * 0.5f, To.y));
			if (BuildSweep(Sill, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.TimberColor);
			}
		}
	}

	struct PolyStorey
	{
		BuildingSpec Body;
		bool bTop = true;
		/** 腰檐 break, as a fraction of this storey's eave apothem. */
		float OpenFraction = 0.0f;
		float WaistRiseHeight = 0.0f;
		float BreakTop = 0.0f;
		float NextFloor = 0.0f;
	};

	/**
	 * The polygonal storey stack — 重檐亭 and polygonal 阁. Same rules as the rectangular stack
	 * (one module, 腰檐 opened at the upper wall, 平座 optional, 通柱 when set back without one),
	 * but a polygonal plan has no bay grid: its columns stand on the vertices, and a setback ring
	 * is an inner polygon of 金柱.
	 */
	std::vector<PolyStorey> PlanPolygonalStoreys(const BuildingSpec& Spec)
	{
		const int32_t Count = std::clamp(Spec.StoreyCount, 1, 5);
		const int32_t Setback = std::clamp(Spec.StoreySetbackBays, 0, 2);
		// [自定] A polygonal 廊步: the ring of 金柱 stands 0.3 of the apothem inside the 檐柱.
		const float Aisle = Spec.PlanApothem * 0.3f;

		std::vector<PolyStorey> Plans;
		BuildingSpec Current = Spec;
		for (int32_t Storey = 0; Storey < Count; ++Storey)
		{
			PolyStorey Plan;
			Plan.Body = Current;
			Plan.bTop = Storey == Count - 1;
			if (Plan.bTop)
			{
				Plans.push_back(Plan);
				break;
			}

			BuildingSpec Next = Current;
			Next.PlanApothem = std::fmax(Current.PlanApothem - Aisle * float(Setback), Spec.Module);
			Next.Width = Next.PlanApothem * 2.0f;
			Next.Depth = Next.Width;

			// 腰檐 from this storey's eave in to the outer face of the upper storey's columns.
			const float EaveApothem = Current.PlanApothem + Current.EaveOverhang;
			const float BreakApothem = Next.PlanApothem + Spec.ColumnRadius;
			const float Inset = std::fmax(EaveApothem - BreakApothem, Spec.Module * 0.2f);
			Plan.OpenFraction = std::clamp((EaveApothem - Inset) / EaveApothem, 0.05f, 0.95f);
			Plan.WaistRiseHeight = WaistRiseFor(Current, Inset);
			Plan.BreakTop = Current.RoofBase + Plan.WaistRiseHeight;

			const float Floor = Plan.BreakTop + (Spec.bStoreyBalcony ? Spec.BracketHeight : 0.0f);
			Plan.NextFloor = Floor;

			Next.ColumnFootDrop = (!Spec.bStoreyBalcony && Setback > 0)
				? Floor - Current.PlatformHeight + Current.ColumnFootDrop
				: 0.0f;
			Next.ColumnBaseHeight = (Next.ColumnFootDrop > 0.0f) ? Spec.ColumnBaseHeight : 0.0f;
			Next.PlatformHeight = Floor;
			Next.ColumnHeight = Spec.ColumnHeight * std::fmax(Spec.UpperColumnHeightScale, 0.05f);
			Next.EaveHeight = Floor + Next.ColumnHeight;
			Next.RoofBase = Next.EaveHeight + Spec.BracketHeight;
			const float Ratio = Next.PlanApothem / std::fmax(Spec.PlanApothem, POLY_EPSILON);
			Next.RoofHeight = Spec.RoofHeight * Ratio;
			Next.CornerSpan = Spec.CornerSpan * Ratio;

			Plans.push_back(Plan);
			Current = Next;
		}

		return Plans;
	}
} // namespace

void BuildingGen::BuildPolygonalStoreys(
	const BuildingSpec& Spec, ECentralProfile Profile, MeshAccumulator& OutMesh)
{
	const std::vector<PolyStorey> Plans = PlanPolygonalStoreys(Spec);
	for (size_t Index = 0; Index < Plans.size(); ++Index)
	{
		const PolyStorey& Plan = Plans[Index];
		uint32_t Parts = POLY_BODY;
		if (Index == 0)
		{
			if (Spec.BaseKind == 1)
			{
				BuildMasonryTerrace(Spec, OutMesh);
			}
			else
			{
				Parts |= POLY_PLATFORM | POLY_STEPS_FENCE;
			}
		}
		if (Plan.bTop)
		{
			Parts |= POLY_ROOF;
		}
		BuildPolygonalParts(Plan.Body, Profile, Parts, OutMesh);
		if (Plan.bTop)
		{
			continue;
		}

		BuildingSpec Waist = Plan.Body;
		Waist.RoofHeight = Plan.WaistRiseHeight;
		ScaleWaistCornerFlip(Waist, (1.0f - Plan.OpenFraction) * (Waist.PlanApothem + Waist.EaveOverhang),
			Waist.PlanApothem + Waist.EaveOverhang);
		BuildCentralisedShell(Waist, Profile, Plan.OpenFraction, OutMesh);
		if (Spec.bStoreyBalcony && Index + 1 < Plans.size())
		{
			BuildPolygonalBalcony(Plans[Index + 1].Body, Plan.NextFloor, Plan.BreakTop, OutMesh);
		}
	}
}

void BuildingGen::DescribePolygonalStoreys(const BuildingSpec& Spec, std::vector<StoreyFrame>& Out)
{
	Out.clear();
	for (const PolyStorey& Plan : PlanPolygonalStoreys(Spec))
	{
		StoreyFrame Frame;
		Frame.Floor = Plan.Body.PlatformHeight;
		Frame.ColumnFoot = Plan.Body.PlatformHeight - Plan.Body.ColumnFootDrop;
		Frame.ColumnTop = Plan.Body.PlatformHeight + Plan.Body.ColumnHeight;
		Frame.RoofBase = Plan.Body.RoofBase;
		Frame.Width = Plan.Body.PlanApothem * 2.0f;
		Frame.Depth = Frame.Width;
		Frame.BreakTop = Plan.bTop ? -1.0f : Plan.BreakTop;
		for (const Vector2& Corner : PlanPolygon(Plan.Body.PlanApothem, std::max(Spec.Sides, 3)))
		{
			Frame.ColumnLinesX.push_back(Corner.x);
			Frame.ColumnLinesZ.push_back(Corner.y);
		}
		Out.push_back(Frame);
	}
}
