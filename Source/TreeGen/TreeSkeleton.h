#pragma once

// CPU port of the `Stem` work-graph node from "Real-Time GPU Tree Generation"
// (Kuth et al., HPG 2025). The GPU original is a recursive broadcasting node whose
// 32 threads cooperatively track branch splits; here the same thread-budget
// bookkeeping is simulated so that a given seed yields the same skeleton.
//
// Everything view-dependent in the original (single-sided tessellation, camera-facing
// opening angles, distance-based leaf thinning) is dropped or turned into an explicit
// parameter, because the output is a baked mesh rather than a per-frame draw.

#include "TreeGen/TreeMath.h"

#include <vector>

namespace TreeGen
{
	/** Per-stem-level parameter arrays, indexed 0..3 exactly as the HLSL `nFoo[level]`. */
	struct LeafParams
	{
		int32_t Count = 0;
		int32_t ScaleShape = 3;
		float Scale = 0.2f;
		float ScaleX = 0.5f;
		float StemLen = 0.0f;
		float BotAngle = -85.0f;
		float MidAngle = 0.0f;
		float TopAngle = 45.0f;
		float SideOffset = 0.45f;
		int32_t Lobes = 1;
		float LobeAngle = 0.0f;
		float LobeFalloff = 0.0f;
		Color LeafColor = Color(0, 0.125f, 0, 1);
		float Translucency = 0.7f;
		float SeasonOffset = 0.0f;
		float Curl = 0.35f;
		float ColorJitter = 0.12f;
		float ScaleJitter = 0.2f;
		int32_t NeedleBlades = 4;
		bool bTopConvex = false;
		bool bIsNeedle = false;
		bool bEvergreen = false;
	};

	struct FruitParams
	{
		float Chance = 0.0f;
		float DownForce = 1.0f;
		float Size = 0.1f;
		float Shape[4] = { 0.5f, 0.333f, 0.5f, 0.666f };
		Color FruitColor = Color(0.25f, 0, 0, 1);
	};

	struct TreeParams
	{
		int32_t Levels = 3;
		float BaseSize[4] = { 0.25f, 0.05f, 0.05f, 0.05f };
		float AttractionUp = 0.0f;
		float Flare = 0.5f;
		int32_t Lobes = 0;
		float LobeDepth = 0.0f;
		float Scale = 10.0f;
		float ScaleV = 0.0f;
		float Ratio = 0.05f;
		float RatioPower = 1.0f;

		int32_t Shape[4] = { 0, 0, 0, 0 };
		int32_t BaseSplits[4] = { 0, 0, 0, 0 };
		float SegSplits[4] = { 0, 0, 0, 0 };
		float SegSplitBaseOffset[4] = { 0, 0, 0, 0 };
		float SplitAngle[4] = { 0, 0, 0, 0 };
		float SplitAngleV[4] = { 0, 0, 0, 0 };
		int32_t Branches[4] = { 1, 10, 5, 0 };
		float Length[4] = { 1.0f, 0.5f, 0.5f, 0.0f };
		float LengthV[4] = { 0, 0, 0, 0 };
		float Curve[4] = { 0, 0, 0, 0 };
		float CurveV[4] = { 0, 0, 0, 0 };
		float CurveBack[4] = { 0, 0, 0, 0 };
		float Rotate[4] = { 0, 120.0f, 120.0f, 120.0f };
		float RotateV[4] = { 0, 0, 0, 0 };
		float DownAngle[4] = { 0, 30.0f, 30.0f, 30.0f };
		float DownAngleV[4] = { 0, 0, 0, 0 };
		int32_t CurveRes[4] = { 3, 3, 1, 1 };
		float Taper[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		LeafParams Leaf;
		LeafParams Blossom;
		FruitParams Fruit;

		bool bStemBirchTexture = false;
		Color StemSmallColor = Color(0.175f, 0.25f, 0.15f, 1);
		Color StemBigColor = Color(0.24f, 0.2f, 0.17f, 1);
		float StemBumpStrength = 1.0f;
		float StemBumpGapSize = 0.14f;
		float StemBumpVoronoiWeight = 0.5f;
		float StemLichenFrequency = 8.0f;
		float StemLichenSize = 0.7f;
	};

	/** Global state that the GPU sample kept in its persistent scratch buffer. */
	struct GenerationContext
	{
		/** 0 = bare winter, 1 = spring, 2 = summer, 3 = autumn, 4 = winter again. */
		float Season = 2.0f;
		/** Static wind pose. Zero produces a perfectly unbent tree. */
		float WindStrength = 0.0f;
		/** Phase of the wind noise; the sample fed it the frame time. */
		float WindTime = 0.0f;
		/** Uniform leaf thinning in (0, 1]. Surviving leaves grow to compensate. */
		float LeafDensity = 1.0f;

		/** Safety net against parameter combinations that would recurse into millions of stems. */
		int32_t MaxSegments = 20000;
		/**
		 * Hard backstop on foliage. This is not the place to express a triangle budget: cutting
		 * foliage off mid-traversal strips the crown and leaves the lower branches fully leafed.
		 * Scale LeafDensity instead, which thins the whole tree evenly.
		 */
		int32_t MaxLeaves = 400000;
	};

	/** One tessellatable slice of a stem, i.e. one `DrawSegmentRecord`. */
	struct StemSegment
	{
		Vector3 FromPos;
		Vector3 ToPos;
		Quaternion FromRot;
		Quaternion ToRot;
		SegmentInfo Si;
		float AoDistance = 0.0f;
	};

	/** One leaf, blossom petal cluster or fruit, i.e. one `DrawLeafRecord`. */
	struct LeafInstance
	{
		Vector3 Position;
		Quaternion Rotation;
		uint32_t Seed = 0;
		float Scale = 0.0f;
		float AoDistance = 0.0f;
		bool bIsBlossom = false;
	};

	/**
	 * Runs the Weber-Penn model and collects the resulting stems and foliage.
	 * Reusable: call Generate() again to overwrite the previous result.
	 */
	class TreeSkeleton
	{
	public:
		void Generate(const TreeParams& InParams, const GenerationContext& InContext, uint32_t InSeed);

		const std::vector<StemSegment>& GetSegments() const { return Segments; }
		const std::vector<LeafInstance>& GetLeaves() const { return Leaves; }
		const std::vector<LeafInstance>& GetFruits() const { return Fruits; }

		/** Total foliage the last run produced. */
		int32_t GetFoliageCount() const { return FoliageCount; }

		/** True when MaxSegments stopped the recursion, leaving part of the crown missing. */
		bool WereSegmentsTruncated() const { return bSegmentsTruncated; }

		/** True when MaxLeaves stopped foliage output. Scale LeafDensity down instead. */
		bool WasFoliageTruncated() const { return bFoliageTruncated; }

	private:
		struct StemRecord
		{
			Vector3 Pos;
			Quaternion Rot;
			uint32_t Seed = 0;
			uint32_t Children = 0;
			float Scale = 1.0f;
			float Length = 1.0f;
			float Radius = 0.1f;
			float AoDistance = 0.0f;
		};

		void GenerateStem(const StemRecord& In, int32_t Level);

		TreeParams Params;
		GenerationContext Context;

		std::vector<StemSegment> Segments;
		std::vector<LeafInstance> Leaves;
		std::vector<LeafInstance> Fruits;

		int32_t FoliageCount = 0;
		bool bSegmentsTruncated = false;
		bool bFoliageTruncated = false;
	};
} // namespace TreeGen
