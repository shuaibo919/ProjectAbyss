#pragma once

// Ceramic tile skin (瓦面) for every roof in the generator.
//
// The first implementation laid one half-round tube per 瓦垄 on top of a flat boarding surface.
// That was wrong in kind, not just in detail: a real Chinese roof has no flat interval between
// its ridges. 板瓦 pan tiles form concave channels and 筒瓦 barrel tiles cap the joints between
// them, so the weathering surface is one continuous scalloped skin. Rendered, the tube version
// read as corrugated steel, and it cost about two thirds of the whole building's triangles
// because every tube carried a full closed perimeter including an underside nobody can see.
//
// So the skin is generated as a displaced surface instead. The caller owns the roof's
// parameterisation — where the courses go, and where they get cut off at a hip — and supplies one
// column of points per section sample. This file owns the cross-section and the shading.
//
// It comes out cheaper than the tubes it replaces: the two crease pairs are coincident in
// position, so their quads are degenerate and get skipped, which buys the crisp 筒瓦 edge for no
// triangles at all.
//
// ---- 瓦作 (30_瓦作) ----
//
// The legacy skin above is a *corrugated sheet*: one continuous displaced surface with a single
// cross-section, laid out by world-equal spacing, with no longitudinal joint and no recognisable
// eave section (review §12.1). adds three things behind ETileDetail, so that the default stays
// bit-identical:
//
//   1 = 排垄与叠压: courses are laid out on the patch's own metric (the first fundamental form,
//       R1(d)) by arc length across the slope, the course count is an integer solve over the band's
//       real arc length (R7/R8 甲), and each course is broken into 筒瓦/板瓦 pieces with a real
//       longitudinal lap (R8 乙/R21).
//   2 = 1 + 檐口件与泥背: 瓦当 gets a segmented end disc with a pressed face (R4/card T1), 滴水
//       becomes a 13-point 如意 apron wider than its body and hanging below it (R5/card T2), and
//       the skin lifts off the boarding by the 泥背 thickness (R17/card T3).
//
// Sizes that the handbooks leave 待定标 are self-defined and marked [自定] at their definition;
// none of them is measured off an illustration.

#include "AncientBuilding/BuildingBuilder.h"

#include <functional>

namespace BuildingGen
{
	/**
	 * Cross-section of one tile course, in units of the course pitch, measured in the surface's
	 * own frame: x across the slope, y along the outward surface normal.
	 *
	 * The 筒瓦 barrel is a half-ellipse centred on the course line; the 板瓦 pan falls away to
	 * either side and meets its neighbour at the pitch boundary, which is therefore the channel
	 * bottom and the one sample shared between adjacent courses.
	 *
	 * Samples 1/2 and 6/7 are coincident pairs: same position, different normal. That is how the
	 * barrel keeps a hard edge against the pan while the barrel itself stays smooth-shaded.
	 */
	struct TileSection
	{
		static const int32_t SAMPLE_COUNT = 8;

		/** Offset across the slope, in pitches, relative to the course line. */
		static float OffsetAt(int32_t Sample);

		/** Height above the batten line, in pitches. */
		static float HeightAt(int32_t Sample);

		/** Section normal, in the (across, outward) frame. Unit length. */
		static Vector2 NormalAt(int32_t Sample);

		/** True at the 筒瓦 crown — where a 瓦当 goes at the eave. */
		static bool IsCrown(int32_t Sample);

		/** True at the 板瓦 channel bottom — where a 滴水 goes at the eave. */
		static bool IsChannel(int32_t Sample);

		/**
		 * Batten height, in pitches, that keeps the channel bottom clear of the boarding.
		 *
		 * This used to be a hand-picked fraction of the module at each call site, which is how the
		 * roof once ended up z-fighting into mottled noise. Tying it to the section's own depth
		 * means it cannot drift out of step with the tiles again.
		 */
		static float MinimumLift();

		/**
		 * How many of the samples a distance tier actually emits .
		 *
		 * The reduction is a *subset of the same table*, never a different profile: the caller still
		 * lays the columns out with `OffsetAt(SampleIndex(...))`, so a mid-distance face has the same
		 * covered band, the same course count and the same eave ends as the near one — only fewer
		 * samples across it. That is what keeps a LOD from renumbering the tiles (review §8).
		 *
		 *   0 = 8 — every sample.
		 *   1 = 4 — the two 板瓦 creases and the barrel's quarter points go; the pan and the barrel
		 *           each survive as one facet, and the two creases cost no triangles anyway.
		 *   2 = 3 — the barrel keeps only its crown, so the section is a plain corrugation.
		 *
		 * The last kept sample is the pan edge (not the channel bottom of the next course), so the
		 * band the courses cover is identical at every tier.
		 */
		static int32_t SampleCount(int32_t LodLevel);

