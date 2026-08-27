// Vendored from Reference/SlowTree/src/generator/TreeGenerator.cpp (commit 10e6c66)。
// 改动(见 UPSTREAM_SYNC.md):
//  - 头文件路径拉平; LuaEngine.h / io/MeshImport.h 仅在 SLOWTREE_FULL_NODES 下引入
//  - glm 数学全部改写为 godot::Vector3/Vector2(项目决策: 核心只用 Godot 数学库,
//    不做上游位级对拍; 算法与参数语义保留)
//  - generate(): 根 Trunk 按 id 排序后逐株处理(上游按 unordered_map 迭代序, 不确定;
//    单根 Trunk 工程行为与上游一致)
//  - 删除 generateSubtree / generateSpecimen / generateChain / measureSpecimenParent
//    (应用导出功能); 各 build 内的标本/祖先链/测量分支保留(相关成员恒为初始值, 不生效)
//  - afterAppend() 为空实现(拾取/高亮是应用视口功能, 网格批次不受影响); 调用点保留
//  - Custom / ImportTrunk / ImportLeaf / Scatter 四类 builder 与对应分派置于
//    SLOWTREE_FULL_NODES 门后(v1 仅程序化节点)
#include "TreeGenerator.h"
#include "CylinderSegment.h"
#ifdef SLOWTREE_FULL_NODES
#include "LuaEngine.h"
#include "io/MeshImport.h"
#endif
#include "Nodes.h"
#include <godot_cpp/core/math.hpp>
#include <cmath>
#include <random>
#include <algorithm>
#include <unordered_map>

static constexpr int MAX_DEPTH = 6;

namespace
{
	constexpr float kPi    = 3.14159265359f;
	constexpr float kTwoPi = 6.28318530718f;
}

// 每根实例的 variance 采样: 在 [-v, +v] 均匀取偏移(v<=0 时返回0, 完全等同关闭)。
static inline float varyBy(std::mt19937& rng, float v) {
    if (v <= 0.0f) return 0.0f;
    return std::uniform_real_distribution<float>(-v, v)(rng);
}

// ---- 工具 ----
godot::Vector3 TreeGenerator::perpendicular(godot::Vector3 dir) {
    godot::Vector3 ref = (std::abs(dir.y) < 0.9f) ? godot::Vector3(0,1,0) : godot::Vector3(1,0,0);
    return ref.cross(dir).normalized();
}

godot::Vector3 TreeGenerator::rotateAroundAxis(godot::Vector3 v, godot::Vector3 axis, float angleDeg) {
    float rad = godot::Math::deg_to_rad(angleDeg);
    float c   = std::cos(rad), s = std::sin(rad);
    return v*c + axis.cross(v)*s + axis*axis.dot(v)*(1-c);
}

