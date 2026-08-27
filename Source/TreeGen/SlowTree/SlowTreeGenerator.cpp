#include "SlowTreeGenerator.h"

#include "SlowTreeCompute.h"
#include "SlowTreeMaterials.h"
#include "SlowTreePresets.h"
#include "TreeGenerator.h"
#include "VtreeIO.h"
#include "Nodes.h"

#include "TreeGen/TreeLeafOutline.h"
#include "TreeGen/TreeMath.h"

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

	// Seed != 0: 全局旋钮派生各节点种子; == 0: 保留模板种子(位级对拍锚点)。
	// CPU/GPU 两条路径共用(图会被就地改写, 每次生成前需重建或重新派生)。
	void DeriveNodeSeeds(NodeGraph& Graph, int64_t Seed)
	{
		if (Seed == 0)
		{
			return;
		}
		const auto depths = ComputeDepths(Graph);
		for (auto& [id, node] : Graph.nodes())
		{
			const int depth = depths.count(id) ? depths.at(id) : 0;
			SetNodeSeed(node.get(), DeriveNodeSeed(Seed, id, depth));
		}
	}


	// 叶卡轮廓(SpeedTree Mesh Cutout): 用 TreeGen 的叶片轮廓生成器填 cutoutPoints/cutoutTris。
	//
	// SlowTree 的 LeafCluster/Frond 早就写好了消费轮廓的通路(CPU 四处 + leaf_card.comp 的
	// mode=1 分支), 但没有任何东西填过数据, 所以无贴图下叶卡就是不透明矩形 —— 桃花那种大而正对
	// 镜头的花卡最刺眼。轮廓点是叶卡局部 [0,1]^2, SlowTree 自己按 hw/hs 映射到叶面, 所以这里
	// 生成的是**归一化剪影**, 长宽比交给节点自己的 leafSize/leafAspect, 不在这里预乘。
	//
	// 只做 LeafCluster, **不动 Frond**: Frond 本来就是沿脊线的连续叶带, 自带 widthBase/widthTip/
	// profilePow 的收尖轮廓和 serrate 裂片, 无贴图下已经读作有机叶形(水杉/棕榈就靠它)。给它加
	// cutout 会覆盖掉这套宽度曲线, 把能用的东西换掉。
	void FillLeafCutouts(NodeGraph& Graph)
	{
		for (auto& [id, node] : Graph.nodes())
		{
			if (node->getType() != NodeType::LeafCluster)
			{
				continue;
			}

			LeafClusterParams& p = static_cast<LeafClusterNode*>(node.get())->params;
			if (!p.cutoutPoints.empty())
			{
				// 模板自带轮廓(手绘或导入)优先, 不覆盖。
				continue;
			}

			// 叶形随宽高比走: 宽叶(银杏 1.15)钝头, 窄叶/针叶(柳 0.26 / 松 0.1)尖头。
			TreeGen::LeafOutlineShape shape;
			if (p.leafAspect >= 0.8f)
			{
				shape.TopAngle = 30.0f;
				shape.SideOffset = 0.50f;
			}
			else if (p.leafAspect <= 0.25f)
			{
				shape.TopAngle = 62.0f;
				shape.SideOffset = 0.42f;
			}

			// ArcSegments 1, 不是 2: 每片叶 9 个轮廓点 / 7 三角, 而 2 段是 17 点 / 15 三角。
			// 上游 Mesh Cutout 的本意是省**透明像素的 overdraw**(有 alpha 贴图时四边形浪费填充率),
			// 本项目无贴图、叶卡不透明, 所以省不到填充率, 轮廓纯粹是形状开销 —— 一片叶 7 三角
			// 已经是四边形的 3.5 倍, 再加细分不划算。
			std::vector<godot::Vector2> points;
			std::vector<uint32_t> tris;
			TreeGen::BuildLeafCutout(shape, uint32_t(p.seed), 1, 1.0f, points, tris);
			if (points.size() < 3 || tris.size() < 3)
			{
				continue;
			}

			p.cutoutPoints = points;
			p.cutoutTris = tris;
			p.useCutout = true;
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
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("generate", "preset", "seed", "use_gpu", "season"),
		&SlowTreeGenerator::Generate, DEFVAL(false), DEFVAL(2.0f));
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("generate_from_file", "vtree_path", "seed", "use_gpu", "season"),
		&SlowTreeGenerator::GenerateFromFile, DEFVAL(false), DEFVAL(2.0f));
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
	DeriveNodeSeeds(Graph, Seed);
	// 必须在派生种子之后、且 CPU/GPU 两条路径都做, 否则 GPUvsCPU 对拍会挂。
	FillLeafCutouts(Graph);

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

