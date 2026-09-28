#include "AncientBuilding/TileSkin.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>

using namespace BuildingGen;

namespace
{
	/**
	 * The section table. 筒瓦 is a half-ellipse of half-width BARREL_HALF and rise BARREL_RISE
	 * centred on the course line; 板瓦 falls from its edge to PAN_SAG below the batten line at the
	 * pitch boundary, which is where it meets the neighbouring course's pan.
	 *
	 * Barrel normals are the analytic ellipse normals, (cos/a, sin/b) normalised, so the barrel
	 * shades as a smooth cylinder rather than as the three facets it is made of.
	 */
	// A barrel narrower than its trough is what separates a tiled roof from corrugated sheet: the
	// eye reads a round rib against a broad flat channel, not a wave. Roughly 1:2 here.
	const float BARREL_HALF = 0.155f;
	const float BARREL_RISE = 0.160f;

	/** Depth of the 板瓦 channel below the barrel's base. */
	const float PAN_SAG = 0.17f;

	/** Horizontal run of one pan, from the barrel's edge out to the channel bottom. */
	const float PAN_RUN = 0.5f - BARREL_HALF;

	/** Clearance between the channel bottom and the boarding, in pitches. */
	const float PAN_CLEARANCE = 0.05f;

	const float SKIN_PI = 3.14159265358979323846f;
	const float ROOT_HALF = 0.70710678118654752f;

	struct SectionSample
	{
		float Offset;
		float Height;
		Vector2 Normal;
	};

	/** Ellipse normal at parameter Angle, as a unit vector in (across, outward). */
	Vector2 BarrelNormal(float Angle)
	{
		const Vector2 Raw(std::cos(Angle) / BARREL_HALF, std::sin(Angle) / BARREL_RISE);

		return Raw.normalized();
	}

	/**
	 * The pan is a parabola in u, where u is 0 at the channel bottom and 1 at the barrel's edge.
	 * Only the two endpoints are sampled, so the pan's *geometry* is a straight ramp — but the
	 * normals are the parabola's, so it shades as the dished 板瓦 it represents. Paying two more
	 * columns per course to make the geometry match would cost 25% of the skin and only show on
	 * the eave silhouette, which the 滴水 course covers anyway.
	 */
	float PanHeight(float U)
	{
		return -PAN_SAG * (1.0f - U * U);
	}

	/** AcrossSign is -1 for the pan left of a barrel and +1 for the one right of it. */
	Vector2 PanNormal(float U, float AcrossSign)
	{
		const float Slope = 2.0f * PAN_SAG * U / PAN_RUN;

		return Vector2(AcrossSign * Slope, 1.0f).normalized();
	}

	const SectionSample& SampleAt(int32_t Sample)
	{
		static const SectionSample TABLE[TileSection::SAMPLE_COUNT] = {
			// Channel bottom. Shared with the previous course, hence the straight-out normal.
			{ -0.5f, PanHeight(0.0f), PanNormal(0.0f, -1.0f) },
			// Crease A: same point twice, pan normal then barrel normal. The quad between them is
			// degenerate and gets skipped, so the hard edge costs no triangles.
			{ -BARREL_HALF, 0.0f, PanNormal(1.0f, -1.0f) },
			{ -BARREL_HALF, 0.0f, BarrelNormal(SKIN_PI) },
			{ -BARREL_HALF * ROOT_HALF, BARREL_RISE * ROOT_HALF, BarrelNormal(SKIN_PI * 0.75f) },
			{ 0.0f, BARREL_RISE, BarrelNormal(SKIN_PI * 0.5f) },
			{ BARREL_HALF * ROOT_HALF, BARREL_RISE * ROOT_HALF, BarrelNormal(SKIN_PI * 0.25f) },
			// Crease B, mirrored.
			{ BARREL_HALF, 0.0f, BarrelNormal(0.0f) },
			{ BARREL_HALF, 0.0f, PanNormal(1.0f, 1.0f) },
		};

		return TABLE[Sample < 0 ? 0 : (Sample >= TileSection::SAMPLE_COUNT ? TileSection::SAMPLE_COUNT - 1 : Sample)];
	}

	/** Point j of a column, with j clamped so a short neighbour still yields a usable direction. */
	const Vector3& ClampedPoint(const TileSkinColumn& Column, size_t Index)
	{
		return Column.Points[Index < Column.Points.size() ? Index : Column.Points.size() - 1];
	}

	/**
	 * Per-course tint variation.
	 *
	 * A whole roof at one exact colour is the last thing that reads as extruded plastic rather than
	 * fired clay: real 青瓦 vary course to course because each course came out of a different part
	 * of the kiln. Cheap here — colour is already per-vertex, so this costs nothing but a hash.
	 */
	Color CourseTint(const Color& Base, int32_t Course)
	{
		uint32_t Hash = uint32_t(Course) * 2654435761u;
		Hash ^= Hash >> 15;
		Hash *= 2246822519u;
		Hash ^= Hash >> 13;

		// +/- 6% on luminance, and a touch less on the blue so the variation reads as firing
		// rather than as a lighting error.
		const float Unit = float(Hash & 0xFFFFu) / 65535.0f * 2.0f - 1.0f;
		const float Scale = 1.0f + Unit * 0.06f;

		return Color(Base.r * Scale, Base.g * Scale, Base.b * (1.0f + Unit * 0.04f), Base.a);
	}

	// ==================== 瓦作 constants ====================

	/**
	 * 瓦厚 : 宽 = 1 : 20 (30_瓦作 R3, 01:118-122). The width of a unit is its effective coverage
	 * module, so this is the one size here that is not self-defined.
	 */
	const float TILE_THICKNESS_RATIO = 1.0f / 20.0f;

	/**
	 * 单件瓦沿坡的实体长 ÷ 有效覆盖模数 p. **[自定]** — the handbooks give the longitudinal period
	 * q as `tile_length - overlap` but never the length itself (30_瓦作 §6 缺口 4/5). 1.6 keeps the
	 * 筒瓦 a little longer than it is wide, which is what the section's proportions imply.
	 */
	const float TILE_LENGTH_RATIO = 1.6f;

	/**
	 * 叠压比, i.e. `overlap / tile_length`. 压七露三 (30_瓦作 R21) names the convention; the value
	 * is 待定标 (06:1166-1174 gives no number), so 0.7 is **[自定]**. It makes the exposure
	 * q = 0.3 * 1.6 p = 0.48 p, which is the rhythm the reference photograph shows.
	 */
	const float TILE_LAP_RATIO = 0.7f;

	/**
	 * Length of the taper behind each lap joint, as a fraction of q. **[自定]** — the step the upper
	 * tile's tail makes over the piece below is real, but the tiles are tapered so it recovers to
	 * the weathering plane over a short run; 0.35 keeps the joint crisp without leaving a
	 * backward-facing ledge that the following tile would have to climb.
	 */
	const float TILE_TAIL_RATIO = 0.35f;

	/**
	 * Admissible window for the effective coverage module, relative to the nominal pitch
	 * (R8 甲 2). `p_min >= MinTileWidth` is folded in: the unit's physical width is p itself, so
	 * 0.75 already sits well above any碎条 threshold. **[自定]** — the handbooks leave
	 * `p_min`/`p_max` 待定标 and demand only that they be declared.
	 */
	const float TILE_PITCH_MIN_RATIO = 0.75f;
	const float TILE_PITCH_MAX_RATIO = 1.35f;

	/** Fractional index step used to probe the patch's up-slope tangent, in samples. */
	const float TILE_V_PROBE = 0.5f;

	// ---- 檐口件 (R4/R5/cards T1, T2), all self-defined below where the book is silent ----

	/** 瓦当 disc radius ÷ pitch. Carried over from the legacy piece so the eave line reads the same. */
	const float WADANG_RADIUS = 0.185f;
	/** How far the 瓦当 sweeps out over the eave, and back into the barrel, in pitches. */
	const float WADANG_OUT = 0.16f;
	const float WADANG_IN = 0.10f;
	/** 边轮 (raised rim) and 中心凸起 (central boss), as fractions of the disc radius. [自定] */
	const float WADANG_RIM_INNER = 0.78f;
	const float WADANG_BOSS = 0.34f;
	/** Depth of the pressed face, as a fraction of the disc radius. [自定] — card T1 shows a
	 *  coarse impression, not a measured relief. */
	const float WADANG_RELIEF = 0.14f;

	/** 滴水 body half-width and skirt half-width, in pitches. The skirt is the wider of the two:
	 *  that is card T2's one committed fact ("裙边比瓦身宽"). [自定] for the amounts. */
	const float DRIP_BODY_HALF = 0.34f;
	const float DRIP_SKIRT_HALF = 0.42f;

	/** 滴水's two folds (R4's 角 20°, twice, giving R5's dishui bend 40°), in degrees, and the
	 *  down-slope runs (in pitches) at which they happen. Fold positions are [自定]. */
	const float DRIP_FOLD_DEGREES = 20.0f;
	const float DRIP_FOLD_ONE = 0.10f;
	const float DRIP_FOLD_TWO = 0.20f;

	/** Lowest point of the 如意 skirt, down-slope from the eave, in pitches. [自定] */
	const float DRIP_TIP = 0.42f;

	/** Point of a column at a fractional index, so the metric can be probed between samples. */
	Vector3 PatchPoint(const std::vector<Vector3>& Column, float V)
	{
		const int32_t Last = int32_t(Column.size()) - 1;
		const float Clamped = std::fmin(std::fmax(V, 0.0f), float(Last));
		const int32_t Low = int32_t(std::floor(Clamped));
		const int32_t High = Low < Last ? Low + 1 : Last;
		const float T = Clamped - float(Low);

		return Column[Low] + (Column[High] - Column[Low]) * T;
	}

