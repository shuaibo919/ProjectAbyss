#pragma once

// The leaf silhouette generator, shared by both tree backends.
//
// It was file-local in TreeMeshBuilder.cpp, where only the Weber-Penn builder could reach it. But
// SlowTree needs exactly the same thing: its LeafCluster and Frond nodes carry a "Mesh Cutout"
// slot (cutoutPoints / cutoutTris, SpeedTree's term) that replaces the flat quad card with a
// triangulated outline, and nothing was filling it. Untextured quad cards read as opaque
// rectangles, which is the single most visible flaw in the SlowTree presets — worst on blossom,
// where the cards are large and face the camera.
//
// The dependency on the full Weber-Penn LeafParams turned out to be four scalars, so the shape is
// its own tiny struct and neither backend needs the other's parameter types.

#include <godot_cpp/variant/vector2.hpp>

#include <cstdint>
#include <vector>

namespace TreeGen
{
	using godot::Vector2;

	/**
	 * The four scalars the outline actually depends on. Angles are in degrees and describe the
	 * tangent of the blade edge at the base, the widest point, and the tip; SideOffset is how far
	 * out the widest point sits.
	 *
	 * Defaults match ProceduralTreeLeafParameters so an unspecified shape matches TreeGen's leaf.
	 */
	struct LeafOutlineShape
	{
		float BotAngle = -85.0f;
		float MidAngle = 0.0f;
		float TopAngle = 45.0f;
		float SideOffset = 0.45f;
	};

	/**
	 * The nine outline points of one leaf half.
	 *
	 * Indices 0/2/4/6/8 lie on the outline, 1/3/5/7 are quadratic Bezier control points — the GPU
	 * original built them this way so a pixel shader could evaluate the silhouette analytically.
	 */
	void ComputeLeafOutlinePoints(const LeafOutlineShape& Shape, uint32_t Seed, Vector2 OutPoints[9]);

	/**
	 * A closed, triangulated leaf silhouette in [0,1]^2, ready to hand to a SlowTree Mesh Cutout.
	 *
	 * Samples the four Bezier arcs of one half, mirrors it, and fan-triangulates from the base.
	 * The outline is star-shaped about the base point, so a fan is sufficient — but each triangle
	 * is emitted with a consistent winding rather than trusting one reference, the same lesson the
	 * 山花 tympanum taught in AncientBuilding.
	 *
	 * @param ArcSegments  samples per Bezier arc; 2 gives 9 outline points per side, which is
	 *                     plenty for a card a few centimetres across.
	 * @param Aspect       width/height of the target card, so a narrow leaf (willow, needle) is
	 *                     not stretched by the caller after the fact.
	 */
	void BuildLeafCutout(
		const LeafOutlineShape& Shape,
		uint32_t Seed,
		int32_t ArcSegments,
		float Aspect,
		std::vector<Vector2>& OutPoints,
		std::vector<uint32_t>& OutTris);
} // namespace godot
