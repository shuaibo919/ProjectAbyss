#include "SlowTreePresets.h"
#include "VtreeIO.h"
#include <sstream>

// 预设模板库: 每个物种 = 一段内嵌 VEGTOOL 文本(见 SlowTreePresets.h 头注)。
// 这些是 Stage 1 的初始模板(覆盖 golden 对拍所需的全部生成路径: 经典/Interval 分布、
// planar/辐射叶、重力下垂、竹节、Spine+Frond), 视觉调优与同事设计的新模板随
// 转换器流程持续替换。材质贴图为空(纯色), 贴图接入在 SlowTreeMaterials 层。

namespace
{
	// 柳树: 长枝强下垂(gravity 1.9), 平面羽状窄叶, 黄金角分布。
	static const char* kWillowVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 6.2
startRadius 0.3
endRadius 0.07
baseFlare 1.5
noiseAmount 35
noiseFreq 2.2
gnarl 10
taperPow 1.8
sides 7
lengthSegs 14
seed 11
uvTiling 3
mat.albedo 0.3 0.22 0.14
mat.roughness 0.85
ENDNODE
NODE 2 2 300 200
lengthRatio 0.66
radiusScale 0.55
endRatio 0.22
baseFlare 2
taperPow 1.2
spreadAngle 78
rotateOffset 137.5
gravity 1.9
regionStart 0.28
regionEnd 0.92
sizeFalloff 0.25
noiseAmount 55
noiseFreq 1.7
gnarl 8
branchCount 9
sides 5
lengthSegs 7
seed 12
uvTiling 2
mat.albedo 0.32 0.2 0.09
mat.roughness 0.88
ENDNODE
NODE 3 3 500 200
lengthRatio 0.52
radiusScale 0.6
endRatio 0.2
baseFlare 1.6
taperPow 1.1
spreadAngle 60
rotateOffset 137.5
gravity 1.5
regionStart 0.15
regionEnd 0.95
noiseAmount 30
noiseFreq 2.5
gnarl 6
twigCount 7
sides 4
lengthSegs 5
alternating 1
seed 13
mat.albedo 0.3 0.19 0.08
mat.roughness 0.9
ENDNODE
NODE 4 4 700 200
leafCount 14
clusterRadius 0.06
leafSize 0.17
leafAspect 0.26
normalJitter 0.3
normalSoften 0.55
planar 1
sizeFalloff 0.55
seed 14
mat.albedo 0.35 0.55 0.28
mat.roughness 0.85
mat.sssStrength 0.7
ENDNODE
NODE 5 1 100 80
rootCount 6
length 2.6
radiusScale 0.8
endRatio 0.07
baseFlare 2.2
spreadAngle 95
droop 1.1
rotateOffset 137.5
noiseAmount 40
noiseFreq 2.2
gnarl 12
sides 5
lengthSegs 9
seed 15
mat.albedo 0.3 0.19 0.1
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 5
LINK 2 3
LINK 3 4
)VT";

	// 松树: 笔直主干, 竹节式(Interval)分层轮生枝, 细针叶平面羽状。
	static const char* kPineVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 7.5