	/**
	 * Ear-clipping triangulation of a simple polygon, output as index triples wound counter-
	 * clockwise in the outline's own 2D frame.
	 *
	 * The 如意 skirt is concave (the notch between the central point and the side lobes), so the
	 * fan that MeshAccumulator::AddPolygon needs — star-shaped about Points[0] — would drop half of
	 * it. Thirteen points is small enough that the obvious O(n^2) clipper is the right tool.
	 */
	void TriangulateOutline(const std::vector<Vector2>& Outline, std::vector<int32_t>& OutIndices)
	{
		const int32_t Count = int32_t(Outline.size());
		if (Count < 3)
		{
			return;
		}

		// Signed area decides the winding; the clipper wants counter-clockwise.
		float Area = 0.0f;
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			const Vector2& A = Outline[size_t(Index)];
			const Vector2& B = Outline[size_t((Index + 1) % Count)];
			Area += A.x * B.y - B.x * A.y;
		}
		const bool bClockwise = Area < 0.0f;

		std::vector<int32_t> Remaining;
		Remaining.reserve(size_t(Count));
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			Remaining.push_back(bClockwise ? Count - 1 - Index : Index);
		}

		int32_t Guard = Count * Count + 8;
		while (Remaining.size() > 3 && Guard-- > 0)
		{
			bool bClipped = false;
			for (size_t Index = 0; Index < Remaining.size(); ++Index)
			{
				const int32_t Previous = Remaining[(Index + Remaining.size() - 1) % Remaining.size()];
				const int32_t Current = Remaining[Index];
				const int32_t Next = Remaining[(Index + 1) % Remaining.size()];

				const Vector2& A = Outline[size_t(Previous)];
				const Vector2& B = Outline[size_t(Current)];
				const Vector2& C = Outline[size_t(Next)];

				const float Cross = (B - A).cross(C - B);
				if (Cross <= 1e-9f)
				{
					continue;
				}

				bool bClear = true;
				for (const int32_t Other : Remaining)
				{
					if (Other == Previous || Other == Current || Other == Next)
					{
						continue;
					}
					const Vector2& P = Outline[size_t(Other)];
					// Strictly inside counts; a vertex on the edge only blocks the ear if it is
					// strictly between the endpoints, which is what the cross signs test below.
					const float S1 = (B - A).cross(P - A);
					const float S2 = (C - B).cross(P - B);
					const float S3 = (A - C).cross(P - C);
					if (S1 > 1e-9f && S2 > 1e-9f && S3 > 1e-9f)
					{
						bClear = false;
						break;
					}
				}

				if (!bClear)
				{
					continue;
				}

				OutIndices.push_back(Previous);
				OutIndices.push_back(Current);
				OutIndices.push_back(Next);
				Remaining.erase(Remaining.begin() + int32_t(Index));
				bClipped = true;
				break;
			}

			if (!bClipped)
			{
				break;
			}
		}

		if (Remaining.size() == 3)
		{
			OutIndices.push_back(Remaining[0]);
			OutIndices.push_back(Remaining[1]);
			OutIndices.push_back(Remaining[2]);
		}
	}

	/** Triangle wound so its front face — the negated cross product — points along Desired. */
	void AddTriangleOriented(MeshAccumulator& Mesh, const Vector3& A, const Vector3& B, const Vector3& C,
		const Vector3& Desired, const Color& Tint)
	{
		// AddTriangle faces the way -(B-A)x(C-A) points, so keep the geometric normal opposite.
		if (-(B - A).cross(C - A).dot(Desired) >= 0.0f)
		{
			Mesh.AddTriangle(A, B, C, Tint);
		}
		else
		{
			Mesh.AddTriangle(A, C, B, Tint);
		}
	}

	/** One ring of quads in a face's own plane, between two radii, normal along FaceOut. */
	void AddDiscBand(MeshAccumulator& Mesh, const Vector3& Centre, const Vector3& FaceOut,
		const Vector3& U, const Vector3& V, float InnerRadius, float OuterRadius, int32_t Sides,
		const Color& Tint)
	{
		for (int32_t Index = 0; Index < Sides; ++Index)
		{
			const float A0 = SKIN_PI * 2.0f * float(Index) / float(Sides);
			const float A1 = SKIN_PI * 2.0f * float(Index + 1) / float(Sides);
			const Vector3 Inner0 = Centre + (U * std::cos(A0) + V * std::sin(A0)) * InnerRadius;
			const Vector3 Inner1 = Centre + (U * std::cos(A1) + V * std::sin(A1)) * InnerRadius;
			const Vector3 Outer0 = Centre + (U * std::cos(A0) + V * std::sin(A0)) * OuterRadius;
			const Vector3 Outer1 = Centre + (U * std::cos(A1) + V * std::sin(A1)) * OuterRadius;

			Mesh.AddQuadOriented(Inner0, Outer0, Outer1, Inner1, FaceOut, Tint);
		}
	}

	/** A cylinder wall between two axial offsets, facing away from the axis. */
	void AddWallBand(MeshAccumulator& Mesh, const Vector3& Centre, const Vector3& Axis,
		const Vector3& U, const Vector3& V, float Radius, float NearOffset, float FarOffset,
		int32_t Sides, const Color& Tint)
	{
		for (int32_t Index = 0; Index < Sides; ++Index)
		{
			const float A0 = SKIN_PI * 2.0f * float(Index) / float(Sides);
			const float A1 = SKIN_PI * 2.0f * float(Index + 1) / float(Sides);
			const Vector3 Radial0 = (U * std::cos(A0) + V * std::sin(A0)) * Radius;
			const Vector3 Radial1 = (U * std::cos(A1) + V * std::sin(A1)) * Radius;
			const Vector3 Normal = (Radial0 + Radial1).normalized();

			Mesh.AddQuadOriented(
				Centre + Radial0 + Axis * NearOffset,
				Centre + Radial1 + Axis * NearOffset,
				Centre + Radial1 + Axis * FarOffset,
				Centre + Radial0 + Axis * FarOffset,
				Normal, Tint);
		}
	}

	/**
	 * Segment count for a circular feature, from the chord-error bound r(1 - cos(pi/n)) <= eps,
	 * i.e. n >= ceil(pi / acos(1 - eps/r)) with n >= 3 (review §12.3, 50_脊饰 R7.d).
	 *
	 * The legacy 瓦当 used a fixed 7 sides, which reads as a polygon at close range; the count here
	 * follows from the radius and the design camera instead, and the登记 of (r, eps, z, H_px) lives
	 * in TileSkinSettings.
	 */
	int32_t ChordErrorSides(float Radius, float Epsilon)
	{
		if (!(Radius > 0.0f) || !(Epsilon > 0.0f) || Epsilon >= Radius)
		{
			return 3;
		}

		const float Sides = SKIN_PI / std::acos(1.0f - Epsilon / Radius);

		return std::max(int32_t(std::ceil(Sides)), 3);
	}

	/** The world-space chord error that half a pixel at the design camera corresponds to. */
	float ChordErrorFromPixels(const TileSkinSettings& Settings)
	{
		const float Fov = std::fmin(std::fmax(Settings.DesignVerticalFov, 1.0f), 179.0f);
		const float FocalPixels = std::fmax(Settings.DesignViewportHeight, 1.0f)
			/ (2.0f * std::tan(Fov * 0.5f * SKIN_PI / 180.0f));

		return Settings.WadangChordPixels * std::fmax(Settings.DesignViewDistance, 0.1f) / FocalPixels;
	}
} // namespace

float TileSection::OffsetAt(int32_t Sample)
{
	return SampleAt(Sample).Offset;
}

float TileSection::HeightAt(int32_t Sample)
{
	return SampleAt(Sample).Height;
}

Vector2 TileSection::NormalAt(int32_t Sample)
{
	return SampleAt(Sample).Normal;
}

bool TileSection::IsCrown(int32_t Sample)
{
	return Sample == 4;
}

bool TileSection::IsChannel(int32_t Sample)
{
	return Sample == 0;
}

float TileSection::MinimumLift()
{
	return PAN_SAG + PAN_CLEARANCE;
}

/**
 * The emitted sample table per distance tier .
 *
 * Tier 1 drops the two creases (1 and 7 — they are coincident with 2 and 6, so they cost no
 * triangles and only exist to carry the pan's normal) and the barrel's quarter points, leaving one
 * pan facet and one barrel facet. Tier 2 drops the barrel's edge samples as well, so the section is
 * a plain corrugation: channel bottom, crown, pan edge.
 *
 * Every tier shares the first sample (the channel bottom) and the last one (the pan edge at
 * +BARREL_HALF). The *last* sample is what sets how far up the band the courses reach, so trimming
 * from the middle keeps the covered band, the course count and the eave ends identical across tiers
 * — which is what review §8 requires of a LOD: it may change tessellation, not placement.
 */
int32_t TileSection::SampleCount(int32_t LodLevel)
{
	if (LodLevel <= 0)
	{
		return SAMPLE_COUNT;
	}

	return LodLevel == 1 ? 4 : 3;
}

int32_t TileSection::SampleIndex(int32_t LodLevel, int32_t Slot)
{
	if (LodLevel <= 0)
	{
		return Slot < 0 ? 0 : (Slot >= SAMPLE_COUNT ? SAMPLE_COUNT - 1 : Slot);
	}

	static const int32_t MID[TileSection::SAMPLE_COUNT / 2] = { 0, 2, 4, 6 };
	static const int32_t FAR[3] = { 0, 4, 6 };

	const int32_t Count = SampleCount(LodLevel);
	const int32_t Clamped = Slot < 0 ? 0 : (Slot >= Count ? Count - 1 : Slot);

	return LodLevel == 1 ? MID[Clamped] : FAR[Clamped];
}

float BuildingGen::RidgeFootHalfWidth(float ContourScale)
{
	// MakeRidgeContour runs from -0.5 to +0.5 across the sweep whatever the scale.
	return std::fmax(ContourScale, 0.0f) * 0.5f;
}

