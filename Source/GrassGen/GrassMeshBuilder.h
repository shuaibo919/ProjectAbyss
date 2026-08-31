#pragma once

// CPU generator for a single grass "clump" — a handful of blades scattered within a
// small radius, the unit a PCG scatter graph places many of across terrain (the same
// contract ProceduralRock/ProceduralTree already export via bake_mesh() into
// Game/Script/PCG's spawn_meshes node).
//
// Original geometric design (no ported reference, unlike RockGen/TreeGen): each blade
// is a curved, tapered "V-fold" ribbon — two half-width panels meeting at a hard-crease
// spine, the standard real-time-grass trick that reads correctly from most viewing
// angles without doubling geometry. Four species (see EGrassSpecies) pick different
// height/count/curvature ranges and a distinguishing feature: a drooping flag tip
// (thatch), a fluffy seed spike (foxtail), a plain blade (short lawn grass), or a leaf
// bulge / tiny flower fan (weeds). See GrassMeshBuilder.cpp for the per-species profile.

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <vector>

namespace GrassGen
{
	using godot::Color;
	using godot::Vector2;
	using godot::Vector3;

	enum EGrassSpecies
	{
		/** 茅草 — tall, sparse, stiff reed grass; some blades droop a narrow flag tip. */
		GRASS_SPECIES_THATCH = 0,
		/** 狗尾巴草 — medium height, topped with a tapered fluffy seed spike. */
		GRASS_SPECIES_FOXTAIL = 1,
		/** 小草 — short, dense, plain lawn/turf grass. */
		GRASS_SPECIES_SHORT = 2,
		/** 杂草 — irregular heights; some blades widen mid-length or flower at the tip. */
		GRASS_SPECIES_WEED = 3,
	};

	/** Plain snapshot of ProceduralGrassParameters. */
	struct GrassSpec
	{
		EGrassSpecies Species = GRASS_SPECIES_SHORT;
		/** Seeds the deterministic per-blade scatter and shape jitter. */
		float Seed = 1.0f;
		/** Uniform size multiplier applied to the whole clump at the end. */
		float Scale = 1.0f;
		/** Footprint the blade origins are scattered within (metres, before Scale). */
		float ClumpRadius = 0.25f;
		/** Blades in the clump; 0 = pick a species-appropriate random count. */
		int32_t BladeCount = 0;
		/** Multiplies how much each blade bows beyond its lean (species baseline * this). */
		float Curvature = 1.0f;
		/** Degrees the whole clump leans toward LeanAzimuth (e.g. a wind gust), on top of
		 *  each blade's own small random lean. */
		float LeanAngle = 0.0f;
		float LeanAzimuth = 0.0f;
		/** Extra per-blade hue/brightness jitter; 0 = every blade uses the exact gradient. */
		float ColorVariance = 0.15f;
		/** Used verbatim when bUseSpeciesColors is false; otherwise each species' own
		 *  built-in palette is used and these are ignored. */
		Color BaseColor = Color(0.20f, 0.42f, 0.16f);
		Color TipColor = Color(0.55f, 0.72f, 0.22f);
		bool bUseSpeciesColors = true;
	};

	/** Growable mesh, one vertex-coloured surface (the same contract as RockGen). */
	struct MeshAccumulator
	{
		std::vector<Vector3> Vertices;
		std::vector<Vector3> Normals;
		std::vector<Vector2> UVs;
		std::vector<Color> Colors;
		std::vector<int32_t> Indices;

		int32_t GetTriangleCount() const { return int32_t(Indices.size() / 3); }
	};

	void BuildGrass(const GrassSpec& Spec, MeshAccumulator& OutMesh);
} // namespace GrassGen