// 按比例 t∈[0,1] 从 rings 中插值位置、切线方向、right轴
void TreeGenerator::sampleRings(const std::vector<BranchRing>& rings, float t,
    godot::Vector3& outPos, godot::Vector3& outDir, godot::Vector3& outRight, float& outRadius)
{
    if (rings.empty()) return;
    if (rings.size() == 1) {
        outPos    = rings[0].center;
        outDir    = rings[0].up;
        outRight  = rings[0].right;
        outRadius = rings[0].radius;
        return;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    float fi   = t * (float)(rings.size() - 1);
    int   lo   = (int)fi;
    int   hi   = std::min(lo + 1, (int)rings.size() - 1);
    float frac = fi - (float)lo;

    outPos    = rings[lo].center.lerp(rings[hi].center, frac);
    outDir    = rings[lo].up.lerp(rings[hi].up, frac).normalized();
    outRight  = rings[lo].right.lerp(rings[hi].right, frac).normalized();
    outRadius = godot::Math::lerp(rings[lo].radius, rings[hi].radius, frac);
}

// 沿 rings 中心线生成一条骨链: boneCount 段 → boneCount+1 个关节。
// 关节 0 在基部(t=0), 父接到"父枝骨范围内离基部最近的骨"(无父范围则 parent=-1=根)。
// 其余关节沿 t=i/boneCount 均布, 依次首尾相连。结果追加进 m_out->skeleton。
// outBase/outCount 回传新范围, 供调用者设为子节点的父骨范围(m_parentBoneBase/Count)。
void TreeGenerator::emitBoneChain(const std::vector<BranchRing>& rings, int boneCount,
                                  int simGroup, const std::string& name,
                                  int& outBase, int& outCount) {
    outBase = -1; outCount = 0;
    if (!m_out || rings.size() < 2) return;
    // 该节点不绑骨(boneCount<=0): 不产出骨, 但把祖先父骨范围原样回传,
    // 使子节点仍挂到祖先骨上(骨链不断裂), 从而实现"逐节点关闭绑骨"。
    if (boneCount <= 0) { outBase = m_parentBoneBase; outCount = m_parentBoneCount; return; }
    int segs = boneCount;

    godot::Vector3 basePos = rings.front().center;
    // 首关节的父骨: 父范围内离基部最近者(按 3D 距离)。
    int parentOfFirst = -1;
    if (m_parentBoneCount > 0 && m_parentBoneBase >= 0) {
        float best = 1e30f;
        for (int b = m_parentBoneBase; b < m_parentBoneBase + m_parentBoneCount &&
                                       b < (int)m_out->skeleton.size(); ++b) {
            float d = m_out->skeleton[b].pos.distance_to(basePos);
            if (d < best) { best = d; parentOfFirst = b; }
        }
    }

    int base = (int)m_out->skeleton.size();
    for (int i = 0; i <= segs; ++i) {
        float t = (float)i / (float)segs;
        godot::Vector3 pos, dir, right; float rad;
        sampleRings(rings, t, pos, dir, right, rad);
        TreeMeshData::SkelBone bone;
        bone.pos      = pos;
        bone.parent   = (i == 0) ? parentOfFirst : (base + i - 1);
        bone.name     = name + "_" + std::to_string(i);
        bone.simGroup = simGroup;
        m_out->skeleton.push_back(bone);
    }
    outBase = base;
    outCount = segs + 1;
}

MeshBatch& TreeGenerator::getBatch(const MaterialParams& mat, bool isLeaf, bool instanced) {
    // 归并键必须涵盖全部影响渲染的材质字段：仅比 albedo 会把 albedo 相同但
    // roughness/metallic/贴图等不同的材质错误合并，导致调一个节点的粗糙度
    // 影响到另一节点(共用了同一 batch 的材质)。
    auto sameMaterial = [](const MaterialParams& a, const MaterialParams& b) {
        return a.albedo.distance_to(b.albedo) < 0.001f
            && std::abs(a.roughness   - b.roughness)   < 0.001f
            && std::abs(a.metallic    - b.metallic)    < 0.001f
            && std::abs(a.aoStrength  - b.aoStrength)  < 0.001f
            && std::abs(a.sssStrength - b.sssStrength) < 0.001f
            && std::abs(a.alphaCutoff - b.alphaCutoff) < 0.001f
            && a.baseColorTex == b.baseColorTex
            && a.roughnessTex == b.roughnessTex
            && a.normalTex    == b.normalTex
            && a.opacityTex   == b.opacityTex;
    };
    for (auto& b : m_out->batches) {
        if (b.isLeaf == isLeaf && b.instanced == instanced && sameMaterial(b.material, mat))
            return b;
    }
    MeshBatch nb;
    nb.material  = mat;
    nb.isLeaf    = isLeaf;
    nb.instanced = instanced;
    m_out->batches.push_back(std::move(nb));
    return m_out->batches.back();
}

void TreeGenerator::appendCylinder(MeshBatch& batch,
                                    const std::vector<BranchRing>& rings, int sides,
                                    float uvTilingU, float uvTilingV) {
    if (rings.size() < 2) return;
    float pi2 = kTwoPi;

    // 估算总圆柱长度（用于V轴UV缩放）
    float totalLen = 0.0f;
    for (size_t i = 1; i < rings.size(); ++i)
        totalLen += (rings[i].center - rings[i-1].center).length();
    if (totalLen < 1e-5f) totalLen = 1.0f;

    float vAccum = 0.0f;
    // U 沿圆周方向平铺：为保证接缝无缝，取整数次（至少1次）
    float uTiling = std::max(1.0f, std::round(uvTilingU));
    for (size_t ri = 0; ri + 1 < rings.size(); ++ri) {
        const BranchRing& bot = rings[ri];
        const BranchRing& top = rings[ri+1];
        float segLen = (top.center - bot.center).length();
        // V 沿长度平铺 uvTilingV 次，避免整段被单张纹理纵向拉伸
        float vBot = (vAccum / totalLen) * uvTilingV;
        float vTop = ((vAccum + segLen) / totalLen) * uvTilingV;
        vAccum += segLen;

        if (m_gpuEmit) {
            // Stage 2 GPU: 发射段描述子, 顶点/索引展开交给 cylinder.comp。
            emitGpuBranchSeg(batch, bot, top, (int)ri, (int)rings.size() - 1,
                             vBot, vTop, uTiling, sides);
            continue;
        }

        // 每个顶点: pos(3)+normal(3)+uv(2)+wind(2) = 10 floats
        uint32_t base = (uint32_t)(batch.vertices.size() / 10);
        int lastRing = (int)rings.size() - 1;
        for (int ring = 0; ring < 2; ++ring) {
            const BranchRing& r = (ring == 0) ? bot : top;
            float vCoord = (ring == 0) ? vBot : vTop;
            // tRing: 该环沿枝条从基部(0)到尖端(1)的比例, 尖端风力权重更大
            int   ringIdx = (ring == 0) ? (int)ri : (int)ri + 1;
            float tRing   = (lastRing > 0) ? (float)ringIdx / (float)lastRing : 0.0f;
            float w       = m_windW * tRing;
            for (int j = 0; j <= sides; ++j) {
                float angle = (float)j / (float)sides * pi2;
                float uCoord = (float)j / (float)sides * uTiling;
                godot::Vector3 localDir = r.right * std::cos(angle)
                                   + r.up.cross(r.right) * std::sin(angle);
                godot::Vector3 pos    = r.center + localDir * r.radius;
                godot::Vector3 normal = localDir.normalized();
                batch.vertices.insert(batch.vertices.end(),
                    {pos.x,pos.y,pos.z, normal.x,normal.y,normal.z, uCoord, vCoord, w, m_windPhase});
            }
        }
        int rv = sides + 1;
        for (int j = 0; j < sides; ++j) {
            uint32_t b0 = base + j,      b1 = base + j + 1;
            uint32_t t0 = base + rv + j, t1 = base + rv + j + 1;
            batch.indices.insert(batch.indices.end(), {b0,b1,t0, b1,t1,t0});
        }
    }
}

// SpeedTree 式"焊接(weld)"过渡：把子枝基部一圈的每个顶点单独投影到父级表面，
// 得到自然的鞍形交线(而非平圆盘)，再在枝条截面圈与该投影圈之间铺 weldSegs 段
// 环列做平滑(smoothstep→两端相切)过渡；上/下侧沿父级轴向做"蹼状"延展，
// 令枝腋处形成真实枝领而非突兀的吸盘。
void TreeGenerator::appendCollar(MeshBatch& batch,
    godot::Vector3 parentC, godot::Vector3 parentA, float parentR,
    godot::Vector3 childBase, godot::Vector3 childDir, godot::Vector3 childRight,
    float startR, float baseFlare, int sides,
    float uvTilingU, float uvTilingV, float branchTotalLen,
    const std::vector<BranchRing>* trunkRings,
    float collarSink)
{
    if (baseFlare <= 1.0f || parentR <= 1e-5f) return;
    float pi2 = kTwoPi;
    godot::Vector3 A   = parentA.normalized();
    godot::Vector3 up  = childDir.normalized();
    godot::Vector3 fwd = up.cross(childRight).normalized();

    const int weldSegs = 4;                          // 过渡环数(越多越圆滑)
    float flareMax  = startR * (baseFlare - 1.0f);   // 向外扩张量(径向)
    // 上/下"蹼"沿父级轴向的延展量(下侧枝腋略大, 更接近真实枝领)。
    // 用 startR 设上限：baseFlare 较大(如树根 2.5)时不至于把裙边沿轴拉成过长尖刺，
    // 同时仍保留足够延展避免交界生硬(盒子感)。竖直枝 startR 小、上限自然不触发。
    float upperSpread = std::min(flareMax * 1.2f, startR * 0.9f);
    float lowerSpread = std::min(flareMax * 1.6f, startR * 1.2f);
    float landingR    = parentR * 0.99f;             // 末端略沉入父级表面, 被父级面遮住→消除悬空缝隙
    float uTiling  = std::max(1.0f, std::round(uvTilingU));
    float vPerUnit = (branchTotalLen > 1e-5f) ? uvTilingV / branchTotalLen : 0.0f;

    // 当传入父级(树干)rings时: 裙边"落点"半径随树干在该高度的真实半径变化(而非常数)，
    // 使根盘外圈/上缘随树干锥度+树盘外扩起伏贴合，避免树干变细处外圈悬空(圆盘外圈贴不上)。
    // 只有 Roots 会传 trunkRings，Branch/Twig 传 nullptr → 行为与之前完全一致。
    auto landingAt = [&](float axial) -> float {
        if (!trunkRings || trunkRings->size() < 2) return landingR;
        const auto& tr = *trunkRings;
        float a0 = (tr.front().center - parentC).dot(A);
        for (size_t i = 1; i < tr.size(); ++i) {
            float a1 = (tr[i].center - parentC).dot(A);
            if (axial <= a1 || i + 1 == tr.size()) {
                float denom = (a1 - a0);
                float f = (std::abs(denom) > 1e-5f) ? (axial - a0) / denom : 0.0f;
                f = std::clamp(f, 0.0f, 1.0f);
                return godot::Math::lerp(tr[i-1].radius, tr[i].radius, f) * 0.99f;
            }
            a0 = a1;
        }
        return landingR;
    };

    auto smooth = [](float x){ x = std::clamp(x, 0.0f, 1.0f); return x*x*(3.0f-2.0f*x); };

    if (m_gpuEmit) {
        // Stage 2 GPU: 发射裙边描述子(landingAt 的环采样在 collar.comp 内复现)。
        // 注意: 传递的是归一化后的 A/up, 着色器不再二次归一化(避免除自身长度引入噪声)。
        emitGpuCollar(batch, parentC, A, parentR, childBase, up, childRight,
                      startR, flareMax, upperSpread, lowerSpread,
                      landingR, uTiling, vPerUnit, collarSink, sides, trunkRings);
        return;
    }

    int rv = sides + 1;
    uint32_t base = (uint32_t)(batch.vertices.size() / 10);

    for (int k = 0; k <= weldSegs; ++k) {
        float u    = (float)k / (float)weldSegs;
        float su   = smooth(u);
        float R    = startR + flareMax * su;   // 半径沿过渡外扩
        float proj = su;                       // 向父级表面投影比例(两端导数≈0→相切)
        for (int j = 0; j <= sides; ++j) {
            float a = (float)j / (float)sides * pi2;
            godot::Vector3 localDir = childRight * std::cos(a) + fwd * std::sin(a);
            godot::Vector3 p = childBase + localDir * R;      // 枝条截面上的点(外扩)

            // 逐顶点投影到父级圆柱表面(→自然鞍形), 上/下侧按 spread 沿轴延展成蹼
            godot::Vector3 v        = p - parentC;
            float     axialRaw = v.dot(A);
            float     s        = localDir.dot(A);  // 上(+)/下(-)侧因子[-1,1]
            float     axial    = axialRaw + (s >= 0.0f ? upperSpread : lowerSpread) * s;
            godot::Vector3 radial   = v - A * axialRaw;
            float     rl       = radial.length();
            godot::Vector3 rdir     = (rl > 1e-5f) ? radial / rl : localDir;
            // 落点半径按 collarSink 向树干内收，收陷幅度随 su(距圆心距离,0中心→1外圈)加大，
            // 使外圈更深地陷入树干实体、被树皮遮住，消除外圈悬空缝隙。
            float landR = landingAt(axial) * (1.0f - collarSink * su);
            godot::Vector3 onTrunk  = parentC + A * axial + rdir * landR;

            godot::Vector3 pos    = p.lerp(onTrunk, proj);          // 顶端贴枝条, 底端落父级
            godot::Vector3 normal = localDir.lerp(rdir, proj).normalized();
            float vCoord = -(pos - (childBase + localDir * startR)).length() * vPerUnit;
            float uCoord = (float)j / (float)sides * uTiling;
            batch.vertices.insert(batch.vertices.end(),
                {pos.x,pos.y,pos.z, normal.x,normal.y,normal.z, uCoord, vCoord, 0.0f, m_windPhase});
        }
    }
    // k=0 圈(枝条基部) → k=weldSegs 圈(父级表面), 环环相连成焊接带
    for (int k = 0; k < weldSegs; ++k) {
        for (int j = 0; j < sides; ++j) {
            uint32_t a0 = base + k*rv + j,     a1 = base + k*rv + j + 1;
            uint32_t b0 = base + (k+1)*rv + j, b1 = base + (k+1)*rv + j + 1;
            // k 圈→k+1 圈是"逆生长方向"(向父级投影)前进，故绕序相对
            // appendCylinder 翻转，使外表面朝外(否则正面被剔除→看到背面)
            batch.indices.insert(batch.indices.end(), {a0,b0,a1, a1,b0,b1});
        }
    }
}

// ---- 主入口 ----
TreeMeshData TreeGenerator::generate(NodeGraph& graph, NodeId hlNode) {
    TreeMeshData data;
    m_out = &data;
    m_hlNode = hlNode;
    // 一个工程内可有多棵植被：每个"无输入连线的 Trunk"都是一株独立植物，
    // 各自按自身 posX/posZ 摆放到场景中。
    // 上游按 unordered_map 迭代序处理(不确定); 这里收集根后按 id 排序,
    // 令多株工程逐次生成结果确定。单根 Trunk(所有预设/样例)与上游顺序一致。
    std::vector<std::pair<NodeId, TreeNode*>> roots;
    for (const auto& [id, node] : graph.nodes()) {
        const NodeType t = node->getType();
        if (t != NodeType::Trunk
#ifdef SLOWTREE_FULL_NODES
            && t != NodeType::ImportTrunk && t != NodeType::ImportLeaf
#endif
        ) continue;
        // 只处理根（输入未连接的），作为独立植株标志
        bool hasInput = false;
        for (const auto& pin : node->inputPins)
            if (graph.linkFromPin(pin.id) != INVALID_LINK) { hasInput = true; break; }
        if (hasInput) continue;
        roots.emplace_back(id, node.get());
    }
    std::sort(roots.begin(), roots.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });
    for (const auto& [id, node] : roots) {
        if (node->getType() == NodeType::Trunk) {
            const auto& tp = static_cast<const TrunkNode*>(node)->params;
            godot::Vector3 base = {tp.posX, 0.0f, tp.posZ};
            processNode(graph, node, nullptr, base, {0,1,0}, 1.0f, 0);
        }
#ifdef SLOWTREE_FULL_NODES
        else if (node->getType() == NodeType::ImportTrunk) {
            // 导入枝干同样是一株独立植物的根(输入未连接)
            processNode(graph, node, nullptr, {0,0,0}, {0,1,0}, 1.0f, 0);
        } else if (node->getType() == NodeType::ImportLeaf) {
            // 独立预览: 无输入连接时在原点渲染叶单体
            processNode(graph, node, nullptr, {0,0,0}, {0,1,0}, 1.0f, 0);
        }
#endif
    }
    m_out = nullptr;
    return data;
}

