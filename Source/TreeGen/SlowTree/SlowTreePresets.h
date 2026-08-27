#pragma once

// SlowTree 预设系统(用户面 = 预设 + 种子, 无 GraphEdit)。
//
// 实现说明(与计划的偏差, 已记录 UPSTREAM_SYNC.md): 每个预设的"模板图+参数"以
// **内嵌 .vtree 文本**(VEGTOOL 格式)存储, 经 VtreeIO 的同一解析器重建 NodeGraph——
// 而不是程序化 builder 代码。理由:
//   - 与上游应用逐位一致的最强保证: 同事在应用里保存的 .vtree 就是本文件里的文本,
//     解析路径与上游完全同构; golden 对拍时应用直接加载同一份 .vtree, 无反向转换。
//   - 转换器(Tools/convert_vtree_preset.py)两个方向都是纯文本处理: .vtree → 内嵌字符串
//     片段签入; 反方向把内嵌字符串抽出来供同事应用复算 golden。
//   - 全局种子旋钮在加载后统一派生(见 SlowTreeGenerator), 不依赖模板内种子值。

#include "NodeGraph.h"
#include <cstdint>

namespace godot
{
	namespace SlowTreePresets
	{
		// 预设 0 = 上游内置默认工程(HelloTree), 其余为内置物种模板。
		int32_t GetPresetCount();
		const char* GetPresetName(int32_t Preset);

		// 用预设重建节点图(会先 clear)。返回 false = 预设越界。
		bool BuildGraph(int32_t Preset, NodeGraph& Graph);

		// 预设的原始 VEGTOOL 文本(供转换器/自测抽取, 预设 0 为上游 kDefaultTemplate)。
		const char* GetPresetVtree(int32_t Preset);

		/**
		 * 该预设是否常绿(叶色不随季节变化)。颜色推不出针叶/阔叶, 所以这是一张表。
		 */
		bool IsEvergreen(int32_t Preset);
	} // namespace SlowTreePresets
} // namespace godot
