#include "CylinderSegment.h"
#include <godot_cpp/core/math.hpp>
#include <cmath>
#include <algorithm>

namespace
{
	constexpr float kTwoPi = 6.28318530718f;
}

// ---- 私有工具 ----
godot::Vector3 CylinderSegment::bezier2(godot::Vector3 p0, godot::Vector3 p1, godot::Vector3 p2, float t) {
    float u = 1.0f - t;
    return u*u*p0 + 2*u*t*p1 + t*t*p2;
}

godot::Vector3 CylinderSegment::bezierTangent2(godot::Vector3 p0, godot::Vector3 p1, godot::Vector3 p2, float t) {
    float u = 1.0f - t;
    return 2*u*(p1-p0) + 2*t*(p2-p1);
}

godot::Vector3 CylinderSegment::perpendicular(godot::Vector3 dir) {
    godot::Vector3 ref = (std::abs(dir.y) < 0.9f) ? godot::Vector3(0,1,0) : godot::Vector3(1,0,0);
    return ref.cross(dir).normalized();
}

// Parallel Transport Frame：将right从prevUp旋转到newUp
godot::Vector3 CylinderSegment::ptfTransport(godot::Vector3 right, godot::Vector3 prevUp, godot::Vector3 newUp) {
    godot::Vector3 rotAxis = prevUp.cross(newUp);
    float sinA = rotAxis.length();
    if (sinA < 1e-5f) return right;
    rotAxis /= sinA;
    float cosA = prevUp.dot(newUp);
    return (right * cosA
        + rotAxis.cross(right) * sinA
        + rotAxis * rotAxis.dot(right) * (1.0f - cosA)).normalized();
}

// ---- 平滑贝塞尔曲线 ----
std::vector<BranchRing> CylinderSegment::buildCurvedRings(
    godot::Vector3 p0, godot::Vector3 p1, godot::Vector3 p2,
    float radiusStart, float radiusEnd,
    int lengthSegs)
{
    std::vector<BranchRing> rings;
    rings.reserve(lengthSegs + 1);

    godot::Vector3 initTangent = bezierTangent2(p0,p1,p2, 0.0001f).normalized();
    godot::Vector3 right = perpendicular(initTangent);

    for (int i = 0; i <= lengthSegs; ++i) {
        float t = (float)i / (float)lengthSegs;
        BranchRing ring;
        ring.center = bezier2(p0, p1, p2, t);
        ring.radius = godot::Math::lerp(radiusStart, radiusEnd, t);
        ring.up     = bezierTangent2(p0, p1, p2,
                          std::clamp(t, 0.0001f, 0.9999f)).normalized();
        if (i > 0)
            right = ptfTransport(right, rings.back().up, ring.up);
        ring.right = right;
        rings.push_back(ring);
    }
    return rings;
}

