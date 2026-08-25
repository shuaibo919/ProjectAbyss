#pragma once

// Weber-Penn tree parameters, as used by "Real-Time GPU Tree Generation"
// (Kuth et al., HPG 2025). Per-level arrays from the HLSL sample (float4 / int4)
// are exposed as Vector4 / Vector4i so that the whole model stays inspectable
// without dozens of scalar properties.

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/variant/vector4i.hpp>

/** Generates a getter/setter pair that notifies listeners, e.g. the owning ProceduralTree. */
#define TREEGEN_ACCESSORS(Type, Member)                     \
	void Set##Member(Type Value)                            \
	{                                                       \
		Member = Value;                                     \
		emit_changed();                                     \
	}                                                       \
	Type Get##Member() const                                \
	{                                                       \
		return Member;                                      \
	}

namespace godot
{
	/**
	 * Shape of a single leaf, blossom petal or needle. The sample's per-pixel vein
	 * parameters are omitted because the baked mesh has no leaf shader to consume them.
	 */
	class ProceduralTreeLeafParameters : public Resource
	{
		GDCLASS(ProceduralTreeLeafParameters, Resource)

	private:
		int32_t Count = 100;
		/** Index into ShapeRatio(); scales leaves along the parent stem. */
		int32_t ScaleShape = 3;
		float Scale = 0.2f;
		float ScaleX = 0.5f;
		/** Length of the petiole holding the leaf off the stem surface. */
		float StemLen = 0.5f;
		float BotAngle = -85.0f;
		float MidAngle = 0.0f;
		float TopAngle = 45.0f;
		float SideOffset = 0.45f;
		int32_t Lobes = 1;
		float LobeAngle = 0.0f;
		float LobeFalloff = 0.0f;
		Color LeafColor = Color(0.0f, 0.125f, 0.0f, 1.0f);
		float Translucency = 0.7f;
		float SeasonOffset = 0.0f;
		/**
		 * Cross-blade cupping. Not from the paper: the original perturbed leaf normals
		 * per-pixel from its vein pattern, which a baked mesh cannot do, so the blade is
		 * curved instead. Without it foliage lights like flat cardboard.
		 */
		float Curl = 0.35f;
		/** Per-leaf brightness variation, to break up a uniformly coloured canopy. */
		float ColorJitter = 0.12f;
		/** Per-leaf size variation. */
		float ScaleJitter = 0.2f;
		/**
		 * Blades per needle fascicle. The original carved ~20 hair-thin needles per blade in
		 * the pixel shader; a baked mesh gets its volume from more blades instead.
		 */
		int32_t NeedleBlades = 4;
		/** When false the leaf tip is notched instead of rounded. */
		bool bTopConvex = false;
		bool bIsNeedle = false;
		/** Broadleaf evergreen: keeps its leaves and summer colour through winter. */
		bool bEvergreen = false;

	protected:
		static void _bind_methods();

	public:
		TREEGEN_ACCESSORS(int32_t, Count)
		TREEGEN_ACCESSORS(int32_t, ScaleShape)
		TREEGEN_ACCESSORS(float, Scale)
		TREEGEN_ACCESSORS(float, ScaleX)
		TREEGEN_ACCESSORS(float, StemLen)
		TREEGEN_ACCESSORS(float, BotAngle)
		TREEGEN_ACCESSORS(float, MidAngle)
		TREEGEN_ACCESSORS(float, TopAngle)
		TREEGEN_ACCESSORS(float, SideOffset)
		TREEGEN_ACCESSORS(int32_t, Lobes)
		TREEGEN_ACCESSORS(float, LobeAngle)
		TREEGEN_ACCESSORS(float, LobeFalloff)
		TREEGEN_ACCESSORS(Color, LeafColor)
		TREEGEN_ACCESSORS(float, Translucency)
		TREEGEN_ACCESSORS(float, SeasonOffset)
		TREEGEN_ACCESSORS(float, Curl)
		TREEGEN_ACCESSORS(float, ColorJitter)
		TREEGEN_ACCESSORS(float, ScaleJitter)
		TREEGEN_ACCESSORS(int32_t, NeedleBlades)

		void SetTopConvex(bool bValue)
		{
			bTopConvex = bValue;
			emit_changed();
		}

		bool IsTopConvex() const
		{
			return bTopConvex;
		}

		void SetIsNeedle(bool bValue)
		{
			bIsNeedle = bValue;
			emit_changed();
		}

