#pragma once

// 砖石作: arched masonry for 城台 / 墩台, 券门 and 券窗 in brick walls, water gates and arch
// bridges — the parts of a Chinese town that are not timber frame.
//
// One primitive covers all of them: a slab (length × thickness × height, optionally battered
// = 收分) pierced through its thickness by arched openings. A 城台 is a thick slab whose
// openings are the gate passages; a brick wall is a thin one whose openings are doors and
// windows; a bridge is a slab whose "thickness" is the deck width and whose openings the water
// runs through.
//
// No CSG. Each face is decomposed the way a mason sets it out: solid piers between the
// openings, a strip under each sill, and a spandrel over each arch from the intrados up to
// the top. The intrados itself is a strip of quads across the thickness. The 券脸 ring —
// the dressed voussoir band round each arch — is the paper's Eq 1 sweep along the arch curve,
// which is exactly the kind of curve the miter sweep was written for.

#include "AncientBuilding/BuildingBuilder.h"

#include <cstdint>
#include <vector>

namespace BuildingGen
{
	/** 券 shape. */
	enum class EArchProfile : int32_t
	{
		/** 半圆券: a half ellipse of the given rise over the opening width. */
		Semicircle = 0,
		/** 双心券 (尖券): two circular arcs meeting at a point on the centre line. */
		Pointed = 1,
		/** 平券: a flat-topped opening under a lintel course. */
		Flat = 2,
	};

	/** Crenellation along the top edge of a slab. */
	enum class EParapet : int32_t
	{
		None = 0,
		/** 垛口: merlons along the outer edge. */
		Crenel = 1,
		/** 宇墙: a low plain parapet wall. */
		Plain = 2,
	};

	struct ArchOpening
	{
		/** Centre along the slab's length, from the slab centre. */
		float Centre = 0.0f;
		float Width = 3.0f;
		/** Bottom of the opening: 0 for a door or passage, above 0 for a window. */
		float Sill = 0.0f;
		/** Height where the straight jambs end and the arch begins. */
		float Spring = 2.0f;
		/** Height of the arch above the spring line (the lintel course height for 平券). */
		float Rise = 1.5f;
		EArchProfile Profile = EArchProfile::Semicircle;
	};

	struct ArchedSlabDesc
	{
		/** Centre of the slab's footprint, at its foot. */
		Vector3 Origin;
		/** Unit direction along the slab's length (horizontal). */
		Vector3 AxisU = Vector3(1, 0, 0);
		/** Unit direction through its thickness (horizontal, perpendicular to AxisU). */
		Vector3 AxisW = Vector3(0, 0, 1);
		/** Half extents at the foot. */
		float HalfLength = 5.0f;
		float HalfThickness = 1.0f;
		float Height = 6.0f;
		/** 收分: horizontal draw-in per metre of height, on the two ends and the two faces. */
		float BatterEnds = 0.0f;
		float BatterFaces = 0.0f;
		std::vector<ArchOpening> Openings;
		/** Leave the top open (a wall whose coping is someone else's), or cap it. */
		bool bTopCap = true;
		EParapet Parapet = EParapet::None;
		/** 券脸 ring: radial thickness and how far it stands proud of the face. 0 = no ring. */
		float RingThickness = 0.0f;
		float RingProjection = 0.0f;

		Color Tint;
		Color RingTint;
		/** Darker band at the foot (石基 / 下碱), as a height. 0 = none. */
		float PlinthHeight = 0.0f;
		Color PlinthTint;
	};

	/** Appends the slab to the mesh on the Stone slot (the plinth and ring included). */
	void AddArchedSlab(MeshAccumulator& Mesh, const ArchedSlabDesc& Desc);

	/** The 城台 a building with BaseKind 1 stands on, from its spec. Rectangular or polygonal plan. */
	void BuildMasonryTerrace(const BuildingSpec& Spec, MeshAccumulator& Mesh);

	/**
	 * Samples the intrados of one opening as (along, height) points from the left spring to the
	 * right spring (for 平券, the two top corners).
	 */
	std::vector<Vector2> ArchCurve(const ArchOpening& Opening, int32_t Segments);
} // namespace BuildingGen
