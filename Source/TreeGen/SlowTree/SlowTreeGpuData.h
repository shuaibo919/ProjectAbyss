#pragma once

// Stage 2 GPU 细分: 描述子数据(CPU 发射 → GPU 消费)。
//
// 布局约定(与 shaders/*.comp 的 std430 声明逐字段对应):
//   - 所有描述子成员全部是 float(索引/计数以 float 精确携带, 上限 2^24 远大于
//     16M 顶点预算); C++ 侧用扁平 float 数组填充, GLSL 侧按 vec4 读取。
//   - GpuBranchSegDesc  / GpuCollarDesc   / GpuLeafCardDesc = 36 floats (9×vec4)
//   - GpuFrondDesc      = 32 floats (8×vec4)
//   - GpuRingEntry      = 12 floats (3×vec4): center.xyz + radius + right.xyz + up.xyz
//   - GpuCutoutPoint    = 2 floats (vec2)
//   - 顶点/索引输出是统一缓冲: 顶点 = float 序列(分支 stride 10, 叶 stride 16),
//     索引 = uint32 序列。全局 float 缓冲里枝/叶顶点交错, "顶点单位"只能按 stride
//     各自累计 → 描述子同时携带 firstVertexFloats(全局 float 写偏移)与
//     firstVertexUnits(按 stride 计的顶点单位, 仅用于索引基)。
//
// 每 batch 的区间: 生成递归会把不同 batch 的发射交错(枝→子叶→再回枝), 所以
// 不能"每 batch 一个连续区间", 而按发射顺序记录 chunk 列表, 读回后逐 chunk
// 拼回 batch.vertices/indices(与 CPU insert 顺序逐字节一致)。

#include <cstdint>
#include <vector>

namespace godot
{
	// 单一几何发射的顶点/索引区间(全局缓冲内)。
	// GPU 着色器按描述子里的 firstVertex(全局顶点单位)写索引, 而 batch 局部
	// 顶点向量从 0 计数 → 读回拼 batch 时需按 FirstVertex 重定位(见 RunGpu)。
	struct GpuRangeChunk
	{
		uint32_t Batch = 0;         // TreeMeshData::batches 下标
		uint64_t VertStart = 0;     // float 顶点缓冲内的起点
		uint64_t VertCount = 0;     // float 数
		uint64_t FirstVertex = 0;   // 本次发射的全局顶点单位基(仅索引重定位用)
		uint64_t IdxStart = 0;      // uint32 索引缓冲内的起点
		uint64_t IdxCount = 0;      // uint32 数
	};

	// CPU 发射阶段(中心线/RNG/附着全部共享原算法)收集的描述子与缓冲布局。
	struct TreeGpuEmission
	{
		// 每 batch 一组; 每元素 36 / 32 floats(见头注释)。
		std::vector<std::vector<float>> BranchDescs;
		std::vector<std::vector<float>> CollarDescs;
		std::vector<std::vector<float>> LeafDescs;
		std::vector<std::vector<float>> FrondDescs;

		// 共享池: collar 落点采样与 frond 沿脊铺带的环数据; 叶片/frond 轮廓裁剪数据。
		std::vector<float> Rings;          // 12 floats/环
		std::vector<float> CutoutPoints;   // 2 floats/点
		std::vector<uint32_t> CutoutTris;  // 三角形索引(指向局部叶卡顶点)

		// 全局计数(顶点单位 = float 数, 索引单位 = uint32 数)。
		uint64_t VertFloats = 0;
		uint64_t IndexCount = 0;

		// 发射顺序的区间记录(见头注释)。
		std::vector<GpuRangeChunk> Chunks;

		bool IsEmpty() const { return VertFloats == 0; }
	};

	// GpuBranchSegDesc — appendCylinder 的每段一个(环角 cos/sin 是仅有的 GPU 浮点)。
	// [0] botPosRadius  [1] botUp        [2] botRight
	// [3] topPosRadius  [4] topUp        [5] topRight
	// [6] uvWind(vBot, vTop, uTiling, windW)
	// [7] meta(sides, lastRing, botRingIdx, topRingIdx)
	// [8] meta2(windPhase, firstVertexFloats, firstVertexUnits, firstIdx)
	// 顶点写偏移 = firstVertexFloats + 顶点单位*stride; 索引基 = firstVertexUnits / firstIdx。
	constexpr uint32_t GPU_BRANCH_SEG_FLOATS = 36;

	// GpuCollarDesc — appendCollar 的每条裙边一个(weldSegs=4 为着色器常量)。
	// [0] parentCandR   [1] parentA      [2] childBase
	// [3] childDir      [4] childRight
	// [5] shape(startR, flareMax, upperSpread, lowerSpread)
	// [6] shape2(landingR, uTiling, vPerUnit, collarSink)
	// [7] meta(sides, ringOffset, ringCount, windPhase)
	// [8] meta2(firstVertexFloats, firstVertexUnits, firstIdx, 0)
	constexpr uint32_t GPU_COLLAR_FLOATS = 36;

	// GpuLeafCardDesc — buildLeafCluster 的每片叶一个(quad 或 cutout)。
	// [0] pos          [1] leafRight    [2] leafUp       [3] leafNormal
	// [4] size(hs, hw, windW, leafPhase)
	// [5] albedo       [6] anchor(basePos)
	// [7] meta(mode, cutoutPointOffset, cutoutTriOffset, cutoutTriCount)
	// [8] meta2(cutoutPointCount, firstVertexFloats, firstVertexUnits, firstIdx)
	// mode: 0=quad, 1=cutout; cutoutTriCount = 完整数组长, 尾端不足 3 个丢弃(同 CPU)。
	constexpr uint32_t GPU_LEAF_CARD_FLOATS = 36;

	// GpuFrondDesc — buildFrond 的每条叶带一个(网格铺带或 cutout)。
	// [0] profile(widthBase, widthTip, width, profilePow)
	// [1] shape(curl, serrateDepth, serrateFlag, 0)
	// [2] albedo       [3] wind(windW, windPhase, 0, 0)   [4] anchor
	// [5] meta(ringOffset, ringCount, nSeg, totalCols)
	// [6] meta2(cutoutPointOffset, cutoutTriOffset, cutoutTriCount, cutoutPointCount)
	// [7] meta3(firstVertexFloats, firstVertexUnits, firstIdx, 0)
	// mode: cutoutPointCount == 0 → 网格铺带; >0 → 轮廓裁剪(同 CPU 分支条件)。
	constexpr uint32_t GPU_FROND_FLOATS = 32;

	constexpr uint32_t GPU_RING_ENTRY_FLOATS = 12;
	constexpr uint32_t GPU_CUTOUT_POINT_FLOATS = 2;
} // namespace godot
