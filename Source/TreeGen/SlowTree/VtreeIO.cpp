// Vendored from Reference/SlowTree/src/io/ProjectIO.cpp 解析侧 (commit 10e6c66)。
// 改动(见 UPSTREAM_SYNC.md), KV 语义/base64/容错缺键逐字保留以保证解析一致:
//  - 头文件路径拉平(graph/…、renderer/… → 平铺目录)
//  - b64decode 仅在 SLOWTREE_FULL_NODES 下保留(Custom 脚本用)
//  - applyParams 中 Custom / ImportTrunk / ImportLeaf / Scatter 四个 case
//    置于 SLOWTREE_FULL_NODES 门后(v1 仅程序化节点; 门外的图载入后由
//    SlowTreeGenerator 校验层报"不支持节点类型"错误)
//  - 写入侧(writeNode/writeLighting/save)与 OBJ/FBX/USD 导出不随核心移植
#include "VtreeIO.h"
#include "NodeGraph.h"
#include "Nodes.h"
#include "SlowTreeMeshData.h"
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace {

#ifdef SLOWTREE_FULL_NODES
// Base64 解码: 自定义节点脚本是多行文本, 编成单行存入行式 .vtree。
// 仅 Custom 节点参数需要, 随门一并裁剪。
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string b64decode(const std::string& in) {
    static int T[256]; static bool init = false;
    if (!init) { for (int i = 0; i < 256; ++i) T[i] = -1;
                 for (int i = 0; i < 64; ++i) T[(unsigned char)kB64[i]] = i; init = true; }
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] < 0) continue;   // 跳过 '=' 与空白
        val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}
#endif

using KV = std::unordered_map<std::string, std::string>;

