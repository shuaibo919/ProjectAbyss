#pragma once

// Compute-shader plumbing for the SlowTree integration.
//
// Stage 0 artifact: hello_compute_probe() is the gate that proves the custom
// fork's D3D12 RenderingDevice can round-trip a compute dispatch end to end
// (SPIR-V -> pipeline -> dispatch -> readback) before any real tree work is
// built on it.
//
// Stage 2: RunGpu() 执行真正的生成管线——CPU 阶段(TreeGenerator GPU 发射模式)
// 产出的 TreeGpuEmission 描述子被 4 个 compute 着色器(cylinder/collar/
// leaf_card/frond)展开成统一顶点/索引缓冲, 读回后按 chunk 区间拼回
// TreeMeshData.batches(批次/材质/骨架仍由 CPU 阶段生成)。

#include "SlowTreeGpuData.h"
#include "SlowTreeMeshData.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot
{
	class SlowTreeCompute : public Object
	{
		GDCLASS(SlowTreeCompute, Object)

	protected:
		static void _bind_methods();

	public:
		/**
		 * Runs the hello-compute round trip on a fresh local RenderingDevice.
		 * One thread per float: dst[i] = src[i] * 2.0 + 1.0 — bit-exact IEEE
		 * ops, so the readback must match bit-for-bit.
		 *
		 * Returns a Dictionary: ok, verified, element_count, and a timings
		 * sub-dictionary (ms per phase, dispatch min/avg over repeated submits).
		 * Prints a human-readable summary as it goes.
		 */
		static Dictionary hello_compute_probe(uint64_t ElementCount = 1 << 20, bool Verbose = true);

			/**
			 * Stage 2: GPU 细分 + 读回。OutMesh 的批次列表/材质/骨架由 CPU 阶段
			 * (TreeGenerator 的 GPU 发射模式)生成, 此处只填每批次的 vertices/indices。
			 * 输出缓冲尺寸来自 Emission 的精确前缀和(无估算/压缩); 四个着色器
			 * 各自一个 compute list, 一次 submit + sync 后整体读回。
			 * OutStats 非空时接收分阶段计时(ms)与缓冲统计。
			 */
			static bool RunGpu(const TreeGpuEmission& Emission, TreeMeshData& OutMesh,
			                   Dictionary* OutStats, String& OutError);
	};
} // namespace godot
