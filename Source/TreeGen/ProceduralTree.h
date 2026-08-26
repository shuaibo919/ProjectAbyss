#pragma once

// Scene-facing node for the ported "Real-Time GPU Tree Generation" model.
//
// Deliberately asset-free: geometry, colours and materials are all produced in code, so a
// tree needs nothing on disk beyond this extension. Colours ride on vertex colours, which
// StandardMaterial3D consumes as albedo.

#include "TreeGen/ProceduralTreeParameters.h"
#include "TreeGen/SlowTree/SlowTreeGenerator.h"
#include "TreeGen/TreeMeshBuilder.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot
{
	class ProceduralTree : public MeshInstance3D
	{
		GDCLASS(ProceduralTree, MeshInstance3D)

	public:
		// 双后端(开发期共存): Weber-Penn 保持默认直至 SlowTree 预设达标(Stage 4 切换)。
		enum ProceduralTreeBackend
		{
			BACKEND_WEBER_PENN = 0,
			BACKEND_SLOWTREE = 1,
		};

	private:
		Ref<ProceduralTreeParameters> Parameters;
		int32_t Seed = 0;

		// SlowTree 后端: 预设模板 id + 全局种子(0 = 模板原样, 非 0 = Mix32 派生变种)。
		int32_t Backend = BACKEND_WEBER_PENN;
		int32_t SlowTreePreset = 0;

		float Season = 2.0f;
		float WindStrength = 0.0f;
		float WindTime = 0.0f;
		float LeafDensity = 1.0f;

		int32_t RadialSegments = 12;
		int32_t RingsPerSegment = 1;
		int32_t LeafArcSegments = 2;
		int32_t FruitLongitudes = 12;
		int32_t FruitBands = 6;
		bool bBarkDetail = true;
		bool bGenerateLeaves = true;
		bool bGenerateFruit = true;

		int32_t MaxSegments = 20000;
		/** Foliage budget. Exceeding it thins leaves uniformly rather than dropping the crown. */
		int32_t MaxLeaves = 12000;

		bool bAutoRegenerate = true;

		Ref<StandardMaterial3D> BarkMaterial;
		Ref<StandardMaterial3D> FoliageMaterial;
		Ref<StandardMaterial3D> FruitMaterial;

		int32_t LastVertexCount = 0;
		int32_t LastTriangleCount = 0;
		int32_t LastSegmentCount = 0;
		int32_t LastLeafCount = 0;
		bool bLastResultTruncated = false;

		void EnsureParameters();
		void EnsureMaterials();
		void OnParametersChanged();
		void RequestRegenerate();

		/** SlowTree 后端生成路径: 预设模板 → 全局种子派生 → facade → set_mesh + 统计。 */
		void GenerateSlowTree();

		/** Fills the plain-C++ parameter snapshot the generator works on. */
		void CollectTreeParams(TreeGen::TreeParams& OutParams) const;

	protected:
		static void _bind_methods();

		/**
		 * Drops `mesh` from serialisation. The generated mesh is a derived product of the
		 * parameters, and letting MeshInstance3D store it would inline a six-figure-triangle
		 * ArrayMesh into every scene that contains a tree. Use bake_mesh() to keep one.
		 */
		void _validate_property(PropertyInfo& Property) const;

	public:
		ProceduralTree();

		void _ready() override;

		/** Rebuilds the mesh in place. Safe to call from tools, game code or PCG graphs. */
		void Generate();

		/** Builds and returns a mesh without touching this node, for baking or MultiMesh use. */
		Ref<ArrayMesh> BakeMesh();

		void SetParameters(const Ref<ProceduralTreeParameters>& Value);
		Ref<ProceduralTreeParameters> GetParameters() const { return Parameters; }

		/** Convenience wrapper so a tree can be re-seeded to one of the paper's species. */
		void ApplyPreset(int32_t Preset);

		void SetSeed(int32_t Value);
		int32_t GetSeed() const { return Seed; }

		/** 生成后端: BACKEND_WEBER_PENN(默认) / BACKEND_SLOWTREE。切换立即重建。 */
		void SetBackend(int32_t Value);
		int32_t GetBackend() const { return Backend; }

		/** SlowTree 预设模板 id(0..GetPresetCount()-1)。仅 SlowTree 后端生效。 */
		void SetSlowTreePreset(int32_t Value);
		int32_t GetSlowTreePreset() const { return SlowTreePreset; }

		void SetSeason(float Value);
		float GetSeason() const { return Season; }

		void SetWindStrength(float Value);
		float GetWindStrength() const { return WindStrength; }

		void SetWindTime(float Value);
		float GetWindTime() const { return WindTime; }

		void SetLeafDensity(float Value);
		float GetLeafDensity() const { return LeafDensity; }

		void SetRadialSegments(int32_t Value);
		int32_t GetRadialSegments() const { return RadialSegments; }

		void SetRingsPerSegment(int32_t Value);
		int32_t GetRingsPerSegment() const { return RingsPerSegment; }

		void SetLeafArcSegments(int32_t Value);
		int32_t GetLeafArcSegments() const { return LeafArcSegments; }

		void SetFruitLongitudes(int32_t Value);
		int32_t GetFruitLongitudes() const { return FruitLongitudes; }

		void SetFruitBands(int32_t Value);
		int32_t GetFruitBands() const { return FruitBands; }

		void SetBarkDetail(bool bValue);
		bool HasBarkDetail() const { return bBarkDetail; }

		void SetGenerateLeaves(bool bValue);
		bool ShouldGenerateLeaves() const { return bGenerateLeaves; }

		void SetGenerateFruit(bool bValue);
		bool ShouldGenerateFruit() const { return bGenerateFruit; }

		void SetMaxSegments(int32_t Value);
		int32_t GetMaxSegments() const { return MaxSegments; }

		void SetMaxLeaves(int32_t Value);
		int32_t GetMaxLeaves() const { return MaxLeaves; }

		void SetAutoRegenerate(bool bValue);
		bool ShouldAutoRegenerate() const { return bAutoRegenerate; }

		int32_t GetVertexCount() const { return LastVertexCount; }
		int32_t GetTriangleCount() const { return LastTriangleCount; }
		int32_t GetSegmentCount() const { return LastSegmentCount; }
		int32_t GetLeafCount() const { return LastLeafCount; }

		/** True when a primitive cap cut the last generation short. */
		bool WasTruncated() const { return bLastResultTruncated; }
	};
} // namespace godot
