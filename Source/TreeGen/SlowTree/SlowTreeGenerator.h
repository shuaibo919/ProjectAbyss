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
	/**
	 * 用户面形变旋钮(乘法乘数, 1.0 = 预设原样)。只动"粗细/密度", 不动长度/角度/噪声,
	 * 保证调整恒为对基准预设的形变, 预设之间的差别(层数/轮生/叶形)不会被旋钮抹平。
	 * 映射:
	 *  - TrunkThickness  → TrunkParams.startRadius/endRadius(主干粗细)
	 *  - RootThickness   → RootsParams.radiusScale(根部粗细, 相对树干基部半径的比例)
	 *  - BranchThickness → Branch/Twig/Spine 的 radiusScale(枝杈粗细, 相对父级附着点半径)
	 *  - BranchDensity   → BranchClassic.branchCount / Interval.branchesPerNode / Twig.twigCount
	 *                      (每层数量取整 ≥1; Spine 是叶轴不算枝, 数量不受密度影响)
	 */
	struct SlowTreeTuning
	{
		float TrunkThickness = 1.0f;
		float RootThickness = 1.0f;
		float BranchThickness = 1.0f;
		float BranchDensity = 1.0f;
	};

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
		float GenerationMs = 0.0f;   // CPU: 遍历+细分; GPU: 遍历+描述子发射
		float GpuMs = 0.0f;          // 仅 GPU 路径: 设备/上传/dispatch/读回合计
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

		/**
		 * Script-facing: preset + seed → Dictionary(mesh/materials/error/stats/timings)。
		 * Tuning 为 Dictionary{trunk_thickness, root_thickness, branch_thickness, branch_density},
		 * 缺省键按 1.0(预设原样)。
		 */
		static Dictionary Generate(int32_t Preset, int64_t Seed, bool UseGpu = false,
		                           float Season = 2.0f, const Dictionary& Tuning = Dictionary());

		/** Script-facing: .vtree 文件 + seed → 同上(含不支持节点校验)。 */
		static Dictionary GenerateFromFile(const String& VtreePath, int64_t Seed, bool UseGpu = false,
		                                   float Season = 2.0f, const Dictionary& Tuning = Dictionary());

		/**
		 * 核心: 图 → ArrayMesh(多 surface)+ 材质。Seed != 0 时按 Mix(seed, id, depth)
		 * 派生节点种子; == 0 时保留模板种子(位级对拍锚点)。
		 * UseGpu=true 时细分阶段走 compute 管线(Stage 2), 中心线/RNG/附着共享同一套代码。
		 * Tuning 在种子派生前覆盖到节点参数上(见 SlowTreeTuning 头注)。
		 */
		static bool GenerateFromGraph(NodeGraph& Graph, int64_t Seed, SlowTreeMeshResult& Out,
		                              bool UseGpu = false, float Season = 2.0f, bool Evergreen = false,
		                              const SlowTreeTuning& Tuning = {});

		/** 校验层: 图上含 v1 不支持的节点(自定义/导入/散布)时返回错误(中文提示)。 */
		static String ValidateGraph(const NodeGraph& Graph);

		/** CPU 生成(RNG/中心线/附着/细分全在调用线程)。SelfTest/Stage 2 共用。 */
		static bool RunGeneration(NodeGraph& Graph, int64_t Seed, TreeMeshData& OutMesh, String& OutError);

		/**
		 * GPU 生成(Stage 2): 与 RunGeneration 相同的种子派生与图遍历(共享原代码),
		 * 但细分由描述子发射 + SlowTreeCompute::RunGpu 完成; 读回结果按 chunk
		 * 拼回 OutMesh.batches(与 CPU 路径同一批次顺序)。GpuStats 非空时接收
		 * emit_ms / device_ms / buffer_ms / setup_ms / gpu_ms / readback_ms / assemble_ms。
		 */
		static bool RunGenerationGpu(NodeGraph& Graph, int64_t Seed, TreeMeshData& OutMesh,
		                             Dictionary* GpuStats, String& OutError);

		/** TreeMeshData → ArrayMesh(batch = surface)。GPU 读回路径复用本装配(Stage 2)。 */
		/**
		 * TreeMeshData → **单个** vertex-coloured ArrayMesh surface。
		 *
		 * Season 用 TreeGen 的 0..4 语义(0/4 冬, 2 夏), Evergreen=true 时叶色不随季节变化
		 * (针叶/竹)。花瓣按"红大于绿"自动认出并同样不变色。
		 */
		static bool ConvertToGodotMesh(const TreeMeshData& Data, SlowTreeMeshResult& Out,
		                               float Season = 2.0f, bool Evergreen = false);

	private:
		/** 顶点硬上限(Stage 1 CPU 路径超限即报错; Stage 2 改为截断标志)。 */
		static constexpr uint64_t kMaxVertexFloats = 16ull * 1024 * 1024;
	};
} // namespace godot