void TreeGenerator::processNode(
    const NodeGraph& graph, const TreeNode* node,
    const std::vector<BranchRing>* parentRings,
    godot::Vector3 origin, godot::Vector3 dir,
    float parentLen, int depth)
{
    if (!node || depth > MAX_DEPTH) return;

    // 祖先链模式: 只生成链上的节点, 其余(旁支/叶)直接跳过。多实例节点在各自 build
    // 函数内限制为只长一根链上代表实例。
    if (m_chainMode && !m_chainNodes.count(node->id)) return;

    // 高亮描边 + 拾取: 进入本节点时把 m_curNode 指向自身(供 afterAppend 登记拾取三角
    // 归属), 并按 isHl 置 m_hlCapture。子节点递归发生在各 build 函数内部, 会用自己的值
    // 覆盖这两个字段, 返回后恢复, 从而保证描边只含"当前节点这一层"的几何, 而拾取三角
    // 覆盖全部节点(每个节点标记自己那部分)。登记动作在各 append 处调用 afterAppend。
    bool isHl = (m_hlNode != INVALID_NODE && node->id == m_hlNode);
    bool prevCapture = m_hlCapture;
    NodeId prevNode = m_curNode;
    m_hlCapture = isHl;
    m_curNode = node->id;

    // 顶点风力: 按节点类型设定基权重, 相位按 id 哈希(令相邻枝条不同步)。
    // 递归子节点会覆盖这两个值, 返回后恢复(与 m_hlCapture 同理)。
    float prevWindW = m_windW;
    float prevWindPhase = m_windPhase;
    switch (node->getType()) {
        case NodeType::Trunk:       m_windW = 0.05f; break;
        case NodeType::Branch:      m_windW = 0.28f; break;
        case NodeType::Twig:        m_windW = 0.5f;  break;
        case NodeType::Roots:       m_windW = 0.0f;  break;
        case NodeType::LeafCluster: m_windW = 1.0f;  break;
        case NodeType::Spine:       m_windW = 0.5f;  break;
        case NodeType::Frond:       m_windW = 0.7f;  break;
        case NodeType::Export:      m_windW = 0.0f;  break;  // 导出节点无几何
        case NodeType::Custom:      m_windW = 0.28f; break;  // 同 Branch
        case NodeType::ImportTrunk: m_windW = 0.05f; break;  // 同 Trunk
        case NodeType::ImportLeaf:  m_windW = 1.0f;  break;  // 同叶
        case NodeType::Scatter:     m_windW = 1.0f;  break;  // 散布叶
    }
    // id 哈希 → [0, 2π) 相位
    m_windPhase = (float)((node->id * 2654435761u) % 62832u) / 10000.0f;

    switch (node->getType()) {
    case NodeType::Trunk: {
        const auto* tn = static_cast<const TrunkNode*>(node);
        auto rings = buildTrunk(tn, origin, dir);
        float trunkLen = tn->params.length;
        // 绑骨(方案A): 沿主干 rings 生成骨链(simGroup=0 树干), 作为子枝骨的父范围。
        int prevBase = m_parentBoneBase, prevCount = m_parentBoneCount;
        int myBase, myCount;
        emitBoneChain(rings, tn->params.boneCount, 0,
                      std::string(node->getLabel()) + std::to_string(node->id),
                      myBase, myCount);
        m_parentBoneBase = myBase; m_parentBoneCount = myCount;
        for (auto* child : graph.childrenOf(node->id))
            processNode(graph, child, &rings, origin, dir, trunkLen, depth+1);
        m_parentBoneBase = prevBase; m_parentBoneCount = prevCount;
        break;
    }
    case NodeType::Branch:
        if (parentRings && !parentRings->empty())
            buildBranches(static_cast<const BranchNode*>(node),
                          graph, *parentRings, parentLen, depth);
        break;
    case NodeType::Twig:
        if (parentRings && !parentRings->empty())
            buildTwig(static_cast<const TwigNode*>(node),
                      graph, *parentRings, parentLen, depth);
        break;
    case NodeType::Roots:
        if (parentRings && !parentRings->empty())
            buildRoots(static_cast<const RootsNode*>(node), *parentRings);
        break;
    case NodeType::LeafCluster:
        buildLeafCluster(static_cast<const LeafClusterNode*>(node), parentRings, origin, dir);
        break;
    case NodeType::Spine:
        if (parentRings && !parentRings->empty())
            buildSpine(static_cast<const SpineNode*>(node),
                       graph, *parentRings, parentLen, depth);
        break;
    case NodeType::Frond:
        // 方案B 测量: Frond 无实例循环, 直接用其父级(Spine)传入的长度/前环半径
        if (m_measureTarget == node->id && !m_measureDone) {
            m_specParentLen = parentLen;
            if (parentRings && !parentRings->empty())
                m_specParentRadius = parentRings->front().radius;
            m_measureDone = true;
        }
        buildFrond(static_cast<const FrondNode*>(node), parentRings, origin, dir);
        break;
    case NodeType::Export:
        break;  // 导出节点不生成几何(仅作为 Trunk→导出 的连接标记)
#ifdef SLOWTREE_FULL_NODES
    case NodeType::Custom:
        if (parentRings && !parentRings->empty())
            buildCustom(static_cast<const CustomNode*>(node),
                        graph, *parentRings, parentLen, depth);
        break;
    case NodeType::ImportTrunk: {
        const auto* itn = static_cast<const ImportTrunkNode*>(node);
        godot::Vector3 off = origin + godot::Vector3(itn->params.posX, 0.0f, itn->params.posZ);
        auto rings = buildImportTrunk(itn, off);
        float len = 1.0f;
        if (rings.size() >= 2)
            len = (rings.back().center - rings.front().center).length();
        // 供下游 Scatter 沿骨骼末端细枝散布: 记录已加载骨架 + 该枝干的缩放/平移。
        const ImportedMesh* prevTrunk = m_scatterTrunk;
        float prevTrunkScale = m_scatterTrunkScale;
        godot::Vector3 prevTrunkOffset = m_scatterTrunkOffset;
        MaterialParams prevTrunkMat = m_scatterTrunkMaterial;
        m_scatterTrunk = (itn->params.cached && itn->params.cached->hasSkeleton())
                       ? itn->params.cached.get() : nullptr;
        m_scatterTrunkScale = itn->params.scale;
        m_scatterTrunkOffset = off;
        m_scatterTrunkMaterial = itn->params.material;
        for (auto* child : graph.childrenOf(node->id))
            processNode(graph, child, &rings, off, {0,1,0}, len, depth+1);
        m_scatterTrunk = prevTrunk;
        m_scatterTrunkScale = prevTrunkScale;
        m_scatterTrunkOffset = prevTrunkOffset;
        m_scatterTrunkMaterial = prevTrunkMat;
        break;
    }
    case NodeType::ImportLeaf:
        buildImportLeaf(static_cast<const ImportLeafNode*>(node), origin);
        break;
    case NodeType::Scatter:
        buildScatter(static_cast<const ScatterNode*>(node), parentRings, origin, dir);
        break;
#endif
    }

    m_hlCapture = prevCapture;
    m_curNode = prevNode;
    m_windW = prevWindW;
    m_windPhase = prevWindPhase;
}

// 上游此函数把新增三角登记到拾取表、把高亮几何镜像到描边缓冲——两者都是应用视口功能,
// 对 TreeMeshData::batches 无影响。本移植保留函数与全部调用点(最小 diff), 函数体为空:
// 避免为每棵大树积累拾取/描边数据。
void TreeGenerator::afterAppend(const MeshBatch& batch, size_t vFrom, size_t iFrom) {
    (void)batch; (void)vFrom; (void)iFrom;
}

// ---- Trunk ----
std::vector<BranchRing> TreeGenerator::buildTrunk(
    const TrunkNode* node, godot::Vector3 origin, godot::Vector3 dir)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed + m_specimenSeedOffset);

    // 主干锥度从 startRadius 起(不再把 baseFlare 乘进整条锥度)，
    // 树盘(根盘)改为在基部局部外扩、沿 flareHeight 高度用 smoothstep 平滑收敛，
    // 避免过去"整段变粗 + 底部硬折"的不自然过渡。
    auto rings = CylinderSegment::buildNaturalRings(
        origin, dir, p.length,
        p.startRadius, p.endRadius,
        p.lengthSegs, p.noiseAmount, p.noiseFreq,
        p.gnarl, p.taperPow, 0.0f, rng,
        p.jointCount, p.jointBulge);

    float flareExtra = p.startRadius * (p.baseFlare - 1.0f);
    const float fh = 0.18f;  // 树盘(根盘)过渡高度: 基部外扩沿长度平滑收敛的比例(固定)
    if (flareExtra > 0.0f && p.lengthSegs > 0) {
        for (size_t i = 0; i < rings.size(); ++i) {
            float t = (float)i / (float)p.lengthSegs;
            if (t >= fh) break;
            float u = t / fh;                       // 0(基部)→1(过渡顶)
            float w = 1.0f - (3.0f*u*u - 2.0f*u*u*u); // 1-smoothstep: 两端导数为0→无折痕
            rings[i].radius += flareExtra * w;
        }
    }

    auto& batch = getBatch(p.material, false);
    size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
    appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
    afterAppend(batch, hlV, hlI);
    return rings;
}

// ---- Branch ----
void TreeGenerator::buildBranches(
    const BranchNode* node, const NodeGraph& graph,
    const std::vector<BranchRing>& parentRings,
    float parentLen, int depth)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed + depth * 100 + m_specimenSeedOffset);
    std::uniform_real_distribution<float> jitter(-5.0f, 5.0f);
    std::uniform_real_distribution<float> jitterLen(0.85f, 1.15f);

    // 标本模式: 本节点即标本根 → 只长一根, 从原点(0,0,0)沿 +Y 竖直挺立
    // (无重力偏转、无枝领), 子节点相对这根主枝正常生长。
    if (m_specimenRoot == node->id) {
        float thisLen = m_specParentLen * p.lengthRatio;
        float startR  = m_specParentRadius * p.radiusScale;
        float endR    = startR * p.endRatio;
        auto rings = CylinderSegment::buildNaturalRings(
            {0,0,0}, {0,1,0}, thisLen, startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, 0.0f, rng, p.jointCount, p.jointBulge);
        auto& batch = getBatch(p.material, false);
        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
        afterAppend(batch, hlV, hlI);
        godot::Vector3 tip    = rings.empty() ? godot::Vector3(0,thisLen,0) : rings.back().center;
        godot::Vector3 tipDir = rings.empty() ? godot::Vector3(0,1,0)       : rings.back().up;
        int prevBase = m_parentBoneBase, prevCount = m_parentBoneCount;
        int myBase, myCount;
        emitBoneChain(rings, p.boneCount, 1,
                      std::string(node->getLabel()) + std::to_string(node->id),
                      myBase, myCount);
        m_parentBoneBase = myBase; m_parentBoneCount = myCount;
        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
            else
                processNode(graph, child, &rings, {0,0,0}, {0,1,0}, thisLen, depth+1);
        }
        m_parentBoneBase = prevBase; m_parentBoneCount = prevCount;
        return;
    }

    float rs = std::clamp(p.regionStart, 0.0f, 1.0f);
    float re = std::clamp(p.regionEnd,   0.0f, 1.0f);
    if (re < rs) std::swap(rs, re);

    // 依据分布模式生成 (attachT归一化位置, 方位角az) 列表:
    //  - Interval(竹节式): 沿[rs,re]每隔 intervalSpacing 设一个节, 每节环绕 branchesPerNode 根,
    //    枝条只长在离散的节上(竹子特征);
    //  - 其它模式: 沿区间均匀铺 branchCount 根(现有 Classic 行为)。
    struct Attach { float t; float az; };
    std::vector<Attach> attaches;
    if (p.mode == BranchMode::Interval) {
        float spacing = std::max(0.01f, p.intervalSpacing);
        int   perNode = std::max(1, p.branchesPerNode);
        int   nodeIdx = 0;
        for (float t = rs; t <= re + 1e-4f; t += spacing, ++nodeIdx) {
            float baseAz = nodeIdx * p.rotateOffset;  // 逐节整体旋转, 避免上下枝条对齐成一条线
            for (int k = 0; k < perNode; ++k) {
                float az = baseAz + (360.0f / perNode) * k + jitter(rng);
                attaches.push_back({ std::min(t, re), az });
            }
        }
    } else {
        for (int i = 0; i < p.branchCount; ++i) {
            float attachT = (p.branchCount > 1)
                ? godot::Math::lerp(rs, re, (float)i / (float)(p.branchCount-1))
                : godot::Math::lerp(rs, re, 0.5f);
            attaches.push_back({ attachT, i * p.rotateOffset + jitter(rng) });
        }
    }

    for (const auto& at : attaches) {
        float attachT = at.t;

        godot::Vector3 attachPos, attachDir, attachRight;
        float     attachRadius = p.radiusScale;
        sampleRings(parentRings, attachT, attachPos, attachDir, attachRight, attachRadius);

        // 方案B 测量: 记录该 Branch 首个实例的真实父级长度/附着半径供标本用
        if (m_measureTarget == node->id && !m_measureDone) {
            m_specParentLen    = parentLen;
            m_specParentRadius = attachRadius;
            m_measureDone      = true;
        }

        float az = at.az;
        float el = p.spreadAngle + varyBy(rng, p.spreadAngleVar) + jitter(rng);

        // 以attachDir为轴向、attachRight为参考，计算分支方向
        godot::Vector3 branchDir = rotateAroundAxis(attachDir, attachRight, el);
        branchDir = rotateAroundAxis(branchDir, attachDir, az);

        // 每根实例的长度/半径/锥度 variance(默认0=关闭, 行为不变)
        float instLenRatio = std::max(0.02f, p.lengthRatio + varyBy(rng, p.lengthRatioVar));
        float instRadScale = std::max(0.01f, p.radiusScale + varyBy(rng, p.radiusScaleVar));
        float instEndRatio = std::max(0.01f, p.endRatio    + varyBy(rng, p.endRatioVar));
        float instGravity  = p.gravity + varyBy(rng, p.gravityVar);  // 不clamp: 保留旧数据>1/负值语义

        float thisLen = parentLen * instLenRatio * jitterLen(rng);
        // Size Falloff: 沿父级向上(attachT 越大)长度线性衰减, 让树冠上部枝条更短
        thisLen *= std::max(0.05f, 1.0f - p.sizeFalloff * attachT);
        // start半径贴合父级附着点，end按自身锥度收缩
        float startR = attachRadius * instRadScale;
        float endR   = startR * instEndRatio;

        // 枝条从父级“表面”发出（轴心 + 径向×父半径），而非从轴心穿出
        godot::Vector3 radial = branchDir - attachDir * branchDir.dot(attachDir);
        godot::Vector3 surfacePos = (radial.length() > 1e-4f)
            ? attachPos + radial.normalized() * attachRadius
            : attachPos;

        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, branchDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, instGravity, rng,
            p.jointCount, p.jointBulge);

        auto& batch = getBatch(p.material, false);
        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
        // 枝领：把基部一圈投影到父级表面，形成贴合过渡裙
        if (!rings.empty())
            appendCollar(batch, attachPos, attachDir, attachRadius,
                         rings.front().center, rings.front().up, rings.front().right,
                         startR, p.baseFlare, p.sides, p.uvTilingU, p.uvTilingV, thisLen);
        afterAppend(batch, hlV, hlI);

        // 子节点从branch末端（折弯后的真实末端）出发
        godot::Vector3 tip      = rings.empty() ? surfacePos + branchDir * thisLen : rings.back().center;
        godot::Vector3 tipDir   = rings.empty() ? branchDir : rings.back().up;

        int prevBase = m_parentBoneBase, prevCount = m_parentBoneCount;
        int myBase, myCount;
        emitBoneChain(rings, p.boneCount, 1,
                      std::string(node->getLabel()) + std::to_string(node->id),
                      myBase, myCount);
        m_parentBoneBase = myBase; m_parentBoneCount = myCount;
        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                // 叶簇沿整根枝条均匀生长：把本枝 rings 传下去
                processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
            else
                processNode(graph, child, &rings, attachPos, branchDir, thisLen, depth+1);
        }
        m_parentBoneBase = prevBase; m_parentBoneCount = prevCount;
        if (m_chainMode) break;   // 祖先链模式: 只长一根链上代表实例
    }
}

