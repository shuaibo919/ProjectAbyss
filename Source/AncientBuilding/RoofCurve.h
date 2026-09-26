#pragma once

// The continuous 举架 roof curve of ExecutionPlan v2 P1.2.
//
// The legacy profile (BuildingBuilder::BuildRoofProfile) is a polyline: the rise ratio lerps
// from the eave value `a` to the ridge value `b` over `rafter_courses` segments, each segment's
// height scaled so the whole run lands on RoofHeight. The continuous mode replaces that polyline
// with the exact integral of the same linear rise-ratio ramp:
//
//     t in [0,1], eave at 0, ridge at 1
//     x(t) = HalfSpan * (1-t)
//     y(t) = Rise * [ a*t + (b-a)*t^2/2 ] / [ (a+b)/2 ]
//
// The denominator is the mean rise ratio, which is exactly what the legacy code's per-course
// normalisation converges to, so the curve passes through every legacy node when b != a — the
// nodes gain curve between them instead of chords. The endpoints are identical by construction:
// (HalfSpan, 0) at the eave and (0, Rise) at the ridge.
//
// RoofCurveMode 0 (Legacy) keeps the old polyline bit-for-bit; mode 1 (Continuous) routes the
// profile through SampleRoofCurve. Callers of the profile only switch their *shading normals*
// and tile-coverage cutoff when mode 1 is active, so a legacy resource bakes to the same mesh as
// before this file existed.

#include "AncientBuilding/BuildingBuilder.h"

#include <cstdint>
#include <vector>

namespace BuildingGen
{
	/** Curve point at parameter T: (distance from the ridge centreline, height above the roof base). */
	Vector2 RoofCurvePoint(const BuildingSpec& Spec, float HalfSpan, float Rise, float T);
	/** Unit tangent at T in (span, up) axes. */
	Vector2 RoofCurveTangent(const BuildingSpec& Spec, float HalfSpan, float Rise, float T);
	/**
	 * Unit outward normal at T in (span, up) axes: up and away from the ridge. Callers tilt the
	 * span component into their face direction (slope sign or ring azimuth).
	 */
	Vector2 RoofCurveNormal(const BuildingSpec& Spec, float HalfSpan, float Rise, float T);
	/** RoofCurveNormal at a span coordinate rather than a parameter. T clamps to [0,1]. */
	Vector2 RoofCurveNormalAtX(const BuildingSpec& Spec, float HalfSpan, float Rise, float X);
	/**
	 * Adaptive polyline sampling of the curve, eave to ridge. Subdivision criteria are the
	 * spec's roof_chord_error (checked at the 1/4, 1/2 and 3/4 points of each candidate
	 * segment, so an S-bend cannot slip past the midpoint) and roof_max_segment; recursion is
	 * capped at depth 8. When the cap rejects a segment that still violates the chord error a
	 * one-time warning is printed — the result then does not claim to satisfy the tolerance.
	 */
	std::vector<Vector2> SampleRoofCurve(const BuildingSpec& Spec, float HalfSpan, float Rise);
	/**
	 * First profile index kept by the tile skin for the spec's tile_coverage.
	 *
	 * Legacy mode keeps the old index formula so old resources bake identically. Continuous
	 * mode keeps every sample whose span coordinate is <= HalfSpan * coverage, so coverage is a
	 * fixed curve parameter that no longer depends on how many samples the curve resolved to.
	 * Profile.x must run from HalfSpan (eave) down to 0 (ridge); pass HalfSpan = 1 for the
	 * centralised profile whose x is already a radius fraction.
	 */
	size_t RoofCoverageStart(const BuildingSpec& Spec, const std::vector<Vector2>& Profile, float HalfSpan);
	/**
	 * The exact point where the coverage boundary crosses the profile, interpolated between the
	 * two samples straddling it (continuous mode only). Tile-skin columns start with this point
	 * so the bare-roof edge stays put when the sampling density changes. Returns false when
	 * there is no boundary to insert (full/zero coverage, legacy mode, or the boundary lands on
	 * a sample).
	 */
	bool RoofCoverageBoundary(
		const BuildingSpec& Spec, const std::vector<Vector2>& Profile, float HalfSpan,
		size_t Start, Vector2& OutBoundary);
} // namespace BuildingGen
