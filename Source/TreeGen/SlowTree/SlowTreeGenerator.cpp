#include "SlowTreeGenerator.h"
#include "../ProceduralTreeGrowthParameters.h"

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
#include <cmath>
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

	// 用户面形变旋钮: 4 个乘法乘数覆盖到节点参数上(见 SlowTreeTuning 头注)。
	// 只动粗细/密度; count 取整 ≥1(0 倍会生成空树)。
	void ApplyTuning(NodeGraph& Graph, const SlowTreeTuning& Tuning)
	{
		if (Tuning.Foliage.bSpeciesRules)
		{
			SlowTreeFoliage::ApplySpeciesRules(Graph, Tuning.Foliage.Preset);
		}
		for (auto& [id, node] : Graph.nodes())
		{
			switch (node->getType())
			{
				case NodeType::Trunk:
				{
					// 粗细 = 半径绝对缩放。endRadius 是绝对的, 跟着乘以保持锥度比不变
					// (预设间的 taperPow/形状差异不受旋钮影响)。
					TrunkParams& p = static_cast<TrunkNode*>(node.get())->params;
					p.startRadius *= Tuning.TrunkThickness;
					p.endRadius *= Tuning.TrunkThickness;
					break;
				}
				case NodeType::Roots:
				{
					// 根半径是"树干基部半径 × radiusScale"的相对量, 直接乘在比例上。
					RootsParams& p = static_cast<RootsNode*>(node.get())->params;
					p.radiusScale *= Tuning.RootThickness;
					break;
				}
				case NodeType::Branch:
				{
					BranchParams& p = static_cast<BranchNode*>(node.get())->params;
					p.radiusScale *= Tuning.BranchThickness;
					// 密度按模式分组: Interval 模式每节枝数, 其余按条数。
					if (p.mode == BranchMode::Interval)
					{
						p.branchesPerNode = std::max(1, int(std::round(p.branchesPerNode * Tuning.BranchDensity)));
					}
					else
					{
						p.branchCount = std::max(1, int(std::round(p.branchCount * Tuning.BranchDensity)));
					}
					break;
				}
				case NodeType::Twig:
				{
					TwigParams& p = static_cast<TwigNode*>(node.get())->params;
					p.radiusScale *= Tuning.BranchThickness;
					p.twigCount = std::max(1, int(std::round(p.twigCount * Tuning.BranchDensity)));
					break;
				}
				case NodeType::Spine:
				{
					// 叶轴是肉眼可见的管子, 跟枝杈粗细走; spineCount 是叶簇数量不是枝杈, 不受密度影响。
					SpineParams& p = static_cast<SpineNode*>(node.get())->params;
					p.radiusScale *= Tuning.BranchThickness;
					break;
				}
				case NodeType::LeafCluster:
				{
					if (!Tuning.Foliage.bCrossedCards)
					{
						LeafClusterParams& Params = static_cast<LeafClusterNode*>(node.get())->params;
						Params.leafCount = std::max(0, int32_t(std::round(Params.leafCount * Tuning.Foliage.Density)));
					}
					break;
				}
				default:
					break;
			}
		}
	}

	// GDScript Dictionary(键可缺)→ SlowTreeTuning。缺键按 1.0。
	SlowTreeTuning BuildTuning(const Dictionary& Tuning)
	{
		SlowTreeTuning t;
		const Ref<ProceduralTreeGrowthParameters> Growth = Tuning.get("growth_parameters", Variant());
		if (Growth.is_valid()) { t.Foliage.Growth = Growth->MakeSnapshot(); }
		t.Foliage.Growth.bEnabled = bool(Tuning.get("structural_branches", true));
		t.Foliage.Growth.bDebug = bool(Tuning.get("growth_debug", false));
		t.Foliage.bCrossedCards = bool(Tuning.get("crossed_cards", false));
		t.Foliage.bSpeciesRules = bool(Tuning.get("species_rules", false));
		t.Foliage.bGenerateLeaves = bool(Tuning.get("generate_leaves", true));
		t.Foliage.Density = std::clamp(float(Tuning.get("leaf_density", 1.0f)), 0.0f, 1.0f);
		t.Foliage.MaxCards = std::max(0, int32_t(Tuning.get("max_leaves", 12000)));
		t.Foliage.MaxSegments = std::max(1, int32_t(Tuning.get("max_segments", 20000)));
		t.Foliage.RadialSegments = std::clamp(int32_t(Tuning.get("radial_segments", 32)), 3, 32);
		if (Tuning.has("trunk_thickness")) { t.TrunkThickness = float(Tuning["trunk_thickness"]); }
		if (Tuning.has("root_thickness")) { t.RootThickness = float(Tuning["root_thickness"]); }
		if (Tuning.has("branch_thickness")) { t.BranchThickness = float(Tuning["branch_thickness"]); }
		if (Tuning.has("branch_density")) { t.BranchDensity = float(Tuning["branch_density"]); }
		t.TrunkThickness = std::clamp(t.TrunkThickness, 0.1f, 5.0f);
		t.RootThickness = std::clamp(t.RootThickness, 0.1f, 5.0f);
		t.BranchThickness = std::clamp(t.BranchThickness, 0.1f, 5.0f);
		t.BranchDensity = std::clamp(t.BranchDensity, 0.1f, 5.0f);
		return t;
	}

} // namespace

