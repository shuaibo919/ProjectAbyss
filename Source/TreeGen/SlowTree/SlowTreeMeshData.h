#pragma once
// Vendored from Reference/SlowTree/src/renderer/Renderer.h (lines 15-114, commit 10e6c66).
// OpenGL 渲染层被剥离, 仅保留 CPU 侧网格数据结构: MeshBatch / TreeMeshData /
// LightingParams / WindParams。见 UPSTREAM_SYNC.md。
#include "SlowTreeTypes.h"
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/variant/vector4i.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

// 单个材质批次（CPU侧）
// branch 顶点格式: pos(3)+normal(3)+uv(2)+wind(2:weight,phase) = 10 floats
// leaf   顶点格式: pos(3)+normal(3)+uv(2)+albedo(3)+wind(2:weight,phase)+anchor(3) = 16 floats
struct MeshBatch {
    std::vector<float>    vertices;
    std::vector<uint32_t> indices;
    MaterialParams        material;
    bool                  isLeaf = false;
    // 实例化代理: 该 batch 是散布叶的"视口预览烘焙"(几何已在 protos 里实例化导出)。
    // 导出 USD 时必须跳过, 否则每片叶完整几何会被重复并入 base 网格(面数/体积爆炸)。
    bool                  instanced = false;
};

struct TreeMeshData {
    std::vector<MeshBatch> batches;
    // 实例化叶原型(FBX 散布用): 一份原型网格 + 一组每实例 transform。
    // 视口渲染时烘成普通 batch; 导出 USD 时成为 PointInstancer(引擎里只存 1 份原型, 省内存)。
    // bone: 该实例刚性绑定的骨索引(骨骼 Nanite Assembly 用; -1=无骨骼/不绑定)。
    struct ProtoInstance { godot::Vector3 pos; godot::Quaternion rot; godot::Vector3 scale; int bone = -1; };
    // 原型内的材质分段: idx 的一段连续区间用一种材质。一个实例枝可含"枝干材质段 + 叶子材质段",
    // 但整枝共用一份实例 transform(subs 只切几何/材质, 不切实例)。
    struct ProtoSub { uint32_t idxOffset = 0; uint32_t idxCount = 0; MaterialParams material; };
    struct InstancedProto {
        // 原型网格(局部空间, Y-up 米制): 三角化顶点(pos/normal/uv) + 索引。
        std::vector<godot::Vector3> pts;
        std::vector<godot::Vector3> nrms;
        std::vector<godot::Vector2> uvs;
        std::vector<uint32_t>  idx;
        MaterialParams         material;   // 兼容/主材质(= subs[0].material)
        std::vector<ProtoSub>  subs;       // 按材质分段(≥1); 每段一个 draw call, 共用实例
        std::vector<ProtoInstance> instances;
    };
    std::vector<InstancedProto> protos;
    // ---- 骨骼 Nanite Assembly 导出数据(仅当散布用的枝干带骨架时填充; 无骨骼则留空, 走 staticMesh 导出) ----
    // 一根骨(世界空间 rest 位姿): 位置 + 父索引 + 名字 + 仿真组(0=树干,1=枝,2=细枝叶)。
    struct SkelBone { godot::Vector3 pos; int parent; std::string name; int simGroup; };
    struct SkinBase {
        // 树干 base 网格(世界空间, Y-up 米制): pos/normal/uv + 每顶点最多 4 骨蒙皮(索引/权重)。
        std::vector<godot::Vector3>  pts;
        std::vector<godot::Vector3>  nrms;
        std::vector<godot::Vector2>  uvs;
        std::vector<uint32_t>   idx;
        std::vector<godot::Vector4i> boneIdx;   // 骨索引, 空槽 = -1
        std::vector<godot::Vector4>  boneWt;    // 权重(归一)
        MaterialParams          material;  // 兼容字段(= subs[0].material)
        // 按材质分段(≥1): 每段一个 GeomSubset(familyName=materialBind), UE 导入后成独立材质槽。
        // 单材质时留空, 导出走整块单槽。
        std::vector<ProtoSub>   subs;
    };
    std::vector<SkelBone> skeleton;   // 空 = 无骨架(走 staticMesh 导出)
    SkinBase              skinBase;   // 树干蒙皮 base(skeleton 非空时有效)
    bool hasSkeleton() const { return !skeleton.empty(); }
    // 高亮几何(pos(3)+normal(3), 6 floats/顶点): 选中节点"自身"的三角网,
    // 用于视口描边(沿法线外扩 + 模板缓冲勾勒轮廓)。
    std::vector<float>    hlVerts;
    std::vector<uint32_t> hlIdx;
    // 拾取三角形: 每个三角形记录三个世界坐标顶点 + 所属节点 id, 供鼠标射线拾取。
    struct PickTri { godot::Vector3 a, b, c; uint32_t node; };
    std::vector<PickTri>  pickTris;
    void clear() { batches.clear(); protos.clear(); hlVerts.clear(); hlIdx.clear(); pickTris.clear(); skeleton.clear(); skinBase = {}; }
};