float getF(const KV& kv, const char* k, float def) {
    auto it = kv.find(k); if (it == kv.end()) return def;
    try { return std::stof(it->second); } catch (...) { return def; }
}
int getI(const KV& kv, const char* k, int def) {
    auto it = kv.find(k); if (it == kv.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}
godot::Vector3 getV3(const KV& kv, const char* k, godot::Vector3 def) {
    auto it = kv.find(k); if (it == kv.end()) return def;
    std::istringstream ss(it->second);
    godot::Vector3 v = def; ss >> v.x >> v.y >> v.z; return v;
}
std::string getS(const KV& kv, const char* k) {
    auto it = kv.find(k); return it != kv.end() ? it->second : std::string();
}

void readMaterial(const KV& kv, MaterialParams& m) {
    m.albedo       = getV3(kv, "mat.albedo",      m.albedo);
    m.roughness    = getF (kv, "mat.roughness",   m.roughness);
    m.metallic     = getF (kv, "mat.metallic",    m.metallic);
    m.aoStrength   = getF (kv, "mat.aoStrength",  m.aoStrength);
    m.sssStrength  = getF (kv, "mat.sssStrength", m.sssStrength);
    m.alphaCutoff  = getF (kv, "mat.alphaCutoff", m.alphaCutoff);
    m.baseColorTex = getS (kv, "mat.baseColorTex");
    m.roughnessTex = getS (kv, "mat.roughnessTex");
    m.normalTex    = getS (kv, "mat.normalTex");
    m.opacityTex   = getS (kv, "mat.opacityTex");
}

void readLighting(const KV& kv, LightingParams& L) {
    L.lightDir        = getV3(kv, "lightDir",        L.lightDir);
    L.lightColor      = getV3(kv, "lightColor",      L.lightColor);
    L.lightIntensity  = getF (kv, "lightIntensity",  L.lightIntensity);
    L.ambientStrength = getF (kv, "ambientStrength", L.ambientStrength);
    L.exposure        = getF (kv, "exposure",        L.exposure);
    L.ambientTop      = getV3(kv, "ambientTop",      L.ambientTop);
    L.ambientBot      = getV3(kv, "ambientBot",      L.ambientBot);
    L.skyTop          = getV3(kv, "skyTop",          L.skyTop);
    L.skyHorizon      = getV3(kv, "skyHorizon",      L.skyHorizon);
    L.skyGround       = getV3(kv, "skyGround",       L.skyGround);
    L.shadowEnabled   = getI (kv, "shadowEnabled",   L.shadowEnabled ? 1 : 0) != 0;
    L.shadowStrength  = getF (kv, "shadowStrength",  L.shadowStrength);
    L.shadowBias      = getF (kv, "shadowBias",      L.shadowBias);
    L.groundShadowStrength = getF (kv, "groundShadowStrength", L.groundShadowStrength);
    L.groundEnabled   = getI (kv, "groundEnabled",   L.groundEnabled ? 1 : 0) != 0;
    L.groundAlpha     = getF (kv, "groundAlpha",     L.groundAlpha);
}

void applyParams(TreeNode* n, const KV& kv) {
    switch (n->getType()) {
    case NodeType::Trunk: {
        auto& p = static_cast<TrunkNode*>(n)->params;
        p.length=getF(kv,"length",p.length); p.startRadius=getF(kv,"startRadius",p.startRadius);
        p.endRadius=getF(kv,"endRadius",p.endRadius); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.posX=getF(kv,"posX",p.posX); p.posZ=getF(kv,"posZ",p.posZ);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.taperPow=getF(kv,"taperPow",p.taperPow);
        p.jointCount=getI(kv,"jointCount",p.jointCount); p.jointBulge=getF(kv,"jointBulge",p.jointBulge);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        readMaterial(kv, p.material); break;
    }
    case NodeType::Roots: {
        auto& p = static_cast<RootsNode*>(n)->params;
        p.rootCount=getI(kv,"rootCount",p.rootCount); p.length=getF(kv,"length",p.length);
        p.radiusScale=getF(kv,"radiusScale",p.radiusScale); p.endRatio=getF(kv,"endRatio",p.endRatio);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.collarSink=getF(kv,"collarSink",p.collarSink);
        p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle);
        p.gravity=getF(kv,"gravity",getF(kv,"droop",p.gravity)); // droop 为旧字段名, 向后兼容
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount);
        p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq); p.gnarl=getF(kv,"gnarl",p.gnarl);
        p.jointCount=getI(kv,"jointCount",p.jointCount); p.jointBulge=getF(kv,"jointBulge",p.jointBulge);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        p.lengthVar=getF(kv,"lengthVar",p.lengthVar); p.radiusScaleVar=getF(kv,"radiusScaleVar",p.radiusScaleVar);
        p.endRatioVar=getF(kv,"endRatioVar",p.endRatioVar); p.spreadAngleVar=getF(kv,"spreadAngleVar",p.spreadAngleVar);
        p.gravityVar=getF(kv,"gravityVar",p.gravityVar);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Branch: {
        auto& p = static_cast<BranchNode*>(n)->params;
        p.mode=(BranchMode)getI(kv,"mode",(int)p.mode);
        p.lengthRatio=getF(kv,"lengthRatio",p.lengthRatio); p.radiusScale=getF(kv,"radiusScale",p.radiusScale);
        p.endRatio=getF(kv,"endRatio",p.endRatio); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle);
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.gravity=getF(kv,"gravity",p.gravity);
        p.regionStart=getF(kv,"regionStart",p.regionStart); p.regionEnd=getF(kv,"regionEnd",p.regionEnd);
        p.sizeFalloff=getF(kv,"sizeFalloff",p.sizeFalloff);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.branchCount=getI(kv,"branchCount",p.branchCount);
        p.intervalSpacing=getF(kv,"intervalSpacing",p.intervalSpacing);
        p.branchesPerNode=getI(kv,"branchesPerNode",p.branchesPerNode);
        p.jointCount=getI(kv,"jointCount",p.jointCount); p.jointBulge=getF(kv,"jointBulge",p.jointBulge);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        p.lengthRatioVar=getF(kv,"lengthRatioVar",p.lengthRatioVar); p.radiusScaleVar=getF(kv,"radiusScaleVar",p.radiusScaleVar);
        p.endRatioVar=getF(kv,"endRatioVar",p.endRatioVar); p.spreadAngleVar=getF(kv,"spreadAngleVar",p.spreadAngleVar);
        p.gravityVar=getF(kv,"gravityVar",p.gravityVar);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Twig: {
        auto& p = static_cast<TwigNode*>(n)->params;
        p.lengthRatio=getF(kv,"lengthRatio",p.lengthRatio); p.radiusScale=getF(kv,"radiusScale",p.radiusScale);
        p.endRatio=getF(kv,"endRatio",p.endRatio); p.baseFlare=getF(kv,"baseFlare",p.baseFlare);
        p.taperPow=getF(kv,"taperPow",p.taperPow); p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle);
        p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset); p.gravity=getF(kv,"gravity",p.gravity);
        p.regionStart=getF(kv,"regionStart",p.regionStart); p.regionEnd=getF(kv,"regionEnd",p.regionEnd);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.twigCount=getI(kv,"twigCount",p.twigCount);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.alternating=getI(kv,"alternating",p.alternating?1:0)!=0;
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        p.lengthRatioVar=getF(kv,"lengthRatioVar",p.lengthRatioVar); p.radiusScaleVar=getF(kv,"radiusScaleVar",p.radiusScaleVar);
        p.endRatioVar=getF(kv,"endRatioVar",p.endRatioVar); p.spreadAngleVar=getF(kv,"spreadAngleVar",p.spreadAngleVar);
        p.gravityVar=getF(kv,"gravityVar",p.gravityVar);
        readMaterial(kv, p.material); break;
    }
    case NodeType::LeafCluster: {
        auto& p = static_cast<LeafClusterNode*>(n)->params;
        p.leafCount=getI(kv,"leafCount",p.leafCount); p.clusterRadius=getF(kv,"clusterRadius",p.clusterRadius);
        p.leafSize=getF(kv,"leafSize",p.leafSize); p.normalJitter=getF(kv,"normalJitter",p.normalJitter);
        p.normalSoften=getF(kv,"normalSoften",p.normalSoften);
        p.leafAspect=getF(kv,"leafAspect",p.leafAspect);
        p.planar=getI(kv,"planar",p.planar?1:0)!=0; p.sizeFalloff=getF(kv,"sizeFalloff",p.sizeFalloff);
        p.seed=getI(kv,"seed",p.seed);
        p.useCutout=getI(kv,"useCutout",p.useCutout?1:0)!=0;
        {
            std::string sp = getS(kv, "cutoutPoints");
            if (!sp.empty()) {
                std::istringstream ss(sp); size_t cnt = 0; ss >> cnt;
                p.cutoutPoints.clear();
                for (size_t i = 0; i < cnt; ++i) { godot::Vector2 q; ss >> q.x >> q.y; p.cutoutPoints.push_back(q); }
            }
            std::string st = getS(kv, "cutoutTris");
            if (!st.empty()) {
                std::istringstream ss(st); size_t cnt = 0; ss >> cnt;
                p.cutoutTris.clear();
                for (size_t i = 0; i < cnt; ++i) { uint32_t t = 0; ss >> t; p.cutoutTris.push_back(t); }
            }
            std::string sr = getS(kv, "cutoutRing");
            if (!sr.empty()) {
                std::istringstream ss(sr); size_t cnt = 0; ss >> cnt;
                p.cutoutRing.clear();
                for (size_t i = 0; i < cnt; ++i) { godot::Vector2 q; ss >> q.x >> q.y; p.cutoutRing.push_back(q); }
            }
        }
        readMaterial(kv, p.material); break;
    }
    case NodeType::Spine: {
        auto& p = static_cast<SpineNode*>(n)->params;
        p.lengthRatio=getF(kv,"lengthRatio",p.lengthRatio); p.radiusScale=getF(kv,"radiusScale",p.radiusScale);
        p.endRatio=getF(kv,"endRatio",p.endRatio); p.taperPow=getF(kv,"taperPow",p.taperPow);
        p.spreadAngle=getF(kv,"spreadAngle",p.spreadAngle); p.rotateOffset=getF(kv,"rotateOffset",p.rotateOffset);
        p.gravity=getF(kv,"gravity",p.gravity);
        p.regionStart=getF(kv,"regionStart",p.regionStart); p.regionEnd=getF(kv,"regionEnd",p.regionEnd);
        p.noiseAmount=getF(kv,"noiseAmount",p.noiseAmount); p.noiseFreq=getF(kv,"noiseFreq",p.noiseFreq);
        p.gnarl=getF(kv,"gnarl",p.gnarl); p.spineCount=getI(kv,"spineCount",p.spineCount);
        p.sides=getI(kv,"sides",p.sides); p.lengthSegs=getI(kv,"lengthSegs",p.lengthSegs);
        p.seed=getI(kv,"seed",p.seed);
        p.uvTilingU=getF(kv,"uvTilingU",p.uvTilingU);
        p.uvTilingV=getF(kv,"uvTilingV",getF(kv,"uvTiling",p.uvTilingV));
        p.lengthRatioVar=getF(kv,"lengthRatioVar",p.lengthRatioVar); p.radiusScaleVar=getF(kv,"radiusScaleVar",p.radiusScaleVar);
        p.endRatioVar=getF(kv,"endRatioVar",p.endRatioVar); p.spreadAngleVar=getF(kv,"spreadAngleVar",p.spreadAngleVar);
        p.gravityVar=getF(kv,"gravityVar",p.gravityVar);
        readMaterial(kv, p.material); break;
    }
    case NodeType::Frond: {
        auto& p = static_cast<FrondNode*>(n)->params;
        p.width=getF(kv,"width",p.width); p.widthBase=getF(kv,"widthBase",p.widthBase);
        p.widthTip=getF(kv,"widthTip",p.widthTip); p.profilePow=getF(kv,"profilePow",p.profilePow);
        p.curl=getF(kv,"curl",p.curl); p.segsPerSide=getI(kv,"segsPerSide",p.segsPerSide);
        p.serrate=getI(kv,"serrate",p.serrate?1:0)!=0; p.serrateDepth=getF(kv,"serrateDepth",p.serrateDepth);
        p.seed=getI(kv,"seed",p.seed);
        p.useCutout=getI(kv,"useCutout",p.useCutout?1:0)!=0;
        {
            std::string sp = getS(kv, "cutoutPoints");
            if (!sp.empty()) {
                std::istringstream ss(sp); size_t cnt = 0; ss >> cnt;
                p.cutoutPoints.clear();
                for (size_t i = 0; i < cnt; ++i) { godot::Vector2 q; ss >> q.x >> q.y; p.cutoutPoints.push_back(q); }
            }
            std::string st = getS(kv, "cutoutTris");
            if (!st.empty()) {
                std::istringstream ss(st); size_t cnt = 0; ss >> cnt;
                p.cutoutTris.clear();
                for (size_t i = 0; i < cnt; ++i) { uint32_t t = 0; ss >> t; p.cutoutTris.push_back(t); }
            }
            std::string sr = getS(kv, "cutoutRing");
            if (!sr.empty()) {
                std::istringstream ss(sr); size_t cnt = 0; ss >> cnt;
                p.cutoutRing.clear();
                for (size_t i = 0; i < cnt; ++i) { godot::Vector2 q; ss >> q.x >> q.y; p.cutoutRing.push_back(q); }
            }
        }
        readMaterial(kv, p.material); break;
    }
    case NodeType::Export: {
        auto& p = static_cast<ExportNode*>(n)->params;
        // 优先读新键 exportMode; 兼容旧工程: 无 exportMode 时用旧 exportWhole(1=整株→模式1)
        int legacyWhole = getI(kv, "exportWhole", 0);
        p.exportMode = getI(kv, "exportMode", legacyWhole != 0 ? 1 : 0);
        p.format = getI(kv, "format", p.format);
        p.specimenCount = getI(kv, "specimenCount", p.specimenCount);
        p.singleFile = getI(kv, "singleFile", p.singleFile ? 1 : 0) != 0;
        p.specimenSpacing = getF(kv, "specimenSpacing", p.specimenSpacing);
        std::string s = getS(kv, "path");
        if (!s.empty()) p.path = s;
        break;
    }