void SlowTreeGenerator::_bind_methods()
{
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("get_preset_count"), &SlowTreeGenerator::GetPresetCount);
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("get_preset_name", "preset"), &SlowTreeGenerator::GetPresetName);
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("generate", "preset", "seed", "use_gpu", "season", "tuning"),
		&SlowTreeGenerator::Generate, DEFVAL(false), DEFVAL(2.0f), DEFVAL(Dictionary()));
	ClassDB::bind_static_method("SlowTreeGenerator", D_METHOD("generate_from_file", "vtree_path", "seed", "use_gpu", "season", "tuning"),
		&SlowTreeGenerator::GenerateFromFile, DEFVAL(false), DEFVAL(2.0f), DEFVAL(Dictionary()));
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

bool SlowTreeGenerator::RunGeneration(NodeGraph& Graph, int64_t Seed, TreeMeshData& OutMesh, String& OutError,
									  const TreeFoliageOptions& Options)
{
	// Seed != 0: 全局旋钮派生各节点种子; == 0: 保留模板种子(位级对拍锚点)。
	DeriveNodeSeeds(Graph, Seed);
	// 必须在派生种子之后、且 CPU/GPU 两条路径都做, 否则 GPUvsCPU 对拍会挂。
	FillLeafCutouts(Graph);

	TreeGenerator generator;
	generator.SetFoliageOptions(Options);
	if (SlowTreeGrowth::IsEnabled(Options))
	{
		SlowTreeGrowth::Generate(Graph, Options, OutMesh);
	}
	else
	{
		OutMesh = generator.generate(Graph);
	}
	SlowTreeFoliage::AppendCards(OutMesh, Options);

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

bool SlowTreeGenerator::RunGenerationGpu(NodeGraph& Graph, int64_t Seed, TreeMeshData& OutMesh, Dictionary* GpuStats,
										 String& OutError, const TreeFoliageOptions& Options)
{
	// Connected junction topology is emitted on the CPU; the legacy independent-tube kernels cannot express it.
	if (SlowTreeGrowth::IsEnabled(Options))
	{
		if (GpuStats)
		{
			for (const char* Key : {"device_ms", "buffer_ms", "setup_ms", "gpu_ms", "readback_ms", "assemble_ms"})
			{
				(*GpuStats)[Key] = 0.0;
			}
		}
		return RunGeneration(Graph, Seed, OutMesh, OutError, Options);
	}
	// 种子派生/图遍历/中心线/RNG/附着与 CPU 路径完全同一套代码;
	// 只有细分部分被 TreeGenerator 的 GPU 发射模式替换为描述子。
	DeriveNodeSeeds(Graph, Seed);
	FillLeafCutouts(Graph);

	const auto tEmit0 = std::chrono::steady_clock::now();

	TreeGpuEmission emission;
	TreeGenerator generator;
	generator.SetFoliageOptions(Options);
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

	SlowTreeFoliage::AppendCards(OutMesh, Options);
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

SlowTreePreparedMesh SlowTreeGenerator::PrepareMesh(const TreeMeshData& Data, float Season, bool Evergreen)
{
	const auto Started = std::chrono::steady_clock::now();
	SlowTreePreparedMesh Prepared;
	Prepared.LeafCount = Data.FoliageCardCount;
	Prepared.Growth = Data.Growth;
	Prepared.bTruncated = Data.bSegmentBudgetApplied;
	// Legacy geometry remains one surface. Masked sprays have their own shader/surface.
	for (int32_t Group = 0; Group < 2; ++Group)
	{
		const bool bMasked = Group == 1;
		bool bHasTangents = false;
		int64_t VertexCount = 0;
		int64_t IndexCount = 0;
		for (const MeshBatch& Batch : Data.batches)
		{
			if (Batch.bMaskedFoliage == bMasked && !Batch.indices.empty())
			{
				VertexCount += int64_t(Batch.vertices.size()) / (Batch.isLeaf ? 16 : 10);
				IndexCount += int64_t(Batch.indices.size());
				bHasTangents = bHasTangents || !Batch.WoodTangents.empty();
			}
		}
		if (VertexCount == 0 || IndexCount == 0)
		{
			continue;
		}

		PackedVector3Array Positions;
		PackedFloat32Array Tangents;
		PackedVector3Array Normals;
		PackedVector2Array Uvs;
		PackedColorArray Colors;
		PackedFloat32Array Wind;
		PackedFloat32Array Anchors;
		PackedInt32Array Indices;
		Positions.resize(VertexCount);
		if (bHasTangents)
		{
			Tangents.resize(VertexCount * 4);
		}
		Normals.resize(VertexCount);
		Uvs.resize(VertexCount);
		Colors.resize(VertexCount);
		Wind.resize(VertexCount * 2);
		Anchors.resize(VertexCount * 3);
		Indices.resize(IndexCount);
		Vector3* PositionPtr = Positions.ptrw();
		float* TangentPtr = Tangents.ptrw();
		Vector3* NormalPtr = Normals.ptrw();
		Vector2* UvPtr = Uvs.ptrw();
		Color* ColorPtr = Colors.ptrw();
		float* WindPtr = Wind.ptrw();
		float* AnchorPtr = Anchors.ptrw();
		int32_t* IndexPtr = Indices.ptrw();
		int64_t VertexBase = 0;
		int64_t IndexBase = 0;
		for (const MeshBatch& Batch : Data.batches)
		{
			if (Batch.bMaskedFoliage != bMasked || Batch.indices.empty())
			{
				continue;
			}
			const int32_t Stride = Batch.isLeaf ? 16 : 10;
			const int64_t Count = int64_t(Batch.vertices.size()) / Stride;
			Prepared.bBambooCulm = Prepared.bBambooCulm || Batch.bBambooCulm;
			if (Batch.BarkPreset >= 0)
			{
				Prepared.BarkPreset = Batch.BarkPreset;
			}
			const bool bUnchanging = Evergreen || Batch.material.albedo.x > Batch.material.albedo.y;
			for (int64_t Index = 0; Index < Count; ++Index)
			{
				const float* Source = Batch.vertices.data() + Index * Stride;
				const int64_t Destination = VertexBase + Index;
				PositionPtr[Destination] = Vector3(Source[0], Source[1], Source[2]);
				NormalPtr[Destination] = Vector3(Source[3], Source[4], Source[5]);
				if (Batch.WoodTangents.size() == size_t(Count))
				{
					for (int32_t Component = 0; Component < 4; ++Component)
					{
						TangentPtr[Destination * 4 + Component] = Batch.WoodTangents[size_t(Index)][Component];
					}
				}
				UvPtr[Destination] = Vector2(Source[6], Source[7]);
				if (Batch.isLeaf)
				{
					const Color Base(Source[8], Source[9], Source[10]);
					const uint32_t LeafSeed = uint32_t(int32_t(Source[13] * 733.0f)) * 73856093u ^
											  uint32_t(int32_t(Source[14] * 733.0f)) * 19349663u ^
											  uint32_t(int32_t(Source[15] * 733.0f)) * 83492791u;
					ColorPtr[Destination] =
						bMasked ? Base
								: TreeGen::GetSeasonLeafColor(Base, TreeGen::GetNoisedLeafSeason(LeafSeed, Season),
															  bUnchanging, false);
					WindPtr[Destination * 2] = Source[11];
					WindPtr[Destination * 2 + 1] = Source[12];
					for (int32_t Component = 0; Component < 3; ++Component)
					{
						AnchorPtr[Destination * 3 + Component] = Source[13 + Component];
					}
				}
				else
				{
					const Vector3& Albedo = Batch.WoodColors.size() == size_t(Count) ? Batch.WoodColors[size_t(Index)] : Batch.material.albedo;
					ColorPtr[Destination] = Color(Albedo.x, Albedo.y, Albedo.z);
					WindPtr[Destination * 2] = Source[8];
					WindPtr[Destination * 2 + 1] = Source[9];
					for (int32_t Component = 0; Component < 3; ++Component)
					{
						AnchorPtr[Destination * 3 + Component] = Source[Component];
					}
				}
			}
			for (const uint32_t Index : Batch.indices)
			{
				IndexPtr[IndexBase++] = int32_t(VertexBase + Index);
			}
			VertexBase += Count;
		}
		Array Arrays;
		Arrays.resize(Mesh::ARRAY_MAX);
		Arrays[Mesh::ARRAY_VERTEX] = Positions;
		Arrays[Mesh::ARRAY_NORMAL] = Normals;
		if (bHasTangents)
		{
			Arrays[Mesh::ARRAY_TANGENT] = Tangents;
		}
		Arrays[Mesh::ARRAY_TEX_UV] = Uvs;
		Arrays[Mesh::ARRAY_COLOR] = Colors;
		Arrays[Mesh::ARRAY_CUSTOM0] = Wind;
		Arrays[Mesh::ARRAY_CUSTOM1] = Anchors;
		Arrays[Mesh::ARRAY_INDEX] = Indices;
		Prepared.Surfaces.push_back(Arrays);
		Prepared.MaskedSurfaces.push_back(bMasked);
		Prepared.VertexCount += uint32_t(VertexCount);
		Prepared.TriangleCount += uint32_t(IndexCount / 3);
	}
	Prepared.PackMs =
		float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count());
	return Prepared;
}

bool SlowTreeGenerator::CommitMesh(const SlowTreePreparedMesh& Prepared, SlowTreeMeshResult& Out, float Season)
{
	if (!Prepared.Error.is_empty())
	{
		Out.Error = Prepared.Error;
		return false;
	}
	const auto Started = std::chrono::steady_clock::now();
	Out.Mesh.instantiate();
	Out.SurfaceMaterials.clear();
	const uint64_t CustomFlags = (uint64_t(Mesh::ARRAY_CUSTOM_RG_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
								 (uint64_t(Mesh::ARRAY_CUSTOM_RGB_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT);
	for (size_t Index = 0; Index < Prepared.Surfaces.size(); ++Index)
	{
		Out.Mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Prepared.Surfaces[Index], TypedArray<Array>(),
										  Dictionary(), BitField<Mesh::ArrayFormat>(int64_t(CustomFlags)));
		Ref<Material> SurfaceMaterial;
		if (Prepared.MaskedSurfaces[Index])
		{
			Out.Mesh->set_meta("treegen_masked_foliage", true);
			SurfaceMaterial = SlowTreeFoliage::CreateMaterial(Season);
			Out.Mesh->surface_set_name(int32_t(Index), "Foliage");
		}
		else
		{
			Ref<StandardMaterial3D> SolidMaterial;
			SolidMaterial.instantiate();
			SolidMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			SolidMaterial->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
			SolidMaterial->set_roughness(0.85f);
			if (Prepared.bBambooCulm)
			{
				SolidMaterial->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, SlowTreeMaterials::GetBambooFiberTexture());
				SolidMaterial->set_roughness(0.62f);
			}
			else if (Prepared.BarkPreset >= 0)
			{
				SolidMaterial->set_texture(BaseMaterial3D::TEXTURE_ALBEDO,
					SlowTreeMaterials::GetBarkTexture(Prepared.BarkPreset, SlowTreeMaterials::EBarkTexture::Albedo));
				SolidMaterial->set_texture(BaseMaterial3D::TEXTURE_NORMAL,
					SlowTreeMaterials::GetBarkTexture(Prepared.BarkPreset, SlowTreeMaterials::EBarkTexture::Normal));
				SolidMaterial->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
				SolidMaterial->set_normal_scale(0.65f);
				SolidMaterial->set_roughness(0.93f);
			}
			SurfaceMaterial = SolidMaterial;
			Out.Mesh->surface_set_name(int32_t(Index), "Wood / geometry");
		}
		Out.Mesh->surface_set_material(int32_t(Index), SurfaceMaterial);
		Out.SurfaceMaterials.push_back(SurfaceMaterial);
	}
	Out.VertexCount = Prepared.VertexCount;
	Out.TriangleCount = Prepared.TriangleCount;
	Out.SurfaceCount = uint32_t(Prepared.Surfaces.size());
	Out.LeafCount = Prepared.LeafCount;
	Out.Growth = Prepared.Growth;
	Out.Truncated = Prepared.bTruncated;
	Out.GenerationMs = Prepared.GenerationMs;
	Out.ConvertMs =
		Prepared.PackMs +
		float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count());
	return true;
}

SlowTreePreparedMesh SlowTreeGenerator::PreparePreset(int32_t Preset, int64_t Seed, float Season,
													  const SlowTreeTuning& Tuning)
{
	NodeGraph Graph;
	SlowTreePreparedMesh Prepared;
	if (!SlowTreePresets::BuildGraph(Preset, Graph))
	{
		Prepared.Error = "Invalid SlowTree preset.";
		return Prepared;
	}
	SlowTreeTuning Settings = Tuning;
	Settings.Foliage.Preset = Preset;
	ApplyTuning(Graph, Settings);
	TreeMeshData Data;
	const auto Started = std::chrono::steady_clock::now();
	if (!RunGeneration(Graph, Seed, Data, Prepared.Error, Settings.Foliage))
	{
		return Prepared;
	}
	const float GenerationMs =
		float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count());
	Prepared = PrepareMesh(Data, Season, SlowTreePresets::IsEvergreen(Preset));
	Prepared.GenerationMs = GenerationMs;
	return Prepared;
}