#ifdef SLOWTREE_FULL_NODES
// ---- Custom（脚本自定义枝条） ----
// 运行节点内 Lua 脚本得到一批枝条 spec，再沿用与 Branch 完全相同的
// 圆柱/枝领/子节点递归管线生成几何。脚本只产出数值，安全且自动接入
// 风力/拾取/高亮/导出。脚本报错时不崩溃：写回 lastError，跳过本节点几何。
void TreeGenerator::buildCustom(
    const CustomNode* node, const NodeGraph& graph,
    const std::vector<BranchRing>& parentRings,
    float parentLen, int depth)
{
    const auto& p = node->params;

    // 运行脚本
    LuaCtx ctx;
    ctx.count        = p.count;
    ctx.parentLen    = parentLen;
    ctx.depth        = depth;
    ctx.seed         = p.seed + depth * 100 + m_specimenSeedOffset;
    // 附着半径参考: 取父级中部半径(供脚本按比例算)
    {
        godot::Vector3 tp, td, tr; float trad = 0.3f;
        sampleRings(parentRings, 0.5f, tp, td, tr, trad);
        ctx.parentRadius = trad;
    }

    std::vector<BranchSpec> specs;
    std::string err;
    const std::string& script = p.script.empty() ? std::string(kDefaultCustomScript) : p.script;
    bool ok = LuaEngine::run(script, ctx, specs, err);
    node->params.lastError = err;   // 空=成功; 非空=错误/警告(UI 红字显示)
    if (!ok) return;                // 致命错误: 不产出几何(保留上次成功网格由上层管理)

    std::mt19937 rng(p.seed + depth * 100 + m_specimenSeedOffset);

    // 标本模式: 只长第一根 spec, 从原点沿 +Y 竖直挺立(与 Branch 标本一致)
    bool specimen = (m_specimenRoot == node->id);

    auto& batch = getBatch(p.material, false);

    for (size_t si = 0; si < specs.size(); ++si) {
        const BranchSpec& s = specs[si];

        godot::Vector3 surfacePos, branchDir;
        float attachRadius, startR, endR, thisLen;
        godot::Vector3 attachPos, attachDir, attachRight;

        if (specimen) {
            // 竖直单枝: 忽略附着, 从原点沿 +Y
            attachPos = {0,0,0}; attachDir = {0,1,0}; attachRight = {1,0,0};
            attachRadius = m_specParentRadius;
            surfacePos   = {0,0,0};
            branchDir    = {0,1,0};
            startR       = m_specParentRadius * s.radius;
            endR         = startR * s.endRatio;
            thisLen      = m_specParentLen * (s.length / std::max(0.001f, parentLen));
        } else {
            float t = std::clamp(s.t, 0.0f, 1.0f);
            attachRadius = 1.0f;
            sampleRings(parentRings, t, attachPos, attachDir, attachRight, attachRadius);

            // 方案B 测量
            if (m_measureTarget == node->id && !m_measureDone) {
                m_specParentLen    = parentLen;
                m_specParentRadius = attachRadius;
                m_measureDone      = true;
            }

            // 由 elevation(仰角) + azimuth(方位角) 求方向
            branchDir = rotateAroundAxis(attachDir, attachRight, s.elevation);
            branchDir = rotateAroundAxis(branchDir, attachDir, s.azimuth);

            thisLen = std::max(0.01f, s.length);
            startR  = attachRadius * s.radius;
            endR    = startR * s.endRatio;

            // 从父级表面发出
            godot::Vector3 radial = branchDir - attachDir * branchDir.dot(attachDir);
            surfacePos = (radial.length() > 1e-4f)
                ? attachPos + radial.normalized() * attachRadius
                : attachPos;
        }

        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, branchDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, specimen ? 0.0f : p.gravity, rng);

        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
        if (!specimen && !rings.empty())
            appendCollar(batch, attachPos, attachDir, attachRadius,
                         rings.front().center, rings.front().up, rings.front().right,
                         startR, p.baseFlare, p.sides, p.uvTilingU, p.uvTilingV, thisLen);
        afterAppend(batch, hlV, hlI);

        // 子节点从枝条末端出发
        godot::Vector3 tip    = rings.empty() ? surfacePos + branchDir * thisLen : rings.back().center;
        godot::Vector3 tipDir = rings.empty() ? branchDir : rings.back().up;
        int prevBase = m_parentBoneBase, prevCount = m_parentBoneCount;
        int myBase, myCount;
        emitBoneChain(rings, p.boneCount, 1,
                      std::string(node->getLabel()) + std::to_string(node->id),
                      myBase, myCount);
        m_parentBoneBase = myBase; m_parentBoneCount = myCount;
        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
            else
                processNode(graph, child, &rings,
                            specimen ? godot::Vector3(0,0,0) : attachPos,
                            specimen ? godot::Vector3(0,1,0) : branchDir, thisLen, depth+1);
        }
        m_parentBoneBase = prevBase; m_parentBoneCount = prevCount;

        if (specimen) break;         // 标本: 只长一根
        if (m_chainMode) break;      // 祖先链模式: 只长一根代表实例
    }
}
#endif

