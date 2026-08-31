#pragma once

// Parameters of the procedural grass-clump generator (Source/GrassGen/GrassMeshBuilder.h).
// Mirrors ProceduralRockParameters' shape: a species enum picks a code path in the
// builder, while these knobs modulate every species proportionally.

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/color.hpp>

#include <cstdint>

/** Getter/setter pair that notifies listeners, e.g. the owning ProceduralGrass. */
#define GRASS_ACCESSORS(Type, Member)           \
	void Set##Member(Type Value)                \
	{                                           \
		Member = Value;                         \
		emit_changed();                         \
	}                                           \
	Type Get##Member() const                    \
	{                                           \
		return Member;                          \
	}

namespace godot
{
	class ProceduralGrassParameters : public Resource
	{
		GDCLASS(ProceduralGrassParameters, Resource)

	public:
		enum EGrassSpecies
		{
			/** 茅草 — tall, sparse, stiff reed grass; some blades droop a flag tip. */
			SPECIES_THATCH = 0,
			/** 狗尾巴草 — medium height, topped with a fluffy seed spike. */
			SPECIES_FOXTAIL = 1,
			/** 小草 — short, dense, plain lawn/turf grass. */
			SPECIES_SHORT = 2,
			/** 杂草 — irregular heights; some blades bulge mid-length or flower. */
			SPECIES_WEED = 3,
		};

	private:
		// ---- Species ----
		int32_t Species = SPECIES_SHORT;
		/** Seeds the deterministic per-blade scatter and shape jitter. */
		float Seed = 1.0f;

		// ---- Shape ----
		/** Uniform size multiplier applied to the whole clump. */
		float Scale = 1.0f;
		/** Footprint the blade origins are scattered within (metres, before Scale). */
		float ClumpRadius = 0.25f;
		/** Blades in the clump; 0 = pick a species-appropriate random count. */
		int32_t BladeCount = 0;
		/** Multiplies how much each blade bows beyond its lean. */
		float Curvature = 1.0f;
		/** Degrees the whole clump leans toward LeanAzimuth (e.g. a wind gust). */
		float LeanAngle = 0.0f;
		float LeanAzimuth = 0.0f;

		// ---- Color ----
		/** Extra per-blade hue/brightness jitter; 0 = every blade uses the exact gradient. */
		float ColorVariance = 0.15f;
		/** When true (the default), each species uses its own built-in palette and
		 *  BaseColor/TipColor below are ignored — switching species then looks right
		 *  without any extra clicks. Turn off to use BaseColor/TipColor verbatim. */
		bool bUseSpeciesColors = true;
		Color BaseColor = Color(0.20f, 0.42f, 0.16f);
		Color TipColor = Color(0.55f, 0.72f, 0.22f);

	protected:
		static void _bind_methods();

	public:
		GRASS_ACCESSORS(int32_t, Species)
		GRASS_ACCESSORS(float, Seed)
		GRASS_ACCESSORS(float, Scale)
		GRASS_ACCESSORS(float, ClumpRadius)
		GRASS_ACCESSORS(int32_t, BladeCount)
		GRASS_ACCESSORS(float, Curvature)
		GRASS_ACCESSORS(float, LeanAngle)
		GRASS_ACCESSORS(float, LeanAzimuth)
		GRASS_ACCESSORS(float, ColorVariance)

		void SetUseSpeciesColors(bool bValue)
		{
			bUseSpeciesColors = bValue;
			emit_changed();
		}

		bool ShouldUseSpeciesColors() const
		{
			return bUseSpeciesColors;
		}

		GRASS_ACCESSORS(Color, BaseColor)
		GRASS_ACCESSORS(Color, TipColor)

		static String GetSpeciesName(int32_t Species);
		static String GetSpeciesNameLocalized(int32_t Species);
	};
} // namespace godot

VARIANT_ENUM_CAST(ProceduralGrassParameters::EGrassSpecies);