// ---- 自然生长环列 ----
// 沿枝干逐段前进，方向叠加平滑正弦噪声扰动(自然弯曲)与重力下垂，
// 截面坐标系沿长度渐进螺旋扭转(gnarl)，半径按幂曲线锥化(基部饱满、末端尖锐)。
std::vector<BranchRing> CylinderSegment::buildNaturalRings(
    godot::Vector3 origin, godot::Vector3 dir, float length,
    float radiusStart, float radiusEnd,
    int lengthSegs, float noiseAmount, float noiseFreq,
    float gnarl, float taperPow, float gravity,
    std::mt19937& rng,
    int jointCount, float jointBulge)
{
    lengthSegs = std::max(1, lengthSegs);
    taperPow   = std::max(0.05f, taperPow);
    float pi2  = kTwoPi;

    // 绕任意轴旋转向量(角度制)
    auto rotate = [](godot::Vector3 v, godot::Vector3 axis, float deg) {
        float r = godot::Math::deg_to_rad(deg), c = std::cos(r), s = std::sin(r);
        return v*c + axis.cross(v)*s + axis*axis.dot(v)*(1.0f-c);
    };

    // 每根枝条独立的噪声相位，保证不同枝走向各异
    std::uniform_real_distribution<float> phase(0.0f, pi2);
    float phA = phase(rng), phB = phase(rng);

    // 竹节：沿长度周期性膨大半径。峰值落在整数节位，高次幂使膨大带很窄。
    auto jointFactor = [&](float t) -> float {
        if (jointCount <= 0 || jointBulge <= 0.0f) return 1.0f;
        float pulse = 0.5f + 0.5f * std::cos(t * (float)jointCount * pi2);
        return 1.0f + jointBulge * std::pow(pulse, 6.0f);
    };

    std::vector<BranchRing> rings;
    rings.reserve(lengthSegs + 1);

    godot::Vector3 curOrigin = origin;
    godot::Vector3 curDir    = dir.normalized();
    godot::Vector3 right      = perpendicular(curDir);
    float segLen = length / (float)lengthSegs;

    // 起始环
    {
        BranchRing r0;
        r0.center = curOrigin;
        r0.radius = radiusStart * jointFactor(0.0f);
        r0.up     = curDir;
        r0.right  = right;
        rings.push_back(r0);
    }

    for (int i = 1; i <= lengthSegs; ++i) {
        float t = (float)i / (float)lengthSegs;

        // 平滑噪声：两条正交轴上的余弦扰动，除以段数使总弯幅与段数无关
        float defl1 = (noiseAmount / (float)lengthSegs)
                    * std::cos(t * noiseFreq * pi2 + phA);
        float defl2 = (noiseAmount / (float)lengthSegs)
                    * std::cos(t * noiseFreq * pi2 + phB);
        godot::Vector3 fwd = curDir.cross(right).normalized();
        godot::Vector3 newDir = rotate(curDir, right, defl1);
        newDir = rotate(newDir, fwd, defl2);

        // 重力：方向逐段向下拉一点(累计幅度约 gravity*0.5)
        if (gravity > 0.0f)
            newDir = newDir.lerp(godot::Vector3(0,-1,0),
                              gravity * 0.5f / (float)lengthSegs);
        newDir = newDir.normalized();

        // 平行传输 + 螺旋扭转(gnarl)保持坐标系连续并旋拧
        right = ptfTransport(right, curDir, newDir);
        right = rotate(right, newDir, gnarl / (float)lengthSegs);
        right = (right - newDir * right.dot(newDir)).normalized();

        curOrigin += newDir * segLen;
        curDir = newDir;

        // 非线性锥度：radius = lerp(start,end, t^taperPow)，>1时基部饱满末端尖
        float rf = std::pow(t, taperPow);
        BranchRing ring;
        ring.center = curOrigin;
        ring.radius = godot::Math::lerp(radiusStart, radiusEnd, rf) * jointFactor(t);
        ring.up     = curDir;
        ring.right  = right;
        rings.push_back(ring);
    }
    return rings;
}

// ---- buildCap ----
void CylinderSegment::buildCap(
    const BranchRing& bottom, const BranchRing& top,
    int sides,
    std::vector<float>&    outVerts,
    std::vector<uint32_t>& outIdx,
    uint32_t               baseIdx)
{
    float pi2 = kTwoPi;
    int   vBase = (int)outVerts.size() / 6;

    auto pushVert = [&](godot::Vector3 pos, godot::Vector3 normal) {
        outVerts.push_back(pos.x); outVerts.push_back(pos.y); outVerts.push_back(pos.z);
        outVerts.push_back(normal.x); outVerts.push_back(normal.y); outVerts.push_back(normal.z);
    };

    for (int ring = 0; ring < 2; ++ring) {
        const BranchRing& r = (ring == 0) ? bottom : top;
        for (int j = 0; j <= sides; ++j) {
            float angle = (float)j / (float)sides * pi2;
            godot::Vector3 localDir = r.right * std::cos(angle)
                               + r.up.cross(r.right) * std::sin(angle);
            godot::Vector3 pos    = r.center + localDir * r.radius;
            godot::Vector3 normal = localDir.normalized();
            pushVert(pos, normal);
        }
    }

    int ringVerts = sides + 1;
    for (int j = 0; j < sides; ++j) {
        uint32_t b0 = baseIdx + vBase + j;
        uint32_t b1 = baseIdx + vBase + j + 1;
        uint32_t t0 = baseIdx + vBase + ringVerts + j;
        uint32_t t1 = baseIdx + vBase + ringVerts + j + 1;
        outIdx.insert(outIdx.end(), {b0, b1, t0, b1, t1, t0});
    }
}

// ---- build ----
void CylinderSegment::build(
    const std::vector<BranchRing>& rings,
    int sides,
    std::vector<float>&    outVerts,
    std::vector<uint32_t>& outIdx)
{
    if (rings.size() < 2) return;
    for (size_t i = 0; i + 1 < rings.size(); ++i) {
        buildCap(rings[i], rings[i+1], sides, outVerts, outIdx, 0);
    }
}