#ifdef SLOWTREE_FULL_NODES
    case NodeType::Custom: {
        auto& p = static_cast<CustomNode*>(n)->params;
        p.count       = getI(kv, "count",       p.count);
        p.baseFlare   = getF(kv, "baseFlare",   p.baseFlare);
        p.taperPow    = getF(kv, "taperPow",    p.taperPow);
        p.gravity     = getF(kv, "gravity",     p.gravity);
        p.noiseAmount = getF(kv, "noiseAmount", p.noiseAmount);
        p.noiseFreq   = getF(kv, "noiseFreq",   p.noiseFreq);
        p.gnarl       = getF(kv, "gnarl",       p.gnarl);
        p.sides       = getI(kv, "sides",       p.sides);
        p.lengthSegs  = getI(kv, "lengthSegs",  p.lengthSegs);
        p.seed        = getI(kv, "seed",        p.seed);
        p.uvTilingU   = getF(kv, "uvTilingU",   p.uvTilingU);
        p.uvTilingV   = getF(kv, "uvTilingV",   p.uvTilingV);
        readMaterial(kv, p.material);
        std::string b = getS(kv, "scriptB64");
        if (!b.empty()) { std::string sc = b64decode(b); if (!sc.empty()) p.script = sc; }
        // 也接受未编码的原始 script(方便 API/MCP 自动化直接写入)
        std::string raw = getS(kv, "script");
        if (!raw.empty()) p.script = raw;
        break;
    }
    case NodeType::ImportTrunk: {
        auto& p = static_cast<ImportTrunkNode*>(n)->params;
        p.scale = getF(kv, "scale", p.scale);
        p.posX  = getF(kv, "posX",  p.posX);
        p.posZ  = getF(kv, "posZ",  p.posZ);
        readMaterial(kv, p.material);
        std::string s = getS(kv, "fbxPath");
        if (!s.empty()) { p.fbxPath = s; p.requestReload = true; }
        break;
    }
    case NodeType::ImportLeaf: {
        auto& p = static_cast<ImportLeafNode*>(n)->params;
        p.scale = getF(kv, "scale", p.scale);
        readMaterial(kv, p.material);
        std::string s = getS(kv, "fbxPath");
        if (!s.empty()) { p.fbxPath = s; p.requestReload = true; }
        break;
    }
    case NodeType::Scatter: {
        auto& p = static_cast<ScatterNode*>(n)->params;
        p.distribution = (ScatterParams::Distribution)getI(kv, "distribution", (int)p.distribution);
        p.evenSpacing  = getF(kv, "evenSpacing",  p.evenSpacing);
        p.count        = getI(kv, "count",        p.count);
        p.leafScale    = getF(kv, "leafScale",    p.leafScale);
        p.leafScaleVar = getF(kv, "leafScaleVar", p.leafScaleVar);
        p.regionStart  = getF(kv, "regionStart",  p.regionStart);
        p.regionEnd    = getF(kv, "regionEnd",    p.regionEnd);
        p.spreadAngle  = getF(kv, "spreadAngle",  p.spreadAngle);
        p.tuck         = getF(kv, "tuck",         p.tuck);
        p.spiralStep   = getF(kv, "spiralStep",   p.spiralStep);
        p.tipScale     = getF(kv, "tipScale",     p.tipScale);
        p.normalJitter = getF(kv, "normalJitter", p.normalJitter);
        p.seed         = getI(kv, "seed",         p.seed);
        readMaterial(kv, p.material);
        p.variants.clear();
        int vc = getI(kv, "variantCount", -1);
        if (vc >= 0) {
            for (int i = 0; i < vc; ++i) {
                std::string vp = getS(kv, ("variant" + std::to_string(i)).c_str());
                ScatterParams::Variant var;
                var.fbxPath = vp;
                var.trunkPart = getI(kv, ("variantTrunkPart" + std::to_string(i)).c_str(), -1);
                if (!vp.empty()) var.requestReload = true;
                p.variants.push_back(std::move(var));
            }
        } else {
            // 旧格式兼容: 单个 fbxPath → 变体[0]
            std::string s = getS(kv, "fbxPath");
            if (!s.empty()) {
                ScatterParams::Variant var;
                var.fbxPath = s;
                var.requestReload = true;
                p.variants.push_back(std::move(var));
            }
        }
        break;
    }
