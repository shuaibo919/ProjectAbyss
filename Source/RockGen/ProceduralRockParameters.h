#pragma once

// Parameters of the marching-cubes rock generator. Defaults are the defaults of the
// Unity reference (Reference/Unity-Procedural-Rock-Generation/), except Resolution,
// which is CPU-sized because the volume is evaluated on the CPU here rather than in a
// compute shader.

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/color.hpp>

#include <cstdint>

/** Getter/setter pair that notifies listeners, e.g. the owning ProceduralRock. */
#define ROCK_ACCESSORS(Type, Member)            \
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
	class ProceduralRockParameters : public Resource
	{
		GDCLASS(ProceduralRockParameters, Resource)

	public:
		enum ERockForm
		{
			/** The reference's unit sphere — a rounded boulder. */
			FORM_SPHERE = 0,
			/** Squashed along Y via Flatness — a pebble or river stone. */
			FORM_ELLIPSOID = 1,
			/** Rounded box with corner radius from Roundness — a slab with worn edges. */
			FORM_ROUNDED_BOX = 2,
		};

	private:
		// ---- Form ----
		int32_t Form = FORM_SPHERE;
		/** Grid samples per axis; the mesh comes from (Resolution-1)^3 cells. */
		int32_t Resolution = 48;
		/** World size of the generated rock. */
		float Scale = 2.5f;
		/** Bump-sphere smooth-min iterations (the reference's Steps). */
		int32_t Steps = 20;
		/** Smooth-min blend radius (the reference's Smoothness). */
		float Smoothness = 0.05f;
		/** Seed of the bump hash chain (the reference's Seed). */
		float Seed = 880.0f;

		// ---- Surface ----
		/** Roughness: triplanar noise amplitude folded into the field. */
		float DisplacementScale = 0.15f;
		/** Roughness frequency (the reference's DisplacementSpread). */
		float DisplacementSpread = 10.0f;
		/** Y squash for ELLIPSOID / Y extent for ROUNDED_BOX, relative to the other axes. */
		float Flatness = 0.7f;
		/** Corner radius of ROUNDED_BOX as a fraction of its half extent. */
		float Roundness = 0.25f;

		// ---- Ground ----
		/** Flattens the bottom: only the volume above GroundCut (unit space) survives. */
		bool bCutGround = false;
		float GroundCut = -0.3f;

		// ---- Colors ----
		Color BaseColor = Color(0.45f, 0.46f, 0.47f);
		Color CreviceColor = Color(0.16f, 0.17f, 0.18f);

	protected:
		static void _bind_methods();

	public:
		ROCK_ACCESSORS(int32_t, Form)
		ROCK_ACCESSORS(int32_t, Resolution)
		ROCK_ACCESSORS(float, Scale)
		ROCK_ACCESSORS(int32_t, Steps)
		ROCK_ACCESSORS(float, Smoothness)
		ROCK_ACCESSORS(float, Seed)
		ROCK_ACCESSORS(float, DisplacementScale)
		ROCK_ACCESSORS(float, DisplacementSpread)
		ROCK_ACCESSORS(float, Flatness)
		ROCK_ACCESSORS(float, Roundness)

		void SetCutGround(bool bValue)
		{
			bCutGround = bValue;
			emit_changed();
		}

		bool ShouldCutGround() const
		{
			return bCutGround;
		}

		ROCK_ACCESSORS(float, GroundCut)
		ROCK_ACCESSORS(Color, BaseColor)
		ROCK_ACCESSORS(Color, CreviceColor)

		static String GetFormName(int32_t Form);
		static String GetFormNameLocalized(int32_t Form);
	};
} // namespace godot

VARIANT_ENUM_CAST(ProceduralRockParameters::ERockForm);