		/** Table index of the Slot-th emitted sample at a tier. Identity when LodLevel <= 0. */
		static int32_t SampleIndex(int32_t LodLevel, int32_t Slot);
	};

	/**
	 * One line of the skin running up the slope.
	 *
	 * Points lie on the boarding, from the eave upward, and are displaced along the local surface
	 * normal on the way out. Columns may be shorter than their neighbours: that is a course cut
	 * off by a hip, and the skin simply stops there.
	 */
	struct TileSkinColumn
	{
		std::vector<Vector3> Points;

		/** Which section sample this column carries. */
		int32_t Sample = 0;

		/** Which course, so the skin can vary its colour from one course to the next. */
		int32_t Course = 0;

		/** Course pitch in world units, so the section can be scaled per face. */
		float Pitch = 0.0f;

		/** Batten height above the boarding, keeping the skin clear of it. */
		float Lift = 0.0f;
	};

	/** Whether a band of courses closes on itself, as a full ring loft does. */
	enum class ETileSkinLoop
	{
		Open,
		Closed,
	};

	/**
	 * Which ends of the columns terminate at an eave, and so want dressing with 瓦当 and 滴水.
	 *
	 * Columns run eave-first by convention, so AtStart is the usual answer. 卷棚 is the exception:
	 * one profile runs from eave to eave over the roll, so both ends are eaves.
	 */
	enum class ETileEaves
	{
		/** Neither end — a tier whose courses die into a ridge, or a face clipped at a hip. */
		None,
		AtStart,
		AtBothEnds,
	};

	/**
	 * 瓦作 detail level (30_瓦作 §2). Cumulative, and 0 reproduces the legacy mesh byte for byte.
	 */
	enum ETileDetail
	{
		/** The legacy skin: one displaced sheet, world-equal courses, no lap, plain eave pieces. */
		TILE_DETAIL_LEGACY = 0,
		/** 排垄与叠压: metric course layout (R1(d)/R7/R8 甲) + longitudinal lap (R8 乙/R21). */
		TILE_DETAIL_COURSES = 1,
		/** The above plus 檐口件 (R4/R5) and the 泥背 layer (R17). */
		TILE_DETAIL_EAVE = 2,
	};

	/**
	 * The settings the skin needs beyond the columns themselves.
	 *
	 * Only Detail and BeddingThickness are parameters; the four tessellation numbers are the design
	 * camera the 瓦当 disc is sized against (review §12.3: f_px = H_px / (2 tan(θ/2))), kept here
	 * rather than derived so the登记 stays in one place. They default to the dwelling screenshot
	 * rig (720p at 42° from 8 m), which is where the 瓦当 is actually judged.
	 */
	struct TileSkinSettings
	{
		int32_t Detail = TILE_DETAIL_LEGACY;
		/** 泥背层厚 (R17/card T3). 0 keeps the legacy skin sitting straight on the boarding. */
		float BeddingThickness = 0.0f;
		/** Distance tier . 0 = 近景, and the only tier the legacy mesh is defined at. */
		int32_t LodLevel = 0;

		/** Chord-error budget for the 瓦当 disc, in screen pixels. */
		float WadangChordPixels = 0.5f;
		/** Distance the disc is sized against. */
		float DesignViewDistance = 8.0f;
		float DesignViewportHeight = 720.0f;
		float DesignVerticalFov = 42.0f;
	};

	/**
	 * Half the across-slope footprint of a swept ridge body, from its contour scale.
	 *
	 * 脊饰's contour is `(-0.5 .. +0.5) * Scale` across the sweep, so this is the reach of the
	 * ridge's own cover band over the roof face it sits on — the first input of R14.1's envelope.
	 */
	float RidgeFootHalfWidth(float ContourScale);

	/**
	 * How far the tile surface stands off the boarding on a face (R17 height chain).
	 *
	 * 望板 → 灰泥 → 瓦, so the weathering surface is the boarding offset along its own normal by the
	 * batten lift *plus* the 泥背. Everything that bears on the tiles — the ridges, the gable
	 * members, the 宝顶 — is placed against this, not against the boarding, or it sinks into the
	 * tiles by exactly the thickness it ignores (30_瓦作 §4 步骤 4b).
	 *
	 * Returns 0 when the tier does not build a bedded skin at all, so a caller may feed it straight
	 * into its knots without a branch of its own.
	 */
	float TileSurfaceLift(float Pitch, const TileSkinSettings& Settings);