#endif
    }
}

// 解析 .vtree 文本流：首行校验 VEGTOOL，随后逐节点/连线重建图。
// 文件版(load) 与内置默认模板(loadDefaultTemplate) 共用此逻辑。
bool parseStream(NodeGraph& graph, std::istream& f, LightingParams* lighting = nullptr) {
    std::string line;
    if (!std::getline(f, line) || line.rfind("VEGTOOL", 0) != 0) return false;

    graph.clear();

    std::unordered_map<uint32_t, NodeId> idMap;
    std::vector<std::pair<uint32_t,uint32_t>> pendingLinks;

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag; ss >> tag;
        if (tag == "NODE") {
            uint32_t savedId; int typeInt; float px, py;
            ss >> savedId >> typeInt >> px >> py;
            KV kv;
            std::string body;
            while (std::getline(f, body)) {
                if (body == "ENDNODE") break;
                std::istringstream bs(body);
                std::string key; bs >> key;
                std::string val;
                std::getline(bs, val);
                if (!val.empty() && val[0] == ' ') val.erase(0, 1);
                kv[key] = val;
            }
            NodeId newId = graph.addNode((NodeType)typeInt, {px, py});
            idMap[savedId] = newId;
            if (TreeNode* n = graph.getNode(newId))
                applyParams(n, kv);
        } else if (tag == "LIGHTING") {
            // 收集到 ENDLIGHTING 为止；仅当调用方需要(传入非空)时才应用
            KV kv;
            std::string body;
            while (std::getline(f, body)) {
                if (body == "ENDLIGHTING") break;
                std::istringstream bs(body);
                std::string key; bs >> key;
                std::string val;
                std::getline(bs, val);
                if (!val.empty() && val[0] == ' ') val.erase(0, 1);
                kv[key] = val;
            }
            if (lighting) readLighting(kv, *lighting);
        } else if (tag == "LINK") {
            uint32_t from, to; ss >> from >> to;
            pendingLinks.emplace_back(from, to);
        } else if (tag == "COMMENT") {
            float px, py, sx, sy;
            ss >> px >> py >> sx >> sy;
            std::string text;
            std::getline(f, text);            // 标题文本(允许含空格)
            std::string end;
            std::getline(f, end);             // 读掉 ENDCOMMENT
            NodeId cid = graph.addComment({px, py});
            if (CommentFrame* c = graph.getComment(cid)) {
                c->text = text;
                c->size = {sx, sy};
            }
        }
    }

    for (auto& [from, to] : pendingLinks) {
        auto fIt = idMap.find(from), tIt = idMap.find(to);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        TreeNode* pn = graph.getNode(fIt->second);
        TreeNode* cn = graph.getNode(tIt->second);
        if (pn && cn && !cn->inputPins.empty())
            graph.addLink(pn->outputPin.id, cn->inputPins[0].id);
    }

    graph.markDirty();
    return true;
}

