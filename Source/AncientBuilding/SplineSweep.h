#pragma once

// Spline mesh generation from Hu & Qin 2020, section 3.1 (see Docs/AncientBuilding_Spec.md).
//
// The paper's contribution is easy to mis-read as "sweep a contour along a spline". It is
// not. The contour is placed once, on the plane of the first knot, and is never
// re-oriented: every contour vertex then travels in a straight line along the *segment*
// direction and is stopped by the plane perpendicular to the *next knot's* tangent. That is
// a miter joint — the same construction as offsetting a polyline — and it is why acute
// corners no longer pinch.
//
// A consequence worth knowing: cross-section area is deliberately not preserved through a
// bend, so the outside of a corner widens. That is what real tiled ridges do, and it is
// exactly what the naive re-orienting sweep gets wrong.

#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <vector>

namespace BuildingGen
{
	using godot::Vector2;
	using godot::Vector3;

	enum class ESweepMode
	{
		/** Paper equation 1: straight-line propagation stopped by the next knot's miter plane. */
		Miter,
		/**
		 * The prior art the paper replaces: re-place the contour in each knot's own frame.
		 * Kept because it is the only way to demonstrate that the miter actually fixes
		 * anything — see Fig 5a/5b versus 5c.
		 */
		Frame,
	};

	struct SweepSettings
	{
		/** Section profile in the (normal, bitangent) plane of the first knot. */
		std::vector<Vector2> Contour;
		/** True joins the last contour point back to the first, giving a closed tube. */
		bool bClosedContour = true;

		ESweepMode Mode = ESweepMode::Miter;

		/** Equation 2's phi: scales the displacement curve. Zero disables displacement. */
		float DisplacementScale = 0.0f;
		/**
		 * Equation 2's y(x) sampled uniformly over [0, 1]. Fewer than two samples disables
		 * displacement. Sampled by knot index, as in the paper.
		 */
		std::vector<float> DisplacementSamples;

		/**
		 * Equations 3-5's delta, in degrees. At the default 180 the constraint reduces to
		 * plain equation 2 (every vertex displaced, radially). At 90 or below only the
		 * vertices facing the bitangent are displaced, and they move along it — which is how
		 * one-sided profiles like steps and ridge tails are built.
		 */
		float ConstraintAngleDegrees = 180.0f;

		/** Seeds the bitangent, then parallel-transported along the spline to avoid twist. */
		Vector3 UpReference = Vector3(0, 1, 0);

		/** Close the two ends. Only meaningful for a closed contour. */
		bool bGenerateCaps = true;
	};

	struct SweepResult
	{
		std::vector<Vector3> Vertices;
		std::vector<Vector3> Normals;
		std::vector<Vector2> UVs;
		std::vector<int32_t> Indices;

		/** Joints where the miter plane was near-parallel to the segment and had to be clamped. */
		int32_t DegenerateJointCount = 0;
		/**
		 * Largest ratio between a propagated step and its segment length. 1 means no corner
		 * widening; large values mean a corner is sharp enough that it should be subdivided.
		 */
		float MaxMiterStretch = 1.0f;
	};

	/**
	 * Builds the swept mesh. Returns false when there is nothing to build (fewer than two
	 * knots or contour points).
	 *
	 * Vertices are duplicated per contour edge, so profile corners stay hard while the sweep
	 * direction stays smooth — the correct shading for an extrusion.
	 */
	bool BuildSweep(const std::vector<Vector3>& Knots, const SweepSettings& Settings, SweepResult& OutResult);
} // namespace BuildingGen
