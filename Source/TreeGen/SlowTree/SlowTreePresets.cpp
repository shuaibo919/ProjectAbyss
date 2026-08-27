#include "SlowTreePresets.h"
#include "VtreeIO.h"
#include <godot_cpp/variant/string.hpp>
#include <sstream>

// 预设模板库: 每个物种 = 一段内嵌 VEGTOOL 文本(见 SlowTreePresets.h 头注)。
//
// 这些模板最初只为覆盖生成路径(经典/Interval 分布、planar/辐射叶、重力下垂、竹节、
// Spine+Frond), 视觉上是不合格的: 每个都只有 *一层* Branch, 而上游默认模板
// (VtreeIO::loadDefaultTemplate) 有三层。叶数是逐层相乘的, 所以少一层就少一个数量级 ——
// 实测银杏 525 片 vs 默认模板 26880 片, 三角面 6k vs 356k。2026-08-27 按默认模板的层级
// 结构重调了全部五个预设。改这些模板时先数层数, 再动参数。
//
// 材质贴图为空(纯色), 贴图接入在 SlowTreeMaterials 层。上游模板里的贴图路径指向设计者机器上
// 的 SpeedTree Modeler 示例资源, 本项目无资产, 靠顶点色。

namespace
{
	// 柳树: 长枝强下垂(gravity 1.9), 平面羽状窄叶, 黄金角分布。
	static const char* kWillowVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 6.2
startRadius 0.3
endRadius 0.07
baseFlare 1.6
noiseAmount 85
noiseFreq 3.1
gnarl 14
taperPow 1.6
sides 8
lengthSegs 16
seed 11
uvTiling 3
mat.albedo 0.3 0.22 0.14
mat.roughness 0.85
ENDNODE
NODE 2 2 282 200
lengthRatio 0.78
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow 0.75
spreadAngle 52
rotateOffset 137.5
gravity 1.9
regionStart 0.4
regionEnd 0.92
noiseAmount 90
noiseFreq 1.5
gnarl 10
branchCount 10
sides 6
lengthSegs 6
seed 12
uvTiling 2
mat.albedo 0.33 0.2 0.1
mat.roughness 0.87
ENDNODE
NODE 3 2 417 200
lengthRatio 0.5
radiusScale 0.42
endRatio 0.25
baseFlare 2.2
taperPow 1.5
spreadAngle 64
rotateOffset 137.5
gravity 1.7
regionStart 0.4
regionEnd 0.95
noiseAmount 88
noiseFreq 2.1
gnarl 10
branchCount 7
sides 5
lengthSegs 6
seed 16
uvTiling 2
mat.albedo 0.32 0.19 0.1
mat.roughness 0.88
ENDNODE
NODE 4 3 554 200
twigCount 6
lengthRatio 0.62
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow 1.3
spreadAngle 58
rotateOffset 137.5
gravity 1.6
regionStart 0.2
regionEnd 0.87
noiseAmount 35
noiseFreq 3.5
gnarl 8
sides 4
lengthSegs 4
alternating 1
seed 13
mat.albedo 0.3 0.19 0.09
mat.roughness 0.9
ENDNODE
NODE 5 4 682 200
leafCount 14
clusterRadius 0.05
leafSize 0.22
leafAspect 0.26
normalJitter 0.26
normalSoften 0.55
planar 1
sizeFalloff 0.3
seed 14
mat.albedo 0.35 0.55 0.28
mat.roughness 0.82
mat.sssStrength 0.8
ENDNODE
NODE 7 1 282 119
rootCount 6
length 1.5
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.9
noiseAmount 90
noiseFreq 1.7
gnarl 17
sides 6
lengthSegs 10
seed 15
mat.albedo 0.3 0.19 0.1
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 7
LINK 2 3
LINK 3 4
LINK 4 5
)VT";

	// 松树: 笔直主干, 竹节式(Interval)分层轮生枝, 细针叶平面羽状。
	static const char* kPineVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 9.0