// 内置默认工程(取自 HelloTree.vtree)——启动 / Reset to Default 时加载。
static const char* kDefaultTemplate = R"VT(VEGTOOL 1
NODE 1 2 554 199
lengthRatio 0.382
radiusScale 1
endRatio 0.25
baseFlare 2.2
taperPow 1.5
spreadAngle 50
rotateOffset 137.5
gravity 0.18
regionStart 0.509
regionEnd 0.95
noiseAmount 30
noiseFreq 3
gnarl 10
branchCount 4
sides 6
lengthSegs 6
seed 2
uvTiling 2
mat.albedo 0.32 0.18 0.08
mat.roughness 0.509
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex
mat.roughnessTex
mat.normalTex
mat.opacityTex
ENDNODE
NODE 2 0 100 200
length 6.178
startRadius 0.372
endRadius 0.079
baseFlare 1.4
posX 0
posZ 0
noiseAmount 90
noiseFreq 3.272
gnarl 15
taperPow 1.6
sides 8
lengthSegs 16
seed 1
uvTiling 3
mat.albedo 0.38 0.22 0.1
mat.roughness 0.584
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
NODE 3 2 282 199
lengthRatio 0.714
radiusScale 0.689
endRatio 0.255
baseFlare 2.05
taperPow 0.729
spreadAngle 33.177
rotateOffset 154.322
gravity 1
regionStart 0.397
regionEnd 0.911
noiseAmount 90
noiseFreq 1.423
gnarl 10
branchCount 12
sides 6
lengthSegs 6
seed 2
uvTiling 2
mat.albedo 0.32 0.18 0.08
mat.roughness 0.586
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
NODE 4 3 682 199
lengthRatio 0.579
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow 1.3
spreadAngle 65
rotateOffset 137.5
gravity 0.25
regionStart 0.204
regionEnd 0.866
noiseAmount 35
noiseFreq 3.5
gnarl 8
twigCount 7
sides 5
lengthSegs 5
alternating 1
seed 3
uvTiling 1
mat.albedo 0.28 0.16 0.07
mat.roughness 0.9
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
NODE 5 4 833 199
leafCount 10
clusterRadius 0.05
leafSize 0.189
normalJitter 0.252
seed 4
mat.albedo 0.15 0.48 0.06
mat.roughness 0.883
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 1
mat.alphaCutoff 0.663
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Leaves\Front_01.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Leaves\Front_01_Normal_Winter.png
mat.opacityTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Leaves\Front_01_Opacity.png
ENDNODE
NODE 6 2 417 199
lengthRatio 0.386
radiusScale 0.395
endRatio 0.25
baseFlare 2.2
taperPow 1.5
spreadAngle 50
rotateOffset 137.5
gravity 0.18
regionStart 0.392
regionEnd 0.95
noiseAmount 90
noiseFreq 2.019
gnarl 10
branchCount 8
sides 6
lengthSegs 6
seed 2
uvTiling 2
mat.albedo 0.32 0.18 0.08
mat.roughness 0.505
mat.metallic 0
mat.aoStrength 1
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
NODE 7 1 282 119
rootCount 5
length 1.413
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.864
noiseAmount 90
noiseFreq 1.668
gnarl 17.467
sides 6
lengthSegs 10
seed 33
uvTiling 0.472
mat.albedo 0.3 0.19 0.1
mat.roughness 0.561
mat.metallic 0
mat.aoStrength 0.55
mat.sssStrength 0
mat.alphaCutoff 0.5
mat.baseColorTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_diffuse.png
mat.roughnessTex
mat.normalTex C:\Program Files\SpeedTree\SpeedTree Modeler v10.0.0\samples\Games\Broadleaf\Bark\BaseBark_Depth_Normal.png
mat.opacityTex
ENDNODE
LINK 1 4
LINK 2 3
LINK 2 7
LINK 3 6
LINK 4 5
LINK 6 1
)VT";

} // namespace

namespace VtreeIO {

bool load(NodeGraph& graph, const std::string& path, LightingParams* lighting) {
    std::ifstream f(path);
    if (!f) return false;
    return parseStream(graph, f, lighting);
}

bool loadFromStream(NodeGraph& graph, std::istream& stream) {
    return parseStream(graph, stream);
}

void loadDefaultTemplate(NodeGraph& graph) {
    std::istringstream ss(kDefaultTemplate);
    parseStream(graph, ss);
}

const std::string& defaultTemplateText() {
    static const std::string s = kDefaultTemplate;
    return s;
}

} // namespace VtreeIO