	/**
	 * First fundamental form of the tile patch at one station (R1(d), review §4.1).
	 *
	 * Across is ∂P/∂u and UpSlope is ∂P/∂v, both in world units per parameter unit. SPerp is the
	 * only one of the three that is a length scale — `sqrt(E)` and `sqrt(G)` are lengths per their
	 * own parameter, and their product `|Pu||Pv|` is an area, not a spacing (the review's
	 * counterexample: P=(u+v,v,0) gives 1.4142 where the true perpendicular scale is 0.7071).
	 *
	 *   SPerp = sqrt(E - F^2/G)   (requires G > 0 and E*G - F^2 > 0)
	 *
	 * F^2/G is invariant under reparameterising v, so the caller may measure UpSlope over any
	 * consistent step.
	 */
	struct TileMetric
	{
		float E = 0.0f;
		float F = 0.0f;
		float G = 0.0f;
		float SPerp = 0.0f;
		bool bValid = false;
	};

	/** The first fundamental form of a patch whose two tangents are known. */
	TileMetric MeasureTileMetric(const Vector3& Across, const Vector3& UpSlope);

	/** The skin settings a building spec asks for. */
	TileSkinSettings TileSkinSettingsFor(const BuildingSpec& Spec);

	/**
	 * Course layout inputs for the metric path.
	 *
	 * Origin and Covered are in the patch's own across-slope coordinate: the band is the part of
	 * the face the courses have to cover, already net of the ridge cover band, the 博风 edge and the
	 * Cr-bare verge (R8 甲 1). NominalPitch is the style's标称 input (TileCourseWidth, still the
	 * legacy 0.34 m while 定标 is open); MinPitch/MaxPitch are the admissible window for the
	 * effective coverage module p = L / N (R7/R8 甲 3).
	 */
	struct TileCourseLayout
	{
		/** Where the face's definition domain starts, in the across-slope coordinate. */
		float Origin = 0.0f;
		/** How much of the face the courses have to cover, domain width included. */
		float Covered = 0.0f;
		float NominalPitch = 0.0f;
		float MinPitch = 0.0f;
		float MaxPitch = 0.0f;

		/** Distance tier . 0 lays every section sample the way the legacy band did. */
		int32_t LodLevel = 0;

		/**
		 * 包络 (R14.1 / review §8) — how far the boundary member's own cover reaches in from each end
		 * of the domain, in world units: the 垂脊 or 博风板 at a gable verge, the 戗脊 at a hip
		 * corner. Both 0 means "no envelope", and then the whole domain is tiled, which is what the
		 * legacy layout did.
		 *
		 * The roof only ever declares this — it does not cut the courses itself. Cutting is this
		 * file's job and only this file's (30_瓦作 R14.1, 05 v2 §3.2).
		 */
		float NearCover = 0.0f;
		float FarCover = 0.0f;

		/**
		 * How far the outermost *piece* of a course reaches outward past its own column line, in
		 * world units — the lateral half-extent of a 滴水 skirt or a 瓦当 disc, whichever is wider.
		 *
		 * The band is pulled in by this much at each end, so the cut lands where the outermost piece
		 * just reaches the verge: no tile — skin, 瓦当 or 滴水 — crosses the roof's own outline
		 * (判据 C: 无伸出轮廓的半件), and the whole of that piece is still inside the boundary
		 * member's cover, which is the Lap the ridge holds the course by (R14.5/T1).
		 *
		 * It is deliberately a function of the *capability* tier and not of the distance tier: a LOD
		 * may change how finely the band is tessellated, never where it starts or how many courses
		 * cover it (review §8).
		 */
		float NearReach = 0.0f;
		float FarReach = 0.0f;
	};

	/** True when the band has a boundary envelope worth cutting against. */
	inline bool HasCoverEnvelope(const TileCourseLayout& Layout)
	{
		return (Layout.NearCover > 0.0f) || (Layout.FarCover > 0.0f);
	}

	/**
	 * How far the widest eave piece reaches sideways from its own column, in world units
	 * (30_瓦作 R5/判据 C).
	 *
	 * 瓦当 is a disc of radius 0.185 p, the 如意 滴水 is a skirt of half-width 0.42 p, and the
	 * legacy tongue was 0.34 p — so the apron, not the disc, is what sets the band's inset. [自定]
	 * shapes, so the reach follows from them rather than being chosen:
	 * this is the only one of the three that decides whether a piece crosses the roof outline.
	 */
	float TileEdgeReach(float Pitch, const TileSkinSettings& Settings);