// ---- Twig ----
void TreeGenerator::buildTwig(
    const TwigNode* node, const NodeGraph& graph,
    const std::vector<BranchRing>& parentRings,
    float parentLen, int depth)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed + depth * 77 + m_specimenSeedOffset);
    std::uniform_real_distribution<float> jitter(-8.0f, 8.0f);
    std::uniform_real_distribution<float> jitterLen(0.8f, 1.2f);

    // 标本模式: 竖直单枝(原点 +Y, 无重力/枝领), 子节点正常生长
    if (m_specimenRoot == node->id) {
        float thisLen = m_specParentLen * p.lengthRatio;
        float startR  = m_specParentRadius * p.radiusScale;
        float endR    = startR * p.endRatio;
        auto rings = CylinderSegment::buildNaturalRings(
            {0,0,0}, {0,1,0}, thisLen, startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, 0.0f, rng);
        auto& batch = getBatch(p.material, false);
        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
        afterAppend(batch, hlV, hlI);
        godot::Vector3 tip    = rings.empty() ? godot::Vector3(0,thisLen,0) : rings.back().center;
        godot::Vector3 tipDir = rings.empty() ? godot::Vector3(0,1,0)       : rings.back().up;
        int prevBase = m_parentBoneBase, prevCount = m_parentBoneCount;
        int myBase, myCount;
        emitBoneChain(rings, p.boneCount, 2,
                      std::string(node->getLabel()) + std::to_string(node->id),
                      myBase, myCount);
        m_parentBoneBase = myBase; m_parentBoneCount = myCount;
        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
            else
                processNode(graph, child, &rings, {0,0,0}, {0,1,0}, thisLen, depth+1);
        }
        m_parentBoneBase = prevBase; m_parentBoneCount = prevCount;
        return;
    }

    float rs = std::clamp(p.regionStart, 0.0f, 1.0f);
    float re = std::clamp(p.regionEnd,   0.0f, 1.0f);
    if (re < rs) std::swap(rs, re);
    std::uniform_real_distribution<float> attachDist(rs, re);

    float baseAz = 0.0f;
    for (int i = 0; i < p.twigCount; ++i) {
        float az = p.alternating
                   ? (i % 2 == 0 ? baseAz : baseAz + 180.0f) + jitter(rng)
                   : i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + varyBy(rng, p.spreadAngleVar) + jitter(rng);
        if (p.alternating) baseAz += p.rotateOffset;

        float t = attachDist(rng);
        godot::Vector3 attachPos, attachDir, attachRight;
        float     attachRadius = p.radiusScale;
        sampleRings(parentRings, t, attachPos, attachDir, attachRight, attachRadius);

        // 方案B 测量: 记录该 Twig 首个实例的真实父级长度/附着半径
        if (m_measureTarget == node->id && !m_measureDone) {
            m_specParentLen    = parentLen;
            m_specParentRadius = attachRadius;
            m_measureDone      = true;
        }

        godot::Vector3 twigDir = rotateAroundAxis(attachDir, attachRight, el);
        twigDir = rotateAroundAxis(twigDir, attachDir, az);

        // 每根实例的长度/半径/锥度/重力 variance(默认0=关闭)
        float instLenRatio = std::max(0.02f, p.lengthRatio + varyBy(rng, p.lengthRatioVar));
        float instRadScale = std::max(0.01f, p.radiusScale + varyBy(rng, p.radiusScaleVar));
        float instEndRatio = std::max(0.01f, p.endRatio    + varyBy(rng, p.endRatioVar));
        float instGravity  = p.gravity + varyBy(rng, p.gravityVar);  // 不clamp: 保留旧数据>1/负值语义

        float thisLen = parentLen * instLenRatio * jitterLen(rng);
        // start半径贴合父级附着点，end按自身锥度收缩
        float startR = attachRadius * instRadScale;
        float endR   = startR * instEndRatio;

        // 细枝从父级“表面”发出，而非从轴心穿出
        godot::Vector3 radial = twigDir - attachDir * twigDir.dot(attachDir);
        godot::Vector3 surfacePos = (radial.length() > 1e-4f)
            ? attachPos + radial.normalized() * attachRadius
            : attachPos;

        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, twigDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, instGravity, rng);

        auto& batch = getBatch(p.material, false);
        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
        // 枝领：把基部一圈投影到父级表面，形成贴合过渡裙
        if (!rings.empty())
            appendCollar(batch, attachPos, attachDir, attachRadius,
                         rings.front().center, rings.front().up, rings.front().right,
                         startR, p.baseFlare, p.sides, p.uvTilingU, p.uvTilingV, thisLen);
        afterAppend(batch, hlV, hlI);

        godot::Vector3 tip    = rings.empty() ? surfacePos + twigDir * thisLen : rings.back().center;
        godot::Vector3 tipDir = rings.empty() ? twigDir : rings.back().up;

        int prevBase = m_parentBoneBase, prevCount = m_parentBoneCount;
        int myBase, myCount;
        emitBoneChain(rings, p.boneCount, 2,
                      std::string(node->getLabel()) + std::to_string(node->id),
                      myBase, myCount);
        m_parentBoneBase = myBase; m_parentBoneCount = myCount;
        for (auto* child : graph.childrenOf(node->id)) {
            if (child->getType() == NodeType::LeafCluster)
                // 叶簇沿整根细枝均匀生长：把本枝 rings 传下去
                processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
            else
                processNode(graph, child, &rings, attachPos, twigDir, thisLen, depth+1);
        }
        m_parentBoneBase = prevBase; m_parentBoneCount = prevCount;
        if (m_chainMode) break;   // 祖先链模式: 只长一根链上代表实例
    }
}

// ---- Roots ----
// 从树干“基部”一圈向外辐射铺开，再借 droop(下扎)沿长度逐渐转向地下，
// 末端按锥度收细成尖。方位用黄金角分布，接壤处套用枝领裙边贴合树干。
void TreeGenerator::buildRoots(
    const RootsNode* node, const std::vector<BranchRing>& parentRings)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed + 500 + m_specimenSeedOffset);
    std::uniform_real_distribution<float> jitter(-6.0f, 6.0f);
    std::uniform_real_distribution<float> jitterLen(0.8f, 1.25f);

    // 树干基部环(t=0)：附着中心、轴向、half、半径
    godot::Vector3 basePos, baseDir, baseRight;
    float     baseRadius = 1.0f;
    sampleRings(parentRings, 0.0f, basePos, baseDir, baseRight, baseRadius);

    auto& batch = getBatch(p.material, false);

    for (int i = 0; i < p.rootCount; ++i) {
        float az = i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + varyBy(rng, p.spreadAngleVar) + jitter(rng);  // 接近90°=先近水平铺开

        // 以树干轴为参考，先抬到 spreadAngle 再绕轴旋方位(spreadAngle>90 即朝下俯冲入地)
        godot::Vector3 rootDir = rotateAroundAxis(baseDir, baseRight, el);
        rootDir = rotateAroundAxis(rootDir, baseDir, az);
        rootDir = rootDir.normalized();

        // 每根实例的长度/半径/锥度/重力 variance(默认0=关闭)
        float instRadScale = std::max(0.01f, p.radiusScale + varyBy(rng, p.radiusScaleVar));
        float instEndRatio = std::max(0.01f, p.endRatio    + varyBy(rng, p.endRatioVar));
        float instGravity  = p.gravity + varyBy(rng, p.gravityVar);  // 不clamp: 保留旧数据>1/负值语义

        float thisLen = std::max(0.05f, p.length + varyBy(rng, p.lengthVar)) * jitterLen(rng);
        float startR  = baseRadius * instRadScale;
        float endR    = startR * instEndRatio;

        // 从树干“表面”发出，而非从轴心穿出；再沿径向回沉一小段嵌入树干实体，
        // 使根基与树干实体互相重叠(而非仅贴表面)，从根本上消除近水平粗根的接缝空隙。
        // collar 仍从此嵌入点向外平滑过渡，只影响 Roots，不改动 Branch。
        godot::Vector3 radial = rootDir - baseDir * rootDir.dot(baseDir);
        godot::Vector3 surfacePos = basePos;
        if (radial.length() > 1e-4f) {
            godot::Vector3 rdir = radial.normalized();
            float sink = std::min(startR * 0.6f, baseRadius * 0.5f);
            surfacePos = basePos + rdir * (baseRadius - sink);
        }

        // gravity 作为“重力”参数传入，使根沿长度持续向下弯扎入地；节参数用于周期性膨大
        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, rootDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, instGravity, rng,
            p.jointCount, p.jointBulge);

        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
        // 根领：接壤处裙边贴合树干表面(传入树干rings→落点半径随树干高度真实起伏)
        if (!rings.empty())
            appendCollar(batch, basePos, baseDir, baseRadius,
                         rings.front().center, rings.front().up, rings.front().right,
                         startR, p.baseFlare, p.sides, p.uvTilingU, p.uvTilingV, thisLen,
                         &parentRings, p.collarSink);
        afterAppend(batch, hlV, hlI);
        if (m_chainMode) break;   // 祖先链模式: 只长一根链上代表实例
    }
}

// ---- LeafCluster ----
// 叶片沿父级枝条(twig)整根均匀生长；每片叶以"垂直于枝条轴向"的随机外扩方向为
// 生长方向(略带向枝梢前倾)，叶面法线再在垂直于生长方向的平面内随机旋转，
// 避免所有叶片朝同一方向而显假。
void TreeGenerator::buildLeafCluster(
    const LeafClusterNode* node,
    const std::vector<BranchRing>* parentRings,
    godot::Vector3 origin, godot::Vector3 dir)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed + m_specimenSeedOffset);
    std::uniform_real_distribution<float> jitter(-p.normalJitter, p.normalJitter);
    std::uniform_real_distribution<float> radJit(0.7f, 1.3f);
    std::uniform_real_distribution<float> rot01(0.0f, 1.0f);

    float goldenAngle = godot::Math::deg_to_rad(137.508f);
    float pi2 = kTwoPi;
    auto& batch = getBatch(p.material, true);
    godot::Vector3 col = p.material.albedo;

    bool haveRings = (parentRings && !parentRings->empty());

    for (int i = 0; i < p.leafCount; ++i) {
        // 附着点：沿整根枝条均匀分布(带轻微抖动)，而非全挤在末端
        godot::Vector3 axisPos, axisDir, axisRight;
        float     axisRadius = 0.0f;
        float     sizeT = 0.5f;   // 沿叶轴位置(0=基部,1=梢部)，用于 sizeFalloff
        if (haveRings) {
            float t = (p.leafCount > 1) ? (float)i / (float)(p.leafCount - 1) : 0.5f;
            t = std::clamp(t + (rot01(rng) - 0.5f) / (float)p.leafCount, 0.0f, 1.0f);
            sizeT = t;
            sampleRings(*parentRings, t, axisPos, axisDir, axisRight, axisRadius);
        } else {
            sizeT = (p.leafCount > 1) ? (float)i / (float)(p.leafCount - 1) : 0.5f;
            axisPos = origin;
            axisDir = dir.normalized();
            axisRight = perpendicular(axisDir);
        }
        axisDir = axisDir.normalized();
        godot::Vector3 fwd = axisDir.cross(axisRight).normalized();

        // 外扩方向：平面模式=小叶交替伸向叶轴两侧(蕨类)，否则黄金角3D辐射
        godot::Vector3 outDir;
        if (p.planar) {
            float side = (i % 2 == 0) ? 1.0f : -1.0f;
            float wob  = (rot01(rng) - 0.5f) * 0.25f;  // 轻微抖动避免完全规则
            outDir = (axisRight * side + fwd * wob).normalized();
        } else {
            float az = goldenAngle * (float)i + (rot01(rng) - 0.5f) * 0.7f;
            outDir = (axisRight * std::cos(az) + fwd * std::sin(az)).normalized();
        }

        // 叶片生长方向：以垂直外扩为主，略向枝梢方向前倾 + 抖动
        godot::Vector3 leafUp = (outDir + axisDir * (0.25f + jitter(rng))).normalized();

        // 叶基贴在枝条表面，叶片向外伸展。sizeFalloff 令小叶向梢部渐小(蕨类)
        godot::Vector3 basePos = axisPos + outDir * axisRadius;
        float sizeScale = 1.0f - p.sizeFalloff * sizeT;
        float hs = p.leafSize * 0.5f * sizeScale;
        float hw = hs * p.leafAspect;   // 宽高比: 小值=细长叶(竹叶/蕨类小叶)
        godot::Vector3 pos = basePos + leafUp * ((hs + p.clusterRadius * 0.4f) * radJit(rng));

        // 叶面朝向：平面模式统一朝叶轴平面法线(共面平铺)，否则随机旋转
        godot::Vector3 leafRight, leafFwd;
        if (p.planar) {
            leafFwd   = fwd;
            leafRight = leafUp.cross(leafFwd).normalized();
            leafFwd   = leafRight.cross(leafUp).normalized();
        } else {
            godot::Vector3 tmp = (std::abs(leafUp.y) < 0.95f) ? godot::Vector3(0,1,0) : godot::Vector3(1,0,0);
            leafRight = leafUp.cross(tmp).normalized();
            leafFwd   = leafRight.cross(leafUp).normalized();
            float rot = rot01(rng) * pi2;
            leafRight = (leafRight * std::cos(rot) + leafFwd * std::sin(rot)).normalized();
            leafFwd   = leafRight.cross(leafUp).normalized();
        }

        // 4顶点，带UV: 左下(0,0) 右下(1,0) 右上(1,1) 左上(0,1)
        struct LV { godot::Vector3 pos; float u, v; };
        LV lv[4] = {
            {pos - leafRight*hw - leafUp*hs, 0.f, 0.f},
            {pos + leafRight*hw - leafUp*hs, 1.f, 0.f},
            {pos + leafRight*hw + leafUp*hs, 1.f, 1.f},
            {pos - leafRight*hw + leafUp*hs, 0.f, 1.f},
        };
        // 叶片法线软化: 从平面卡片法线 leafFwd 混合到"叶簇轴心→叶片"的球形外法线,
        // 让整簇叶像一个受光的体积(SpeedTree 观感关键), 而非一堆硬纸片。
        godot::Vector3 leafNormal = leafFwd;
        if (p.normalSoften > 0.0f) {
            godot::Vector3 outwardN = pos - axisPos;
            if (outwardN.dot(outwardN) > 1e-8f) {
                outwardN = outwardN.normalized();
                leafNormal = leafFwd.lerp(outwardN,
                                          std::clamp(p.normalSoften, 0.0f, 1.0f)).normalized();
            }
        }

        // 每顶点: pos(3)+normal(3)+uv(2)+albedo(3)+wind(2)+anchor(3) = 16 floats
        // 叶片绕 basePos(附着点)整体摆动, 每片叶相位错开(i*2.4)
        float leafPhase = m_windPhase + (float)i * 2.4f;

        if (m_gpuEmit) {
            // Stage 2 GPU: 发射叶卡描述子, quad/轮廓展开交给 leaf_card.comp。
            emitGpuLeafCard(batch, node, pos, leafRight, leafUp, leafNormal, basePos,
                            hs, hw, leafPhase, col);
            continue;
        }

        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        uint32_t base = (uint32_t)(batch.vertices.size() / 16);

        auto emitVert = [&](const godot::Vector3& wp, float u, float v) {
            batch.vertices.insert(batch.vertices.end(),
                {wp.x,wp.y,wp.z,
                 leafNormal.x,leafNormal.y,leafNormal.z,
                 u, v,
                 col.x,col.y,col.z,
                 m_windW, leafPhase,
                 basePos.x, basePos.y, basePos.z});
        };

        if (p.useCutout && p.cutoutPoints.size() >= 3 && p.cutoutTris.size() >= 3) {
            // 轮廓网格: UV 点(u,v)映射到叶卡平面(u=0.5,v=0.5 为中心, 边长 2hw×2hs)
            for (const godot::Vector2& q : p.cutoutPoints) {
                godot::Vector3 wp = pos + leafRight * ((q.x - 0.5f) * 2.0f * hw)
                                   + leafUp    * ((q.y - 0.5f) * 2.0f * hs);
                emitVert(wp, q.x, q.y);
            }
            uint32_t vCount = (uint32_t)p.cutoutPoints.size();
            for (size_t k = 0; k + 2 < p.cutoutTris.size(); k += 3) {
                uint32_t a = p.cutoutTris[k], b = p.cutoutTris[k+1], c = p.cutoutTris[k+2];
                if (a < vCount && b < vCount && c < vCount)
                    batch.indices.insert(batch.indices.end(), {base+a, base+b, base+c});
            }
        } else {
            for (auto& lvi : lv) emitVert(lvi.pos, lvi.u, lvi.v);
            batch.indices.insert(batch.indices.end(),
                {base,base+1,base+2, base,base+2,base+3});
        }
        afterAppend(batch, hlV, hlI);
    }
}