		bool IsNeedle() const
		{
			return bIsNeedle;
		}

		void SetEvergreen(bool bValue)
		{
			bEvergreen = bValue;
			emit_changed();
		}

		bool IsEvergreen() const
		{
			return bEvergreen;
		}
	};

	/** Fruit shape and ripening. Fruits replace a fraction of the blossoms. */
	class ProceduralTreeFruitParameters : public Resource
	{
		GDCLASS(ProceduralTreeFruitParameters, Resource)

	private:
		/** Probability that a blossom slot becomes a fruit instead. */
		float Chance = 0.0f;
		/** How far the fruit's own weight bends it towards the ground. */
		float DownForce = 1.0f;
		float Size = 0.1f;
		/** Control points of the cubic Bezier profile revolved into the fruit body. */
		Vector4 Shape = Vector4(0.5f, 0.333f, 0.5f, 0.666f);
		Color FruitColor = Color(0.25f, 0.0f, 0.0f, 1.0f);

	protected:
		static void _bind_methods();

	public:
		TREEGEN_ACCESSORS(float, Chance)
		TREEGEN_ACCESSORS(float, DownForce)
		TREEGEN_ACCESSORS(float, Size)
		TREEGEN_ACCESSORS(Vector4, Shape)
		TREEGEN_ACCESSORS(Color, FruitColor)
	};

	/**
	 * A complete tree species. Component `x` of every per-level vector applies to the
	 * trunk, `y` to first-order branches, and so on; this matches the HLSL sample's
	 * `nFoo[level]` indexing exactly.
	 */
	class ProceduralTreeParameters : public Resource
	{
		GDCLASS(ProceduralTreeParameters, Resource)

	public:
		enum EPreset
		{
			PRESET_DEFAULT = 0,
			// The four species shipped with the paper's sample.
			PRESET_APPLE = 1,
			PRESET_SASSAFRAS = 2,
			PRESET_PALM = 3,
			PRESET_TAMARACK = 4,
			// Species common to Chinese landscapes.
			PRESET_GINKGO = 5,
			PRESET_PEACH = 6,
			PRESET_CAMPHOR = 7,
			PRESET_PINE = 8,
			PRESET_CHINESE_FIR = 9,
			PRESET_WILLOW = 10,

			PRESET_COUNT = 11,
		};

	private:
		/** Number of stem levels. The deepest level bears leaves rather than branches. */
		int32_t Levels = 3;
		/** Fraction of each stem kept bare at its base, per level. */
		Vector4 BaseSize = Vector4(0.25f, 0.05f, 0.05f, 0.05f);
		/** Upward bending applied to levels above 1; negative values droop. */
		float AttractionUp = 0.0f;
		float Flare = 0.5f;
		int32_t Lobes = 0;
		float LobeDepth = 0.0f;
		float Scale = 10.0f;
		float ScaleV = 0.0f;
		/** Trunk radius as a fraction of trunk length. */
		float Ratio = 0.05f;
		float RatioPower = 1.0f;

		Vector4i Shape = Vector4i(0, 0, 0, 0);
		Vector4i BaseSplits = Vector4i(1, 0, 0, 0);
		Vector4 SegSplits = Vector4(0, 0, 0, 0);
		Vector4 SegSplitBaseOffset = Vector4(0, 0, 0, 0);
		Vector4 SplitAngle = Vector4(0, 0, 0, 0);
		Vector4 SplitAngleV = Vector4(0, 0, 0, 0);
		Vector4i Branches = Vector4i(1, 10, 5, 0);
		Vector4 Length = Vector4(1.0f, 0.5f, 0.5f, 0.0f);
		Vector4 LengthV = Vector4(0, 0, 0, 0);
		Vector4 Curve = Vector4(0, 0, 0, 0);
		/** Positive values add random curvature; negative values make the stem a helix. */
		Vector4 CurveV = Vector4(0, 0, 0, 0);
		Vector4 CurveBack = Vector4(0, 0, 0, 0);
		Vector4 Rotate = Vector4(0.0f, 120.0f, 120.0f, 120.0f);
		Vector4 RotateV = Vector4(0, 0, 0, 0);
		Vector4 DownAngle = Vector4(0.0f, 30.0f, 30.0f, 30.0f);
		Vector4 DownAngleV = Vector4(0, 0, 0, 0);
		/** Segments per stem. Also the number of child attachment slots. */
		Vector4i CurveRes = Vector4i(3, 3, 1, 1);
		Vector4 Taper = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

