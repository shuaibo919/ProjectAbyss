# ProceduralRock 规格说明

`ProceduralRock` 是一个无资产的程序化岩石节点，用法与 `AncientBuilding`、`ProceduralTree`
一致：场景里放一个节点，Inspector 里调参数，网格即时重建；`bake_mesh()` 供烘焙或
MultiMesh 使用。重活全部在 `abyss` GDExtension 里（`Source/RockGen/`）。

## 1. 算法来源

核心算法移植自 **Unity Procedural Rock Generation**（Przemyslaw Zaworski，MIT，
`Reference/Unity-Procedural-Rock-Generation/`），该参考本身基于两个来源：

* https://www.shadertoy.com/view/ldSSzV
* http://paulbourke.net/geometry/polygonise/ （marching cubes 经典表格）

辅以 **proc-rock**（`Reference/proc-rock/`）的形态分类思路（pebble / cube / boulder 与
ground cut），但舍弃其 CGAL / libigl 依赖 —— 三个基础形态改用解析 SDF 表达。

## 2. 标量场（ScalarField.compute 的移植）

单位空间 `[-0.5, 0.5]³` 内定义：

1. **基础形态 SDF**（`BaseForm`）：
   * Boulder：`length(p) - 0.95`（参考原样）
   * Pebble：Y 轴压扁的椭球，`Flatness * (length(p / (1, Flatness, 1)) - 0.95)`
   * Slab：圆角盒，半边长 `(0.95, 0.95·Flatness, 0.95)`，圆角半径 `0.95·Roundness`
2. **RockSDF**：基础形态与 N 个哈希种子的球做 smooth-min（IQ 公式，b 取负）：
   ```
   j = i + seed
   r = 2.5 + frac(sin(j * 727.1) * 435.545)
   v = normalize(frac(sin((127.231, 491.7, 718.423) * j) * 435.543) * 2 - 1)
   a = d;  b = length(p + v * r) - r * 0.8
   h = clamp(0.5 + 0.5 * (-b - a) / k, 0, 1)
   d = lerp(a, -b, h) + k * h * (1 - h)
   ```
   因为 lerp 目标取 `-b`，每个 bump 只会**向内雕**，表面永远不超出基础形态。
3. **位移**：三平面 FBM（6 个八度的 value noise，域旋转与参考一致），按表面法向
   平方加权（`Surface3D`），幅度 `DisplacementScale`，频率 `DisplacementSpread`。
4. **密度**：`smoothstep(0.05, 0, d)` —— 内 1 外 0，0.05 宽的软壳。HLSL 对 a > b 的
   smoothstep 未定义，所以反斜率的斜坡显式写出。

## 3. 惰性求值（CPU 移植的关键改动）

参考在 GPU 上对每个体素算满场。CPU 版把所有"只依赖种子"和"会被反复读取"的量
全部缓存，热路径上不做重复计算：

* **Bump 链预计算**：哈希球链只依赖 seed，每次生成只算 `Steps` 次三角函数，
  而不是每格点一次。
* **第一趟**：基础 `Map` 填满 `Resolution³` 网格（无噪声位移），同时缓存其中心差分
  梯度。
* **第二趟**：marching cubes 遍历 `(Resolution-1)³` 个 cell，只有"活跃" cell 才补算
  位移。活跃判据是精确的：位移幅度 ≤ `DisplacementScale · FBM_MAX`（FBM_MAX =
  0.984375，六八度权重和），密度在 (0,1) 之间要求 `|Map + disp| < 0.05`，故
  `|Map| ≤ 0.05 + DisplacementScale·FBM_MAX` 之外密度必为 0 或 1，不可能被跨越。
  cell 跨越切割平面时同样活跃。
* **位移按格点记忆化**：最多 8 个 cell 共享一个角点，`Surface3D` 噪声每个壳内格点
  只求一次（而不是每角点每 cell 一次），存储为格点位移数组。
* **法线与裂缝色走网格插值**：顶点法线 = 缓存的 `∇Map` 三线性插值（等价于对参考
  的体积纹理采样梯度），裂缝色 = 格点位移高度的三线性插值。参考的 `NormalMap`
  对每个体素做 6 次解析差分，在 CPU 上每顶点一次不可接受。