// ---- Spine ----
// 蕨叶叶轴：像细枝(Twig)一样从父级 rings 附着点发出一条受 gravity 下垂、noise/gnarl
// 扰动的弯曲中心线，渲染成细茎，并把这条 rings 传给 Frond 子节点沿其铺连续叶带。
void TreeGenerator::buildSpine(
    const SpineNode* node, const NodeGraph& graph,
    const std::vector<BranchRing>& parentRings,
    float parentLen, int depth)
{
    const auto& p = node->params;
    std::mt19937 rng(p.seed + depth * 61 + m_specimenSeedOffset);
    std::uniform_real_distribution<float> jitter(-6.0f, 6.0f);
    std::uniform_real_distribution<float> jitterLen(0.85f, 1.15f);

    // 标本模式: 竖直单叶轴(原点 +Y, 无重力), Frond 子节点沿其铺叶带
    if (m_specimenRoot == node->id) {
        float thisLen = m_specParentLen * p.lengthRatio;
        float startR  = m_specParentRadius * p.radiusScale;
        float endR    = startR * p.endRatio;
        auto rings = CylinderSegment::buildNaturalRings(
            {0,0,0}, {0,1,0}, thisLen, startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, 0.0f, rng);
        auto& batch = getBatch(p.material, false);
        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
        afterAppend(batch, hlV, hlI);
        godot::Vector3 tip    = rings.empty() ? godot::Vector3(0,thisLen,0) : rings.back().center;
        godot::Vector3 tipDir = rings.empty() ? godot::Vector3(0,1,0)       : rings.back().up;
        int prevBase = m_parentBoneBase, prevCount = m_parentBoneCount;
        int myBase, myCount;
        emitBoneChain(rings, p.boneCount, 2,
                      std::string(node->getLabel()) + std::to_string(node->id),
                      myBase, myCount);
        m_parentBoneBase = myBase; m_parentBoneCount = myCount;
        for (auto* child : graph.childrenOf(node->id))
            processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
        m_parentBoneBase = prevBase; m_parentBoneCount = prevCount;
        return;
    }

    float rs = std::clamp(p.regionStart, 0.0f, 1.0f);
    float re = std::clamp(p.regionEnd,   0.0f, 1.0f);
    if (re < rs) std::swap(rs, re);

    for (int i = 0; i < p.spineCount; ++i) {
        float attachT = (p.spineCount > 1)
            ? godot::Math::lerp(rs, re, (float)i / (float)(p.spineCount - 1))
            : godot::Math::lerp(rs, re, 0.5f);

        godot::Vector3 attachPos, attachDir, attachRight;
        float     attachRadius = p.radiusScale;
        sampleRings(parentRings, attachT, attachPos, attachDir, attachRight, attachRadius);

        // 方案B 测量: 记录该 Spine 首个实例的真实父级长度/附着半径
        if (m_measureTarget == node->id && !m_measureDone) {
            m_specParentLen    = parentLen;
            m_specParentRadius = attachRadius;
            m_measureDone      = true;
        }

        float az = i * p.rotateOffset + jitter(rng);
        float el = p.spreadAngle + varyBy(rng, p.spreadAngleVar) + jitter(rng);
        godot::Vector3 spineDir = rotateAroundAxis(attachDir, attachRight, el);
        spineDir = rotateAroundAxis(spineDir, attachDir, az);

        // 每根实例的长度/半径/锥度/重力 variance(默认0=关闭)
        float instLenRatio = std::max(0.02f, p.lengthRatio + varyBy(rng, p.lengthRatioVar));
        float instRadScale = std::max(0.01f, p.radiusScale + varyBy(rng, p.radiusScaleVar));
        float instEndRatio = std::max(0.01f, p.endRatio    + varyBy(rng, p.endRatioVar));
        float instGravity  = p.gravity + varyBy(rng, p.gravityVar);  // 不clamp: 保留旧数据>1/负值语义

        float thisLen = parentLen * instLenRatio * jitterLen(rng);
        float startR  = attachRadius * instRadScale;
        float endR    = startR * instEndRatio;

        // 从父级表面发出
        godot::Vector3 radial = spineDir - attachDir * spineDir.dot(attachDir);
        godot::Vector3 surfacePos = (radial.length() > 1e-4f)
            ? attachPos + radial.normalized() * attachRadius
            : attachPos;

        auto rings = CylinderSegment::buildNaturalRings(
            surfacePos, spineDir, thisLen,
            startR, endR,
            p.lengthSegs, p.noiseAmount, p.noiseFreq,
            p.gnarl, p.taperPow, instGravity, rng);

        auto& batch = getBatch(p.material, false);
        size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
        appendCylinder(batch, rings, p.sides, p.uvTilingU, p.uvTilingV);
        afterAppend(batch, hlV, hlI);
        // Frond 子节点沿整条叶轴 rings 铺叶带
        godot::Vector3 tip    = rings.empty() ? surfacePos + spineDir * thisLen : rings.back().center;
        godot::Vector3 tipDir = rings.empty() ? spineDir : rings.back().up;
        int prevBase = m_parentBoneBase, prevCount = m_parentBoneCount;
        int myBase, myCount;
        emitBoneChain(rings, p.boneCount, 2,
                      std::string(node->getLabel()) + std::to_string(node->id),
                      myBase, myCount);
        m_parentBoneBase = myBase; m_parentBoneCount = myCount;
        for (auto* child : graph.childrenOf(node->id))
            processNode(graph, child, &rings, tip, tipDir, thisLen, depth+1);
        m_parentBoneBase = prevBase; m_parentBoneCount = prevCount;
        if (m_chainMode) break;   // 祖先链模式: 只长一根链上代表实例
    }
}

