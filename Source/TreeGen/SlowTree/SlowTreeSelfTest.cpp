#include "SlowTreeSelfTest.h"

#include "NodeGraph.h"
#include "SlowTreeGenerator.h"
#include "SlowTreePresets.h"
#include "VtreeIO.h"

#include <cstring>

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

	// Stage 2: GPU vs CPU 对拍。同一预设图分别走 CPU 细分与 GPU compute 读回:
	//  - batch 数/顺序/尺寸一致
	//  - 索引位级一致(纯数据搬运, GPU 侧无任何重排)
	//  - 顶点浮点: pos/normal |Δ| ≤ 5e-4(仅环角 cos/sin 与 collar 的 pos 推导上 GPU;
	//    collar 法线在 normalize(mix(localDir, rdir, proj)) 输入近零时把 ~1e-7 三角差
	//    放大到 ~1.8e-4, 两种实现各取合法方向, 视觉不可见);
	//    uv/wind/albedo/anchor ≤ 1e-3(collar vCoord 由 pos 推导会被 vPerUnit 放大;
	//    其余为纯拷贝/除法, 预期位级一致, 位级差异数作为观察值上报)
	//  - GPU 读回结果走同一 ConvertToGodotMesh 装配, 再做结构自检(端到端)
	Dictionary RunGpuVsCpu(int32_t Preset, int64_t Seed)
	{
		Dictionary r;
		r["test"] = "gpu_vs_cpu";
		r["preset"] = Preset;
		r["name"] = String(SlowTreePresets::GetPresetName(Preset));
		r["seed"] = Seed;

		// 种子派生会就地改写节点图, 两条路径各建一份图。
		NodeGraph cpuGraph;
		NodeGraph gpuGraph;
		if (!SlowTreePresets::BuildGraph(Preset, cpuGraph) || !SlowTreePresets::BuildGraph(Preset, gpuGraph))
		{
			r["ok"] = false;
			r["error"] = "预设图构建失败";
			return r;
		}

		TreeMeshData cpuMesh;
		String cpuErr;
		const auto tCpu0 = std::chrono::steady_clock::now();
		if (!SlowTreeGenerator::RunGeneration(cpuGraph, Seed, cpuMesh, cpuErr))
		{
			r["ok"] = false;
			r["error"] = "CPU 生成失败: " + cpuErr;
			return r;
		}
		const double cpuMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - tCpu0).count();

		TreeMeshData gpuMesh;
		Dictionary gpuStats;
		String gpuErr;
		if (!SlowTreeGenerator::RunGenerationGpu(gpuGraph, Seed, gpuMesh, &gpuStats, gpuErr))
		{
			r["ok"] = false;
			r["error"] = "GPU 生成失败: " + gpuErr;
			return r;
		}

		const size_t batchCount = cpuMesh.batches.size();
		if (gpuMesh.batches.size() != batchCount)
		{
			r["ok"] = false;
			r["error"] = vformat("batch 数不一致: CPU %d vs GPU %d",
				int64_t(batchCount), int64_t(gpuMesh.batches.size()));
			return r;
		}

		int64_t bitDiffFloats = 0; // 位级差异浮点数(观察值; 预期只有 collar vCoord 附近)
		int64_t totalFloats = 0;
		for (size_t b = 0; b < batchCount; ++b)
		{
			const MeshBatch& cb = cpuMesh.batches[b];
			const MeshBatch& gb = gpuMesh.batches[b];
			if (cb.isLeaf != gb.isLeaf)
			{
				r["ok"] = false;
				r["error"] = vformat("batch %d 叶/枝类型不一致", int64_t(b));
				return r;
			}
			if (cb.vertices.size() != gb.vertices.size() || cb.indices.size() != gb.indices.size())
			{
				r["ok"] = false;
				r["error"] = vformat("batch %d 尺寸不一致: 顶点 %d vs %d, 索引 %d vs %d",
					int64_t(b), int64_t(cb.vertices.size()), int64_t(gb.vertices.size()),
					int64_t(cb.indices.size()), int64_t(gb.indices.size()));
				return r;
			}

			// 索引 memcmp 位级一致。
			if (memcmp(cb.indices.data(), gb.indices.data(), cb.indices.size() * sizeof(uint32_t)) != 0)
			{
				r["ok"] = false;
				r["error"] = vformat("batch %d 索引与 CPU 路径位级不一致", int64_t(b));
				return r;
			}

			// 顶点浮点: pos/normal 通道 ε=5e-4(三角 + 病态 normalize 放大, 见文件头);
			// 其余数据通道(uv/wind/albedo/anchor)为纯拷贝/除法, 但 collar 的 vCoord
			// 由 pos 推导(误差被 vPerUnit 放大), 故放宽到 1e-3; 位级差异数作为观察值。
			const size_t stride = cb.isLeaf ? 16 : 10;
			totalFloats += int64_t(cb.vertices.size());
			for (size_t i = 0; i < cb.vertices.size(); ++i)
			{
				const float a = cb.vertices[i];
				const float v = gb.vertices[i];
				const float d = (a > v) ? (a - v) : (v - a);
				// pos/normal ε=5e-4: 病态 collar 顶点的 normalize 放大(见文件头注释),
				// 实测最大 ~1.8e-4; 其余通道 1e-3。
				const float kEps = (i % stride < 6) ? 5e-4f : 1e-3f;
				if (d > kEps)
				{
					r["ok"] = false;
					r["error"] = vformat(
						"batch %d float[%d] 偏差过大: CPU %.6f vs GPU %.6f (顶点 %d, 通道 %d)",
						int64_t(b), int64_t(i), double(a), double(v),
						int64_t(i / stride), int64_t(i % stride));
					// 诊断转储: 失败顶点前后各 2 个顶点的 CPU/GPU 原始浮点。
					const size_t winStart = (i / stride >= 2) ? (i / stride - 2) * stride : 0;
					const size_t winEnd = std::min(cb.vertices.size(), (i / stride + 3) * stride);
					String dump = vformat("  diag batch %d verts[%d..%d):", int64_t(b),
						int64_t(winStart / stride), int64_t(winEnd / stride));
					UtilityFunctions::print(dump);
					for (size_t k = winStart; k < winEnd; ++k)
					{
						UtilityFunctions::print(vformat("    [%d] CPU %.6f  GPU %.6f%s",
							int64_t(k), double(cb.vertices[k]), double(gb.vertices[k]),
							(k % stride == 0) ? "  <v" : ""));
					}
					return r;
				}
				if (memcmp(&a, &v, sizeof(float)) != 0)
				{
					++bitDiffFloats;
				}
			}
		}

		// GPU 读回 → 同一装配逻辑 → 结构自检。
		SlowTreeMeshResult gpuResult;
		if (!SlowTreeGenerator::ConvertToGodotMesh(gpuMesh, gpuResult))
		{
			r["ok"] = false;
			r["error"] = "GPU 输出转网格失败: " + gpuResult.Error;
			return r;
		}
		const String check = CheckMesh(gpuResult.Mesh, int64_t(gpuResult.VertexCount), int64_t(gpuResult.TriangleCount));
		if (!check.is_empty())
		{
			r["ok"] = false;
			r["error"] = check;
			return r;
		}

		// 性能观察: GPU 路径各阶段合计(device/upload/setup/dispatch/readback/assemble)。
		const char* kGpuStageKeys[] = { "device_ms", "buffer_ms", "setup_ms", "gpu_ms", "readback_ms", "assemble_ms" };
		float gpuTotalMs = 0.0f;
		for (const char* key : kGpuStageKeys)
		{
			if (gpuStats.has(key))
			{
				gpuTotalMs += float(gpuStats[key]);
			}
		}

		r["vertex_count"] = gpuResult.VertexCount;
		r["triangle_count"] = gpuResult.TriangleCount;
		r["surface_count"] = gpuResult.SurfaceCount;
		r["bit_diff_floats"] = bitDiffFloats;
		r["total_floats"] = totalFloats;
		r["gpu_total_ms"] = gpuTotalMs;
		// 分段计时(骨架/发射/上传/细分/读回/装配)——性能 crossover 观察表。
		r["gpu_phases"] = vformat(
			"emit=%.1f device=%.1f buffer=%.1f setup=%.1f dispatch=%.1f readback=%.1f assemble=%.1f",
			double(gpuStats.get("emit_ms", 0.0)), double(gpuStats.get("device_ms", 0.0)),
			double(gpuStats.get("buffer_ms", 0.0)), double(gpuStats.get("setup_ms", 0.0)),
			double(gpuStats.get("gpu_ms", 0.0)), double(gpuStats.get("readback_ms", 0.0)),
			double(gpuStats.get("assemble_ms", 0.0)));
		r["cpu_ms"] = cpuMs;
		r["ok"] = true;
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

	// 6) Stage 2: GPU vs CPU 对拍(每预设 seed=0 + 预设 0 的种子变种)
	for (int32_t p = 0; p < presetCount; ++p)
	{
		const Dictionary r = RunGpuVsCpu(p, 0);
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
	{
		const Dictionary r = RunGpuVsCpu(0, 12345);
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

	Dictionary out;
	out["ok"] = failed == 0;
	out["passed"] = passed;
	out["failed"] = failed;
	out["results"] = results;
	out["summary"] = vformat("SlowTree 自检: %d/%d 通过", passed, passed + failed);
	return out;
}