startRadius 0.24
endRadius 0.05
baseFlare 1.6
noiseAmount 85
noiseFreq 3.1
gnarl 14
taperPow 1.6
sides 8
lengthSegs 16
seed 21
uvTiling 3
mat.albedo 0.34 0.24 0.15
mat.roughness 0.85
ENDNODE
NODE 2 2 282 200
mode 6
intervalSpacing 0.1
branchesPerNode 5
lengthRatio 0.34
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow 1.15
spreadAngle 62
rotateOffset 137.5
gravity 0.3
regionStart 0.15
regionEnd 1.0
noiseAmount 20
noiseFreq 1.2
gnarl 3
sizeFalloff 0.85
branchCount 5
sides 6
lengthSegs 6
seed 22
uvTiling 2
mat.albedo 0.33 0.2 0.1
mat.roughness 0.87
ENDNODE
NODE 3 2 417 200
lengthRatio 0.45
radiusScale 0.45
endRatio 0.25
baseFlare 2.2
taperPow 1.3
spreadAngle 45
rotateOffset 137.5
gravity 0.25
regionStart 0.4
regionEnd 0.95
noiseAmount 18
noiseFreq 2.1
gnarl 3
sizeFalloff 0.3
branchCount 5
sides 4
lengthSegs 4
seed 23
uvTiling 2
mat.albedo 0.3 0.19 0.09
mat.roughness 0.88
ENDNODE
NODE 4 5 554 200
spineCount 7
lengthRatio 0.85
radiusScale 0.12
endRatio 0.15
taperPow 1.2
spreadAngle 26
rotateOffset 137.5
gravity 0.35
regionStart 0.1
regionEnd 0.95
noiseAmount 14
noiseFreq 1.8
gnarl 3
sides 3
lengthSegs 6
seed 26
mat.albedo 0.13 0.3 0.12
mat.roughness 0.7
ENDNODE
NODE 5 6 682 200
width 0.22
widthBase 0.85
widthTip 0.2
profilePow 0.8
curl 0
segsPerSide 2
serrate 1
serrateDepth 0.65
seed 27
mat.albedo 0.11 0.3 0.13
mat.roughness 0.75
mat.sssStrength 0.6
ENDNODE
NODE 7 1 282 119
rootCount 6
length 1.6
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.9
noiseAmount 90
noiseFreq 1.7
gnarl 17
sides 6
lengthSegs 10
seed 25
mat.albedo 0.3 0.19 0.1
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 7
LINK 2 3
LINK 3 4
LINK 4 5
)VT";

	// 银杏: 扇形宽叶(leafAspect 1.15), 法线软化高的圆润树冠, 中度下垂。
	static const char* kGinkgoVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 6.6
startRadius 0.3
endRadius 0.075
baseFlare 1.6
noiseAmount 85
noiseFreq 3.1
gnarl 14
taperPow 1.6
sides 8
lengthSegs 16
seed 31
uvTiling 3
mat.albedo 0.36 0.24 0.14
mat.roughness 0.85
ENDNODE
NODE 2 2 282 200
lengthRatio 0.7
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow 0.75
spreadAngle 36
rotateOffset 137.5
gravity 0.95
regionStart 0.4
regionEnd 0.92
noiseAmount 90
noiseFreq 1.5
gnarl 10
branchCount 11
sides 6
lengthSegs 6
seed 32
uvTiling 2
mat.albedo 0.33 0.2 0.1
mat.roughness 0.87
ENDNODE
NODE 3 2 417 200
lengthRatio 0.42
radiusScale 0.42
endRatio 0.25
baseFlare 2.2
taperPow 1.5
spreadAngle 52
rotateOffset 137.5
gravity 0.2
regionStart 0.4
regionEnd 0.95
noiseAmount 88
noiseFreq 2.1
gnarl 10
branchCount 8
sides 5
lengthSegs 6
seed 36
uvTiling 2
mat.albedo 0.32 0.19 0.1
mat.roughness 0.88
ENDNODE
NODE 4 3 554 200
twigCount 6
lengthRatio 0.58
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow 1.3
spreadAngle 66
rotateOffset 137.5
gravity 0.26
regionStart 0.2
regionEnd 0.87
noiseAmount 35
noiseFreq 3.5
gnarl 8
sides 4
lengthSegs 4
alternating 1
seed 33
mat.albedo 0.3 0.19 0.09
mat.roughness 0.9
ENDNODE
NODE 5 4 682 200
leafCount 10
clusterRadius 0.05
leafSize 0.2
leafAspect 1.15
normalJitter 0.26
normalSoften 0.65
seed 34
mat.albedo 0.55 0.62 0.2
mat.roughness 0.82
mat.sssStrength 0.8
ENDNODE
NODE 7 1 282 119
rootCount 6
length 1.4
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.9
noiseAmount 90
noiseFreq 1.7
gnarl 17
sides 6
lengthSegs 10
seed 35
mat.albedo 0.3 0.19 0.1
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 7
LINK 2 3
LINK 3 4
LINK 4 5
)VT";

	// 竹子: 竹节主干(jointCount 16), 竹节轮生枝(Interval), 竹叶直挂枝条。
	static const char* kBambooVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 8.5