startRadius 0.34
endRadius 0.09
baseFlare 1.5
noiseAmount 22
noiseFreq 1.3
gnarl 6
taperPow 1.35
sides 7
lengthSegs 13
seed 21
uvTiling 3
mat.albedo 0.34 0.24 0.15
mat.roughness 0.86
ENDNODE
NODE 2 2 300 200
mode 6
intervalSpacing 0.24
branchesPerNode 4
lengthRatio 0.5
radiusScale 0.5
endRatio 0.3
baseFlare 1.8
taperPow 1.1
spreadAngle 82
rotateOffset 137.5
gravity 0.55
regionStart 0.3
regionEnd 0.9
sizeFalloff 0.3
noiseAmount 25
noiseFreq 1.5
gnarl 5
sides 5
lengthSegs 6
seed 22
uvTiling 2
mat.albedo 0.32 0.2 0.1
mat.roughness 0.87
ENDNODE
NODE 3 3 500 200
twigCount 3
lengthRatio 0.4
radiusScale 0.65
endRatio 0.25
baseFlare 1.5
taperPow 1.1
spreadAngle 65
rotateOffset 137.5
gravity 0.5
regionStart 0.2
regionEnd 0.9
noiseAmount 20
noiseFreq 2
gnarl 4
sides 4
lengthSegs 4
alternating 1
seed 23
mat.albedo 0.3 0.19 0.09
mat.roughness 0.9
ENDNODE
NODE 4 4 700 200
leafCount 9
clusterRadius 0.05
leafSize 0.12
leafAspect 0.16
normalJitter 0.25
normalSoften 0.25
planar 1
sizeFalloff 0.5
seed 24
mat.albedo 0.12 0.34 0.16
mat.roughness 0.8
mat.sssStrength 0.35
ENDNODE
NODE 5 1 100 80
rootCount 5
length 2.2
radiusScale 0.75
endRatio 0.08
baseFlare 2.2
spreadAngle 90
droop 0.9
seed 25
mat.albedo 0.3 0.19 0.1
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 5
LINK 2 3
LINK 3 4
)VT";

	// 银杏: 扇形宽叶(leafAspect 1.15), 法线软化高的圆润树冠, 中度下垂。
	static const char* kGinkgoVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 6.6
startRadius 0.3
endRadius 0.08
baseFlare 1.6
noiseAmount 40
noiseFreq 2.8
gnarl 13
taperPow 1.7
sides 7
lengthSegs 14
seed 31
uvTiling 3
mat.albedo 0.36 0.24 0.14
mat.roughness 0.85
ENDNODE
NODE 2 2 300 200
lengthRatio 0.68
radiusScale 0.5
endRatio 0.24
baseFlare 2
taperPow 1.2
spreadAngle 42
rotateOffset 137.5
gravity 0.35
regionStart 0.3
regionEnd 0.88
sizeFalloff 0.15
noiseAmount 40
noiseFreq 2.2
gnarl 8
branchCount 7
sides 6
lengthSegs 7
seed 32
uvTiling 2
mat.albedo 0.33 0.2 0.1
mat.roughness 0.87
ENDNODE
NODE 3 3 500 200
twigCount 5
lengthRatio 0.46
radiusScale 0.6
endRatio 0.22
baseFlare 1.6
taperPow 1.15
spreadAngle 48
rotateOffset 137.5
gravity 0.4
regionStart 0.2
regionEnd 0.92
noiseAmount 25
noiseFreq 2.8
gnarl 5
sides 5
lengthSegs 5
alternating 1
seed 33
mat.albedo 0.3 0.19 0.09
mat.roughness 0.9
ENDNODE
NODE 4 4 700 200
leafCount 15
clusterRadius 0.07
leafSize 0.085
leafAspect 1.15
normalJitter 0.3
normalSoften 0.65
sizeFalloff 0.35
seed 34
mat.albedo 0.55 0.62 0.2
mat.roughness 0.82
mat.sssStrength 0.85
ENDNODE
NODE 5 1 100 80
rootCount 5
length 2.4
radiusScale 0.8
endRatio 0.07
baseFlare 2.2
spreadAngle 92
droop 1
seed 35
mat.albedo 0.3 0.19 0.1
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 5
LINK 2 3
LINK 3 4
)VT";

	// 竹子: 竹节主干(jointCount 16), 竹节轮生枝(Interval), 竹叶直挂枝条。
	static const char* kBambooVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 8.5