float BuildingGen::TileEdgeReach(float Pitch, const TileSkinSettings& Settings)
{
	if (!(Pitch > 0.0f))
	{
		return 0.0f;
	}

	// The widest thing a course hangs over the eave line: the 如意 apron once the tier builds one,
	// the plain tongue before that. The 瓦当 disc (0.185 p) is always the narrower of the two.
	const float Half = Settings.Detail >= TILE_DETAIL_EAVE ? DRIP_SKIRT_HALF : DRIP_BODY_HALF;

	return Pitch * Half;
}

TileCourseLayout BuildingGen::TileBandFor(
	const BuildingSpec& Spec,
	float DomainLow,
	float DomainHigh,
	float Pitch,
	int32_t Courses,
	float NearBoundaryScale,
	float FarBoundaryScale)
{
	TileCourseLayout Layout;
	Layout.Origin = DomainLow;
	Layout.NominalPitch = Pitch;
	Layout.LodLevel = Spec.LODLevel;

	if (Spec.TileDetail < TILE_DETAIL_COURSES)
	{
		// The band every caller used before the envelope existed: one covered length, no cover at
		// either end, so the two paths differ in method rather than in where tiles may be.
		Layout.Covered = Pitch * (float(Courses) - 0.28f);
		return Layout;
	}

	const TileSkinSettings Settings = TileSkinSettingsFor(Spec);
	Layout.Covered = DomainHigh - DomainLow;
	Layout.NearCover = RidgeFootHalfWidth(NearBoundaryScale);
	Layout.FarCover = RidgeFootHalfWidth(FarBoundaryScale);
	Layout.NearReach = TileEdgeReach(Pitch, Settings);
	Layout.FarReach = Layout.NearReach;

	// The Lap the ridge holds the outermost course by: the piece has to end up under the ridge's
	// footprint, or the two are merely adjacent and the joint is an open groove (R14.5/T1).
	if (Layout.NearReach > std::fmin(Layout.NearCover, Layout.FarCover) + 1e-4f
		|| !(DomainHigh - DomainLow > Layout.NearReach + Layout.FarReach))
	{
		WARN_PRINT_ONCE(godot::String::utf8("AncientBuilding 瓦作: a boundary member covers ")
			+ godot::String::num(std::fmin(Layout.NearCover, Layout.FarCover), 3)
			+ " m of a roof face whose outermost tile piece reaches "
			+ godot::String::num(Layout.NearReach, 3)
			+ " m in from the verge, so the band cannot be cut inside the cover; that face falls "
			"back to the legacy band.");
		Layout.NearCover = 0.0f;
		Layout.FarCover = 0.0f;
		Layout.NearReach = 0.0f;
		Layout.FarReach = 0.0f;
		Layout.Covered = Pitch * (float(Courses) - 0.28f);
	}

	return Layout;
}

float BuildingGen::RoofBeddingLift(const BuildingSpec& Spec)
{
	if (Spec.TileDetail < TILE_DETAIL_EAVE)
	{
		return 0.0f;
	}

	return std::fmax(Spec.TileBeddingThickness, 0.0f);
}

float BuildingGen::TileSurfaceLift(float Pitch, const TileSkinSettings& Settings)
{
	if (!(Pitch > 0.0f))
	{
		return 0.0f;
	}

	const float Batten = Pitch * TileSection::MinimumLift();
	// 泥背 only exists from the tier that builds it (BuildTileSkin gates the same way), so a
	// caller on the legacy path gets a lift of 0 and its knots do not move.
	const float Bedding = Settings.Detail >= TILE_DETAIL_EAVE
		? std::fmax(Settings.BeddingThickness, 0.0f)
		: 0.0f;

	return Batten + Bedding;
}

TileSkinSettings BuildingGen::TileSkinSettingsFor(const BuildingSpec& Spec)
{
	TileSkinSettings Out;
	Out.Detail = Spec.TileDetail;
	Out.BeddingThickness = Spec.TileBeddingThickness;
	Out.LodLevel = Spec.LODLevel;

	return Out;
}

TileMetric BuildingGen::MeasureTileMetric(const Vector3& Across, const Vector3& UpSlope)
{
	TileMetric Out;
	Out.E = Across.dot(Across);
	Out.F = Across.dot(UpSlope);
	Out.G = UpSlope.dot(UpSlope);

	// Both guards are the handbook's: G > 0 (a nonzero along-slope tangent) and EG - F^2 > 0 (the
	// patch is regular there). Without them the layout would be dividing by a vanishing scale,
	// which is what R14.2 forbids.
	if (!(Out.G > 0.0f))
	{
		return Out;
	}

	const float Determinant = Out.E * Out.G - Out.F * Out.F;
	if (!(Determinant > 0.0f))
	{
		return Out;
	}

	Out.SPerp = std::sqrt(Determinant / Out.G);
	Out.bValid = std::isfinite(Out.SPerp) && Out.SPerp > 0.0f;

	return Out;
}

// ==================== Course layout (R1(d), R7, R8 甲) ====================

namespace
{
	using PlaceFn = std::function<std::vector<Vector3>(float)>;

	/** First fundamental form of the patch at (U, V), V a fractional index into the column. */
	TileMetric PatchMetric(const PlaceFn& Place, float U, float V, float Du)
	{
		const std::vector<Vector3> Low = Place(U - Du);
		const std::vector<Vector3> Mid = Place(U);
		const std::vector<Vector3> High = Place(U + Du);

		const size_t Shared = std::min(Low.size(), std::min(Mid.size(), High.size()));
		if (Shared < 2)
		{
			return TileMetric();
		}

		const Vector3 Across = (PatchPoint(High, V) - PatchPoint(Low, V)) / (2.0f * Du);
		const Vector3 Slope = PatchPoint(Mid, V + TILE_V_PROBE) - PatchPoint(Mid, V - TILE_V_PROBE);

		return MeasureTileMetric(Across, Slope);
	}

	/**
	 * The cross-section of the course band: the curve across the patch that is *perpendicular* to
	 * the courses in the surface's own metric, plus the arc length along it (R1(d), review §4.1).
	 *
	 * Courses are v-curves, so a station is a value of u, and the spacing between stations is
	 * measured on this curve. Its own parameter ODE is
	 *
	 *     du/ds =    1 / s_perp
	 *     dv/ds = -(F/G) / s_perp
	 *
	 * — the v drift is what keeps the step perpendicular to the courses once the patch is no longer
	 * a rectangle. On every flat face and straight slope F = 0 and s_perp = 1, so this reduces to
	 * the plain parameter layout: that coincidence is exactly why the legacy "UV equal spacing
	 * times the tangent lengths" went unnoticed (R1(d)).
	 *
	 * Steps are solved by bisection: u(s) is monotone wherever s_perp > 0, so bisecting the step
	 * length against the target converges without ever dividing by a vanishing scale.
	 */
	class CrossSection
	{
	public:
		CrossSection(const PlaceFn& Place, float Du, float StepArc)
			: Place(Place), Du(Du), Step(StepArc) {}

		void Reset(float UStart, float VStart)
		{
			U = UStart;
			V = VStart;
			S = 0.0f;
			Steps = 0;
			bValid = true;
		}

		bool bOk() const { return bValid; }
		float Arc() const { return S; }
		float UAt() const { return U; }

		/** Marches to a u, bisecting the closing step so it lands exactly on it. */
		bool AdvanceToU(float UEnd)
		{
			return March(false, UEnd);
		}

		/** Marches until the band's arc length reaches Target, bisecting the closing step. */
		bool AdvanceToArc(float Target)
		{
			return March(true, Target);
		}

	private:
		/**
		 * One step of the cross-section ODE, RK4 in u. dU is in the patch's own u units; the arc
		 * advance and the v drift come back with it.
		 */
		bool StepOf(float dU, float& OutU, float& OutV, float& OutArc) const
		{
			const float Span = std::fabs(dU);
			const int32_t Sub = std::min(
				std::max(int32_t(std::ceil(Span / std::fmax(Step, 1e-6f))), 1), 32);
			const float H = dU / float(Sub);

			float u = U;
			float v = V;
			float s = 0.0f;
			for (int32_t Index = 0; Index < Sub; ++Index)
			{
				const TileMetric M1 = PatchMetric(Place, u, v, Du);
				if (!M1.bValid)
				{
					return false;
				}
				const float D1 = -M1.F / M1.G;
				const float G1 = M1.SPerp;

				const TileMetric M2 = PatchMetric(Place, u + H * 0.5f, v + H * 0.5f * D1, Du);
				if (!M2.bValid)
				{
					return false;
				}
				const float D2 = -M2.F / M2.G;
				const float G2 = M2.SPerp;

				const TileMetric M3 = PatchMetric(Place, u + H * 0.5f, v + H * 0.5f * D2, Du);
				if (!M3.bValid)
				{
					return false;
				}
				const float D3 = -M3.F / M3.G;
				const float G3 = M3.SPerp;

				const TileMetric M4 = PatchMetric(Place, u + H, v + H * D3, Du);
				if (!M4.bValid)
				{
					return false;
				}
				const float D4 = -M4.F / M4.G;
				const float G4 = M4.SPerp;

				v += H / 6.0f * (D1 + 2.0f * D2 + 2.0f * D3 + D4);
				s += H / 6.0f * (G1 + 2.0f * G2 + 2.0f * G3 + G4);
				u += H;
			}

			OutU = u;
			OutV = v;
			OutArc = s;

			return true;
		}