startRadius 0.15
endRadius 0.09
baseFlare 1.6
noiseAmount 14
noiseFreq 3.1
gnarl 3
taperPow 1.6
sides 8
lengthSegs 16
seed 41
uvTiling 3
jointCount 16
jointBulge 0.18
mat.albedo 0.24 0.42 0.12
mat.roughness 0.85
ENDNODE
NODE 2 2 282 200
mode 6
intervalSpacing 0.1
branchesPerNode 2
lengthRatio 0.35
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow 1.15
spreadAngle 58
rotateOffset 137.5
gravity 0.3
regionStart 0.45
regionEnd 0.95
noiseAmount 14
noiseFreq 2.4
gnarl 3
sides 6
lengthSegs 6
seed 42
uvTiling 2
mat.albedo 0.22 0.4 0.11
mat.roughness 0.87
ENDNODE
NODE 3 3 417 200
twigCount 3
lengthRatio 0.45
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow 1.1
spreadAngle 38
rotateOffset 137.5
gravity 0.4
regionStart 0.2
regionEnd 0.87
noiseAmount 12
noiseFreq 3.5
gnarl 2
sides 4
lengthSegs 4
alternating 1
seed 44
mat.albedo 0.22 0.4 0.11
mat.roughness 0.9
ENDNODE
NODE 4 4 554 200
leafCount 9
clusterRadius 0.05
leafSize 0.17
leafAspect 0.22
normalJitter 0.26
normalSoften 0.4
planar 1
sizeFalloff 0.35
seed 43
mat.albedo 0.13 0.4 0.17
mat.roughness 0.82
mat.sssStrength 0.8
ENDNODE
NODE 5 0 100 200
length 7.4
startRadius 0.13
endRadius 0.08
baseFlare 1.6
noiseAmount 14
noiseFreq 3.1
gnarl 3
taperPow 1.6
sides 8
lengthSegs 16
seed 51
uvTiling 3
jointCount 16
jointBulge 0.18
posX 0.45
posZ 0.5
mat.albedo 0.24 0.42 0.12
mat.roughness 0.85
ENDNODE
NODE 6 2 282 200
mode 6
intervalSpacing 0.09
branchesPerNode 2
lengthRatio 0.35
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow 1.15
spreadAngle 58
rotateOffset 137.5
gravity 0.3
regionStart 0.45
regionEnd 0.95
noiseAmount 14
noiseFreq 2.4
gnarl 3
sides 6
lengthSegs 6
seed 52
uvTiling 2
mat.albedo 0.22 0.4 0.11
mat.roughness 0.87
ENDNODE
NODE 8 3 417 200
twigCount 3
lengthRatio 0.45
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow 1.1
spreadAngle 38
rotateOffset 137.5
gravity 0.4
regionStart 0.2
regionEnd 0.87
noiseAmount 12
noiseFreq 3.5
gnarl 2
sides 4
lengthSegs 4
alternating 1
seed 54
mat.albedo 0.22 0.4 0.11
mat.roughness 0.9
ENDNODE
NODE 9 4 554 200
leafCount 9
clusterRadius 0.05
leafSize 0.17
leafAspect 0.22
normalJitter 0.26
normalSoften 0.4
planar 1
sizeFalloff 0.35
seed 53
mat.albedo 0.13 0.4 0.17
mat.roughness 0.82
mat.sssStrength 0.8
ENDNODE
NODE 10 0 100 200
length 6.6
startRadius 0.12
endRadius 0.07
baseFlare 1.6
noiseAmount 14
noiseFreq 3.1
gnarl 3
taperPow 1.6
sides 8
lengthSegs 16
seed 61
uvTiling 3
jointCount 16
jointBulge 0.18
posX -0.5
posZ 0.35
mat.albedo 0.24 0.42 0.12
mat.roughness 0.85
ENDNODE
NODE 11 2 282 200
mode 6
intervalSpacing 0.08
branchesPerNode 2
lengthRatio 0.35
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow 1.15
spreadAngle 58
rotateOffset 137.5
gravity 0.3
regionStart 0.45
regionEnd 0.95
noiseAmount 14
noiseFreq 2.4
gnarl 3
sides 6
lengthSegs 6
seed 62
uvTiling 2
mat.albedo 0.22 0.4 0.11
mat.roughness 0.87
ENDNODE
NODE 12 3 417 200
twigCount 3
lengthRatio 0.45
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow 1.1
spreadAngle 38
rotateOffset 137.5
gravity 0.4
regionStart 0.2
regionEnd 0.87
noiseAmount 12
noiseFreq 3.5
gnarl 2
sides 4
lengthSegs 4
alternating 1
seed 64
mat.albedo 0.22 0.4 0.11
mat.roughness 0.9
ENDNODE
NODE 13 4 554 200
leafCount 9
clusterRadius 0.05
leafSize 0.17
leafAspect 0.22
normalJitter 0.26
normalSoften 0.4
planar 1
sizeFalloff 0.35
seed 63
mat.albedo 0.13 0.4 0.17
mat.roughness 0.82
mat.sssStrength 0.8
ENDNODE
NODE 7 1 282 119
rootCount 6
length 1.3
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.9
noiseAmount 90
noiseFreq 1.7
gnarl 17
sides 6
lengthSegs 10
seed 45
mat.albedo 0.28 0.2 0.12
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 7
LINK 2 3
LINK 3 4
LINK 5 6
LINK 6 8
LINK 8 9
LINK 10 11
LINK 11 12
LINK 12 13
)VT";

	// 水杉: 羽状复叶——Twig 挂 Spine 叶轴, Frond 沿叶轴铺连续叶带(覆盖 Spine/Frond 路径)。
	static const char* kMetasequoiaVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 7.8