startRadius 0.15
endRadius 0.09
baseFlare 1.3
noiseAmount 6
noiseFreq 6
gnarl 0
taperPow 1.05
jointCount 16
jointBulge 0.18
sides 6
lengthSegs 17
seed 41
uvTiling 4
mat.albedo 0.24 0.42 0.12
mat.roughness 0.78
ENDNODE
NODE 2 2 300 200
mode 6
intervalSpacing 0.3
branchesPerNode 2
lengthRatio 0.42
radiusScale 0.6
endRatio 0.3
baseFlare 1.5
taperPow 1.1
spreadAngle 45
rotateOffset 90
gravity 0.35
regionStart 0.2
regionEnd 0.95
sizeFalloff 0.2
noiseAmount 15
noiseFreq 2.5
gnarl 3
sides 4
lengthSegs 5
seed 42
uvTiling 2
mat.albedo 0.22 0.4 0.11
mat.roughness 0.8
ENDNODE
NODE 3 4 500 200
leafCount 10
clusterRadius 0.05
leafSize 0.15
leafAspect 0.22
normalJitter 0.28
normalSoften 0.4
planar 1
sizeFalloff 0.6
seed 43
mat.albedo 0.2 0.5 0.2
mat.roughness 0.75
mat.sssStrength 0.5
ENDNODE
NODE 4 1 100 80
rootCount 4
length 1.6
radiusScale 0.7
endRatio 0.08
baseFlare 2
spreadAngle 92
droop 1.1
seed 44
mat.albedo 0.28 0.2 0.12
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 4
LINK 2 3
)VT";

	// 水杉: 羽状复叶——Twig 挂 Spine 叶轴, Frond 沿叶轴铺连续叶带(覆盖 Spine/Frond 路径)。
	static const char* kMetasequoiaVtree = R"VT(VEGTOOL 1
NODE 1 0 100 200
length 7.8
startRadius 0.32
endRadius 0.08
baseFlare 1.6
noiseAmount 20
noiseFreq 1.5
gnarl 7
taperPow 1.45
sides 7
lengthSegs 14
seed 51
uvTiling 3
mat.albedo 0.35 0.24 0.14
mat.roughness 0.86
ENDNODE
NODE 2 2 300 200
lengthRatio 0.55
radiusScale 0.48
endRatio 0.26
baseFlare 1.9
taperPow 1.2
spreadAngle 70
rotateOffset 137.5
gravity 0.8
regionStart 0.25
regionEnd 0.9
sizeFalloff 0.25
noiseAmount 30
noiseFreq 1.8
gnarl 6
branchCount 8
sides 5
lengthSegs 6
seed 52
uvTiling 2
mat.albedo 0.32 0.2 0.1
mat.roughness 0.87
ENDNODE
NODE 3 3 500 200
twigCount 4
lengthRatio 0.45
radiusScale 0.65
endRatio 0.22
baseFlare 1.5
taperPow 1.15
spreadAngle 60
rotateOffset 137.5
gravity 0.7
regionStart 0.2
regionEnd 0.9
noiseAmount 20
noiseFreq 2.2
gnarl 4
sides 4
lengthSegs 5
alternating 1
seed 53
mat.albedo 0.3 0.19 0.09
mat.roughness 0.9
ENDNODE
NODE 4 5 700 200
spineCount 5
lengthRatio 0.55
radiusScale 0.4
endRatio 0.15
taperPow 1.1
spreadAngle 75
rotateOffset 137.5
gravity 0.9
regionStart 0.1
regionEnd 0.95
noiseAmount 10
noiseFreq 1.8
gnarl 3
sides 4
lengthSegs 10
seed 54
mat.albedo 0.22 0.42 0.1
mat.roughness 0.7
ENDNODE
NODE 5 6 900 200
width 0.16
widthBase 0.25
widthTip 0
profilePow 0.55
curl 0
segsPerSide 1
serrate 0
seed 55
mat.albedo 0.15 0.4 0.18
mat.roughness 0.75
mat.sssStrength 0.6
ENDNODE
NODE 6 1 100 80
rootCount 5
length 2.3
radiusScale 0.75
endRatio 0.07
baseFlare 2.2
spreadAngle 92
droop 1
seed 56
mat.albedo 0.3 0.19 0.1
mat.roughness 0.9
ENDNODE
LINK 1 2
LINK 1 6
LINK 2 3
LINK 3 4
LINK 4 5
)VT";

	struct PresetDef
	{
		const char* Name;
		const char* Vtree;
	};

	const PresetDef PRESETS[] = {
		{ "HelloTree", nullptr },   // 占位: 文本来自 VtreeIO::defaultTemplateText()
		{ "Willow", kWillowVtree },     // 柳
		{ "Pine", kPineVtree },         // 松
		{ "Ginkgo", kGinkgoVtree },     // 银杏
		{ "Bamboo", kBambooVtree },     // 竹
		{ "Metasequoia", kMetasequoiaVtree }, // 水杉
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
