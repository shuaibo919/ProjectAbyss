#include "AncientBuilding/TileSkin.h"

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

// ==================== Skin ====================

namespace
{
	/** A column's points after displacement, with the smooth normal and frame at each. */
	struct ResolvedColumn
	{
		std::vector<Vector3> Points;
		std::vector<Vector3> Normals;
		/** Outward surface normal, kept so the eave tiles can be oriented. */
		std::vector<Vector3> Outward;
		/** Direction the column runs, pointing away from the eave. */
		std::vector<Vector3> UpSlope;
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
		bool bWrap)
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
		const float Rise = Column.Lift + Column.Pitch * TileSection::HeightAt(Column.Sample);

		Out.Points.reserve(Count);
		Out.Normals.reserve(Count);
		Out.Outward.reserve(Count);
		Out.UpSlope.reserve(Count);

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

			const Vector3 AcrossUnit = Across.normalized();
			const Vector3 AlongUnit = Along.length_squared() > 1e-12f
				? Along.normalized()
				: Across.cross(Outward).normalized();

			Out.Points.push_back(Column.Points[Point] + Outward * Rise);
			Out.Normals.push_back((AcrossUnit * Section.x + Outward * Section.y).normalized());
			Out.Outward.push_back(Outward);
			Out.UpSlope.push_back(AlongUnit);
		}

		return Out;
	}

	/**
	 * 瓦当 (勾头): the round medallion capping a 筒瓦 where it reaches the eave.
	 *
	 * Built as a short capped cylinder swept along the eave's outward direction, because BuildSweep
	 * orients its contour from the spine and so handles an arbitrary axis — which MeshAccumulator's
	 * vertical-axis AddColumn cannot. Only the outer end is capped; the other is inside the barrel.
	 */
	void AddBarrelEnd(
		const Vector3& CrownPoint,
		const Vector3& Outward,
		const Vector3& UpSlope,
		float Pitch,
		const Color& Tint,
		MeshAccumulator& Mesh)
	{
		// The barrel's axis sits a rise below its crown, which is where the medallion centres.
		const Vector3 Axis = CrownPoint - Outward * (BARREL_RISE * Pitch);
		const float Radius = Pitch * 0.185f;

		std::vector<Vector3> Spine;
		Spine.push_back(Axis + UpSlope * (Pitch * 0.10f));
		Spine.push_back(Axis - UpSlope * (Pitch * 0.16f));

		std::vector<Vector2> Contour;
		const int32_t Sides = 7;
		for (int32_t Index = 0; Index < Sides; ++Index)
		{
			const float Angle = SKIN_PI * 2.0f * float(Index) / float(Sides);
			Contour.push_back(Vector2(std::cos(Angle) * Radius, std::sin(Angle) * Radius));
		}

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
	 * 滴水: the tongue hanging from a 板瓦 channel at the eave, which is what turns the raw
	 * sawtooth left by the section into the scalloped drip line a Chinese roof is recognised by.
	 *
	 * The spine has three knots, not two: the tile continues the roof plane past the eave and then
	 * its outer edge turns down. Without that bend it is only visible from directly outside and
	 * reads as a dark notch between the 瓦当 rather than as a tile.
	 */
	void AddChannelEnd(
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
} // namespace

void BuildingGen::BuildTileSkin(
	const std::vector<TileSkinColumn>& Columns,
	ETileSkinLoop Loop,
	ETileEaves Eaves,
	const Color& Tint,
	MeshAccumulator& Mesh)
{
	if (Columns.size() < 2)
	{
		return;
	}

	const bool bWrap = Loop == ETileSkinLoop::Closed;

	std::vector<ResolvedColumn> Resolved;
	Resolved.reserve(Columns.size());
	for (size_t Index = 0; Index < Columns.size(); ++Index)
	{
		Resolved.push_back(ResolveColumn(Columns, Index, bWrap));
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

	if (Eaves == ETileEaves::None)
	{
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
			AddBarrelEnd(Line.Points[0], Line.Outward[0], Line.UpSlope[0], Column.Pitch, Shade, Mesh);
		}
		else
		{
			AddChannelEnd(Line.Points[0], Line.Outward[0], Line.UpSlope[0], Column.Pitch, Shade, Mesh);
		}

		if (Eaves != ETileEaves::AtBothEnds)
		{
			continue;
		}

		if (bCrown)
		{
			AddBarrelEnd(
				Line.Points[Last], Line.Outward[Last], -Line.UpSlope[Last], Column.Pitch, Shade, Mesh);
		}
		else
		{
			AddChannelEnd(
				Line.Points[Last], Line.Outward[Last], -Line.UpSlope[Last], Column.Pitch, Shade, Mesh);
		}
	}
}