// ---- Frond ----
// 沿父级(Spine)rings 生成一条连续带状蕨叶：每个 ring 处沿其 right 轴向左右外扩，
// 半宽按叶形轮廓(基部窄→中部最宽→梢部收尖)变化，curl 令叶面沿 up 方向卷曲。
// 与 LeafCluster 的离散小卡片不同：Frond 是"沿曲线拉伸的连续叶片网格"。
void TreeGenerator::buildFrond(
    const FrondNode* node,
    const std::vector<BranchRing>* parentRings,
    godot::Vector3 origin, godot::Vector3 dir)
{
    if (!parentRings || parentRings->size() < 2) return;
    const auto& p = node->params;
    const auto& rings = *parentRings;
    auto& batch = getBatch(p.material, true);
    size_t hlV = batch.vertices.size(), hlI = batch.indices.size();
    godot::Vector3 col = p.material.albedo;

    int nSeg = (int)rings.size() - 1;         // 沿脊线段数
    int cols = std::max(1, p.segsPerSide);     // 每侧横向细分
    int totalCols = cols * 2 + 1;              // 左...中...右 顶点列数
    float pi = kPi;

    // 半宽轮廓: t=0 用 widthBase, t=1 用 widthTip, 中部按 profilePow 抬到最大(=width)
    auto halfWidthAt = [&](float t) -> float {
        float base = godot::Math::lerp(p.widthBase, p.widthTip, t);   // 端点线性过渡
        // sin(π·t) 在 t=0/1 端点因 π 浮点误差可能为极小负数, pow(负, 非整数)=NaN
        // (widthTip=0 时末行整行 NaN)。clamp ≥0 只消除端点浮点噪声, 不改变曲线形状;
        // 上游同款缺陷, 见 UPSTREAM_SYNC.md 偏差清单。
        float bump = std::pow(std::max(0.0f, std::sin(pi * std::clamp(t,0.0f,1.0f))),
                              std::max(0.05f, p.profilePow)); // 中部隆起
        return p.width * (base + (1.0f - base) * bump);
    };

    // 逐 ring 生成一排横向顶点(共 rings.size() 行 × totalCols 列)
    godot::Vector3 frondAnchor = rings.front().center;   // 叶带整体绕基部摆动

    if (m_gpuEmit) {
        // Stage 2 GPU: 发射叶带描述子(网格铺带/轮廓裁剪同一描述子, 模式由
        // pointCount 区分), 顶点/索引展开交给 frond.comp。
        const bool cutout = p.useCutout && p.cutoutPoints.size() >= 3 && p.cutoutTris.size() >= 3;
        uint32_t pointOff = 0, triOff = 0, pointCount = 0, triCount = 0;
        if (cutout) {
            gpuCutoutPool(&p.cutoutPoints, &p.cutoutTris, pointOff, triOff);
            pointCount = (uint32_t)p.cutoutPoints.size();
            triCount   = (uint32_t)p.cutoutTris.size();
        }
        emitGpuFrond(batch, node, rings, frondAnchor, col, nSeg, totalCols,
                     pointOff, triOff, triCount, pointCount);
        return;
    }

    // 叶带曲面采样: (u,v)∈[0,1] → 世界坐标+法线。u=横向 lateral, v=沿脊线 t。
    // 供轮廓网格(cutout)按叶带局部 UV 映射到实际曲面。
    auto sampleFrond = [&](float u, float v, godot::Vector3& outPos, godot::Vector3& outN) {
        float t = std::clamp(v, 0.0f, 1.0f);
        float f = t * (float)nSeg;
        int   i0 = (int)std::floor(f);
        i0 = std::max(0, std::min(nSeg - 1, i0));
        float lerp = f - (float)i0;
        const BranchRing& a = rings[i0];
        const BranchRing& b = rings[i0 + 1];
        godot::Vector3 center = a.center.lerp(b.center, lerp);
        godot::Vector3 rightAxis = a.right.normalized().lerp(b.right.normalized(), lerp).normalized();
        godot::Vector3 upAxis    = a.up.normalized().lerp(b.up.normalized(), lerp).normalized();
        godot::Vector3 faceN = rightAxis.cross(upAxis).normalized();
        float hw = halfWidthAt(t);
        float lateral = u * 2.0f - 1.0f;   // [0,1] → [-1,1]
        float lift = p.curl * hw * lateral * lateral;
        outPos = center + rightAxis * (lateral * hw) + faceN * lift;
        outN = faceN;
    };

    // 轮廓裁剪网格: 用贴合剪影的三角网格代替整片叶带矩形
    if (p.useCutout && p.cutoutPoints.size() >= 3 && p.cutoutTris.size() >= 3) {
        uint32_t base = (uint32_t)(batch.vertices.size() / 16);
        for (const godot::Vector2& q : p.cutoutPoints) {
            godot::Vector3 pos, faceN;
            sampleFrond(q.x, q.y, pos, faceN);
            float wFrond = m_windW * q.y;   // 尖端摆动更明显
            batch.vertices.insert(batch.vertices.end(),
                {pos.x,pos.y,pos.z, faceN.x,faceN.y,faceN.z, q.x, q.y,
                 col.x,col.y,col.z,
                 wFrond, m_windPhase,
                 frondAnchor.x, frondAnchor.y, frondAnchor.z});
        }
        uint32_t vCount = (uint32_t)p.cutoutPoints.size();
        for (size_t k = 0; k + 2 < p.cutoutTris.size(); k += 3) {
            uint32_t ia = p.cutoutTris[k], ib = p.cutoutTris[k+1], ic = p.cutoutTris[k+2];
            if (ia < vCount && ib < vCount && ic < vCount) {
                batch.indices.insert(batch.indices.end(),
                    {base+ia, base+ib, base+ic});     // 正面
                batch.indices.insert(batch.indices.end(),
                    {base+ia, base+ic, base+ib});     // 背面(双面可见)
            }
        }
        afterAppend(batch, hlV, hlI);
        return;
    }

    std::vector<uint32_t> rowBase(rings.size());
    for (size_t ri = 0; ri < rings.size(); ++ri) {
        const BranchRing& r = rings[ri];
        float t  = (rings.size() > 1) ? (float)ri / (float)(rings.size()-1) : 0.0f;
        float hw = halfWidthAt(t);
        godot::Vector3 rightAxis = r.right.normalized();
        godot::Vector3 upAxis    = r.up.normalized();
        // 叶面法线 ≈ 脊线 up × right (垂直于叶带平面)
        godot::Vector3 faceN = rightAxis.cross(upAxis).normalized();
        float vCoord = t;
        float wFrond = m_windW * t;   // 尖端摆动更明显

        rowBase[ri] = (uint32_t)(batch.vertices.size() / 16);
        for (int c = 0; c < totalCols; ++c) {
            float lateral = ((float)c / (float)(totalCols-1)) * 2.0f - 1.0f; // [-1,1]
            float x = lateral * hw;
            // curl: 叶面沿两侧向 up 方向卷起(|lateral|^2 越靠边卷得越多)
            float lift = p.curl * hw * lateral * lateral;
            // 锯齿: 叶缘按脊线位置周期性内缩，形成羽状裂片
            float edge = 1.0f;
            if (p.serrate && cols >= 1 && std::abs(std::abs(lateral)-1.0f) < 1e-3f)
                edge = 1.0f - p.serrateDepth * (0.5f + 0.5f*std::sin((float)ri*2.3f));
            godot::Vector3 pos = r.center + rightAxis * (x * edge) + faceN * lift;
            float uCoord = (float)c / (float)(totalCols-1);
            batch.vertices.insert(batch.vertices.end(),
                {pos.x,pos.y,pos.z, faceN.x,faceN.y,faceN.z, uCoord, vCoord,
                 col.x,col.y,col.z,
                 wFrond, m_windPhase,
                 frondAnchor.x, frondAnchor.y, frondAnchor.z});
        }
    }

    // 相邻两行之间铺四边形(双面: 叶片两面都可见, 各生成一组三角形)
    for (int ri = 0; ri < nSeg; ++ri) {
        uint32_t b0 = rowBase[ri];
        uint32_t b1 = rowBase[ri+1];
        for (int c = 0; c < totalCols-1; ++c) {
            uint32_t v00 = b0+c,   v01 = b0+c+1;
            uint32_t v10 = b1+c,   v11 = b1+c+1;
            batch.indices.insert(batch.indices.end(),
                {v00,v01,v11, v00,v11,v10});   // 正面
            batch.indices.insert(batch.indices.end(),
                {v00,v11,v01, v00,v10,v11});   // 背面
        }
    }
    afterAppend(batch, hlV, hlI);
}

// ================= Stage 2 GPU 描述子发射 =================
// 四个 emit 位点的 GPU 变体: 参数是 CPU 位点算好的中间值(共享原算法),
// 按 SlowTreeGpuData.h 的布局写入扁平 float 描述子。只有环角 cos/sin、
// 叶形 sin/pow 等三角与幂函数留在 GPU 侧(容差 ε=1e-4, 见自检)。
//
// firstVertex 为"顶点单位"(着色器乘 stride 10/16 得 float 偏移),
// firstIdx 为 uint32 索引偏移; 两者都来自发射时刻的全局计数,
// 与 CPU 路径在同一批次内的插入顺序一致(读回按 chunk 拼回 batch)。

void TreeGenerator::EnableGpuEmission(godot::TreeGpuEmission* emission) {
    m_gpu     = emission;
    m_gpuEmit = (emission != nullptr);
    m_gpuVerts = 0;
    m_gpuIdx   = 0;
    m_gpuVerts10 = 0;
    m_gpuVerts16 = 0;
    m_gpuCutoutPool.clear();
}

int TreeGenerator::gpuBatchFor(MeshBatch& batch) {
    const int idx = (int)(&batch - m_out->batches.data());
    const size_t want = (size_t)(idx + 1);
    if (m_gpu->BranchDescs.size() < want) m_gpu->BranchDescs.resize(want);
    if (m_gpu->CollarDescs.size() < want) m_gpu->CollarDescs.resize(want);
    if (m_gpu->LeafDescs.size()   < want) m_gpu->LeafDescs.resize(want);
    if (m_gpu->FrondDescs.size()  < want) m_gpu->FrondDescs.resize(want);
    return idx;
}

void TreeGenerator::gpuCommit(int batchIdx, uint64_t firstVertex, uint64_t vertFloats, uint64_t idxCount) {
    if (vertFloats > 0 || idxCount > 0) {
        m_gpu->Chunks.push_back({(uint32_t)batchIdx, m_gpuVerts, vertFloats, firstVertex, m_gpuIdx, idxCount});
    }
    m_gpuVerts += vertFloats;
    m_gpuIdx += idxCount;
    m_gpu->VertFloats = m_gpuVerts;
    m_gpu->IndexCount = m_gpuIdx;
}

uint32_t TreeGenerator::gpuRingPoolOffset(const std::vector<BranchRing>* rings) {
    // 不去重: 兄弟节点的局部 rings 向量会复用同一栈地址, 指针去重会把当前环列
    // 错配到已销毁的旧环列。环数据量小(12 floats/环), 直接追加。
    const uint32_t off = (uint32_t)(m_gpu->Rings.size() / godot::GPU_RING_ENTRY_FLOATS);
    for (const BranchRing& r : *rings) {
        // 与着色器 RingEntry(vec4 centerRadius; vec4 right; vec4 up) 逐字段对齐:
        // 12 floats/环(std430 下 3×vec4), 尾部各补 0 占位。
        m_gpu->Rings.insert(m_gpu->Rings.end(),
            {r.center.x, r.center.y, r.center.z, r.radius,
             r.right.x, r.right.y, r.right.z, 0.0f,
             r.up.x, r.up.y, r.up.z, 0.0f});
    }
    return off;
}

void TreeGenerator::gpuCutoutPool(const std::vector<godot::Vector2>* points,
                                  const std::vector<uint32_t>* tris,
                                  uint32_t& outPointOff, uint32_t& outTriOff) {
    auto it = m_gpuCutoutPool.find(points);
    if (it != m_gpuCutoutPool.end()) {
        outPointOff = it->second.first;
        outTriOff   = it->second.second;
        return;
    }
    outPointOff = (uint32_t)(m_gpu->CutoutPoints.size() / godot::GPU_CUTOUT_POINT_FLOATS);
    for (const godot::Vector2& q : *points)
        m_gpu->CutoutPoints.insert(m_gpu->CutoutPoints.end(), {q.x, q.y});
    outTriOff = (uint32_t)m_gpu->CutoutTris.size();
    m_gpu->CutoutTris.insert(m_gpu->CutoutTris.end(), tris->begin(), tris->end());
    m_gpuCutoutPool.emplace(points, std::make_pair(outPointOff, outTriOff));
}

uint32_t TreeGenerator::CountCutoutTris(const std::vector<uint32_t>& tris, uint32_t vCount) {
    uint32_t n = 0;
    for (size_t k = 0; k + 2 < tris.size(); k += 3) {
        const uint32_t a = tris[k], b = tris[k+1], c = tris[k+2];
        if (a < vCount && b < vCount && c < vCount) ++n;
    }
    return n;
}