bool SlowTreeGenerator::ConvertToGodotMesh(const TreeMeshData& Data, SlowTreeMeshResult& Out, float Season,
										   bool Evergreen)
{
	return CommitMesh(PrepareMesh(Data, Season, Evergreen), Out, Season);
}

bool SlowTreeGenerator::GenerateFromGraph(NodeGraph& Graph, int64_t Seed,
                                         SlowTreeMeshResult& Out, bool UseGpu,
                                         float Season, bool Evergreen,
                                         const SlowTreeTuning& Tuning)
{
	const auto t0 = std::chrono::steady_clock::now();

	const String validation = ValidateGraph(Graph);
	if (!validation.is_empty())
	{
		Out.Error = validation;
		return false;
	}

	// 旋钮是生成期形变: 在种子派生前作用于图(派生只覆盖 seed 字段, 顺序上无冲突,
	// 但先形变后派生语义更清晰 — 派生基于形变后的结构, 深度/节点 id 不变)。
	ApplyTuning(Graph, Tuning);

	TreeMeshData data;
	const auto t1 = std::chrono::steady_clock::now();
	String error;
	Dictionary gpuStats;
	if (UseGpu)
	{
		if (!RunGenerationGpu(Graph, Seed, data, &gpuStats, error, Tuning.Foliage))
		{
			Out.Error = error;
			return false;
		}
	}
	else if (!RunGeneration(Graph, Seed, data, error, Tuning.Foliage))
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

Dictionary SlowTreeGenerator::Generate(int32_t Preset, int64_t Seed, bool UseGpu, float Season,
                                       const Dictionary& Tuning)
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
		SlowTreeTuning Settings = BuildTuning(Tuning);
		Settings.Foliage.Preset = Preset;
		GenerateFromGraph(graph, Seed, result, UseGpu, Season, SlowTreePresets::IsEvergreen(Preset), Settings);
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
	out["leaf_count"] = result.LeafCount;
	out["truncated"] = result.Truncated;
	out["generation_ms"] = result.GenerationMs;
	out["gpu_ms"] = result.GpuMs;
	Dictionary GrowthStats;
	GrowthStats["trunks"] = result.Growth.Trunks;
	GrowthStats["stems"] = result.Growth.Stems;
	GrowthStats["forks"] = result.Growth.Forks;
	GrowthStats["segments"] = result.Growth.Segments;
	GrowthStats["junctions"] = result.Growth.Junctions;
	GrowthStats["omitted_junctions"] = result.Growth.OmittedJunctions;
	GrowthStats["wood_vertices"] = result.Growth.WoodVertices;
	out["growth_stats"] = GrowthStats;
	out["tessellation_backend"] = result.Growth.Stems > 0 ? "cpu_connected" : (UseGpu ? "gpu" : "cpu");
	if (!result.Growth.Points.empty())
	{
		Dictionary Skeleton;
		PackedVector3Array Points;
		PackedFloat32Array Radii;
		PackedInt32Array Offsets;
		PackedInt32Array Parents;
		PackedInt32Array Attachments;
		PackedInt32Array Roles;
		for (const Vector3& Point : result.Growth.Points) { Points.push_back(Point); }
		for (const float Radius : result.Growth.Radii) { Radii.push_back(Radius); }
		for (const int32_t Offset : result.Growth.Offsets) { Offsets.push_back(Offset); }
		for (const int32_t Parent : result.Growth.Parents) { Parents.push_back(Parent); }
		for (const int32_t Attachment : result.Growth.Attachments) { Attachments.push_back(Attachment); }
		for (const int32_t Role : result.Growth.Roles) { Roles.push_back(Role); }
		Skeleton["points"] = Points;
		Skeleton["radii"] = Radii;
		Skeleton["offsets"] = Offsets;
		Skeleton["parents"] = Parents;
		Skeleton["attachments"] = Attachments;
		Skeleton["roles"] = Roles;
		out["growth_skeleton"] = Skeleton;
	}
	out["convert_ms"] = result.ConvertMs;
	return out;
}

Dictionary SlowTreeGenerator::GenerateFromFile(const String& VtreePath, int64_t Seed,
                                              bool UseGpu, float Season, const Dictionary& Tuning)
{
	SlowTreeMeshResult result;
	NodeGraph graph;
	if (!VtreeIO::load(graph, VtreePath.utf8().get_data()))
	{
		result.Error = vformat("无法加载 .vtree: %s(首行需为 VEGTOOL)。", VtreePath);
	}
	else
	{
		SlowTreeTuning Settings = BuildTuning(Tuning);
		// Species recipes describe bundled presets only; an imported graph owns its complete topology.
		Settings.Foliage.Growth.bEnabled = false;
		GenerateFromGraph(graph, Seed, result, UseGpu, Season, false, Settings);
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
	out["leaf_count"] = result.LeafCount;
	out["truncated"] = result.Truncated;
	out["generation_ms"] = result.GenerationMs;
	out["convert_ms"] = result.ConvertMs;
	return out;
}