bool SlowTreeGenerator::RunGenerationGpu(NodeGraph& Graph, int64_t Seed, TreeMeshData& OutMesh,
                                         Dictionary* GpuStats, String& OutError)
{
	// 种子派生/图遍历/中心线/RNG/附着与 CPU 路径完全同一套代码;
	// 只有细分部分被 TreeGenerator 的 GPU 发射模式替换为描述子。
	DeriveNodeSeeds(Graph, Seed);
	FillLeafCutouts(Graph);

	const auto tEmit0 = std::chrono::steady_clock::now();

	TreeGpuEmission emission;
	TreeGenerator generator;
	generator.EnableGpuEmission(&emission);
	OutMesh = generator.generate(Graph);
	generator.EnableGpuEmission(nullptr);   // 恢复 CPU 模式(生成器随后销毁, 防御性)

	const double EmitMs = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - tEmit0).count();

	// 顶点硬上限: GPU 路径同样按 float 数检查(读回装配前拦截, 避免 64MB+ 缓冲)。
	if (emission.VertFloats > kMaxVertexFloats)
	{
		OutError = vformat(
			"生成结果超过顶点硬上限(%.1fM floats > %dM)。请降低叶量/细分。",
			double(emission.VertFloats) / (1024.0 * 1024.0), int(kMaxVertexFloats / (1024 * 1024)));
		return false;
	}

	Dictionary stats;
	if (!SlowTreeCompute::RunGpu(emission, OutMesh, &stats, OutError))
	{
		return false;
	}

	if (GpuStats)
	{
		(*GpuStats)["emit_ms"] = EmitMs;
		(*GpuStats)["device_ms"] = stats["device_ms"];
		(*GpuStats)["buffer_ms"] = stats["buffer_ms"];
		(*GpuStats)["setup_ms"] = stats["setup_ms"];
		(*GpuStats)["gpu_ms"] = stats["gpu_ms"];
		(*GpuStats)["readback_ms"] = stats["readback_ms"];
		(*GpuStats)["assemble_ms"] = stats["assemble_ms"];
		(*GpuStats)["vertex_floats"] = emission.VertFloats;
		(*GpuStats)["index_count"] = emission.IndexCount;
		(*GpuStats)["cylinder_descs"] = stats["cylinder_descs"];
		(*GpuStats)["collar_descs"] = stats["collar_descs"];
		(*GpuStats)["leaf_descs"] = stats["leaf_descs"];
		(*GpuStats)["frond_descs"] = stats["frond_descs"];
	}

	return true;
}

