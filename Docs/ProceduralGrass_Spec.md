# ProceduralGrass 规格说明

`ProceduralGrass` 是一个无资产的程序化草丛节点，用法与 `AncientBuilding`、
`ProceduralTree`、`ProceduralRock` 一致：场景里放一个节点，Inspector 里调参数，网格
即时重建；`bake_mesh()` 供烘焙或 MultiMesh 使用。重活全部在 `abyss` GDExtension 里
（`Source/GrassGen/`）。

## 1. 算法来源

**原创几何设计，无移植参考**（与 RockGen/TreeGen 不同，它们各自移植了一篇论文/一个
参考实现）。核心思路是实时渲染里常见的"折叠卡片"草叶技巧：每片叶子是两个半宽面板
沿中缝硬折出一个浅 V 形，从大多数视角都能读出厚度，且不需要为了双面可见而翻倍几何体
（材质仍然关闭背面剔除，做双重保险）。

## 2. 一株草丛 = 一簇叶片

一个 `ProceduralGrass` 实例生成**一簇**（`ClumpRadius` 范围内若干片叶子），这正是
PCG 散布图要大量摆放的最小单位——与 `ProceduralRock`/`ProceduralTree` 通过
`bake_mesh()` 喂给现有 `Game/Script/PCG/pcg_scatter.gd` / `spawn_meshes` 管线的方式
完全一致。

## 3. 叶片几何

每片叶子由一条二次贝塞尔中心线定义（基部 → 随风倾斜 → 叶尖，倾斜方向和幅度决定第二
控制点，从而带出弓形弧度），沿高度采 8 段：

* **宽度**：基部到叶尖线性收窄，各物种的特征在此基础上覆盖（见下）。
* **V 折截面**：中缝(spine)不偏移，左右两翼各按 `FOLD_HALF_ANGLE_DEG`(18°) 向同一侧
  折出，两翼各自的法线用相反顺序的叉积计算，天然得到互相朝外的一对折面。
* **散布**：`ClumpRadius` 内按 `pow(u, 1.6)` 做半径采样（偏向中心，比面积均匀采样更
  像自然的一丛草），角度均匀分布；`TreeGen::Random`（`Source/TreeGen/TreeMath.h`）
  提供确定性哈希随机——种子相同则整簇一致，这是与 TreeGen/RockGen 共享的工具，没有
  重新发明一套 PRNG。
* **顶点色**：每片叶子按 `BaseColor → TipColor` 做渐变，再叠加物种自带的色相/明度/
  饱和度抖动（`ColorVariance` 控制幅度）。

## 4. 四个物种

物种只切换 `GrassMeshBuilder.cpp` 里 `SpeciesProfile` 表的一行（数量/高度/宽度/弯曲
基线/特征概率/额外色相范围/默认配色），和 `RockGen` 的 `Spec.Form` 分支同一个设计：

| 物种 | 高度 | 每丛数量 | 弯曲 | 特征 |
|---|---|---|---|---|
| THATCH 茅草 | 0.6–1.4 m | 5–9（稀疏） | 低（挺直） | ~30% 叶片在顶部 20% 处下垂收窄成"旗尖" |
| FOXTAIL 狗尾巴草 | 0.25–0.5 m | 8–14 | 中等 | 顶部 40% 替换成锥形穗状花序，逐环随机扰动半径制造"毛绒"轮廓 |
| SHORT 小草 | 0.05–0.15 m | 20–40（密集） | 极低 | 无特殊几何，纯色调最鲜绿 |
| WEED 杂草 | 0.1–0.4 m（高方差） | 10–20 | 多变 | 部分叶片中段膨起("阔叶"钟形曲线)，另一部分叶尖长出三瓣小花(黄/白/紫三色轮换) |

`bUseSpeciesColors`（默认开）让切换物种立刻呈现对应配色，无需手动重新调色；关掉后
`BaseColor`/`TipColor` 按用户设定的值原样使用，覆盖全部物种。

## 5. 节点结构（沿用 AncientBuilding/Tree/Rock 的模式）

```
Source/GrassGen/
├── GrassMeshBuilder.h/.cpp        # 纯 C++ 生成器（GrassSpec + MeshAccumulator）
├── ProceduralGrassParameters.h/.cpp  # Resource，改动发 changed 信号
├── ProceduralGrass.h/.cpp         # MeshInstance3D 节点
└── ProceduralGrassEditorPlugin.h/.cpp  # 编辑器侧边栏（注册于 EDITOR 级）
```

* `mesh` 属性从序列化中去掉（`_validate_property`）——网格是参数的派生产物。
* 材质：`StandardMaterial3D`，顶点色 albedo，roughness 0.95，关闭背面剔除（V 折的另
  一面是正常可见角度，不是需要剔除的错误几何）。
* Flow 图节点：`Game/addons/procedural_grass/`，与 `procedural_rock` 插件同样的
  `register_node_directory` 模式；每个 variant 只生成一次，写入 Resource 流给
  `spawn_meshes`。

## 6. 验收

`Game/Develop/GrassGenValidate.gd`（`res://Develop/GrassGenValidate.tscn`）断言：

* 同种子两次生成顶点数一致（确定性）；
* 不同种子不同；
* 四个物种都能生成非空几何；
* 顶点/法线全部有限。

并把四个物种截图到 `Reference/Shots/Grass/`（经 `Game/Develop/Tools/shot_output.gd`，
不落在 `Game/` 下拖慢编辑器扫描）供视觉验收。