struct LightingParams {
    godot::Vector3 lightDir    = godot::Vector3(0.5f, 1.0f, 0.3f).normalized();
    godot::Vector3 lightColor  = {1.2f, 1.1f, 0.95f};
    float     lightIntensity = 2.2f;   // 主光亮度倍增
    float     ambientStrength = 1.4f;  // 环境光亮度倍增
    float     exposure      = 1.3f;    // 曝光(色调映射前)
    godot::Vector3 ambientTop  = {0.3f, 0.45f, 0.6f};
    godot::Vector3 ambientBot  = {0.12f, 0.10f, 0.08f};
    // 渐变天空背景(类 SpeedTree)
    godot::Vector3 skyTop      = {0.35f, 0.52f, 0.78f};  // 天顶蓝
    godot::Vector3 skyHorizon  = {0.78f, 0.76f, 0.70f};  // 地平线暖雾
    godot::Vector3 skyGround   = {0.52f, 0.50f, 0.47f};  // 地面灰
    // 阴影(shadow map 自阴影)
    bool  shadowEnabled  = true;
    float shadowStrength = 0.6f;    // 阴影浓度(0=无, 1=全黑)
    float shadowBias     = 0.0025f; // 深度偏移，抑制阴影痤疮(shadow acne)
    float groundShadowStrength = 0.4f; // 地面接收阴影的强度(独立于树自阴影)
    // 地面(用天空渐变着色, 与远端天空融合; 接收阴影压暗)
    bool  groundEnabled  = true;
    float groundAlpha    = 0.85f;   // 地面不透明度: <1 让被遮挡的植被半透明透出
};

// 顶点风力动画参数(类 SpeedTree 顶点风)。三层叠加:
//  1) 全局摆动(Global): 整树随高度比按 hr^2 加权左右摇摆
//  2) 枝条颤动(Branch): 每顶点按烘焙权重+相位做正弦位移(尖端摆动大)
//  3) 叶片摆动(Leaf):   叶片绕锚点(basePos)做小角度旋转(ripple/tumble)
// 全部在顶点着色器完成, CPU 只上传少量 uniform + 时间。阴影 pass 不施加风力(静态)。
struct WindParams {
    bool  enabled        = true;
    float dirAngleDeg    = 30.0f;   // 风向(绕Y轴, 度)
    float strength       = 1.0f;    // 全局强度总倍增
    float globalStrength = 0.18f;   // 全局摆动幅度
    float globalFreq     = 0.9f;    // 全局摆动频率
    float branchStrength = 0.06f;   // 枝条颤动幅度
    float branchFreq     = 2.2f;    // 枝条颤动频率
    float leafStrength   = 0.15f;   // 叶片旋转幅度(弧度)
    float leafFreq       = 5.0f;    // 叶片旋转频率
};