void TreeGenerator::emitGpuBranchSeg(MeshBatch& batch, const BranchRing& bot, const BranchRing& top,
                                     int ringIdx, int lastRing, float vBot, float vTop,
                                     float uTiling, int sides) {
    const int batchIdx = gpuBatchFor(batch);
    // firstVertexFloats = 全局 float 缓冲写偏移; firstVertexUnits = 分支顶点单位
    // (索引基)。两者不能互相推导: 全局缓冲里叶顶点(16 floats)与枝顶点交错。
    const uint64_t firstVertexFloats = m_gpuVerts;
    const uint64_t firstVertexUnits  = m_gpuVerts10;
    const uint64_t firstIdx          = m_gpuIdx;
    std::vector<float>& d = m_gpu->BranchDescs[batchIdx];
    const size_t off = d.size();
    d.resize(off + godot::GPU_BRANCH_SEG_FLOATS);
    float* f = d.data() + off;
    f[0]=bot.center.x;  f[1]=bot.center.y;  f[2]=bot.center.z;  f[3]=bot.radius;
    f[4]=bot.up.x;      f[5]=bot.up.y;      f[6]=bot.up.z;      f[7]=0.0f;
    f[8]=bot.right.x;   f[9]=bot.right.y;   f[10]=bot.right.z;  f[11]=0.0f;
    f[12]=top.center.x; f[13]=top.center.y; f[14]=top.center.z; f[15]=top.radius;
    f[16]=top.up.x;     f[17]=top.up.y;     f[18]=top.up.z;     f[19]=0.0f;
    f[20]=top.right.x;  f[21]=top.right.y;  f[22]=top.right.z;  f[23]=0.0f;
    f[24]=vBot; f[25]=vTop; f[26]=uTiling; f[27]=m_windW;
    f[28]=(float)sides; f[29]=(float)lastRing; f[30]=(float)ringIdx; f[31]=(float)(ringIdx+1);
    f[32]=m_windPhase; f[33]=(float)firstVertexFloats; f[34]=(float)firstVertexUnits; f[35]=(float)firstIdx;
    m_gpuVerts10 += (uint64_t)2 * (uint64_t)(sides + 1);
    gpuCommit(batchIdx, firstVertexUnits, (uint64_t)10 * 2 * (uint64_t)(sides+1), (uint64_t)6 * (uint64_t)sides);
}

void TreeGenerator::emitGpuCollar(MeshBatch& batch,
                                  godot::Vector3 parentC, godot::Vector3 parentA, float parentR,
                                  godot::Vector3 childBase, godot::Vector3 childDir, godot::Vector3 childRight,
                                  float startR, float flareMax, float upperSpread, float lowerSpread,
                                  float landingR, float uTiling, float vPerUnit, float collarSink,
                                  int sides, const std::vector<BranchRing>* trunkRings) {
    const int batchIdx = gpuBatchFor(batch);
    uint32_t ringOff = 0, ringCount = 0;
    if (trunkRings && trunkRings->size() >= 2) {
        ringOff   = gpuRingPoolOffset(trunkRings);
        ringCount = (uint32_t)trunkRings->size();
    }
    const uint64_t firstVertexFloats = m_gpuVerts;
    const uint64_t firstVertexUnits  = m_gpuVerts10;
    const uint64_t firstIdx          = m_gpuIdx;
    std::vector<float>& d = m_gpu->CollarDescs[batchIdx];
    const size_t off = d.size();
    d.resize(off + godot::GPU_COLLAR_FLOATS);
    float* f = d.data() + off;
    f[0]=parentC.x;  f[1]=parentC.y;  f[2]=parentC.z;  f[3]=parentR;
    f[4]=parentA.x;  f[5]=parentA.y;  f[6]=parentA.z;  f[7]=0.0f;
    f[8]=childBase.x;  f[9]=childBase.y;  f[10]=childBase.z;  f[11]=0.0f;
    f[12]=childDir.x;  f[13]=childDir.y;  f[14]=childDir.z;  f[15]=0.0f;
    f[16]=childRight.x; f[17]=childRight.y; f[18]=childRight.z; f[19]=0.0f;
    f[20]=startR; f[21]=flareMax; f[22]=upperSpread; f[23]=lowerSpread;
    f[24]=landingR; f[25]=uTiling; f[26]=vPerUnit; f[27]=collarSink;
    f[28]=(float)sides; f[29]=(float)ringOff; f[30]=(float)ringCount; f[31]=m_windPhase;
    f[32]=(float)firstVertexFloats; f[33]=(float)firstVertexUnits; f[34]=(float)firstIdx; f[35]=0.0f;
    constexpr int weldSegs = 4;   // 与 CPU appendCollar 常量一致
    m_gpuVerts10 += (uint64_t)(weldSegs + 1) * (uint64_t)(sides + 1);
    gpuCommit(batchIdx, firstVertexUnits, (uint64_t)10 * (uint64_t)(weldSegs+1) * (uint64_t)(sides+1),
              (uint64_t)6 * (uint64_t)weldSegs * (uint64_t)sides);
}

void TreeGenerator::emitGpuLeafCard(MeshBatch& batch, const LeafClusterNode* node,
                                    godot::Vector3 pos, godot::Vector3 leafRight, godot::Vector3 leafUp,
                                    godot::Vector3 leafNormal, godot::Vector3 basePos,
                                    float hs, float hw, float leafPhase, godot::Vector3 col) {
    const auto& p = node->params;
    const int batchIdx = gpuBatchFor(batch);
    const bool cutout = p.useCutout && p.cutoutPoints.size() >= 3 && p.cutoutTris.size() >= 3;
    uint32_t pointOff = 0, triOff = 0, pointCount = 0, triCount = 0;
    if (cutout) {
        gpuCutoutPool(&p.cutoutPoints, &p.cutoutTris, pointOff, triOff);
        pointCount = (uint32_t)p.cutoutPoints.size();
        triCount   = (uint32_t)p.cutoutTris.size();
    }
    const uint64_t firstVertexFloats = m_gpuVerts;
    const uint64_t firstVertexUnits  = m_gpuVerts16;
    const uint64_t firstIdx          = m_gpuIdx;
    std::vector<float>& d = m_gpu->LeafDescs[batchIdx];
    const size_t off = d.size();
    d.resize(off + godot::GPU_LEAF_CARD_FLOATS);
    float* f = d.data() + off;
    f[0]=pos.x;  f[1]=pos.y;  f[2]=pos.z;  f[3]=0.0f;
    f[4]=leafRight.x;  f[5]=leafRight.y;  f[6]=leafRight.z;  f[7]=0.0f;
    f[8]=leafUp.x;     f[9]=leafUp.y;     f[10]=leafUp.z;    f[11]=0.0f;
    f[12]=leafNormal.x; f[13]=leafNormal.y; f[14]=leafNormal.z; f[15]=0.0f;
    f[16]=hs; f[17]=hw; f[18]=m_windW; f[19]=leafPhase;
    f[20]=col.x; f[21]=col.y; f[22]=col.z; f[23]=0.0f;
    f[24]=basePos.x; f[25]=basePos.y; f[26]=basePos.z; f[27]=0.0f;
    f[28]=cutout?1.0f:0.0f; f[29]=(float)pointOff; f[30]=(float)triOff; f[31]=(float)triCount;
    f[32]=(float)pointCount; f[33]=(float)firstVertexFloats; f[34]=(float)firstVertexUnits; f[35]=(float)firstIdx;
    const uint64_t vertFloats = cutout ? (uint64_t)16 * pointCount : 64;
    // idxCount 的单位是**索引**, 不是三角形 —— 四边形分支给的 6 就是 2 个三角 x 3。
    // CountCutoutTris 返回三角形数, 所以必须 x3。少乘 3 的话 GPU 只预留了实际要写的
    // 三分之一索引, 之后每片叶的索引区都错位, 对拍报 "batch N 索引与 CPU 路径位级不一致"。
    // 这条分支此前从未跑过: 项目里没有任何东西设过 useCutout, 所以这个 bug 一直潜伏。
    const uint64_t idxCount   = cutout ? CountCutoutTris(p.cutoutTris, pointCount) * 3 : 6;
    m_gpuVerts16 += cutout ? pointCount : 4;
    gpuCommit(batchIdx, firstVertexUnits, vertFloats, idxCount);
}

void TreeGenerator::emitGpuFrond(MeshBatch& batch, const FrondNode* node,
                                 const std::vector<BranchRing>& rings, godot::Vector3 frondAnchor,
                                 godot::Vector3 col, int nSeg, int totalCols,
                                 uint32_t pointOff, uint32_t triOff, uint32_t triCount, uint32_t pointCount) {
    const auto& p = node->params;
    const int batchIdx = gpuBatchFor(batch);
    const uint32_t ringOff = gpuRingPoolOffset(&rings);
    const uint64_t firstVertexFloats = m_gpuVerts;
    const uint64_t firstVertexUnits  = m_gpuVerts16;
    const uint64_t firstIdx          = m_gpuIdx;
    std::vector<float>& d = m_gpu->FrondDescs[batchIdx];
    const size_t off = d.size();
    d.resize(off + godot::GPU_FROND_FLOATS);
    float* f = d.data() + off;
    f[0]=p.widthBase; f[1]=p.widthTip; f[2]=p.width; f[3]=p.profilePow;
    f[4]=p.curl; f[5]=p.serrateDepth; f[6]=(p.serrate?1.0f:0.0f); f[7]=0.0f;
    f[8]=col.x; f[9]=col.y; f[10]=col.z; f[11]=0.0f;
    f[12]=m_windW; f[13]=m_windPhase; f[14]=0.0f; f[15]=0.0f;
    f[16]=frondAnchor.x; f[17]=frondAnchor.y; f[18]=frondAnchor.z; f[19]=0.0f;
    f[20]=(float)ringOff; f[21]=(float)rings.size(); f[22]=(float)nSeg; f[23]=(float)totalCols;
    f[24]=(float)pointOff; f[25]=(float)triOff; f[26]=(float)triCount; f[27]=(float)pointCount;
    f[28]=(float)firstVertexFloats; f[29]=(float)firstVertexUnits; f[30]=(float)firstIdx; f[31]=0.0f;
    const bool cutout = pointCount > 0;
    const uint64_t vertFloats = cutout
        ? (uint64_t)16 * pointCount
        : (uint64_t)16 * rings.size() * (uint64_t)totalCols;
    const uint64_t idxCount = cutout
        ? (uint64_t)2 * CountCutoutTris(p.cutoutTris, pointCount)
        : (uint64_t)12 * (uint64_t)nSeg * (uint64_t)(totalCols-1);
    m_gpuVerts16 += cutout ? pointCount : rings.size() * (uint64_t)totalCols;
    gpuCommit(batchIdx, firstVertexUnits, vertFloats, idxCount);
}