		Ref<ProceduralTreeLeafParameters> Leaf;
		Ref<ProceduralTreeLeafParameters> Blossom;
		Ref<ProceduralTreeFruitParameters> Fruit;

		bool bStemBirchTexture = false;
		Color StemSmallColor = Color(0.175f, 0.25f, 0.15f, 1.0f);
		Color StemBigColor = Color(0.24f, 0.2f, 0.17f, 1.0f);
		float StemBumpStrength = 1.0f;
		float StemBumpGapSize = 0.14f;
		float StemBumpVoronoiWeight = 0.5f;
		float StemLichenFrequency = 8.0f;
		float StemLichenSize = 0.7f;

		void ForwardSubResourceChanged();
		void AdoptSubResource(const Ref<Resource>& Previous, const Ref<Resource>& Next);

	protected:
		static void _bind_methods();

	public:
		ProceduralTreeParameters();

		TREEGEN_ACCESSORS(int32_t, Levels)
		TREEGEN_ACCESSORS(Vector4, BaseSize)
		TREEGEN_ACCESSORS(float, AttractionUp)
		TREEGEN_ACCESSORS(float, Flare)
		TREEGEN_ACCESSORS(int32_t, Lobes)
		TREEGEN_ACCESSORS(float, LobeDepth)
		TREEGEN_ACCESSORS(float, Scale)
		TREEGEN_ACCESSORS(float, ScaleV)
		TREEGEN_ACCESSORS(float, Ratio)
		TREEGEN_ACCESSORS(float, RatioPower)
		TREEGEN_ACCESSORS(Vector4i, Shape)
		TREEGEN_ACCESSORS(Vector4i, BaseSplits)
		TREEGEN_ACCESSORS(Vector4, SegSplits)
		TREEGEN_ACCESSORS(Vector4, SegSplitBaseOffset)
		TREEGEN_ACCESSORS(Vector4, SplitAngle)
		TREEGEN_ACCESSORS(Vector4, SplitAngleV)
		TREEGEN_ACCESSORS(Vector4i, Branches)
		TREEGEN_ACCESSORS(Vector4, Length)
		TREEGEN_ACCESSORS(Vector4, LengthV)
		TREEGEN_ACCESSORS(Vector4, Curve)
		TREEGEN_ACCESSORS(Vector4, CurveV)
		TREEGEN_ACCESSORS(Vector4, CurveBack)
		TREEGEN_ACCESSORS(Vector4, Rotate)
		TREEGEN_ACCESSORS(Vector4, RotateV)
		TREEGEN_ACCESSORS(Vector4, DownAngle)
		TREEGEN_ACCESSORS(Vector4, DownAngleV)
		TREEGEN_ACCESSORS(Vector4i, CurveRes)
		TREEGEN_ACCESSORS(Vector4, Taper)
		TREEGEN_ACCESSORS(Color, StemSmallColor)
		TREEGEN_ACCESSORS(Color, StemBigColor)
		TREEGEN_ACCESSORS(float, StemBumpStrength)
		TREEGEN_ACCESSORS(float, StemBumpGapSize)
		TREEGEN_ACCESSORS(float, StemBumpVoronoiWeight)
		TREEGEN_ACCESSORS(float, StemLichenFrequency)
		TREEGEN_ACCESSORS(float, StemLichenSize)

		void SetStemBirchTexture(bool bValue)
		{
			bStemBirchTexture = bValue;
			emit_changed();
		}

		bool HasStemBirchTexture() const
		{
			return bStemBirchTexture;
		}

		void SetLeaf(const Ref<ProceduralTreeLeafParameters>& Value);
		Ref<ProceduralTreeLeafParameters> GetLeaf() const { return Leaf; }

		void SetBlossom(const Ref<ProceduralTreeLeafParameters>& Value);
		Ref<ProceduralTreeLeafParameters> GetBlossom() const { return Blossom; }

		void SetFruit(const Ref<ProceduralTreeFruitParameters>& Value);
		Ref<ProceduralTreeFruitParameters> GetFruit() const { return Fruit; }

		/** Overwrites every parameter with one of the four species from the paper. */
		void ApplyPreset(int32_t Preset);

		/** Creates any missing leaf / blossom / fruit sub-resource. */
		void EnsureSubResources();

		static String GetPresetName(int32_t Preset);
	};
} // namespace godot

VARIANT_ENUM_CAST(ProceduralTreeParameters::EPreset);
