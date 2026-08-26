#include "SlowTreeSelfTest.h"

#include "NodeGraph.h"
#include "SlowTreeGenerator.h"
#include "SlowTreePresets.h"
#include "VtreeIO.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace
{
	// 单树结构自检: mesh 有效、顶点/三角非空、全通道无 NaN、AABB 在合理树尺内。
	String CheckMesh(const Ref<ArrayMesh>& Mesh, int64_t VertexCount, int64_t TriangleCount)
	{
		if (!Mesh.is_valid())
		{
			return "mesh 无效";
		}
		if (VertexCount == 0)
		{
			return "顶点数为 0";
		}
		if (TriangleCount == 0)
		{
			return "三角形数为 0";
		}
		if (Mesh->get_surface_count() == 0)
		{
			return "无 surface";
		}
		if (TriangleCount > 20'000'000)
		{
			return "三角形数超出自检预算";
		}

		Vector3 aabbMin(1e30f, 1e30f, 1e30f);
		Vector3 aabbMax(-1e30f, -1e30f, -1e30f);
		for (int32_t s = 0; s < Mesh->get_surface_count(); ++s)
		{
			const Array arrays = Mesh->surface_get_arrays(s);
			if (arrays.is_empty())
			{
				return "surface 无数组";
			}
			const PackedVector3Array pos = arrays[Mesh::ARRAY_VERTEX];
			const PackedVector3Array nrm = arrays[Mesh::ARRAY_NORMAL];
			const PackedFloat32Array wind = arrays[Mesh::ARRAY_CUSTOM0];
			const int64_t count = pos.size();
			if (count == 0)
			{
				return "surface 无顶点";
			}
			if (nrm.size() != count)
			{
				return "法线数量不匹配";
			}
			if (wind.size() != count * 2)
			{
				return "风场通道数量不匹配(应为每顶点 2 floats)";
			}
			for (int64_t i = 0; i < count; ++i)
			{
				const Vector3 p = pos[i];
				const Vector3 n = nrm[i];
				if (!Math::is_finite(p.x) || !Math::is_finite(p.y) || !Math::is_finite(p.z))
				{
					return vformat("顶点含 NaN/Inf (surface %d, vertex %d, pos %s)", s, i, String(p));
				}
				if (!Math::is_finite(n.x) || !Math::is_finite(n.y) || !Math::is_finite(n.z))
				{
					return vformat("法线含 NaN/Inf (surface %d, vertex %d, nrm %s)", s, i, String(n));
				}
				if (!Math::is_finite(wind[i * 2]) || !Math::is_finite(wind[i * 2 + 1]))
				{
					return vformat("风场通道含 NaN/Inf (surface %d, vertex %d)", s, i);
				}
				aabbMin = aabbMin.min(p);
				aabbMax = aabbMax.max(p);
			}
		}

		const Vector3 extent = aabbMax - aabbMin;
		if (extent.x < 0.001f || extent.y < 0.001f)
		{
			return "AABB 沿 X/Y 退化";
		}
		if (aabbMin.y < -500.0f || aabbMax.y > 500.0f || extent.y > 500.0f)
		{
			return "AABB 高度不合理";
		}
		if (extent.x > 500.0f || extent.z > 500.0f)
		{
			return "AABB 半径不合理";
		}
		return String();
	}

	Dictionary CheckPreset(int32_t Preset, int64_t Seed)
	{
		Dictionary r;
		r["preset"] = Preset;
		r["name"] = String(SlowTreePresets::GetPresetName(Preset));
		const Dictionary result = SlowTreeGenerator::Generate(Preset, Seed);
		r["vertex_count"] = result["vertex_count"];
		r["triangle_count"] = result["triangle_count"];
		r["surface_count"] = result["surface_count"];
		r["generation_ms"] = result["generation_ms"];

		const String error = result["error"];
		if (!error.is_empty())
		{
			r["ok"] = false;
			r["error"] = error;
			return r;
		}
		const Ref<ArrayMesh> mesh = result["mesh"];
		const String check = CheckMesh(mesh, int64_t(result["vertex_count"]), int64_t(result["triangle_count"]));
		r["ok"] = check.is_empty();
		if (!check.is_empty())
		{
			r["error"] = check;
		}
		return r;
	}
} // namespace

