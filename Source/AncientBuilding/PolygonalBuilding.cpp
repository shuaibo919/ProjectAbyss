#include "AncientBuilding/BuildingBuilder.h"

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

	std::vector<Vector2> MakeRidgeSection(float Scale)
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

	std::vector<Vector2> MakeTileSection(float Width, float Lift)
	{
		const float R = Width * 0.5f;
		std::vector<Vector2> Contour;
		const int32_t Steps = 5;
		for (int32_t Index = 0; Index <= Steps; ++Index)
		{
			const float Angle = POLY_PI * float(Index) / float(Steps);
			Contour.push_back(Vector2(-R * std::cos(Angle), Lift + R * 0.62f * std::sin(Angle)));
		}

		return Contour;
	}

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
	const int32_t Sides = EffectiveSides(Spec);
	const float EaveApothem = Spec.PlanApothem + Spec.EaveOverhang;
	const Vector3 Apex(0.0f, Spec.RoofBase + Spec.RoofHeight, 0.0f);

	const std::vector<Vector2> Shape = BuildCentralProfile(Spec, Profile);

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
			if (Normal.y < 0.0f)
			{
				Normal = -Normal;
			}

			OutMesh.AddQuadOriented(
				Low[Index], Low[Next], High[Next], High[Index], Normal, Spec.TileColor * 0.7f);
		}
	}

	// ---- Tile courses running up each facet, from the eave towards the apex ----
	{
		const std::vector<Vector2> Corners = PlanPolygon(EaveApothem, Sides);
		const float SideLength = Corners[0].distance_to(Corners[size_t(1 % Sides)]);
		const int32_t Courses = std::max(
			int32_t(SideLength / std::fmax(Spec.TileCourseWidth, 0.05f)), 1);
		const float Spacing = SideLength / float(Courses);
		const std::vector<Vector2> TileContour = MakeTileSection(Spacing * 0.9f, Spec.Module * 0.07f);

		const size_t KeepFrom = size_t(std::floor(
			float(Shape.size() - 1) * (1.0f - std::fmin(std::fmax(Spec.TileCoverage, 0.0f), 1.0f))));

		for (int32_t Side = 0; Side < Sides; ++Side)
		{
			const Vector2& From = Corners[size_t(Side)];
			const Vector2& To = Corners[size_t((Side + 1) % Sides)];

			for (int32_t Course = 0; Course < Courses; ++Course)
			{
				// Position across the facet, held constant as the course climbs. Because every
				// ring is the same polygon scaled about the centre, holding the *fraction* keeps
				// the course on the facet all the way to the apex — no clipping needed here.
				const float Fraction = (float(Course) + 0.5f) / float(Courses);

				std::vector<Vector3> Knots;
				for (size_t Index = KeepFrom; Index < Shape.size(); ++Index)
				{
					const Vector2& Step = Shape[Index];
					const Vector2 Plan = (From + (To - From) * Fraction) * Step.x;
					Knots.push_back(Flip.Apply(
						Vector3(Plan.x, Spec.RoofBase + Step.y * Spec.RoofHeight, Plan.y)));
				}
				if (Knots.size() < 2)
				{
					continue;
				}

				SweepSettings Settings;
				Settings.Contour = TileContour;
				Settings.bClosedContour = true;
				Settings.bGenerateCaps = false;
				Settings.UpReference = Vector3(0, 1, 0);

				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					OutMesh.AddSweep(Sweep, Spec.TileColor);
				}
			}
		}
	}

	// ---- 垂脊 from the apex down each corner, and the eave drip course ----
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
				if (Shape[Index].y > 0.9f)
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

			SweepSettings Settings;
			Settings.Contour = MakeRidgeSection(Spec.Module * 0.85f * Spec.RidgeScale);
			Settings.bClosedContour = true;
			Settings.UpReference = Vector3(0, 1, 0);

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				OutMesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}
	}

	{
		std::vector<Vector3> Knots = RingAt(1.0f, Spec.RoofBase);
		Knots.push_back(Knots.front());

		SweepSettings Settings;
		Settings.Contour = MakeEaveSection(Spec.Module * 1.1f * Spec.RidgeScale);
		Settings.bClosedContour = true;
		Settings.bGenerateCaps = false;

		SweepResult Sweep;
		if (BuildSweep(Knots, Settings, Sweep))
		{
			OutMesh.AddSweep(Sweep, Spec.RidgeColor);
		}
	}

	BuildFinial(Spec, Apex, OutMesh);
}

void BuildingGen::BuildPolygonalBuilding(
	const BuildingSpec& Spec, ECentralProfile Profile, MeshAccumulator& OutMesh)
{
	const int32_t Sides = std::max(Spec.Sides, 3);
	const float BodyApothem = Spec.PlanApothem;
	const float PlatformApothem = BodyApothem + (Spec.PlatformHalfWidth - Spec.Width * 0.5f);

	const std::vector<Vector2> PlatformPlan = PlanPolygon(PlatformApothem, Sides);
	const std::vector<Vector2> BodyPlan = PlanPolygon(BodyApothem, Sides);

	// ---- Platform: a prism with a slightly wider 阶条石 cap ----
	if (Spec.PlatformHeight > 0.0f)
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

		AddPrism(Inner, 0.0f, BodyHeight, Spec.StoneColor);
		AddPrism(PlatformPlan, BodyHeight, Spec.PlatformHeight, Spec.StoneColor * 1.06f);
	}

	// ---- Body: a column on every vertex, walls between them ----
	const float Base = Spec.PlatformHeight;

	if (Spec.bGenerateColumns)
	{
		for (const Vector2& Point : BodyPlan)
		{
			OutMesh.AddColumn(
				Vector3(Point.x, Base, Point.y), Spec.ColumnHeight,
				Spec.ColumnRadius, Spec.ColumnRadius * 0.88f, Spec.ColumnSides, Spec.TimberColor);
		}
	}

	if (Spec.bGenerateWalls)
	{
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
	if (Spec.BracketHeight > 0.0f)
	{
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
	if (Spec.bGenerateSteps && Spec.PlatformHeight > 0.0f && Spec.StepRunDepth > 0.0f)
	{
		const int32_t Steps = std::max(Spec.StepCount, 1);
		const float RunWidth = Spec.FenceGapWidth;
		const float TreadDepth = Spec.StepRunDepth / float(Steps);

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

	if (Spec.bGenerateFence)
	{
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

	BuildCentralisedRoof(Spec, Profile, OutMesh);
}