		/** Walks forward until the target u (bArcTarget false) or arc length is reached. */
		bool March(bool bArcTarget, float Target)
		{
			if (!bValid)
			{
				return false;
			}
			if (bArcTarget ? (Target <= S) : (Target <= U))
			{
				return true;
			}

			for (int32_t Guard = 0; Guard < 4096; ++Guard)
			{
				const TileMetric Here = PatchMetric(Place, U, V, Du);
				if (!Here.bValid)
				{
					bValid = false;
					return false;
				}

				// A full step covers one nominal module; s_perp converts it to patch units.
				const float Full = Step / Here.SPerp;
				const bool bClosing = bArcTarget ? (S + Step >= Target) : (U + Full >= Target);

				float dU = Full;
				if (bClosing)
				{
					float Lo = 0.0f;
					float Hi = Full;
					for (int32_t Iteration = 0; Iteration < 40; ++Iteration)
					{
						const float Mid = (Lo + Hi) * 0.5f;
						float u = 0.0f;
						float v = 0.0f;
						float arc = 0.0f;
						if (!StepOf(Mid, u, v, arc))
						{
							bValid = false;
							return false;
						}
						const float Value = bArcTarget ? (S + arc) : u;
						if (Value < Target)
						{
							Lo = Mid;
						}
						else
						{
							Hi = Mid;
						}
					}
					dU = (Lo + Hi) * 0.5f;
				}

				float nU = 0.0f;
				float nV = 0.0f;
				float nArc = 0.0f;
				if (!StepOf(dU, nU, nV, nArc))
				{
					bValid = false;
					return false;
				}

				U = nU;
				V = nV;
				S += nArc;
				++Steps;

				if (bClosing)
				{
					return true;
				}
			}

			bValid = false;

			return false;
		}

		const PlaceFn& Place;
		float Du = 0.0f;
		float Step = 0.0f;
		float U = 0.0f;
		float V = 0.0f;
		float S = 0.0f;
		int32_t Steps = 0;
		bool bValid = true;
	};

	/**
	 * R8 甲 3: enumerate the feasible unit counts, filter, pick the one nearest the nominal, and
	 * hand back p = L / N.
	 *
	 * N counts whole section units, so the band is exactly N * p wide with no half unit at either
	 * end — the "差一列" trap in R8 甲 5 is about a count of *centres*, which this is not.
	 *
	 * The odd filter is the handbook's 正脊居中 rule: with an odd count the band's centre falls on a
	 * section boundary, i.e. a channel bottom, so a ridge or hip cover band centred on the face
	 * lands on a joint instead of half a 筒瓦. It is a preference, not a hard constraint, so a
	 * feasible count always comes back.
	 */
	int32_t SolveCourseCount(float Length, const TileCourseLayout& Layout, float& OutPitch)
	{
		const float MinPitch = std::fmax(Layout.MinPitch, 1e-6f);
		const float MaxPitch = std::fmax(Layout.MaxPitch, MinPitch);
		const float Ideal = Length / std::fmax(Layout.NominalPitch, 1e-6f);

		const int32_t Low = std::max(int32_t(std::ceil(Length / MaxPitch)), 1);
		const int32_t High = std::max(int32_t(std::floor(Length / MinPitch)), Low);

		int32_t Best = 0;
		float BestScore = 0.0f;
		for (int32_t Pass = 0; Pass < 2 && Best == 0; ++Pass)
		{
			for (int32_t Count = Low; Count <= High; ++Count)
			{
				if (Pass == 0 && (Count % 2) == 0)
				{
					continue;
				}

				const float Score = std::fabs(float(Count) - Ideal);
				if (Best == 0 || Score < BestScore)
				{
					Best = Count;
					BestScore = Score;
				}
			}
		}

		if (Best == 0)
		{
			Best = Low;
		}

		OutPitch = Length / float(Best);

		return Best;
	}
} // namespace

void BuildingGen::LayTileCoursesLegacy(
	float Origin,
	float Pitch,
	int32_t Courses,
	const std::function<std::vector<Vector3>(float)>& PlaceColumn,
	std::vector<TileSkinColumn>& OutColumns,
	int32_t LodLevel)
{
	const int32_t Slots = TileSection::SampleCount(LodLevel);

	OutColumns.reserve(OutColumns.size() + size_t(Courses * Slots));

	for (int32_t Course = 0; Course < Courses; ++Course)
	{
		const float Centre = Origin + Pitch * (float(Course) + 0.5f);

		for (int32_t Slot = 0; Slot < Slots; ++Slot)
		{
			const int32_t Sample = TileSection::SampleIndex(LodLevel, Slot);

			TileSkinColumn Column;
			Column.Sample = Sample;
			Column.Course = Course;
			Column.Pitch = Pitch;
			Column.Lift = Pitch * TileSection::MinimumLift();
			Column.Points = PlaceColumn(Centre + Pitch * TileSection::OffsetAt(Sample));

			OutColumns.push_back(Column);
		}
	}
}

void BuildingGen::LayTileCourses(
	int32_t Detail,
	const TileCourseLayout& Layout,
	int32_t Courses,
	const std::function<std::vector<Vector3>(float)>& PlaceColumn,
	std::vector<TileSkinColumn>& OutColumns)
{
	const float Pitch = Layout.NominalPitch;

	if (Detail <= TILE_DETAIL_LEGACY || !(Pitch > 0.0f) || Courses < 1)
	{
		LayTileCoursesLegacy(
			Layout.Origin, Pitch, Courses, PlaceColumn, OutColumns, Layout.LodLevel);
		return;
	}

	/**
	 * 包络 (R14.1 / review §8): the band is cut inside the boundary members' cover before anything
	 * is laid. The cut position is set by the outermost *piece* the course hangs out — its 滴水
	 * skirt reaches sideways past its own column line — so the last piece ends exactly on the verge
	 * and nothing crosses the roof's outline (判据 C).
	 *
	 * That piece is a fraction of the *effective* pitch, which the solve only produces at the end:
	 * the band is W - (rn + rf)·p wide over N courses, so p = W / (N + rn + rf). The provisional
	 * band below only exists to give the integer solve the length it is choosing a count for.
	 */
	const float DomainLow = Layout.Origin;
	const float DomainHigh = Layout.Origin + Layout.Covered;
	const bool bEnvelope = HasCoverEnvelope(Layout);
	const float Width = DomainHigh - DomainLow;
	const float NearRatio = (Pitch > 0.0f) ? std::fmax(Layout.NearReach, 0.0f) / Pitch : 0.0f;
	const float FarRatio = (Pitch > 0.0f) ? std::fmax(Layout.FarReach, 0.0f) / Pitch : 0.0f;
	const float RatioSum = bEnvelope ? (NearRatio + FarRatio) : 0.0f;
	const float ProvisionalLow = bEnvelope ? DomainLow + NearRatio * Pitch : DomainLow;
	const float ProvisionalHigh = bEnvelope ? DomainHigh - FarRatio * Pitch : DomainHigh;

	if (!(ProvisionalHigh > ProvisionalLow) || !(Width > 0.0f))
	{
		WARN_PRINT_ONCE(godot::String::utf8("AncientBuilding 瓦作: the ridge cover envelope leaves no room for a course "
			"band on a roof face; that face falls back to the legacy layout."));
		LayTileCoursesLegacy(DomainLow, Pitch, Courses, PlaceColumn, OutColumns, Layout.LodLevel);
		return;
	}

	const float Du = std::fmax(Pitch * 0.01f, 1e-4f);

	// 1. The provisional band's real arc length, integrated across the slope (R1(d)).
	CrossSection Section(PlaceColumn, Du, Pitch * 0.25f);
	Section.Reset(ProvisionalLow, 0.0f);
	if (!Section.AdvanceToU(ProvisionalHigh))
	{
		WARN_PRINT_ONCE(godot::String::utf8("AncientBuilding 瓦作: the roof patch is not regular across a course band "
			"(G <= 0 or EG - F^2 <= 0); that band falls back to the legacy layout."));
		LayTileCoursesLegacy(DomainLow, Pitch, Courses, PlaceColumn, OutColumns, Layout.LodLevel);
		return;
	}

	// 2. Integer solve, then p = L / N (R8 甲 3). The module window is derived from the face's own
	// nominal pitch unless the caller declared its own (R8 甲 2).
	TileCourseLayout Solved = Layout;
	if (!(Solved.MinPitch > 0.0f))
	{
		Solved.MinPitch = Pitch * TILE_PITCH_MIN_RATIO;
	}
	if (!(Solved.MaxPitch > 0.0f))
	{
		Solved.MaxPitch = Pitch * TILE_PITCH_MAX_RATIO;
	}

	float EffectivePitch = Pitch;
	const float BandLength = Section.Arc();
	const int32_t Count = SolveCourseCount(BandLength, Solved, EffectivePitch);
	if (!(EffectivePitch > 0.0f))
	{
		LayTileCoursesLegacy(DomainLow, Pitch, Courses, PlaceColumn, OutColumns, Layout.LodLevel);
		return;
	}

	// 2b. With an envelope the pitch is re-derived from the count so that the outermost piece lands
	// exactly on the verge: the provisional solve fixed N, and p = W / (N + rn + rf) is the pitch at
	// which the cut band and the piece that reaches past it together fill the domain.
	if (bEnvelope)
	{
		EffectivePitch = Width / (float(Count) + RatioSum);
	}
	const float BandLow = bEnvelope ? DomainLow + NearRatio * EffectivePitch : DomainLow;
	const float BandHigh = bEnvelope ? DomainHigh - FarRatio * EffectivePitch : DomainHigh;

	// 3. Stations at equal arc spacing; the section samples are arc offsets from the centre.
	const int32_t Slots = TileSection::SampleCount(Layout.LodLevel);
	std::vector<TileSkinColumn> Laid;
	Laid.reserve(size_t(Count * Slots));
	std::vector<float> Stations;
	Stations.reserve(size_t(Count) + 1);

	Section.Reset(BandLow, 0.0f);
	bool bPlaced = true;
	for (int32_t Course = 0; Course < Count && bPlaced; ++Course)
	{
		const float CentreArc = (float(Course) + 0.5f) * EffectivePitch;

		for (int32_t Slot = 0; Slot < Slots; ++Slot)
		{
			const int32_t Sample = TileSection::SampleIndex(Layout.LodLevel, Slot);
			const float SampleArc = CentreArc + EffectivePitch * TileSection::OffsetAt(Sample);
			if (!Section.AdvanceToArc(SampleArc))
			{
				bPlaced = false;
				break;
			}

			if (Sample == 0)
			{
				Stations.push_back(Section.UAt());
			}

			TileSkinColumn Column;
			Column.Sample = Sample;
			Column.Course = Course;
			Column.Pitch = EffectivePitch;
			Column.Lift = EffectivePitch * TileSection::MinimumLift();
			Column.Points = PlaceColumn(Section.UAt());

			Laid.push_back(Column);
		}
	}

	if (bPlaced)
	{
		// The far edge of the last course, so the last unit is measured over its whole width and
		// not down to whatever sample the layout happened to stop on.
		Section.AdvanceToArc(float(Count) * EffectivePitch);
		Stations.push_back(Section.UAt());
	}

	if (!bPlaced)
	{
		// A patch that turns back on itself mid-band: nothing placed is better than a band with a
		// hole in it, so the whole band goes down the legacy path.
		WARN_PRINT_ONCE(godot::String::utf8("AncientBuilding 瓦作: the cross-section stopped advancing across a course "
			"band; that band falls back to the legacy layout (R14.2: no division by a vanishing "
			"scale)."));
		LayTileCoursesLegacy(DomainLow, Pitch, Courses, PlaceColumn, OutColumns, Layout.LodLevel);
		return;
	}

	// 4. Multi-section constraint (R1(d)): the stations were solved on one cross-section, so the
	// handbook requires the module to be checked on several others — at least the eave side, the
	// ridge side and the narrowest place on the face. This re-measures the world distance between
	// adjacent stations *along the v = const lines*, which is the quantity the收窄 has to come from.
	{
		float WorstLow = EffectivePitch;
		float WorstHigh = EffectivePitch;
		for (size_t Station = 1; Station < Stations.size(); ++Station)
		{
			const float U0 = Stations[Station - 1];
			const float U1 = Stations[Station];
			if (!(U1 > U0))
			{
				continue;
			}

			for (int32_t Probe = 0; Probe < 3; ++Probe)
			{
				const std::vector<Vector3> Probe0 = PlaceColumn(U0);
				const std::vector<Vector3> Probe1 = PlaceColumn(U1);
				const size_t Shared = std::min(Probe0.size(), Probe1.size());
				if (Shared < 2)
				{
					continue;
				}

				// Eave side, mid-slope, ridge side.
				const float V = float(Shared - 1) * 0.5f * float(Probe);

				const int32_t Steps = 4;
				float Local = 0.0f;
				for (int32_t Step = 0; Step < Steps; ++Step)
				{
					const float At = U0 + (U1 - U0) * ((float(Step) + 0.5f) / float(Steps));
					const std::vector<Vector3> Low = PlaceColumn(At - Du);
					const std::vector<Vector3> High = PlaceColumn(At + Du);
					const size_t Span = std::min(Low.size(), High.size());
					if (Span < 2)
					{
						continue;
					}
					const float Clamped = std::fmin(std::fmax(V, 0.0f), float(Span) - 1.0f);
					const Vector3 Along = (PatchPoint(High, Clamped) - PatchPoint(Low, Clamped)) / (2.0f * Du);
					Local += std::sqrt(Along.dot(Along));
				}
				Local *= (U1 - U0) / float(Steps);

				if (Local > 0.0f)
				{
					WorstLow = std::fmin(WorstLow, Local);
					WorstHigh = std::fmax(WorstHigh, Local);
				}
			}
		}

		if (WorstLow < Solved.MinPitch - 1e-4f || WorstHigh > Solved.MaxPitch + 1e-4f)
		{
			WARN_PRINT_ONCE(godot::String::utf8("AncientBuilding 瓦作: the effective module is ")
				+ godot::String::num(EffectivePitch, 3)
				+ " m on the cross-section it was solved on but spans ["
				+ godot::String::num(WorstLow, 3) + ", " + godot::String::num(WorstHigh, 3)
				+ "] m elsewhere on the face, against an admissible ["
				+ godot::String::num(Solved.MinPitch, 3) + ", " + godot::String::num(Solved.MaxPitch, 3)
				+ "]. A face whose scale varies that much needs the corner-tile handling of R1(c)/R14 "
				+ godot::String::utf8("and a 定标 of p_min/p_max; the layout stands as solved, since R14.2 puts the收束 "
				"under the 宝顶 or the 垂脊."));
		}
	}

	OutColumns.insert(OutColumns.end(), Laid.begin(), Laid.end());
}

