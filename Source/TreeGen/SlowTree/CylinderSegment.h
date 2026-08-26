#pragma once
// Vendored from Reference/SlowTree (cylinder 几何, commit 见 UPSTREAM_SYNC.md)。
// glm::vec3 已改写为 godot::Vector3(项目决策: 核心只用 Godot 数学库)。
#include <godot_cpp/variant/vector3.hpp>
#include <vector>
#include <cstdint>
#include <random>

// 圆柱段的一个截面环（改名避免与imgui内部Ring冲突）
struct BranchRing {
    godot::Vector3 center;
    float          radius;
    godot::Vector3 up;
    godot::Vector3 right;
};

class CylinderSegment {
public:
    // 平滑贝塞尔曲线环列
    static std::vector<BranchRing> buildCurvedRings(
        godot::Vector3 p0, godot::Vector3 p1, godot::Vector3 p2,
        float radiusStart, float radiusEnd,
        int lengthSegs);

    // 自然生长环列：叠加样条噪声扰动 + 螺旋扭曲(gnarl) + 非线性锥度
    // noiseAmount: 每段方向随机扰动最大角度(度), 让枝干自然弯曲而非笔直
    // noiseFreq:   扰动频率, 越大方向变化越频繁
    // gnarl:       沿枝干长度累计的螺旋扭转总角度(度)
    // taperPow:    锥度曲线幂(1=线性插值, >1基部饱满末端尖锐)
    // jointCount:  沿长度的竹节数(0=无节, 用于竹子): 在每个节位周期性膨大半径
    // jointBulge:  竹节膨大幅度(半径倍增比例, 0=无膨大)
    static std::vector<BranchRing> buildNaturalRings(
        godot::Vector3 origin, godot::Vector3 dir, float length,
        float radiusStart, float radiusEnd,
        int lengthSegs, float noiseAmount, float noiseFreq,
        float gnarl, float taperPow, float gravity,
        std::mt19937& rng,
        int jointCount = 0, float jointBulge = 0.0f);

    static void buildCap(
        const BranchRing& bottom, const BranchRing& top,
        int sides,
        std::vector<float>&    outVerts,
        std::vector<uint32_t>& outIdx,
        uint32_t               baseIdx);

    static void build(
        const std::vector<BranchRing>& rings,
        int sides,
        std::vector<float>&    outVerts,
        std::vector<uint32_t>& outIdx);

private:
    static godot::Vector3 bezier2(godot::Vector3 p0, godot::Vector3 p1, godot::Vector3 p2, float t);
    static godot::Vector3 bezierTangent2(godot::Vector3 p0, godot::Vector3 p1, godot::Vector3 p2, float t);
    static godot::Vector3 perpendicular(godot::Vector3 dir);
    static godot::Vector3 ptfTransport(godot::Vector3 right, godot::Vector3 prevUp, godot::Vector3 newUp);
};