bool SlowTreeGenerator::ConvertToGodotMesh(
	const TreeMeshData& Data, SlowTreeMeshResult& Out, float Season, bool Evergreen)
{
	Out.Mesh.instantiate();
	Out.SurfaceMaterials.clear();

	// 一个 surface, 顶点色。原实现是每个 MeshBatch 一个 surface(桃 7 个), 与本项目
	// "单 surface 顶点色" 约定冲突, 而 PCG 的 spawn_meshes 依赖那个约定。
	//
	// 代价(明知): 逐节点的 roughness/metallic/normal/sss 全部丢弃, 只保留 albedo 进顶点色。
	// 这是无资产契约的必然结果, 也是彩墨 NPR 想要的方向。
	//
	// 同时修掉一个既有 bug: 叶顶点的实际布局是
	//   pos(0-2) normal(3-5) uv(6-7) **albedo(8-10) wind(11-12)** anchor(13-15)
	// (见 TreeGenerator 的 emitVert 与 leaf_card.comp 的 stride 注释), 而原 AddBatchSurface
	// 按 wind(8-9) colour(10-12) 读, 于是每片叶的顶点色变成 (col.b, windW, leafPhase)。
	// LeafCluster 的 windW 恒为 1.0, 顶点色又以乘法叠在材质 albedo 上, 所以**所有叶片一直被
	// 悄悄压暗和偏色** —— 之前记录的"针叶颜色偏灰绿"就是这个。
	std::vector<Vector3> positions;
	std::vector<Vector3> normals;
	std::vector<Vector2> uvs;
	std::vector<Color> colors;
	std::vector<float> wind;
	std::vector<float> anchors;
	std::vector<int32_t> indices;

	uint32_t vertexBase = 0;
	for (const MeshBatch& batch : Data.batches)
	{
		if (batch.vertices.empty() || batch.indices.empty())
		{
			continue;
		}

		const int stride = batch.isLeaf ? 16 : 10;
		const int64_t count = int64_t(batch.vertices.size()) / stride;
		if (count == 0)
		{
			continue;
		}

		// 花不随季节变色。判据: 叶片是绿色主导, 花不是 —— GetSeasonLeafColor 对 needle/blossom
		// 直接返回夏色, 所以这里只需要认出"不是叶子"。常绿由调用方给(颜色推不出来)。
		const MaterialParams& mat = batch.material;
		const bool bTreatAsUnchanging = Evergreen || (mat.albedo.x > mat.albedo.y);

		const float* v = batch.vertices.data();
		for (int64_t i = 0; i < count; ++i)
		{
			const float* q = v + i * stride;
			positions.push_back(Vector3(q[0], q[1], q[2]));
			normals.push_back(Vector3(q[3], q[4], q[5]));
			uvs.push_back(Vector2(q[6], q[7]));

			if (batch.isLeaf)
			{
				const Color Base(q[8], q[9], q[10]);
				// 季节抖动的种子必须**逐叶**而不是逐顶点, 否则同一片叶的几个顶点拿到不同季节,
				// 叶面上出现渐变。同一片叶的所有顶点共享摆动锚点(basePos, 13-15), 拿它做哈希
				// 就得到稳定的逐叶种子。
				const uint32_t Seed = uint32_t(
					int32_t(q[13] * 733.0f) * 73856093
					^ int32_t(q[14] * 733.0f) * 19349663
					^ int32_t(q[15] * 733.0f) * 83492791);
				const float Noised = TreeGen::GetNoisedLeafSeason(Seed, Season);
				colors.push_back(TreeGen::GetSeasonLeafColor(
					Base, Noised, bTreatAsUnchanging, false));
				wind.push_back(q[11]);
				wind.push_back(q[12]);
				anchors.push_back(q[13]);
				anchors.push_back(q[14]);
				anchors.push_back(q[15]);
			}
			else
			{
				// 枝干没有顶点色通道, 用该 batch 的材质 albedo 填, 这样单 surface 也能分色。
				colors.push_back(Color(mat.albedo.x, mat.albedo.y, mat.albedo.z));
				wind.push_back(q[8]);
				wind.push_back(q[9]);
				// 枝干无摆动锚点, 用自身位置(等于不产生额外位移)。
				anchors.push_back(q[0]);
				anchors.push_back(q[1]);
				anchors.push_back(q[2]);
			}
		}

		for (const uint32_t idx : batch.indices)
		{
			indices.push_back(int32_t(vertexBase + idx));
		}

		vertexBase += uint32_t(count);
		Out.TriangleCount += uint32_t(batch.indices.size() / 3);
	}

	if (positions.empty() || indices.empty())
	{
		Out.VertexCount = 0;
		Out.SurfaceCount = 0;
		return true;
	}

	PackedVector3Array pos;
	PackedVector3Array nrm;
	PackedVector2Array uv;
	PackedColorArray col;
	PackedFloat32Array wnd;
	PackedFloat32Array anc;
	PackedInt32Array idx;
	pos.resize(int64_t(positions.size()));
	nrm.resize(int64_t(normals.size()));
	uv.resize(int64_t(uvs.size()));
	col.resize(int64_t(colors.size()));
	wnd.resize(int64_t(wind.size()));
	anc.resize(int64_t(anchors.size()));
	idx.resize(int64_t(indices.size()));
	for (size_t i = 0; i < positions.size(); ++i)
	{
		pos.set(int64_t(i), positions[i]);
		nrm.set(int64_t(i), normals[i]);
		uv.set(int64_t(i), uvs[i]);
		col.set(int64_t(i), colors[i]);
	}
	for (size_t i = 0; i < wind.size(); ++i)
	{
		wnd.set(int64_t(i), wind[i]);
	}
	for (size_t i = 0; i < anchors.size(); ++i)
	{
		anc.set(int64_t(i), anchors[i]);
	}
	for (size_t i = 0; i < indices.size(); ++i)
	{
		idx.set(int64_t(i), indices[i]);
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = pos;
	arrays[Mesh::ARRAY_NORMAL] = nrm;
	arrays[Mesh::ARRAY_TEX_UV] = uv;
	arrays[Mesh::ARRAY_COLOR] = col;
	arrays[Mesh::ARRAY_CUSTOM0] = wnd;
	arrays[Mesh::ARRAY_CUSTOM1] = anc;
	arrays[Mesh::ARRAY_INDEX] = idx;

	uint64_t customFlags = 0;
	customFlags |= uint64_t(Mesh::ARRAY_CUSTOM_RG_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
	customFlags |= uint64_t(Mesh::ARRAY_CUSTOM_RGB_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT;
	const BitField<Mesh::ArrayFormat> flags{ int64_t(customFlags) };

	Out.Mesh->add_surface_from_arrays(
		Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), flags);

	// 一棵树一个材质。叶卡是薄片必须双面, 枝干是闭合管、双面只多花填充率不出错, 所以统一
	// CULL_DISABLED 而不是为此拆回两个 surface。
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	material->set_roughness(0.85f);
	material->set_metallic(0.0f);
	Out.Mesh->surface_set_material(0, material);
	Out.SurfaceMaterials.push_back(material);

	Out.VertexCount = vertexBase;
	Out.SurfaceCount = 1;
	return true;
}

bool SlowTreeGenerator::GenerateFromGraph(NodeGraph& Graph, int64_t Seed,
                                         SlowTreeMeshResult& Out, bool UseGpu,
                                         float Season, bool Evergreen)
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
	Dictionary gpuStats;
	if (UseGpu)
	{
		if (!RunGenerationGpu(Graph, Seed, data, &gpuStats, error))
		{
			Out.Error = error;
			return false;
		}
	}
	else if (!RunGeneration(Graph, Seed, data, error))
	{
		Out.Error = error;
		return false;
	}
	const auto t2 = std::chrono::steady_clock::now();
	if (!ConvertToGodotMesh(data, Out, Season, Evergreen))
	{
		Out.Error = "装配 ArrayMesh 失败。";
		return false;
	}
	const auto t3 = std::chrono::steady_clock::now();

	Out.GraphBuildMs = float(std::chrono::duration<double, std::milli>(t1 - t0).count());
	Out.GenerationMs = float(std::chrono::duration<double, std::milli>(t2 - t1).count());
	Out.ConvertMs = float(std::chrono::duration<double, std::milli>(t3 - t2).count());
	if (UseGpu)
	{
		// GPU 侧合计 = 设备 + 上传 + 管线 + dispatch + 读回 + 装配
		Out.GpuMs = float(double(gpuStats["device_ms"]) + double(gpuStats["buffer_ms"]) +
		                  double(gpuStats["setup_ms"]) + double(gpuStats["gpu_ms"]) +
		                  double(gpuStats["readback_ms"]) + double(gpuStats["assemble_ms"]));
	}
	return true;
}

Dictionary SlowTreeGenerator::Generate(int32_t Preset, int64_t Seed, bool UseGpu, float Season)
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
		GenerateFromGraph(graph, Seed, result, UseGpu, Season, SlowTreePresets::IsEvergreen(Preset));
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
	out["gpu_ms"] = result.GpuMs;
	out["convert_ms"] = result.ConvertMs;
	return out;
}

Dictionary SlowTreeGenerator::GenerateFromFile(const String& VtreePath, int64_t Seed,
                                              bool UseGpu, float Season)
{
	SlowTreeMeshResult result;
	NodeGraph graph;
	if (!VtreeIO::load(graph, VtreePath.utf8().get_data()))
	{
		result.Error = vformat("无法加载 .vtree: %s(首行需为 VEGTOOL)。", VtreePath);
	}
	else
	{
		GenerateFromGraph(graph, Seed, result, UseGpu, Season, false);
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