// ==================== Skin ====================

namespace
{
	/** A column's points after displacement, with the smooth normal and frame at each. */
	struct ResolvedColumn
	{
		std::vector<Vector3> Points;
		std::vector<Vector3> Normals;
		/** The un-displaced surface points, so the 泥背 layer's edge can be closed at the eave. */
		std::vector<Vector3> Boarding;
		/** Outward surface normal, kept so the eave tiles can be oriented. */
		std::vector<Vector3> Outward;
		/** Direction the column runs, pointing away from the eave. */
		std::vector<Vector3> UpSlope;
		/** Across the slope, in the same sense as the section's x axis. */
		std::vector<Vector3> Across;
	};

	/**
	 * 纵向叠压 (R8 乙 8 / R10 / R21): the joint stations of the 筒瓦 and 板瓦 pieces along a course.
	 *
	 * The legacy skin had no longitudinal period at all — one Pitch doubled as both the course
	 * width and the sampling step — which is why adding metrics alone still yields a corrugated
	 * sheet (review §12.1). Every piece here ends at a joint where the piece *above* steps over it,
	 * which is the rhythm the reference photograph shows.
	 */
	struct LapSettings
	{
		bool bEnabled = false;
		/** q, the period along the slope, in world units. */
		float Joint = 0.0f;
		/** Run behind each joint over which the step returns to the weathering plane. */
		float Tail = 0.0f;
		/** Height of the step, in world units. */
		float Thickness = 0.0f;
		/** 卷棚: one profile runs eave to eave, so the pieces are laid from both ends inward. */
		bool bFromBothEnds = false;
		/**
		 *  tier 1: one slant per joint instead of riser + plateau + taper (30_瓦作 §2 中景).
		 *
		 * This is where the mid-distance saving actually is. The full step pins four points per
		 * joint per column and a course band carries tens of joints, so the lap — not the section,
		 * not the eave pieces — is what the roof's triangle count is made of. Keeping the slant
		 * keeps the rhythm and the silhouette; what goes is the sub-millimetre plateau nobody can
		 * see from the mid distance the tier exists for.
		 */
		bool bSimplified = false;
	};

	/** One point of a resolved column, mid-assembly. */
	struct ResolvedSample
	{
		Vector3 Position;
		Vector3 Boarding;
		Vector3 Normal;
		Vector3 Outward;
		Vector3 UpSlope;
		Vector3 Across;
	};

