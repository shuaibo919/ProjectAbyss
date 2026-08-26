#include "SlowTreeGenerator.h"

#include "SlowTreeMaterials.h"
#include "SlowTreePresets.h"
#include "TreeGenerator.h"
#include "VtreeIO.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <queue>
#include <unordered_map>
#include <vector>

using namespace godot;

namespace
{
	// ---- 全局种子派生 ----
	// Murmur3 终结器: 把 globalSeed 与 (nodeId, depth) 混合成每节点种子。
	uint32_t Mix32(uint32_t h)
	{
		h ^= h >> 16;
		h *= 0x85ebca6bu;
		h ^= h >> 13;
		h *= 0xc2b2ae35u;
		h ^= h >> 16;
		return h;
	}

	int DeriveNodeSeed(int64_t GlobalSeed, NodeId Id, int Depth)
	{
		const uint32_t lo = uint32_t(GlobalSeed & 0xffffffff);
		const uint32_t hi = uint32_t((GlobalSeed >> 32) & 0xffffffff);
		uint32_t h = lo ^ (hi * 2654435761u);
		h = Mix32(h ^ (uint32_t(Id) * 2654435761u) ^ (uint32_t(Depth) * 2246822519u));
		return int(h & 0x7fffffffu);
	}

	// 从根(无输入连线的 Trunk)出发 BFS 求每节点深度(根=0, 子=父+1)。
	std::unordered_map<NodeId, int> ComputeDepths(const NodeGraph& Graph)
	{
		std::unordered_map<NodeId, int> depths;
		std::vector<std::pair<NodeId, const TreeNode*>> roots;
		for (const auto& [id, node] : Graph.nodes())
		{
			if (node->getType() != NodeType::Trunk)
			{
				continue;
			}
			bool hasInput = false;
			for (const auto& pin : node->inputPins)
			{
				if (Graph.linkFromPin(pin.id) != INVALID_LINK)
				{
					hasInput = true;
					break;
				}
			}
			if (!hasInput)
			{
				roots.emplace_back(id, node.get());
			}
		}
		std::sort(roots.begin(), roots.end(),
			[](const auto& a, const auto& b) { return a.first < b.first; });

		std::queue<std::pair<NodeId, int>> frontier;
		for (const auto& [id, node] : roots)
		{
			frontier.push({ id, 0 });
		}
		while (!frontier.empty())
		{
			const auto [id, depth] = frontier.front();
			frontier.pop();
			if (depths.count(id))
			{
				continue;
			}
			depths[id] = depth;
			for (const TreeNode* child : Graph.childrenOf(id))
			{
				if (!depths.count(child->id))
				{
					frontier.push({ child->id, depth + 1 });
				}
			}
		}
		return depths;
	}

	// 带 seed 参数的节点类型: 派生种子统一覆盖(不含 Export/Import*)。
	void SetNodeSeed(TreeNode* Node, int Seed)
	{
		switch (Node->getType())
		{
			case NodeType::Trunk:       static_cast<TrunkNode*>(Node)->params.seed = Seed; break;
			case NodeType::Branch:      static_cast<BranchNode*>(Node)->params.seed = Seed; break;
			case NodeType::Twig:        static_cast<TwigNode*>(Node)->params.seed = Seed; break;
			case NodeType::LeafCluster: static_cast<LeafClusterNode*>(Node)->params.seed = Seed; break;
			case NodeType::Roots:       static_cast<RootsNode*>(Node)->params.seed = Seed; break;
			case NodeType::Spine:       static_cast<SpineNode*>(Node)->params.seed = Seed; break;
			case NodeType::Frond:       static_cast<FrondNode*>(Node)->params.seed = Seed; break;
#ifdef SLOWTREE_FULL_NODES
			case NodeType::Custom:      static_cast<CustomNode*>(Node)->params.seed = Seed; break;
			case NodeType::Scatter:     static_cast<ScatterNode*>(Node)->params.seed = Seed; break;
#endif
			default: break;
		}
	}