startRadius 0.32
endRadius 0.08
baseFlare 1.6
noiseAmount 85
noiseFreq 3.1
gnarl 14
taperPow 1.6
sides 8
lengthSegs 16
seed 51
uvTiling 3
mat.albedo 0.34 0.23 0.13
mat.roughness 0.85
ENDNODE
NODE 2 2 282 200
lengthRatio 0.58
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow 1.1
spreadAngle 70
rotateOffset 137.5
gravity 0.8
regionStart 0.4
regionEnd 0.92
noiseAmount 40
noiseFreq 1.5
gnarl 6
branchCount 10
sides 6
lengthSegs 6
seed 52
uvTiling 2
mat.albedo 0.33 0.2 0.1
mat.roughness 0.87
ENDNODE
NODE 3 2 417 200
lengthRatio 0.46
radiusScale 0.42
endRatio 0.25
baseFlare 2.2
taperPow 1.5
spreadAngle 62
rotateOffset 137.5
gravity 0.65
regionStart 0.4
regionEnd 0.95
noiseAmount 88
noiseFreq 2.1
gnarl 10
branchCount 6
sides 5
lengthSegs 6
seed 53
uvTiling 2
mat.albedo 0.32 0.19 0.1
mat.roughness 0.88
ENDNODE
NODE 4 5 554 200
spineCount 8
lengthRatio 0.6
radiusScale 0.4
endRatio 0.15
taperPow 1.2
spreadAngle 76
rotateOffset 137.5
gravity 0.9
regionStart 0.15
regionEnd 0.95
noiseAmount 10
noiseFreq 1.8
gnarl 3
sides 4
lengthSegs 8
seed 54
mat.albedo 0.22 0.42 0.1
mat.roughness 0.7
ENDNODE
NODE 5 6 682 200
width 0.115
widthBase 0.3
widthTip 0.0
profilePow 0.5
curl 0
segsPerSide 1
serrate 1
serrateDepth 0.45
seed 55
mat.albedo 0.15 0.4 0.18
mat.roughness 0.75
mat.sssStrength 0.6
ENDNODE
NODE 7 1 282 119
rootCount 6
length 1.6
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.9
noiseAmount 90
noiseFreq 1.7
gnarl 17
sides 6
lengthSegs 10
seed 56
mat.albedo 0.3 0.19 0.1
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 7
LINK 2 3
LINK 3 4
LINK 4 5
)VT";

	struct PresetDef
	{
		const char* Name;
		const char* Vtree;
	};

	static const char* kPeachVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 2.6