	/**
	 * Displaces one column off the boarding and works out its shading normals.
	 *
	 * The local frame comes from finite differences: across the slope from the neighbouring
	 * columns, up the slope from the neighbouring points. Both directions are horizontal-ish and
	 * roughly orthogonal on every roof here, so the frame needs no orthonormalisation beyond
	 * taking the cross product.
	 */
	ResolvedColumn ResolveColumn(
		const std::vector<TileSkinColumn>& Columns,
		size_t Index,
		bool bWrap,
		const LapSettings& Laps,
		float Bedding)
	{
		const TileSkinColumn& Column = Columns[Index];
		const size_t Count = Column.Points.size();

		ResolvedColumn Out;
		if (Count < 2)
		{
			return Out;
		}

		// Neighbours for the across direction, wrapping or clamping at the band's edges.
		const size_t Last = Columns.size() - 1;
		size_t Before = Index;
		size_t After = Index;
		if (Index > 0)
		{
			Before = Index - 1;
		}
		else if (bWrap)
		{
			Before = Last;
		}
		if (Index < Last)
		{
			After = Index + 1;
		}
		else if (bWrap)
		{
			After = 0;
		}

		const Vector2 Section = TileSection::NormalAt(Column.Sample);
		const float Rise = Column.Lift + Column.Pitch * TileSection::HeightAt(Column.Sample) + Bedding;

		// The frame at every boarding point, before any lap subdivision: the lap steps must not
		// feed back into the slope direction, or the whole surface would tilt.
		std::vector<ResolvedSample> Base;
		Base.reserve(Count);

		for (size_t Point = 0; Point < Count; ++Point)
		{
			// Up-slope. Central difference where possible so the frame does not swing at the ends.
			const size_t Low = Point > 0 ? Point - 1 : Point;
			const size_t High = Point + 1 < Count ? Point + 1 : Point;
			Vector3 Along = Column.Points[High] - Column.Points[Low];

			Vector3 Across = ClampedPoint(Columns[After], Point) - ClampedPoint(Columns[Before], Point);
			if (Across.length_squared() < 1e-12f)
			{
				// Two coincident columns at a band edge with nothing to interpolate against. Fall
				// back to something perpendicular to the slope so the frame stays valid.
				Across = Along.cross(Vector3(0, 1, 0));
			}

			Vector3 Outward = Across.cross(Along);
			if (Outward.length_squared() < 1e-12f)
			{
				Outward = Vector3(0, 1, 0);
			}
			Outward = Outward.normalized();
			if (Outward.y < 0.0f)
			{
				Outward = -Outward;
				Across = -Across;
			}

			ResolvedSample Sample;
			const Vector3 AcrossUnit = Across.normalized();
			const Vector3 AlongUnit = Along.length_squared() > 1e-12f
				? Along.normalized()
				: Across.cross(Outward).normalized();

			Sample.Position = Column.Points[Point] + Outward * Rise;
			Sample.Boarding = Column.Points[Point];
			Sample.Normal = (AcrossUnit * Section.x + Outward * Section.y).normalized();
			Sample.Outward = Outward;
			Sample.UpSlope = AlongUnit;
			Sample.Across = AcrossUnit;
			Base.push_back(Sample);
		}

		if (!Laps.bEnabled || !(Laps.Joint > 0.0f) || !(Laps.Thickness > 0.0f))
		{
			Out.Points.reserve(Count);
			Out.Normals.reserve(Count);
			Out.Boarding.reserve(Count);
			Out.Outward.reserve(Count);
			Out.UpSlope.reserve(Count);
			Out.Across.reserve(Count);
			for (const ResolvedSample& Sample : Base)
			{
				Out.Points.push_back(Sample.Position);
				Out.Normals.push_back(Sample.Normal);
				Out.Boarding.push_back(Sample.Boarding);
				Out.Outward.push_back(Sample.Outward);
				Out.UpSlope.push_back(Sample.UpSlope);
				Out.Across.push_back(Sample.Across);
			}

			return Out;
		}

		// Arc length along the boarding, which is the parameter the pieces are laid by.
		std::vector<float> Arc(Count, 0.0f);
		for (size_t Point = 1; Point < Count; ++Point)
		{
			Arc[Point] = Arc[Point - 1] + Column.Points[Point].distance_to(Column.Points[Point - 1]);
		}
		const float Total = Arc[Count - 1];

		/**
		 * Height of the lap step at an arc position.
		 *
		 * Each piece's tail stands a tile-thickness proud where it laps the piece below and tapers
		 * back to the weathering plane behind the joint. Zero net drift up the slope, so the
		 * courses do not walk off the ridge — the failure mode of a literal layer count.
		 */
		struct Joint
		{
			float Start;
			int32_t Climb;  // +1 measured from the first eave, -1 from the far one
		};
		std::vector<Joint> Joints;
		if (Total > Laps.Joint)
		{
			for (float At = Laps.Joint; At < Total; At += Laps.Joint)
			{
				Joints.push_back({ At, 1 });
				if (Laps.bFromBothEnds && Total - At > Laps.Joint * 0.5f)
				{
					Joints.push_back({ Total - At, -1 });
				}
			}
			std::sort(Joints.begin(), Joints.end(),
				[](const Joint& Left, const Joint& Right) { return Left.Start < Right.Start; });
		}

		auto StepAt = [&](float At) -> float
		{
			float Height = 0.0f;
			for (const Joint& Current : Joints)
			{
				const float Behind = (Current.Climb > 0) ? (At - Current.Start) : (Current.Start - At);
				if (Behind < 0.0f || Behind >= Laps.Tail)
				{
					continue;
				}
				Height = std::fmax(Height, Laps.Thickness * (1.0f - Behind / Laps.Tail));
			}

			return Height;
		};

		// Merge the boarding points with the joint breakpoints, in arc order.
		//
		// A joint needs both of its sides pinned, or the step would be smeared over whichever
		// gap the boarding polyline happens to have there: the foot of the step on the piece
		// below, the head of the step on the piece above, a second copy of the head so the taper
		// starts from the weathering plane's normal (the coincident pair costs no triangles,
		// exactly like the section's crease pairs), and the end of the taper. Four points per
		// joint, and no fine subdivision anywhere else, because the taper is linear in arc.
		struct Marker
		{
			float At;
			float Height;
			bool bRiser;
			int32_t Climb;
		};
		std::vector<Marker> Markers;
		Markers.reserve(Joints.size() * 4);
		for (const Joint& Current : Joints)
		{
			if (Laps.bSimplified)
			{
				// One slanted facet, so the joint keeps a down-slope-facing edge — the read of "逐片
				// 叠压" (card T4) — at half the points.
				if (Current.Climb > 0)
				{
					Markers.push_back({ Current.Start, 0.0f, true, Current.Climb });
					Markers.push_back({ Current.Start + Laps.Tail, Laps.Thickness, true, Current.Climb });
				}
				else
				{
					Markers.push_back({ Current.Start - Laps.Tail, Laps.Thickness, true, Current.Climb });
					Markers.push_back({ Current.Start, 0.0f, true, Current.Climb });
				}
				continue;
			}

			if (Current.Climb > 0)
			{
				Markers.push_back({ Current.Start, 0.0f, false, Current.Climb });
				Markers.push_back({ Current.Start, Laps.Thickness, true, Current.Climb });
				Markers.push_back({ Current.Start, Laps.Thickness, false, Current.Climb });
				Markers.push_back({ Current.Start + Laps.Tail, 0.0f, false, Current.Climb });
			}
			else
			{
				Markers.push_back({ Current.Start - Laps.Tail, 0.0f, false, Current.Climb });
				Markers.push_back({ Current.Start, Laps.Thickness, true, Current.Climb });
				Markers.push_back({ Current.Start, 0.0f, false, Current.Climb });
			}
		}
		std::stable_sort(Markers.begin(), Markers.end(),
			[](const Marker& Left, const Marker& Right) { return Left.At < Right.At; });

		auto FrameAt = [&](size_t Point, float ArcAt) -> ResolvedSample
		{
			// Interpolate the frame between the boarding points bracketing this arc.
			while (Point + 1 < Count && Arc[Point + 1] < ArcAt)
			{
				++Point;
			}
			while (Point > 0 && Arc[Point] > ArcAt)
			{
				--Point;
			}

			const size_t Next = Point + 1 < Count ? Point + 1 : Point;
			const float Span = Arc[Next] - Arc[Point];
			const float T = Span > 1e-6f ? (ArcAt - Arc[Point]) / Span : 0.0f;

			ResolvedSample Sample;
			Sample.Position = Base[Point].Position + (Base[Next].Position - Base[Point].Position) * T;
			Sample.Boarding = Base[Point].Boarding + (Base[Next].Boarding - Base[Point].Boarding) * T;
			Sample.Outward = (Base[Point].Outward + (Base[Next].Outward - Base[Point].Outward) * T).normalized();
			Sample.UpSlope = (Base[Point].UpSlope + (Base[Next].UpSlope - Base[Point].UpSlope) * T).normalized();
			Sample.Across = (Base[Point].Across + (Base[Next].Across - Base[Point].Across) * T).normalized();
			Sample.Normal = (Base[Point].Normal + (Base[Next].Normal - Base[Point].Normal) * T).normalized();

			return Sample;
		};

		auto Push = [&](const ResolvedSample& Sample, float Height, bool bRiser, int32_t Climb)
		{
			Out.Points.push_back(Sample.Position + Sample.Outward * Height);
			// The step's own face is spanned by the across direction and the outward one, so its
			// normal is the down-slope direction whichever eave the piece was laid from.
			Out.Normals.push_back(bRiser ? Sample.UpSlope * float(-Climb) : Sample.Normal);
			Out.Boarding.push_back(Sample.Boarding);
			Out.Outward.push_back(Sample.Outward);
			Out.UpSlope.push_back(Sample.UpSlope);
			Out.Across.push_back(Sample.Across);
		};

		size_t NextMarker = 0;
		for (size_t Point = 0; Point + 1 < Count; ++Point)
		{
			Push(Base[Point], StepAt(Arc[Point]), false, 1);

			while (NextMarker < Markers.size() && Markers[NextMarker].At < Arc[Point + 1] - 1e-5f)
			{
				const Marker& Current = Markers[NextMarker];
				if (Current.At > Arc[Point] + 1e-5f)
				{
					Push(FrameAt(Point, Current.At), Current.Height, Current.bRiser, Current.Climb);
				}
				++NextMarker;
			}
		}
		Push(Base[Count - 1], StepAt(Arc[Count - 1]), false, 1);

		return Out;
	}

	/**
	 * 瓦当 (勾头): the round medallion capping a 筒瓦 where it reaches the eave.
	 *
	 * Card T1's one committed fact is that this is a 筒瓦 body *plus an end disc*, not a cut barrel,
	 * and the disc carries a pressed face. The legacy piece was a short capped cylinder, so the
	 * body was already right; what is added here is the disc's depth — 边轮 and 中心凸起 — and a
	 * segment count that follows from the disc radius and the design camera rather than the fixed
	 * seven sides (review §12.3).
	 */
	void AddBarrelEnd(
		const Vector3& CrownPoint,
		const Vector3& Outward,
		const Vector3& UpSlope,
		const Vector3& Across,
		float Pitch,
		const TileSkinSettings& Settings,
		const Color& Tint,
		MeshAccumulator& Mesh)
	{
		// The barrel's axis sits a rise below its crown, which is where the medallion centres.
		const Vector3 Axis = CrownPoint - Outward * (BARREL_RISE * Pitch);
		const float Radius = Pitch * WADANG_RADIUS;
		// Legacy drew the disc with a flat seven sides; the chord-error count replaces it only from
		// detail 2 up, so the default path stays bit-identical (review §12.3 wants the count
		// derived rather than picked, and 7 is not derived from anything).
		//
		// A distance tier scales the *budget*, not the disc: the same chord-error formula is fed a
		// coarser epsilon, so the outline stays the outline and only its tessellation thins.
		const int32_t Sides = Settings.Detail >= TILE_DETAIL_EAVE
			? std::max(ChordErrorSides(Radius, ChordErrorFromPixels(Settings))
				>> std::min(std::max(Settings.LodLevel, 0), 8), 3)
			: 7;

		std::vector<Vector3> Spine;
		Spine.push_back(Axis + UpSlope * (Pitch * WADANG_IN));
		Spine.push_back(Axis - UpSlope * (Pitch * WADANG_OUT));

		std::vector<Vector2> Contour;
		for (int32_t Index = 0; Index < Sides; ++Index)
		{
			const float Angle = SKIN_PI * 2.0f * float(Index) / float(Sides);
			Contour.push_back(Vector2(std::cos(Angle) * Radius, std::sin(Angle) * Radius));
		}

		SweepSettings Sweep;
		Sweep.Contour = Contour;
		Sweep.bClosedContour = true;
		Sweep.bGenerateCaps = true;
		Sweep.UpReference = Outward;

		SweepResult Result;
		if (!BuildSweep(Spine, Sweep, Result))
		{
			return;
		}
		Mesh.AddSweep(Result, Tint);

		if (Settings.Detail < TILE_DETAIL_EAVE)
		{
			return;
		}

		// ---- The pressed face (card T1): a raised 边轮 and a central boss on the outer cap ----
		const Vector3 Face = Axis - UpSlope * (Pitch * WADANG_OUT);
		const Vector3 FaceOut = -UpSlope;
		Vector3 FaceU = Across - UpSlope * Across.dot(UpSlope);
		if (FaceU.length_squared() < 1e-8f)
		{
			FaceU = Outward;
		}
		FaceU = FaceU.normalized();
		const Vector3 FaceV = FaceOut.cross(FaceU).normalized();

		const float Relief = Radius * WADANG_RELIEF;
		AddDiscBand(Mesh, Face + FaceOut * Relief, FaceOut, FaceU, FaceV,
			Radius * WADANG_RIM_INNER, Radius, Sides, Tint);
		AddWallBand(Mesh, Face, FaceOut, FaceU, FaceV, Radius,
			0.0f, Relief, Sides, Tint);
		AddWallBand(Mesh, Face, FaceOut, FaceU, FaceV, Radius * WADANG_RIM_INNER,
			Relief, 0.0f, Sides, Tint);
		AddDiscBand(Mesh, Face + FaceOut * Relief, FaceOut, FaceU, FaceV,
			0.0f, Radius * WADANG_BOSS, Sides, Tint);
		AddWallBand(Mesh, Face, FaceOut, FaceU, FaceV, Radius * WADANG_BOSS,
			0.0f, Relief, Sides, Tint);
	}