	// 把 SlowTree batch 装配成一个 ArrayMesh surface(分支 stride 10, 叶 stride 16)。
	// 风场通道从第一天就进顶点格式: CUSTOM0 = (windWeight, windPhase) RG32F;
	// 叶片另带 COLOR = albedo、CUSTOM1 = anchor RGB32F(Stage 3 风着色器消费)。
	void AddBatchSurface(const MeshBatch& Batch, Ref<ArrayMesh>& Mesh)
	{
		const int stride = Batch.isLeaf ? 16 : 10;
		const int64_t vertexCount = int64_t(Batch.vertices.size()) / stride;
		if (vertexCount == 0 || Batch.indices.empty())
		{
			return;
		}

		PackedVector3Array positions;
		PackedVector3Array normals;
		PackedVector2Array uvs;
		PackedFloat32Array wind;     // (weight, phase) RG32F, 每顶点 2 floats
		PackedColorArray colors;     // 仅叶片
		PackedFloat32Array anchors;  // 仅叶片, 每顶点 3 floats
		positions.resize(vertexCount);
		normals.resize(vertexCount);
		uvs.resize(vertexCount);
		wind.resize(vertexCount * 2);
		if (Batch.isLeaf)
		{
			colors.resize(vertexCount);
			anchors.resize(vertexCount * 3);
		}

		const float* v = Batch.vertices.data();
		for (int64_t i = 0; i < vertexCount; ++i)
		{
			const float* p = v + i * stride;
			positions.set(i, Vector3(p[0], p[1], p[2]));
			normals.set(i, Vector3(p[3], p[4], p[5]));
			uvs.set(i, Vector2(p[6], p[7]));
			wind.set(i * 2 + 0, p[8]);
			wind.set(i * 2 + 1, p[9]);
			if (Batch.isLeaf)
			{
				colors.set(i, Color(p[10], p[11], p[12]));
				anchors.set(i * 3 + 0, p[13]);
				anchors.set(i * 3 + 1, p[14]);
				anchors.set(i * 3 + 2, p[15]);
			}
		}

		PackedInt32Array indices;
		indices.resize(int64_t(Batch.indices.size()));
		for (int64_t i = 0; i < int64_t(Batch.indices.size()); ++i)
		{
			indices.set(i, int32_t(Batch.indices[i]));
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = positions;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		arrays[Mesh::ARRAY_CUSTOM0] = wind;
		if (Batch.isLeaf)
		{
			arrays[Mesh::ARRAY_COLOR] = colors;
			arrays[Mesh::ARRAY_CUSTOM1] = anchors;
		}
		arrays[Mesh::ARRAY_INDEX] = indices;

		// fork 的自定义数组分量数来自调用方 flags(见 rendering_server.cpp
		// mesh_create_surface_data_from_arrays): 类型值 << SHIFT。风 RG32F、锚 RGB32F。
		// 数据必须是 PackedFloat32Array(引擎 _surface_set_data 的硬性类型检查)。
		uint64_t customFlags = 0;
		customFlags |= uint64_t(Mesh::ARRAY_CUSTOM_RG_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
		if (Batch.isLeaf)
		{
			customFlags |= uint64_t(Mesh::ARRAY_CUSTOM_RGB_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT;
		}
		const BitField<Mesh::ArrayFormat> flags{ int64_t(customFlags) };

		Mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), flags);
	}
} // namespace

void SlowTreeGenerator::_bind_methods()
{
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("get_preset_count"), &SlowTreeGenerator::GetPresetCount);
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("get_preset_name", "preset"), &SlowTreeGenerator::GetPresetName);
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("generate", "preset", "seed"), &SlowTreeGenerator::Generate);
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("generate_from_file", "vtree_path", "seed"), &SlowTreeGenerator::GenerateFromFile);
}

int32_t SlowTreeGenerator::GetPresetCount()
{
	return SlowTreePresets::GetPresetCount();
}

String SlowTreeGenerator::GetPresetName(int32_t Preset)
{
	return String(SlowTreePresets::GetPresetName(Preset));
}

String SlowTreeGenerator::ValidateGraph(const NodeGraph& Graph)
{
	for (const auto& [id, node] : Graph.nodes())
	{
		const NodeType type = node->getType();
		if (type == NodeType::Custom || type == NodeType::ImportTrunk ||
			type == NodeType::ImportLeaf || type == NodeType::Scatter)
		{
			return vformat(
				"节点 #%d (%s) 类型不受支持: v1 仅支持程序化节点 "
				"(Trunk/Roots/Branch/Twig/LeafCluster/Spine/Frond)。",
				int64_t(id), String(node->getLabel()));
		}
	}

	// 至少一棵根 Trunk, 否则无几何可生成。
	bool hasRoot = false;
	for (const auto& [id, node] : Graph.nodes())
	{
		if (node->getType() != NodeType::Trunk)
		{
			continue;
		}
		bool hasInput = false;
		for (const auto& pin : node->inputPins)
		{
			if (Graph.linkFromPin(pin.id) != INVALID_LINK)
			{
				hasInput = true;
				break;
			}
		}
		if (!hasInput)
		{
			hasRoot = true;
			break;
		}
	}
	if (!hasRoot)
	{
		return "图中没有根 Trunk 节点(无输入连线的 Trunk), 无法生成。";
	}

	return String();
}

