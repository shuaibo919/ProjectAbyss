#pragma once

// SlowTree 结构自检(无位级 golden——项目决策"效果差不多就行", 见 UPSTREAM_SYNC.md):
//  1) 每个预设 seed=0 生成 → 网格非空 / 无 NaN / AABB 合理 / 顶点预算
//  2) 同种子两次生成结构一致(确定性)
//  3) 不同种子变种均可生成
//  4) 内置默认模板(HelloTree .vtree 解析路径)结构自检
//  5) 校验层拒绝 v1 不支持的节点(Scatter)
// headless 驱动: Game/tests/treegen_slowtree_selftest.gd

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot
{
	class SlowTreeSelfTest : public Object
	{
		GDCLASS(SlowTreeSelfTest, Object)

	protected:
		static void _bind_methods();

	public:
		/** 跑全部自检, 返回 Dictionary(ok/passed/failed/results/summary)。 */
		static Dictionary RunAll();
	};
} // namespace godot