	/**
	 * 滴水, legacy shape: a narrow tongue the same width as the channel it closes.
	 *
	 * Kept verbatim so detail 0 and 1 bake what they always did. R5 records what is missing from
	 * it — no skirt wider than the body, no lobes, no central point — which is why detail 2
	 * replaces it outright rather than adjusting it.
	 */
	void AddChannelEndLegacy(
		const Vector3& ChannelPoint,
		const Vector3& Outward,
		const Vector3& UpSlope,
		float Pitch,
		const Color& Tint,
		MeshAccumulator& Mesh)
	{
		const Vector3 Out = -UpSlope;
		const Vector3 Lip = ChannelPoint + Out * (Pitch * 0.26f);

		std::vector<Vector3> Spine;
		Spine.push_back(ChannelPoint + UpSlope * (Pitch * 0.06f));
		Spine.push_back(Lip);
		Spine.push_back(Lip + Out * (Pitch * 0.07f) + Vector3(0.0f, -Pitch * 0.30f, 0.0f));

		// Across the channel, then a little thickness, with the corners taken off so the tongue
		// reads as a rounded lip instead of a slab.
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.34f * Pitch, 0.05f * Pitch));
		Contour.push_back(Vector2(0.34f * Pitch, 0.05f * Pitch));
		Contour.push_back(Vector2(0.26f * Pitch, -0.09f * Pitch));
		Contour.push_back(Vector2(-0.26f * Pitch, -0.09f * Pitch));

		SweepSettings Settings;
		Settings.Contour = Contour;
		Settings.bClosedContour = true;
		Settings.bGenerateCaps = true;
		Settings.UpReference = Outward;

		SweepResult Sweep;
		if (BuildSweep(Spine, Settings, Sweep))
		{
			Mesh.AddSweep(Sweep, Tint);
		}
	}

	/**
	 * 滴水: the apron hanging from a 板瓦 channel at the eave.
	 *
	 * Card T2 commits to exactly two things: the skirt is *wider than the tile body* and it hangs
	 * *below* it. Both are what separate a 滴水 from a plain 板瓦 end, and the legacy tongue had
	 * neither — it was the same width as the channel it closed, with no lobes and no central point.
	 *
	 * The outline is the 13-point 如意 head (R5): symmetric, 0.42 p at the shoulders against the
	 * body's 0.34 p, with a notch either side of a central point that hangs lowest. Its exact
	 * shape is [自定] — the handbook forbids measuring one off the illustration.
	 *
	 * The plate folds twice by R4's 20° (R5's dishui bend of 40° in total): a short body continues
	 * the roof plane past the eave, then the skirt turns down.
	 */
	void AddDripApron(
		const Vector3& ChannelPoint,
		const Vector3& Outward,
		const Vector3& UpSlope,
		const Vector3& Across,
		float Pitch,
		int32_t LodLevel,
		const Color& Tint,
		MeshAccumulator& Mesh)
	{
		Vector3 FaceU = Across;
		Vector3 Down = -UpSlope;
		const Vector3 Normal = Outward;
		// A folded plate needs an orthonormal frame; the roof's own one is only nearly so.
		Down = (Down - Normal * Down.dot(Normal)).normalized();
		FaceU = (FaceU - Down * FaceU.dot(Down) - Normal * FaceU.dot(Normal)).normalized();

		// "Down" means the world's down, projected into the plane the skirt swings in. Taking it
		// from the roof's own down-slope direction instead would make a hanging skirt flare *up*
		// out of a shallow roof and tuck under a steep one.
		const Vector3 WorldDown(0.0f, -1.0f, 0.0f);
		Vector3 Hang = WorldDown - FaceU * WorldDown.dot(FaceU);
		Hang = Hang.length_squared() > 1e-8f ? Hang.normalized() : Down;

		const float Thickness = Pitch * TILE_THICKNESS_RATIO;

		// The 如意 outline, (across, down-slope) in pitches, counter-clockwise. The full thirteen
		// points; a distance tier drops the intermediate ones between the shoulder, the side lobes
		// and the central point, which leaves the silhouette (裙边宽于瓦身 + 明显下垂) but not the
		// rounding of each lobe.
		const float Half = DRIP_BODY_HALF;
		const float Skirt = DRIP_SKIRT_HALF;
		const std::vector<Vector2> Full = {
			Vector2(-Half, 0.0f),
			Vector2(-Half - 0.02f, 0.10f),
			Vector2(-Skirt, 0.18f),
			Vector2(-0.38f, 0.28f),
			Vector2(-0.26f, 0.22f),
			Vector2(-0.14f, 0.30f),
			Vector2(0.0f, DRIP_TIP),
			Vector2(0.14f, 0.30f),
			Vector2(0.26f, 0.22f),
			Vector2(0.38f, 0.28f),
			Vector2(Skirt, 0.18f),
			Vector2(Half + 0.02f, 0.10f),
			Vector2(Half, 0.0f),
		};
		const std::vector<Vector2> Reduced = {
			Vector2(-Half, 0.0f),
			Vector2(-Skirt, 0.18f),
			Vector2(-0.26f, 0.22f),
			Vector2(-0.14f, 0.30f),
			Vector2(0.0f, DRIP_TIP),
			Vector2(0.14f, 0.30f),
			Vector2(0.26f, 0.22f),
			Vector2(Skirt, 0.18f),
			Vector2(Half, 0.0f),
		};
		const std::vector<Vector2>& Outline = LodLevel >= 1 ? Reduced : Full;

		// Fold the outline into 3D: the run past each fold line turns down by 20 degrees, so the
		// skirt as a whole is 40 degrees off the roof plane.
		std::vector<Vector3> Front;
		std::vector<Vector3> Back;
		Front.reserve(Outline.size());
		Back.reserve(Outline.size());
		for (const Vector2& Point : Outline)
		{
			const float Abscissa = Point.x * Pitch;
			const float Ordinate = Point.y * Pitch;

			Vector3 Position = ChannelPoint + FaceU * Abscissa;
			Vector3 Direction = Down;
			float Left = Ordinate;
			float Folded = 0.0f;
			const float FoldOne = DRIP_FOLD_ONE * Pitch;
			const float FoldTwo = DRIP_FOLD_TWO * Pitch;
			const float First = std::fmin(Left, FoldOne);
			Position += Direction * First;
			Left -= First;
			if (Left > 0.0f)
			{
				Folded += DRIP_FOLD_DEGREES;
				const float Radians = Folded * SKIN_PI / 180.0f;
				Direction = (Down * std::cos(Radians) + Hang * std::sin(Radians)).normalized();
				const float Second = std::fmin(Left, FoldTwo - FoldOne);
				Position += Direction * Second;
				Left -= Second;
			}
			if (Left > 0.0f)
			{
				Folded += DRIP_FOLD_DEGREES;
				const float Radians = Folded * SKIN_PI / 180.0f;
				Direction = (Down * std::cos(Radians) + Hang * std::sin(Radians)).normalized();
				Position += Direction * Left;
			}

			const Vector3 PlateNormal = Direction.cross(FaceU).normalized();
			Front.push_back(Position + PlateNormal * (Thickness * 0.5f));
			Back.push_back(Position - PlateNormal * (Thickness * 0.5f));
		}

		std::vector<int32_t> Indices;
		TriangulateOutline(Outline, Indices);
		if (Indices.size() < 3)
		{
			return;
		}

		const Vector3 Up = Outward;
		for (size_t Index = 0; Index + 2 < Indices.size(); Index += 3)
		{
			const int32_t A = Indices[Index];
			const int32_t B = Indices[Index + 1];
			const int32_t C = Indices[Index + 2];
			AddTriangleOriented(Mesh, Front[size_t(A)], Front[size_t(B)], Front[size_t(C)], Up, Tint);
			AddTriangleOriented(Mesh, Back[size_t(C)], Back[size_t(B)], Back[size_t(A)], -Up, Tint);
		}

		for (size_t Index = 0; Index < Outline.size(); ++Index)
		{
			const size_t Next = (Index + 1) % Outline.size();
			const Vector3 Edge = Front[Next] - Front[Index];
			const Vector3 Rim = (Back[Index] - Front[Index]).cross(Edge);
			if (Rim.length_squared() < 1e-12f)
			{
				continue;
			}
			Mesh.AddQuadOriented(Front[Index], Front[Next], Back[Next], Back[Index], Rim.normalized(), Tint);
		}
	}

	/**
	 * 檐口条 ( tier 2): the whole eave as one plain bar.
	 *
	 * A far tier has no business carrying a round 瓦当 per 筒瓦 and an 如意 滴水 per 板瓦: at the
	 * distance the tier is for, the eave is a dark line with a scalloped top edge, and that is what
	 * a bar along the crown line gives — for two knots and four contour points instead of one piece
	 * per course. From and To are the crown points of the first and last course, so the bar rides
	 * the top of the barrels and its contour reaches down past the channel bottom.
	 */
	void AddEaveBar(
		const Vector3& From,
		const Vector3& To,
		const Vector3& Outward,
		float Pitch,
		const Color& Tint,
		MeshAccumulator& Mesh)
	{
		if (From.distance_squared_to(To) < 1e-8f)
		{
			return;
		}

		// The section's own depth either way from the crown: -PAN_SAG to +BARREL_RISE, in pitches,
		// plus a little for the 滴水 that is not there at this tier.
		const std::vector<Vector2> Contour = {
			Vector2(-0.5f * Pitch, BARREL_RISE * Pitch),
			Vector2(0.5f * Pitch, BARREL_RISE * Pitch),
			Vector2(0.5f * Pitch, -(PAN_SAG + 0.10f) * Pitch),
			Vector2(-0.5f * Pitch, -(PAN_SAG + 0.10f) * Pitch),
		};

		std::vector<Vector3> Spine;
		Spine.push_back(From);
		Spine.push_back(To);

		SweepSettings Settings;
		Settings.Contour = Contour;
		Settings.bClosedContour = true;
		Settings.bGenerateCaps = true;
		Settings.UpReference = Outward;

		SweepResult Sweep;
		if (BuildSweep(Spine, Settings, Sweep))
		{
			Mesh.AddSweep(Sweep, Tint);
		}
	}
} // namespace