	/**
	 * 包络 (30_瓦作 R14.1 / review §8) — the course band for one roof face.
	 *
	 * The roof layer only *declares* this: the face's definition domain, and how far the boundary
	 * members' cover reaches in from each end of it (the 垂脊 contour's own foot, the 博风板's edge,
	 * the 正脊 cover band). Cutting the courses is TileLayout's job and nobody else's, so the
	 * declaration goes in here and the cut comes back out of LayTileCourses.
	 *
	 * Below `TILE_DETAIL_COURSES` the band is the legacy one, band for band, so a legacy resource
	 * bakes unchanged.
	 */
	TileCourseLayout TileBandFor(
		const BuildingSpec& Spec,
		float DomainLow,
		float DomainHigh,
		float Pitch,
		int32_t Courses,
		float NearBoundaryScale,
		float FarBoundaryScale);

	/**
	 * 泥背高度链 (30_瓦作 R17, §4 步骤 4b).
	 *
	 * The layer order is 望板 → 灰泥（掺亚麻丝）→ 瓦, so the weathering surface is the boarding offset
	 * along its own normal. Every member that bears on the tiles — the ridges, the gable members,
	 * the 宝顶 — has to be placed against that surface, not against the boarding: ignore it and the
	 * ridge sinks into the tiles by exactly the thickness it ignores, and over-apply it and the
	 * ridge floats above them.
	 *
	 * This is only the 泥背 *delta*. The skin's own batten lift was already part of the arrangement
	 * these knots were tuned against, so folding it in here would move every existing roof. The
	 * delta is 0 whenever the tier does not build a bedded skin at all, which is what keeps the
	 * default bake byte for byte what it was.
	 */
	float RoofBeddingLift(const BuildingSpec& Spec);

	/**
	 * Emits the scalloped skin over a set of columns, plus the eave tiles that terminate it.
	 *
	 * 瓦当 (the round cap on each 筒瓦) and 滴水 (the tongue hanging from each 板瓦 channel) are
	 * generated here rather than by the callers because this is the only place that knows where the
	 * barrels and channels ended up after displacement, and in what frame. They are also the
	 * cheapest detail on the roof by a wide margin — one per course, not one per course per rafter
	 * — while sitting on the one edge of the roof a player actually sees from the ground.
	 */
	void BuildTileSkin(
		const std::vector<TileSkinColumn>& Columns,
		ETileSkinLoop Loop,
		ETileEaves Eaves,
		const TileSkinSettings& Settings,
		const Color& Tint,
		MeshAccumulator& Mesh);

	/**
	 * Fills Columns with one entry per emitted section sample per course, positioned by PlaceColumn.
	 *
	 * PlaceColumn receives the across-slope coordinate in world units and returns the column's points
	 * along the slope, or fewer than two to drop it. Dropped columns are still appended, empty, so
	 * that column index stays a fixed function of course and sample — BuildTileSkin just emits no
	 * quads for them.
	 *
	 * Detail 0 runs the legacy loop verbatim over `[Origin, Origin + Pitch * (Courses - 0.28)]`, the
	 * band TileBandFor hands back when there is no envelope: it starts on a channel bottom and ends on
	 * a pan edge, so on a closed ring it closes exactly and on an open slope it leaves a fraction of a
	 * pitch bare at the far verge. Detail 1 or 2 replaces it with the metric layout: the band's real
	 * arc length is integrated over the patch (R1(d)), an integer course count is solved against it
	 * (R7/R8 甲), and the sections are placed at equal *arc* spacing with p = L / N — after the
	 * boundary envelope has cut the band (R14.1). Columns come out in the same course-major,
	 * sample-minor order either way.
	 *
	 * The band is already cut to `[Origin + NearReach, Origin + Covered - FarReach]` inside this
	 * function; callers hand in the definition domain, not a pre-cut band (05 v2 §3.2: TileLayout is
	 * the only thing that cuts a tile course).
	 */
	void LayTileCourses(
		int32_t Detail,
		const TileCourseLayout& Layout,
		int32_t Courses,
		const std::function<std::vector<Vector3>(float)>& PlaceColumn,
		std::vector<TileSkinColumn>& OutColumns);

	/**
	 * The legacy course layout, kept as its own entry point so the byte-compatible path is one
	 * function with no branch inside it.
	 *
	 * LodLevel only narrows the section there: 0 lays all SAMPLE_COUNT samples exactly as it
	 * always has, so every earlier resource still bakes to the same mesh.
	 */
	void LayTileCoursesLegacy(
		float Origin,
		float Pitch,
		int32_t Courses,
		const std::function<std::vector<Vector3>(float)>& PlaceColumn,
		std::vector<TileSkinColumn>& OutColumns,
		int32_t LodLevel = 0);
} // namespace BuildingGen