void SlowTreeSelfTest::_bind_methods()
{
	ClassDB::bind_static_method("SlowTreeSelfTest", D_METHOD("run_all"), &SlowTreeSelfTest::RunAll);
}

Dictionary SlowTreeSelfTest::RunAll()
{
	Array results;
	int passed = 0;
	int failed = 0;

	// 1) 每个预设 seed=0(模板种子)结构自检
	const int32_t presetCount = SlowTreeGenerator::GetPresetCount();
	for (int32_t p = 0; p < presetCount; ++p)
	{
		const Dictionary r = CheckPreset(p, 0);
		if (bool(r["ok"]))
		{
			++passed;
		}
		else
		{
			++failed;
		}
		results.append(r);
	}

	// 2) 确定性: 同种子两次生成结构一致
	{
		const Dictionary a = SlowTreeGenerator::Generate(0, 12345);
		const Dictionary b = SlowTreeGenerator::Generate(0, 12345);
		Dictionary r;
		r["test"] = "determinism";
		const bool ok = String(a["error"]).is_empty() && String(b["error"]).is_empty()
			&& int64_t(a["vertex_count"]) == int64_t(b["vertex_count"])
			&& int64_t(a["triangle_count"]) == int64_t(b["triangle_count"])
			&& int64_t(a["surface_count"]) == int64_t(b["surface_count"]);
		r["ok"] = ok;
		if (!ok)
		{
			r["error"] = "同种子两次生成的顶点/三角形/surface 数不一致";
		}
		if (ok)
		{
			++passed;
		}
		else
		{
			++failed;
		}
		results.append(r);
	}

	// 3) 种子变种: 不同种子均能成功生成
	{
		const Dictionary a = SlowTreeGenerator::Generate(0, 1);
		const Dictionary b = SlowTreeGenerator::Generate(0, 2);
		Dictionary r;
		r["test"] = "seed_variation";
		const bool ok = String(a["error"]).is_empty() && String(b["error"]).is_empty()
			&& int64_t(a["vertex_count"]) > 0 && int64_t(b["vertex_count"]) > 0;
		r["ok"] = ok;
		if (!ok)
		{
			r["error"] = "种子变种生成失败";
		}
		if (ok)
		{
			++passed;
		}
		else
		{
			++failed;
		}
		results.append(r);
	}

	// 4) 内置默认模板(HelloTree, .vtree 解析路径)结构自检
	{
		NodeGraph graph;
		graph.buildDefaultTemplate();
		SlowTreeMeshResult result;
		const bool genOk = SlowTreeGenerator::GenerateFromGraph(graph, 0, result);
		Dictionary r;
		r["test"] = "default_template";
		String check;
		if (genOk)
		{
			check = CheckMesh(result.Mesh, int64_t(result.VertexCount), int64_t(result.TriangleCount));
		}
		const bool ok = genOk && check.is_empty();
		r["ok"] = ok;
		if (!ok)
		{
			r["error"] = genOk ? check : result.Error;
		}
		if (ok)
		{
			++passed;
		}
		else
		{
			++failed;
		}
		results.append(r);
	}

	// 5) v1 节点门: 含 Scatter 的图被校验层拒绝
	{
		NodeGraph graph;
		graph.clear();
		graph.addNode(NodeType::Trunk);
		graph.addNode(NodeType::Scatter);
		const String err = SlowTreeGenerator::ValidateGraph(graph);
		Dictionary r;
		r["test"] = "validate_rejects_scatter";
		const bool ok = !err.is_empty();
		r["ok"] = ok;
		if (!ok)
		{
			r["error"] = "含 Scatter 节点的图未被校验层拒绝";
		}
		if (ok)
		{
			++passed;
		}
		else
		{
			++failed;
		}
		results.append(r);
	}

	Dictionary out;
	out["ok"] = failed == 0;
	out["passed"] = passed;
	out["failed"] = failed;
	out["results"] = results;
	out["summary"] = vformat("SlowTree 自检: %d/%d 通过", passed, passed + failed);
	return out;
}