void BuildingGen::BuildTileSkin(
	const std::vector<TileSkinColumn>& Columns,
	ETileSkinLoop Loop,
	ETileEaves Eaves,
	const TileSkinSettings& Settings,
	const Color& Tint,
	MeshAccumulator& Mesh)
{
	if (Columns.size() < 2)
	{
		return;
	}

	const bool bWrap = Loop == ETileSkinLoop::Closed;
	const int32_t Lod = std::max(Settings.LodLevel, 0);
	const float Bedding = Settings.Detail >= TILE_DETAIL_EAVE
		? std::fmax(Settings.BeddingThickness, 0.0f)
		: 0.0f;

	// 纵向叠压 (R8 乙 8): the pieces break on a period q = tile_length - overlap, and the period is
	// read off the effective coverage module rather than off the course width, because the two are
	// now the same quantity and the course width is only its legacy stand-in (R10).
	LapSettings Laps;
	// Far tier: one layer, no lap at all — this is the single biggest thing a distant roof can shed,
	// and at that distance a lap is a shading band, not a piece of tile.
	Laps.bEnabled = Settings.Detail >= TILE_DETAIL_COURSES && Lod < 2;
	Laps.bSimplified = Lod >= 1;
	Laps.bFromBothEnds = Eaves == ETileEaves::AtBothEnds;
	if (Laps.bEnabled && !Columns.empty())
	{
		const float Pitch = Columns.front().Pitch;
		const float TileLength = Pitch * TILE_LENGTH_RATIO;
		Laps.Joint = TileLength * (1.0f - TILE_LAP_RATIO);
		Laps.Tail = Laps.Joint * TILE_TAIL_RATIO;
		Laps.Thickness = Pitch * TILE_THICKNESS_RATIO;
		if (!(Laps.Joint > 0.0f))
		{
			Laps.bEnabled = false;
		}
	}

	std::vector<ResolvedColumn> Resolved;
	Resolved.reserve(Columns.size());
	for (size_t Index = 0; Index < Columns.size(); ++Index)
	{
		Resolved.push_back(ResolveColumn(Columns, Index, bWrap, Laps, Bedding));
	}

	const size_t StripCount = bWrap ? Columns.size() : Columns.size() - 1;

	for (size_t Strip = 0; Strip < StripCount; ++Strip)
	{
		const ResolvedColumn& Left = Resolved[Strip];
		const ResolvedColumn& Right = Resolved[(Strip + 1) % Resolved.size()];

		// The strip that crosses a course boundary is the descending pan; give it the course it
		// falls away from, so a course's colour covers its barrel and both its flanks.
		const Color Shade = CourseTint(Tint, Columns[Strip].Course);

		// A course cut off at a hip is shorter than its neighbour; the skin stops where the
		// shorter of the two does.
		const size_t Shared = std::min(Left.Points.size(), Right.Points.size());

		for (size_t Point = 0; Point + 1 < Shared; ++Point)
		{
			Mesh.AddQuadSmooth(
				Left.Points[Point],
				Right.Points[Point],
				Right.Points[Point + 1],
				Left.Points[Point + 1],
				Left.Normals[Point],
				Right.Normals[Point],
				Right.Normals[Point + 1],
				Left.Normals[Point + 1],
				Shade);
		}
	}

	// 泥背 (R17/card T3): the layer order is 望板 -> 灰泥 -> 瓦, so the lifted skin would otherwise
	// float a thickness above the boarding at the eave. Close that step across the section; it is
	// the one edge of the bedding anybody can see.
	if (Bedding > 0.0f)
	{
		for (size_t Strip = 0; Strip < StripCount; ++Strip)
		{
			const ResolvedColumn& Left = Resolved[Strip];
			const ResolvedColumn& Right = Resolved[(Strip + 1) % Resolved.size()];
			const size_t Shared = std::min(Left.Points.size(), Right.Points.size());
			if (Shared < 1)
			{
				continue;
			}

			const Color Shade = CourseTint(Tint, Columns[Strip].Course);
			const size_t Ends[2] = { 0, Shared - 1 };
			for (const size_t End : Ends)
			{
				if (Eaves != ETileEaves::AtBothEnds && End != 0)
				{
					continue;
				}
				Mesh.AddQuadOriented(
					Left.Boarding[End], Right.Boarding[End],
					Right.Points[End], Left.Points[End],
					End == 0 ? -Left.UpSlope[End] : Left.UpSlope[End], Shade);
			}
		}
	}

	if (Eaves == ETileEaves::None)
	{
		return;
	}

	// ---- Tier 2: the eave is a bar, not a row of pieces ----
	if (Lod >= 2)
	{
		for (int32_t End = 0; End < 2; ++End)
		{
			if (End == 1 && Eaves != ETileEaves::AtBothEnds)
			{
				break;
			}

			// The crown line is the top of the barrel row, so a bar there caps what the player sees
			// from below and its contour reaches down past the channel bottom.
			const Vector3* First = nullptr;
			const Vector3* Last = nullptr;
			Vector3 Outward;
			Color Shade;
			float Pitch = 0.0f;
			for (size_t Index = 0; Index < Columns.size(); ++Index)
			{
				const TileSkinColumn& Column = Columns[Index];
				if (!TileSection::IsCrown(Column.Sample) || Resolved[Index].Points.size() < 2)
				{
					continue;
				}

				const ResolvedColumn& Line = Resolved[Index];
				const size_t At = End == 0 ? 0 : Line.Points.size() - 1;
				if (First == nullptr)
				{
					First = &Line.Points[At];
					Outward = Line.Outward[At];
					Shade = CourseTint(Tint, Column.Course);
					Pitch = Column.Pitch;
				}
				Last = &Line.Points[At];
			}

			if (First != nullptr && Last != nullptr)
			{
				AddEaveBar(*First, *Last, Outward, Pitch, Shade, Mesh);
			}
		}

		return;
	}

	// ---- 瓦当 and 滴水 along whichever ends are eaves ----

	for (size_t Index = 0; Index < Columns.size(); ++Index)
	{
		const TileSkinColumn& Column = Columns[Index];
		const ResolvedColumn& Line = Resolved[Index];
		const bool bCrown = TileSection::IsCrown(Column.Sample);
		const bool bChannel = TileSection::IsChannel(Column.Sample);

		if (Line.Points.size() < 2 || (!bCrown && !bChannel))
		{
			continue;
		}

		const Color Shade = CourseTint(Tint, Column.Course);
		const size_t Last = Line.Points.size() - 1;

		// UpSlope always points from the eave inward at the start, and the reverse at the far end,
		// so the far eave negates it.
		if (bCrown)
		{
			AddBarrelEnd(Line.Points[0], Line.Outward[0], Line.UpSlope[0], Line.Across[0],
				Column.Pitch, Settings, Shade, Mesh);
		}
		else if (Settings.Detail >= TILE_DETAIL_EAVE)
		{
			AddDripApron(Line.Points[0], Line.Outward[0], Line.UpSlope[0], Line.Across[0],
				Column.Pitch, Lod, Shade, Mesh);
		}
		else
		{
			AddChannelEndLegacy(Line.Points[0], Line.Outward[0], Line.UpSlope[0],
				Column.Pitch, Shade, Mesh);
		}

		if (Eaves != ETileEaves::AtBothEnds)
		{
			continue;
		}

		if (bCrown)
		{
			AddBarrelEnd(Line.Points[Last], Line.Outward[Last], -Line.UpSlope[Last], Line.Across[Last],
				Column.Pitch, Settings, Shade, Mesh);
		}
		else if (Settings.Detail >= TILE_DETAIL_EAVE)
		{
			AddDripApron(Line.Points[Last], Line.Outward[Last], -Line.UpSlope[Last], Line.Across[Last],
				Column.Pitch, Lod, Shade, Mesh);
		}
		else
		{
			AddChannelEndLegacy(Line.Points[Last], Line.Outward[Last], -Line.UpSlope[Last],
				Column.Pitch, Shade, Mesh);
		}
	}
}