startRadius 0.13
endRadius 0.04
baseFlare 1.6
noiseAmount 85
noiseFreq 3.1
gnarl 14
taperPow 1.6
sides 8
lengthSegs 16
seed 61
uvTiling 3
mat.albedo 0.28 0.24 0.21
mat.roughness 0.85
ENDNODE
NODE 2 2 282 200
lengthRatio 0.85
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow 0.75
spreadAngle 48
rotateOffset 144
gravity 0.9
regionStart 0.3
regionEnd 0.92
noiseAmount 90
noiseFreq 1.5
gnarl 10
branchCount 7
sides 6
lengthSegs 6
seed 62
uvTiling 2
mat.albedo 0.30 0.25 0.21
mat.roughness 0.87
ENDNODE
NODE 3 2 417 200
lengthRatio 0.5
radiusScale 0.45
endRatio 0.25
baseFlare 2.2
taperPow 1.5
spreadAngle 58
rotateOffset 144
gravity 0.55
regionStart 0.4
regionEnd 0.95
noiseAmount 88
noiseFreq 2.1
gnarl 10
branchCount 6
sides 5
lengthSegs 6
seed 63
uvTiling 2
mat.albedo 0.34 0.24 0.19
mat.roughness 0.88
ENDNODE
NODE 4 3 554 200
twigCount 7
lengthRatio 0.6
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow 1.3
spreadAngle 66
rotateOffset 144
gravity 0.4
regionStart 0.2
regionEnd 0.87
noiseAmount 35
noiseFreq 3.5
gnarl 8
sides 4
lengthSegs 4
alternating 1
seed 64
mat.albedo 0.44 0.25 0.18
mat.roughness 0.9
ENDNODE
NODE 5 4 682 200
leafCount 18
clusterRadius 0.05
leafSize 0.09
leafAspect 1.0
normalJitter 0.26
normalSoften 0.85
seed 65
mat.albedo 0.95 0.51 0.62
mat.roughness 0.82
mat.sssStrength 0.8
ENDNODE
NODE 6 4 810 200
leafCount 2
clusterRadius 0.05
leafSize 0.08
leafAspect 0.32
normalJitter 0.26
normalSoften 0.4
planar 1
seed 66
mat.albedo 0.45 0.58 0.26
mat.roughness 0.82
mat.sssStrength 0.8
ENDNODE
NODE 7 1 282 119
rootCount 6
length 0.55
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.9
noiseAmount 90
noiseFreq 1.7
gnarl 17
sides 6
lengthSegs 10
seed 67
mat.albedo 0.28 0.23 0.2
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 7
LINK 2 3
LINK 3 4
LINK 4 5
LINK 4 6
)VT";

	const PresetDef PRESETS[] = {
		{ "HelloTree", nullptr },   // 占位: 文本来自 VtreeIO::defaultTemplateText()
		{ "Willow", kWillowVtree },     // 柳
		{ "Pine", kPineVtree },         // 松
		{ "Ginkgo", kGinkgoVtree },     // 银杏
		{ "Bamboo", kBambooVtree },     // 竹
		{ "Metasequoia", kMetasequoiaVtree }, // 水杉
		{ "Peach", kPeachVtree },       // 桃
	};

	const int32_t PRESET_COUNT = int32_t(sizeof(PRESETS) / sizeof(PRESETS[0]));
} // namespace

namespace godot
{
	namespace SlowTreePresets
	{
		int32_t GetPresetCount()
		{
			return PRESET_COUNT;
		}

		const char* GetPresetName(int32_t Preset)
		{
			if (Preset < 0 || Preset >= PRESET_COUNT)
			{
				return "Unknown";
			}
			return PRESETS[Preset].Name;
		}

		const char* GetPresetVtree(int32_t Preset)
		{
			if (Preset == 0)
			{
				return VtreeIO::defaultTemplateText().c_str();
			}
			if (Preset < 0 || Preset >= PRESET_COUNT)
			{
				return nullptr;
			}
			return PRESETS[Preset].Vtree;
		}

		bool IsEvergreen(int32_t Preset)
		{
			// 松(针叶)、竹、水杉里只有松和竹常绿 —— 水杉是**落叶**针叶树, 秋天转红褐。
			const char* name = GetPresetName(Preset);
			return String(name) == "Pine" || String(name) == "Bamboo";
		}

		bool BuildGraph(int32_t Preset, NodeGraph& Graph)
		{
			if (Preset == 0)
			{
				VtreeIO::loadDefaultTemplate(Graph);
				return true;
			}
			if (Preset < 0 || Preset >= PRESET_COUNT || PRESETS[Preset].Vtree == nullptr)
			{
				return false;
			}
			// 与上游 loadDefaultTemplate 相同: 内嵌文本 → 同一 parseStream。
			std::istringstream ss(PRESETS[Preset].Vtree);
			// parseStream 是 VtreeIO.cpp 内部符号; 经公开接口走内存解析:
			// 写入临时文件会有 I/O 与路径语义差异, 故这里直接调 VtreeIO 的
			// 内存解析入口(见 VtreeIO.h 声明)。
			return VtreeIO::loadFromStream(Graph, ss);
		}
	} // namespace SlowTreePresets
} // namespace godot