* **并行化**：三趟全部按 z 切片分给 `std::thread`（至多 16 个，每趟互不相扰的 z
  块），pass 2 每线程写各自的累积器、最后按 z 序拼接——输出与串行版只在"壳上位移
  采样点用直接格点坐标而非 cell 派生坐标"这一处相差 ≤1 float ulp，百万级边中通常
  只有个位数等值面 crossing 会翻转（96³ 实测 43194 vs 43192 tris），同版本内重跑
  完全确定。

一个 48³ 的岩石因此只做约 10⁵ 次噪声采样而不是天真实现的 3×10⁹ 次 —— 这是编辑器里
拖滑块实时重建和不可用之间的差别。

## 4. Marching cubes（Triangulation.compute 的移植）

* 表格：Paul Bourke 的 edgeTable/triTable，由 `Source/RockGen/tools/convert_mc_tables.py`
  从参考 shader 直接抽取生成 `RockMarchingCubesTables.h`，杜绝手抄错误。
* 角点顺序与参考一致；`bit i` 在角点 i **密度 < 0.5（外侧）**时置位。按此约定配合
  Bourke 表格，三角形正面朝外。
* 顶点法线 = 基础场 `∇Map` 的网格梯度三线性插值（见 §3；位移的高频梯度故意略去，
  否则会在 marching cubes 分辨率上产生明暗锯齿）。密度沿 d 递减（smoothstep
  反斜率），所以 `∇Map` 已指向外侧。
* 切割面：cut 施加在**密度空间**（平面以下密度直接取 0），切面正好落在
  `GroundCut` 平面上，而不是被 0.05 软壳推偏；切面上的顶点法线强制 `(0, -1, 0)`。
* UV：逐三角形立方投影（参考的 `CubeProjection`），单位空间 0..1，与 Scale 无关；
  顶点按面展开，保证每张面有完整 UV。
* 顶点色：由局部位移高度在 `BaseColor`/`CreviceColor` 之间插值 —— 噪声在表面上
  走得越深，颜色越暗，模拟石缝。

## 5. 节点结构（沿用 AncientBuilding/Tree 的模式）

```
Source/RockGen/
├── RockMeshBuilder.h/.cpp        # 纯 C++ 生成器（RockSpec + MeshAccumulator）
├── RockMarchingCubesTables.h     # 生成的 Bourke 表格
├── ProceduralRockParameters.h/.cpp  # Resource，改动发 changed 信号
├── ProceduralRock.h/.cpp         # MeshInstance3D 节点
├── ProceduralRockEditorPlugin.h/.cpp  # 编辑器侧边栏（注册于 EDITOR 级）
└── tools/convert_mc_tables.py    # 表格生成脚本（来源可追溯）
```

* `mesh` 属性从序列化中去掉（`_validate_property`）—— 网格是参数的派生产物，
  存进场景会把大 ArrayMesh 内联到每个含岩石的场景里。
* 材质：`StandardMaterial3D`，顶点色 albedo，roughness 0.9。
* Flow 图节点：`Game/addons/procedural_rock/`，与 `ancient_building` 插件同样的
  register_node_directory 模式；每个 variant 只生成一次，写入 Resource 流给
  `spawn_meshes`。

## 6. 参数与参考的对应

| ProceduralRock | Unity 参考 | proc-rock |
|---|---|---|
| form | —（只有球） | Pebble / Liquid / Cube / Boulder 四选一（简化为三） |
| seed | Seed | Seed |
| resolution | Resolution（100，GPU） | Geometry Amount 的间接对应 |
| scale | Scale（2.5） | — |
| steps | Steps（20） | — |
| smoothness | Smoothness（0.05） | — |
| displacement_scale | DisplacementScale（0.15） | Roughness |
| displacement_spread | DisplacementSpread（10） | Distortion（Billow 噪声频率） |
| flatness / roundness | — | 形态几何 |
| cut_ground / ground_cut | — | Cut Ground |
| base_color / crevice_color | —（灰阶 SampleColor） | 纹理着色 |

## 7. 验收

`Game/Develop/RockGenValidate.gd`（`res://Develop/RockGenValidate.tscn`）断言：

* 同种子两次生成顶点数一致（确定性）；
* 不同种子不同；
* 高 resolution 成本更高；
* cut ground 时 AABB 底面落在 `ground_cut × scale`（±2cm）；
* 顶点/法线全部有限；
* 三种形态都能生成。

并把各预设截图到 `Game/Develop/RockGenShots/` 供视觉验收。
