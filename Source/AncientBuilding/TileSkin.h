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

#include "AncientBuilding/BuildingBuilder.h"

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
		const Color& Tint,
		MeshAccumulator& Mesh);

	/**
	 * Fills Columns with SAMPLE_COUNT entries per course, positioned by PlaceColumn.
	 *
	 * PlaceColumn receives the across-slope offset in world units and returns the column's points
	 * along the slope, or fewer than two to drop it. Dropped columns are still appended, empty, so
	 * that column index stays a fixed function of course and sample — BuildTileSkin just emits no
	 * quads for them.
	 *
	 * Note the band this covers is [Origin, Origin + Pitch * (Courses - 0.28)]: it starts on a
	 * channel bottom and ends on a pan edge. On a closed ring that closes up exactly; on an open
	 * slope it leaves a fraction of a pitch bare at the far verge, which the 垂脊 covers.
	 */
	template <typename TPlaceColumn>
	void LayTileCourses(
		float Origin,
		float Pitch,
		int32_t Courses,
		const TPlaceColumn& PlaceColumn,
		std::vector<TileSkinColumn>& OutColumns)
	{
		OutColumns.reserve(OutColumns.size() + size_t(Courses * TileSection::SAMPLE_COUNT));

		for (int32_t Course = 0; Course < Courses; ++Course)
		{
			const float Centre = Origin + Pitch * (float(Course) + 0.5f);

			for (int32_t Sample = 0; Sample < TileSection::SAMPLE_COUNT; ++Sample)
			{
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
} // namespace BuildingGen