bool SlowTreeGenerator::RunGeneration(NodeGraph& Graph, int64_t Seed, TreeMeshData& OutMesh, String& OutError)
{
	// Seed != 0: 全局旋钮派生各节点种子; == 0: 保留模板种子(位级对拍锚点)。
	if (Seed != 0)
	{
		const auto depths = ComputeDepths(Graph);
		for (auto& [id, node] : Graph.nodes())
		{
			const int depth = depths.count(id) ? depths.at(id) : 0;
			SetNodeSeed(node.get(), DeriveNodeSeed(Seed, id, depth));
		}
	}

	TreeGenerator generator;
	OutMesh = generator.generate(Graph);

	// 顶点硬上限(v1 CPU 路径: 超限即报错; Stage 2 GPU 路径改为截断标志 + 警告)。
	uint64_t totalVertexFloats = 0;
	for (const MeshBatch& batch : OutMesh.batches)
	{
		totalVertexFloats += batch.vertices.size();
	}
	if (totalVertexFloats > kMaxVertexFloats)
	{
		OutError = vformat(
			"生成结果超过顶点硬上限(%.1fM floats > %dM)。请降低叶量/细分或等待 GPU 路径。",
			double(totalVertexFloats) / (1024.0 * 1024.0), int(kMaxVertexFloats / (1024 * 1024)));
		return false;
	}

	return true;
}

bool SlowTreeGenerator::ConvertToGodotMesh(const TreeMeshData& Data, SlowTreeMeshResult& Out)
{
	Out.Mesh.instantiate();
	Out.SurfaceMaterials.clear();

	uint32_t surfaceIndex = 0;
	for (const MeshBatch& batch : Data.batches)
	{
		if (batch.vertices.empty() || batch.indices.empty())
		{
			continue;
		}
		AddBatchSurface(batch, Out.Mesh);
		Ref<StandardMaterial3D> material = SlowTreeMaterials::Create(batch.material, batch.isLeaf);
		Out.Mesh->surface_set_material(int32_t(surfaceIndex), material);
		Out.SurfaceMaterials.push_back(material);

		const int stride = batch.isLeaf ? 16 : 10;
		Out.VertexCount += uint32_t(batch.vertices.size() / stride);
		Out.TriangleCount += uint32_t(batch.indices.size() / 3);
		++surfaceIndex;
	}
	Out.SurfaceCount = surfaceIndex;
	return true;
}

bool SlowTreeGenerator::GenerateFromGraph(NodeGraph& Graph, int64_t Seed, SlowTreeMeshResult& Out)
{
	const auto t0 = std::chrono::steady_clock::now();

	const String validation = ValidateGraph(Graph);
	if (!validation.is_empty())
	{
		Out.Error = validation;
		return false;
	}

	TreeMeshData data;
	const auto t1 = std::chrono::steady_clock::now();
	String error;
	if (!RunGeneration(Graph, Seed, data, error))
	{
		Out.Error = error;
		return false;
	}
	const auto t2 = std::chrono::steady_clock::now();
	if (!ConvertToGodotMesh(data, Out))
	{
		Out.Error = "装配 ArrayMesh 失败。";
		return false;
	}
	const auto t3 = std::chrono::steady_clock::now();

	Out.GraphBuildMs = float(std::chrono::duration<double, std::milli>(t1 - t0).count());
	Out.GenerationMs = float(std::chrono::duration<double, std::milli>(t2 - t1).count());
	Out.ConvertMs = float(std::chrono::duration<double, std::milli>(t3 - t2).count());
	return true;
}

Dictionary SlowTreeGenerator::Generate(int32_t Preset, int64_t Seed)
{
	SlowTreeMeshResult result;
	NodeGraph graph;
	const bool built = SlowTreePresets::BuildGraph(Preset, graph);
	if (!built)
	{
		result.Error = vformat("预设 %d 越界(共 %d 个)。", int64_t(Preset), int64_t(SlowTreePresets::GetPresetCount()));
	}
	else
	{
		GenerateFromGraph(graph, Seed, result);
	}

	Dictionary out;
	out["mesh"] = result.Mesh;
	Array materials;
	for (const Ref<Material>& material : result.SurfaceMaterials)
	{
		materials.append(material);
	}
	out["materials"] = materials;
	out["error"] = result.Error;
	out["vertex_count"] = result.VertexCount;
	out["triangle_count"] = result.TriangleCount;
	out["surface_count"] = result.SurfaceCount;
	out["truncated"] = result.Truncated;
	out["generation_ms"] = result.GenerationMs;
	out["convert_ms"] = result.ConvertMs;
	return out;
}

Dictionary SlowTreeGenerator::GenerateFromFile(const String& VtreePath, int64_t Seed)
{
	SlowTreeMeshResult result;
	NodeGraph graph;
	if (!VtreeIO::load(graph, VtreePath.utf8().get_data()))
	{
		result.Error = vformat("无法加载 .vtree: %s(首行需为 VEGTOOL)。", VtreePath);
	}
	else
	{
		GenerateFromGraph(graph, Seed, result);
	}

	Dictionary out;
	out["mesh"] = result.Mesh;
	Array materials;
	for (const Ref<Material>& material : result.SurfaceMaterials)
	{
		materials.append(material);
	}
	out["materials"] = materials;
	out["error"] = result.Error;
	out["vertex_count"] = result.VertexCount;
	out["triangle_count"] = result.TriangleCount;
	out["surface_count"] = result.SurfaceCount;
	out["truncated"] = result.Truncated;
	out["generation_ms"] = result.GenerationMs;
	out["convert_ms"] = result.ConvertMs;
	return out;
}
