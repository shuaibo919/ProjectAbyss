#pragma once

#include "SlowTreeGrowth.h"

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <cstdint>
#include <atomic>
#include <memory>
#include <vector>

struct BranchRing;
struct TreeMeshData;
struct MaterialParams;
class NodeGraph;

namespace godot
{
	/** Atlas cells describe botanical organs, independently of branch topology. */
	enum class ETreeFoliageShape : int32_t
	{
		Broadleaf,
		Willow,
		Pine,
		Ginkgo,
		Bamboo,
		Metasequoia,
		Peach,
		Blossom
	};

	struct TreeFoliageOptions
	{
		bool bCrossedCards = false;
		bool bSpeciesRules = false;
		bool bGenerateLeaves = true;
		int32_t Preset = 0;
		int32_t MaxCards = 12000;
		int32_t MaxSegments = 20000;
		int32_t RadialSegments = 32;
		float Density = 1.0f;
		TreeGrowthSettings Growth;
		std::shared_ptr<std::atomic<bool>> Cancelled;
	};

	/** One attached spray, represented by two intersecting quads (four triangles). */
	struct TreeFoliageCard
	{
		Vector3 Anchor;
		Vector3 Up;
		Vector3 Right;
		Vector3 Color;
		float Length = 0.5f;
		float Width = 0.4f;
		float Phase = 0.0f;
		uint32_t Seed = 0;
		ETreeFoliageShape Shape = ETreeFoliageShape::Broadleaf;
	};

	class SlowTreeFoliage
	{
	  public:
		static void ApplySpeciesRules(NodeGraph& Graph, int32_t Preset);
		/** Distinct parent shoots receive distinct, reproducible growth patterns. */
		static uint32_t GetShootSeed(const Vector3& Position, int32_t Seed);
		static void ShapePendantShoot(std::vector<BranchRing>& Rings, float Length);
		static void CollectCards(TreeMeshData& Data, const std::vector<BranchRing>& Rings,
								 const MaterialParams& Material, const TreeFoliageOptions& Options, int32_t LeafCount,
								 float LeafSize, int32_t Seed, float Phase);
		static void AppendCards(TreeMeshData& Data, const TreeFoliageOptions& Options);
		/** Main thread only: creates/reuses the procedural atlas and shader. */
		static Ref<Material> CreateMaterial(float Season);
		static void UpdateMaterial(const Ref<Material>& Material, float Season, float WindStrength, float WindTime);
	};
} // namespace godot
