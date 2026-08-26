#pragma once

// SlowTree facade: 预设 → NodeGraph → TreeGenerator(CPU 生成) → ArrayMesh + 材质。
//
// 阶段归属:
//  - Stage 1: 全部 CPU 路径(RunGeneration + ConvertToGodotMesh)。
//  - Stage 2: ConvertToGodotMesh 的输入改为 GPU 读回的 Packed 数组(同一装配逻辑);
//    RunGeneration 的细分部分由 compute 描述子发射替代, 中心线/RNG/附着计算不变。
//  - Stage 3: 生成搬进 SlowTreeWorker 后台线程; 材质换成 SlowTreeWindShader; 烘焙面板。
//
// 全局种子模型: SlowTree 每节点独立 seed, 无全局旋钮。本层在加载模板后按
//   node_seed = Mix(globalSeed, nodeId, depth) 派生各节点种子(节点间差异 + 确定性)。
//   globalSeed == 0 时**不覆盖**模板种子 → 与上游应用对同一 .vtree 逐位一致
//   (golden 对拍的锚点)。非 0 时一棵树一个旋钮即可变种。

#include "NodeGraph.h"
#include "SlowTreeMeshData.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <cstdint>

namespace godot
{
	/** Generation output: one ArrayMesh surface per SlowTree batch, materials parallel. */
	struct SlowTreeMeshResult
	{
		Ref<ArrayMesh> Mesh;
		Vector<Ref<Material>> SurfaceMaterials;
		uint32_t VertexCount = 0;
		uint32_t TriangleCount = 0;
		uint32_t SurfaceCount = 0;
		bool Truncated = false;
		String Error;

		// 分阶段耗时(ms, Stage 2/3 性能对比用)。
		float GraphBuildMs = 0.0f;
		float GenerationMs = 0.0f;
		float ConvertMs = 0.0f;

		bool IsError() const { return !Error.is_empty(); }
	};

	class SlowTreeGenerator : public Object
	{
		GDCLASS(SlowTreeGenerator, Object)

	protected:
		static void _bind_methods();

	public:
		static int32_t GetPresetCount();
		static String GetPresetName(int32_t Preset);

		/** Script-facing: preset + seed → Dictionary(mesh/materials/error/stats/timings). */
		static Dictionary Generate(int32_t Preset, int64_t Seed);

		/** Script-facing: .vtree 文件 + seed → 同上(含不支持节点校验)。 */
		static Dictionary GenerateFromFile(const String& VtreePath, int64_t Seed);

		/**
		 * 核心: 图 → ArrayMesh(多 surface)+ 材质。Seed != 0 时按 Mix(seed, id, depth)
		 * 派生节点种子; == 0 时保留模板种子(位级对拍锚点)。
		 */
		static bool GenerateFromGraph(NodeGraph& Graph, int64_t Seed, SlowTreeMeshResult& Out);

		/** 校验层: 图上含 v1 不支持的节点(自定义/导入/散布)时返回错误(中文提示)。 */
		static String ValidateGraph(const NodeGraph& Graph);

		/** CPU 生成(RNG/中心线/附着/细分全在调用线程)。SelfTest/Stage 2 共用。 */
		static bool RunGeneration(NodeGraph& Graph, int64_t Seed, TreeMeshData& OutMesh, String& OutError);

		/** TreeMeshData → ArrayMesh(batch = surface)。GPU 读回路径复用本装配(Stage 2)。 */
		static bool ConvertToGodotMesh(const TreeMeshData& Data, SlowTreeMeshResult& Out);

	private:
		/** 顶点硬上限(Stage 1 CPU 路径超限即报错; Stage 2 改为截断标志)。 */
		static constexpr uint64_t kMaxVertexFloats = 16ull * 1024 * 1024;
	};
} // namespace godot
