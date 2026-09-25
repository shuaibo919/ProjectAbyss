# AncientBuilding 优化执行文档（三篇论文驱动）

> 本文档是 `Docs/AncientBuilding_QualityPlan.zh-CN.md`（astra 评审规划）的**详细执行版**：每条任务给出论文依据、现状证据、改动文件与函数、算法要点、参数默认值和验收标准。
> 论文原文已到位（见 §6），PCG Ancient 样例到位后即可按阶段 A 开工。
> 所有 C++ 改动需重编 GDExtension（uv+scons，见仓库构建记忆）后进编辑器验证。

---

## 1. 三篇论文 → 本项目技术映射总表

| 论文·章节 | 技术 | 本项目现状 | 去向 |
|---|---|---|---|
| **Hu&Qin §3.1.1** 光线投射顶点计算 (eq.1) | 扫掠截面沿切线逐结投射，避免锐角处管状网格畸变 | `SplineSweep.cpp:145 BuildSweep` 已存在，算法细节待核对 | 阶段 C 脊/连檐扫掠 |
| **Hu&Qin §3.1.2–3.1.3** 位移曲线+方向约束 (eq.2–5) | 曲线 y(x) 沿径向推拉顶点做构件造型 | **已存在**：`AncientSplineSweep` 节点（`DisplacementCurve`/`ConstraintAngle`/`ContourPreset`）+ `MeshAccumulator::AddSweep` | 阶段 C 复用到柱础/阶条石/戗脊尾 |
| **Hu&Qin §3.2** 构件实例化+切余+径向剔除 (eq.6–7) | k=⌈段长/构件宽⌉ 实例化，切面裁掉越界部分 | 瓦垄 sweep、翼角切瓦（`BuildingBuilder.cpp:1244-1259`）、`fence_lambda`(=λ) 已存在 | 阶段 B 瓦面搭接、C 椽头 |
| **Hu&Qin §4.1** 层次树 | frame+构件统一树描述，构件可编辑/替换 | 无（单入口 `BuildBuilding` 直出单网格） | **阶段 E 构件替换接口** |
| **Hu&Qin §4.2** Cr 瓦面覆盖率 | 头盔/圆脊顶瓦面覆盖独立参数 | **已存在**：`settings.gd:32 tile_coverage` | 阶段 A 扩展至 9 屋顶时沿用 |
| **Hu&Qin §4.2** sides 多边形建筑 | `sides` 参数描述圆亭 | **已存在**：`PolygonalBuilding.cpp`（`BuildCentralisedRoof`/`BuildPolygonalBuilding`） | — |
| **Hu&Qin 表1** 11D 模数、bracket height | 尺寸模数体系 | **已实现**（`AncientBuildingParameters.h:251-266`，与论文同源） | 阶段 A4 明间/柱高独立 |
| **Hu&Qin §5.1** 采样率 s=⌈d×i/t⌉ (eq.20) | 距离自适应细分，s≥1 不增面 | 无 | 阶段 A3（网格质量版）+ G1（LOD 版） |
| **Hu&Qin §5.2/5.3** 构件代理 quad / 建筑 billboard | 两级投影代理，双条件更新 | 无 | 阶段 G2/G3 |
| **Hu&Qin §6.3 负面反馈** | ①瓦片与脊相交 ②参数化加载慢 ③多屋顶提取慢 | ①我们同坑（瓦垄连续条带直抵脊下）②一次生成无缓存 ③N/A | 阶段 B4、G4 |
| **Qin2023 Table 7** 建筑等级表 | 等级→{屋顶/开间/台基/踏步} 全映射 | 无等级概念 | **阶段 F1** |
| **Qin2023 eq.7–10** 位置→等级控制函数 | f_p = clamp(max(len,width)/‖(x,z)‖,1,8) + Perlin 噪声 | 无 | 阶段 F5（远期 H1 城市级） |
| **Qin2023 §4.4** 城墙凸包+敌楼射线 | 城市边界 | 无 | 远期 H2 |
| **Xie2025** 类型/朝代/分布统计 | 亭43/台55/楼277/阁107；清275/明150 | 无 | 阶段 F3/F4（内容参数，非几何） |
| **Wang2008** 双反射 RMF | 四阶精度旋转最小化标架, 弯折/拐点稳定 | **已存在**：`SplineSweep.cpp:33 ParallelTransport`（投影族二阶, 逐结再正交化, 脊线不扭转） | 需要更高精度时换双反射（同成本四阶） |

## 2. 现状基线（已核实）

astra 评审 6 点逐条核实结论见 `AncientBuilding_QualityPlan.zh-CN.md` §1（含 file:line 证据）。本次补充核实：

- **已具备可复用资产**（不要重复造轮子）：
  - `MeshAccumulator::AddQuadSmooth`（`BuildingBuilder.cpp:340`）——已支持每顶点着色法线+几何法线定绕序，平滑法线基础设施就位；
  - `CornerFlip`（`BuildingBuilder.cpp:389-456`）——翼角起翘用平方衰减，质量已好；
  - `AddSweep`/`AddQuadOriented`、`AddColumn(Sides)`、`AddRoofPanel`、`GetBoardThickness`；
  - `AncientSplineSweep` 节点（位移曲线+方向约束全套参数）；
  - `settings.gd` 已有 `tile_coverage`(=Cr)、`fence_lambda`(=λ)、`corner_rise_scale`、`bays_x/z`、`material_style`(官式/茅草/土木)、`variant_count`+MultiMesh 实例化。
- **核心缺陷锚点**（本执行文档要动的）：
  - `AncientBuildingParameters.h:286-291` `GetRoofHeight()=1.3×Depth/((RafterCourses-1)×0.5)`——结构分段数耦合屋顶高度；
  - `BuildingBuilder.cpp:287-321 BuildRoofProfile`——分段数=Courses，曲线形状与高度已部分解耦（rise ratios 先算后缩放），但分段数未解耦；
  - `BuildingBuilder.cpp:254-283 AddColumn`——平面法线柱面（每面一个法线）；
  - `TileSkin.cpp:346-446 BuildTileSkin`——每垄连续 quad strip，无沿坡向搭接台阶；
  - `AncientBuilding.cpp:153-188 Generate`——单 surface 直出，无 tangent、无 LOD、无构件定位点输出；
  - `AncientBuilding.cpp:81-90 EnsureMaterial`——单 StandardMaterial3D（顶点色 albedo，粗糙度 0.88 固定，金属 0）。

## 3. 执行阶段与任务卡

阶段顺序 = 依赖序：A 是地基（所有阶段共用新参数层），B/C 可在 A 后并行，D 贯穿，E 依赖各生成函数输出定位点，F 纯参数层可随时做，G 依赖 A3 的采样机制。每阶段独立提交、独立验收。

### 阶段 A：参数解耦 + 自适应采样（地基）

**论文依据**：astra 意见 1；Hu&Qin §5.1 eq.20（自适应采样思想）+ 表1 模数体系。

#### A1 三层参数拆分
- **文件**：`Source/AncientBuilding/AncientBuildingParameters.h`（+ .cpp 绑定）
- **改法**：`BuildingSpec`/参数类按三层重组（保持旧字段兼容别名，避免一次全改）：
  - **结构层**：步架数、举架比、檐出、明间/次间面阔、柱高、斗拱层高；
  - **造型层**：现有 rise-ratio 体系、翼角起翘、收山位置、瓦面覆盖 Cr；
  - **网格层（新增）**：曲线误差容限 `profile_error`（默认 0.5%×D）、最大分段长 `max_segment`（默认 1.5×D）、柱截面边数 `column_sides`（默认 16）、瓦垄密度/瓦宽。
- **验收**：编辑器中三层分组显示；改网格层参数不引起任何几何外形变化（截图 diff 为 0）。

#### A2 屋顶高度与分段数解耦
- **文件**：`AncientBuildingParameters.h:286-291`；`BuildingBuilder.cpp:287-321`
- **改法**：`RoofHeight` 改由结构层直接驱动（檐口高、脊高两个绝对参数，或保留"举折表"驱动），`RafterCourses` 只决定结构分段；删掉 `GetRoofHeight` 里对 RafterCourses 的除法。
- **回归风险**：现有场景屋顶高度会变——提供旧公式作为"Legacy"预设，或迁移现有 .tscn 时手动确认。

#### A3 曲率自适应分段
- **文件**：`BuildRoofProfile`（`BuildingBuilder.cpp:287`）+ 新函数 `RefineProfile(Profile, Spec)`
- **算法**：每个步架 rise 段先粗采样，再按弦高误差二分：段内取中点算曲线高差 `h = y_mid - lerp(y0,y1)`，`h > profile_error` 时递归二分，深度上限 `max_subdiv`（默认 4，即每步架最多 16 段）。曲线取值仍走 rise-ratio 体系 → **形状不变，只加密采样**。
- **验收**：RafterCourses=3 与 9 的屋顶侧轮廓一致（截图像素级），仅三角数不同。

#### A4 明间/次间面阔与柱高独立
- **文件**：`BuildingBuilder.cpp:69-79 BayPositions`；`AncientBuildingParameters.h:251-266`
- **改法**：开间划分由均匀 → 明间系数 `central_bay_ratio`（默认 1.2，明间=次间×1.2，等级 F1 联动）；檐柱高从 `11D` 硬编码改为 `column_height_ratio`（默认 11D 不变，等级 F1 联动）。
- **论文依据**：Qin2023 §3.2 开间等级（殿 9 间、官 7 间、公侯门 3 间、庶民≤3 间）。

#### A5 settings 层暴露
- **文件**：`Game/addons/ancient_building/nodes/ancient_building_settings.gd`
- **改法**：新增 `@export_group("Mesh Quality")`：`profile_error`/`max_segment`/`column_sides`；`@export_group("Plan")` 加 `central_bay_ratio`/`column_height_ratio`。
- **验收**：POC 地图里同一个 40 屋村庄切换网格质量档，外观无变化、三角数可调。

### 阶段 B：平滑法线 + 倒角 + 瓦面搭接

**论文依据**：astra 意见 3/4；Hu&Qin §4.2（瓦面分段 subspline 实例化）+ §6.3 负面反馈①。

#### B1 柱面平滑法线
- **文件**：`BuildingBuilder.cpp:254-283 AddColumn`
- **改法**：每侧边顶点输出解析圆柱法线（cos/sin 于该边中点角），不再逐面 flat；`Sides` 由 A1 网格层 `column_sides` 驱动（默认 16，近景可 24）。
- **验收**：侧光下柱面无棱；`AddQuadSmooth` 语义不冲突（柱可直写顶点法线）。

#### B2 梁头/檐口倒角（低优先）
- **文件**：`AddBox`/`AddSweep` 交汇构件（额枋、柱头、阶条石帽）
- **改法**：对暴露棱线用 45° 双顶点法线（shading bevel，几何不动）或小斜切面；先只做柱础与阶条石帽。
- **验收**：特写棱边有高光过渡而非硬黑线。

#### B3 瓦面纵向搭接
- **文件**：`TileSkin.cpp:346-446 BuildTileSkin`
- **改法**：每垄由连续 strip → **按真实瓦长分段**（Hu&Qin §4.2 思路）：沿坡向每 `tile_length`（默认 0.35m，与 `tile_course_width` 同源米制）切一排，排尾沿瓦面法线抬高 `lap_lift`（默认 0.02–0.04m）形成台阶搭接；瓦头（瓦当）在每排末端凸出。搭接台阶复用现有解析法线（筒瓦/板瓦截面）继续沿坡向采样。
- **成本控制**：排数 = 坡长/瓦长，通过 `max_courses`（默认 48）钳制；`tile_course_width` 不再承担"成本旋钮"职责，改由本参数与 LOD 承担（astra 意见 3 后半）。
- **验收**：45° 俯视近景可见每排瓦头台阶；侧光下瓦垄有纵向明暗节奏。

#### B4 瓦面-正脊相交修复
- **文件**：`BuildTileSkin` 终点处理
- **改法**：瓦垄 sweep 终点按脊曲线 y(x) 截断（沿坡向投影求交，等价于 Hu&Qin §3.2.2 的切割平面逻辑），最后一排瓦片在脊下保留完整排。
- **论文依据**：Hu&Qin §6.3 用户反馈①——原作者确认的坑，我们当前代码同坑。
- **验收**：增大瓦宽（tile_course_width=2.0）时正脊处无穿模。

#### B5 米制 UV
- **文件**：`MeshAccumulator` UV 规则 + `TileSkin.cpp`
- **改法**：瓦垄 U=沿垄弧长/瓦长（连续纹理），V=0..1 每垄；柱/梁 V=高度/参照长（默认 1m 重复）；统一规则写入注释。
- **验收**：换 4K 瓦片纹理贴图无拉伸。

### 阶段 C：檐下结构（椽头/飞椽/斗拱剪影/柱础台基）

**论文依据**：astra 意见 2；Hu&Qin §3.1（样条扫掠+位移曲线，本项目已有对应节点）。

#### C1 椽头 + 飞椽
- **文件**：`BuildingBuilder.cpp` 新增 `AddRafters(Mesh, Spec)`；调用点 `BuildBuilding:1893-1957`
- **改法**：沿檐口 subspline 按 `rafter_spacing`（默认=瓦垄宽，即一垄一椽）实例化圆形截面椽（`AddSweep` 圆轮廓 8 边），椽头伸出檐口；飞椽在望板之上再抬一皮、再出挑 0.5×椽径。连檐板已有，确认衔接。
- **成本**：默认只在歇山/庑殿檐下生成；`generate_rafters` 开关（默认 true）。

#### C2 斗拱剪影改善
- **文件**：`BuildingBuilder.cpp:660-717 AddBracketSet`
- **改法**：盒体保留（结构正确），每层斗/拱出跳处加 45° 收分斜面 + 底部替木；斗拱侧缘用双顶点法线软化。
- **验收**：中景逆光下斗拱层有剪影层次，不再是一排盒子。

#### C3 柱础/阶条石造型（复用 AncientSplineSweep）
- **文件**：`AddColumn` 底部；`AddBox` 阶条石帽
- **改法**：柱础用位移曲线 y(x)（鼓形轮廓）绕柱轴回转（现有 `AncientSplineSweep` 的 `ContourPreset`+`DisplacementCurve` 直接复刻到 building 内部）；阶条石帽加一层圆角。

#### C4 踏步/台基按等级
- **文件**：steps 生成段
- **改法**：等级驱动三档：素石踏步（现状）/ 垂带（两侧竖带，复用 AddSweep）/ 垂带+栏杆（复用 fence 生成器）。参数 `steps_grade`。

### 阶段 D：四类材质响应 + 稳定参数化

**论文依据**：astra 意见 4（水墨风低噪声）。

#### D1 切线输出
- **文件**：`AncientBuilding.cpp:173-179 Generate` 数组装配；`MeshAccumulator` 增 `Tangents` 通道
- **改法**：扫掠/瓦面用沿坡向切线解析计算；平面构件用 UV 梯度近似。ARRAY_TANGENT 只在 `use_tangents`（默认 true）时输出。

#### D2 四类材质响应
- **文件**：`AncientBuilding.cpp:81-90 EnsureMaterial`；shader 资源
- **改法**：材质从"单一 StandardMaterial3D 顶点色"升级为着色器输入四通道：
  1. **颜色**：顶点色（现有，保留）；
  2. **粗糙度**：从固定 0.88 改为 per-风格（官式瓦 0.55/木 0.75；茅草 0.9）；
  3. **法线**：瓦面/木纹程序法线贴图（或顶点法线+细节噪声）；
  4. **金属**：瓦面 0.12，木 0。
- 实现顺序：先粗糙度/金属两个标量 uniform（改 `EnsureMaterial` 即可），法线贴图后置。

#### D3 稳定色差 ID
- **文件**：`BuildingBuilder.cpp:99-121`（PieceCounter → 确定性 ID）
- **改法**：MottleColor 种子从递增计数器改为 `Hash(构件类型, 序号)`（柱序号/垄号/排号），跨重生成、跨变体、跨 LOD 稳定。TileSkin 的 CourseTint 已是稳定哈希，作为模板。
- **验收**：同参数两次生成顶点色逐字节一致。

#### D4 低噪声
- **文件**：`AncientBuildingParameters.cpp:92 GetStyleMottle`
- **改法**：色差幅度减半，色差集中在大尺度构件（柱、墙），瓦片/椽不做 per-vertex 色差（水墨风要"稳"）。

### 阶段 E：构件替换接口（程序骨架 + 精品构件）

**论文依据**：astra 意见 5；Hu&Qin §4.1 层次树 + §3.2 实例化。

#### E1 构件定位点输出
- **文件**：`BuildingBuilder.cpp` 各 Add* 函数；`BuildingSpec` 增 `ComponentNodes` 列表
- **改法**：每个程序构件写入 `{Type(枚举), Transform(局部), ParamDict(尺寸/编号)}`（Hu&Qin §4.1 层次树的"节点"即此记录）；`BuildBuilding` 完成后随网格一起返回。
- **注意**：本阶段只做记录与暴露，不做渲染替换。

#### E2 GDScript 接口
- **文件**：`Source/AncientBuilding/AncientBuilding.cpp` 暴露 `get_component_nodes()`（Array of Dictionary）+ `set_component_overrides(Dictionary)`；`ancient_building.gd` 侧用 MultiMesh 按类型批量挂精品构件，按 `override_types`（默认空=纯程序）替换对应程序构件。
- **验收**：精品斗拱替换程序盒体后，其余部分照常生成；关掉 override 回到纯程序。

#### E3 逐类型开关
- **文件**：`settings.gd` 增 `component_overrides`（bitmask：瓦/斗拱/椽/柱/柱础/栏杆）
- **配合**：与 MultiMesh 分块（远期 H）衔接。

### 阶段 F：等级系统与内容参数（论文 2/3 驱动，纯参数层）

**论文依据**：Qin2023 Table 7 + §3.2；Xie2025 统计。

#### F1 等级映射表
- **文件**：`settings.gd` 增 `building_level`（1–9，默认 5）；新 GDScript 常量表（或 C++ 枚举扩展）
- **映射**（Qin2023 Table 7，直接落地）：
  - ≥9：多檐攒尖塔 + 八角身；8：重檐庑殿；7：重檐歇山 + 9 间 + 高阶带栏 + 带栏踏步；
  - 6：单檐庑殿；5：单檐歇山 + 7 间 + 高阶无栏 + 垂带踏步；
  - 4：悬山；3：硬山；2：卷棚 + 3 间；≤1：亭/廊。
- **依赖**：9 屋顶类型在 C++ 枚举已有（`AncientBuildingParameters.h:38-58`），PCG 层从 3 种放开为 9 种（astra 意见 6 后半）；重檐=两层屋面叠加（`BuildRoofProfile` 跑两遍，规模不同）。
- **验收**：`building_level` 从 1 拖到 9，屋顶/开间/台基/踏步全链路联动。

#### F2 开间等级规则
- 殿 9 间 5 进 / 三至五品官 7 间 / 公侯门 3 间 / 庶民正房 ≤3 间（Qin2023 §3.2）→ `bays_x/bays_z` 由等级表驱动，A4 的明间系数联动（等级越高明间越宽）。

#### F3 朝代风格预设（明/清）
- **文件**：`settings.gd` 增 `dynasty_style`（明/清/自定义）
- **数据依据**：Xie2025——现存 482 座中清 275（57%）、明 150（31%）。预设两组：清式（举折较陡、斗拱小密、琉璃瓦色）、明式（举折缓、斗拱硕大、青灰瓦色）。
- **验收**：同尺寸建筑切换预设，侧轮廓与瓦色符合朝代特征。

#### F4 类型权重（城市 PCG 默认）
- 亭 9% / 台 11% / 楼 57% / 阁 22%（Xie2025 482 座占比），作为村庄/城市生成器的类型权重默认值（几何生成需 H1 支持多类型）。

#### F5 位置→等级控制函数
- **文件**：PCG spawner 层（GDScript）
- **公式**（Qin2023 eq.7–10）：`f_p = clamp(max(len,width)/‖(x,z)‖, 1, 8)`（smooth 版）或 step 版（旋转 45°）；叠加低频 Perlin `N_L`（中心偶现低等级）与高频 `N_H⁹/10`（随机点缀亭/塔）。
- **验收**：城市中央高等级、边缘低等级，噪声符合图 13 效果。

### 阶段 G：LOD 三档（Hu&Qin §5 落地）

**论文依据**：astra 意见 6 前半；Hu&Qin §5 全部 + 表 7/8 数据。

#### G1 LOD1 距离自适应采样
- **文件**：`AncientBuilding.cpp Generate` 增 `LodDistance` 入参；各 Build 函数的段数统一走采样率
- **公式**：`s = ⌈d×i/t⌉`（eq.20），i=基础段数，t=远距基准（默认 240m 起调），s≥1 保证不增面（论文对 Huang 方法的修正点）。
- **应用**：柱截面边数、屋顶 profile 段数、瓦垄排数、斗拱出跳细节分层切换。
- **数据目标**（论文表 7 曲线）：近景全量 → ξ 处约 1/35 → ξmax 处 billboard。

#### G2 LOD2 构件代理 quad
- d>ξ（默认 200m）时斗拱/椽/栏杆等细构件整组替换为投影 quad（128² 贴图，离线烘焙进 atlas）；quad 朝向取构件局部法线（论文：无需二次更新）。

#### G3 LOD3 建筑 billboard
- d>ξmax（默认 300m）整建筑单 billboard（512² 烘焙）；双条件更新：视角夹角 β>10° 或 NDC 位移 s>0.1（eq./§5.3 默认值）。Godot 侧实现：`GeometryInstance3D` LOD + 自定义 billboard 节点，或直接复用 MultiMesh + `visibility_range`。

#### G4 生成缓存
- **文件**：`AncientBuilding.cpp` 增参数哈希缓存
- **改法**：`BakeMesh()` 按 {参数哈希, LOD 档} 缓存 ArrayMesh（论文负面反馈②：参数化加载慢的解法）；PCG 层按 variant 预生成。
- **验收**：拖 40 屋村庄场景加载 <3s（项目性能目标），编辑器改参数仅重算受影响档。

### 远期（不在本轮，接口预留）

- **H1 城市级 SE L-System**（Qin2023 §4.2）：GRID 网格吸附 eq.1、对称因子 SYM、S 函数剪枝；与 F5 等级函数、F4 类型权重、E2 构件接口对齐；
- **H2 城墙/敌楼**（§4.4）：特征点凸包 + 外推 dwall/2 + 中心射线求交；
- **H3 树分布**（eq.3–4）：密度反比建筑密度，r=20；
- **H4 框架提取**（Hu&Qin §4.3 voxel+DBSCAN）：面向编辑工具的逆向建模，优先级最低。

## 4. 基准与验收

沿用 `AncientBuilding_QualityPlan.zh-CN.md` §4 的 3 机位 × {三角形数, 生成时间, GPU frame time} 基准表，补充：

- 每阶段提交时对 POC 场景截图对比（同参数、同光照）；
- 回归矩阵：硬山/歇山/庑殿/攒尖/多边形 × 等级 3/5/7 × 网格质量低/中/高；
- 瓦面搭接与瓦-脊相交修复用大瓦宽（tile_course_width=2.0）专项验证；
- 最终目标：歇山质量样板在 3 机位下 60 FPS（近景机位允许 45 FPS 缓冲），生成时间 <1s/座。

## 5. astra 评审 6 点闭环

| astra 意见 | 本执行文档 | 论文支撑 |
|---|---|---|
| 1 参数解耦/自适应采样/独立柱高明间 | 阶段 A | Hu&Qin eq.20、表1 |
| 2 檐下结构优先 | 阶段 C | Hu&Qin §3.1（已部分移植） |
| 3 瓦面搭接+真实尺寸 | 阶段 B3/B4 | Hu&Qin §4.2、§6.3 反馈① |
| 4 四类材质/米制UV/切线/平滑柱面/稳定ID/低噪声 | 阶段 B1/B5/D | astra 原审 + 现状核实 |
| 5 程序骨架+可替换构件 | 阶段 E | Hu&Qin §4.1 层次树 |
| 6 LOD三档+9屋顶+分块 | 阶段 G + F1 | Hu&Qin §5 表7/8、Qin2023 Table7 |

## 6. 参考资料

- Hu & Qin 2020：`Reference/hu2020.pdf` → md `Reference/MD/hu2020/hu2020/hu2020.md`（本移植母本）
- Qin et al. 2023 古城布局：`Reference/MD/city/Procedural-modeling-and-layout-method-for-a-generic-ancient-Chinese-city/Procedural-modeling-and-layout-method-for-a-generic-ancient-Chinese-city.md`
- Xie et al. 2025（Buildings 15:1652，文旅统计）：`Reference/MD/buildings-15-01652/buildings-15-01652/buildings-15-01652.md`
- PCG 补充文献（2026-09-25 增补，城镇级 PCG 依据）：
  - dong2021 屋顶等级：`Reference/MD/pcg_supplement/dong2021_roof_classification/...`
  - mueller2006 CGA shape grammar：`Reference/MD/pcg_supplement/mueller2006_cga/...`
  - garland1998 QEM 简化（阶段 G1 LOD 依据）：`Reference/MD/pcg_supplement/garland1998_attribute_qem/...`
  - wang2008 双反射 RMF：`Reference/MD/pcg_supplement/wang2008_rmf/wang2008_rmf/wang2008_rmf.md`
- astra 评审规划：`Docs/AncientBuilding_QualityPlan.zh-CN.md`
- 现行规格：`Docs/AncientBuilding_Spec.md`

> `Reference/` 在 .gitignore 中；如需 agents 直接读上述 md，先移入 `Docs/`。

## 7. 城镇级 PCG 扩展（2026-09-25 已实施）

用 AncientBuilding 搭中大型古代城镇的落地层：`Game/addons/ancient_town/` + `Game/Map/Map_AncientTown.*`。设计依据 dong2021（屋顶等级体系）与 mueller2006（CGA 语法 → 终端 → MultiMesh 映射）。

### 7.1 能力扩展（两层）

1. **town_lots 布局节点**（`addons/ancient_town/nodes/town_lots.gd`）：0 入 5 出（Buildings/Roads/Walls/Props/Trees），`settlement_type` 选 聚落/村镇/市集/城市 四种布局算法：
   - **聚落 hamlet**：蜿蜒小路随机游走（分段随机转角 ≈ 仿 garland 讨论的有机生长），路两侧交替排屋 + 祠堂地标 + 水井 + 外圈树；
   - **村镇 village**：主街 + 2-3 侧巷，东端视景终点庙宇（庑殿 L4），西入口牌坊，中心广场水井+摊位；
   - **市集 market**：3×3 地块网格（CGA 式 Split），中心开敞广场四象限摊位行，临街店铺，双牌坊；
   - **城市 city**：4×4 里坊制（周礼考工记 + Qin2023 网格）：中轴朱雀大街 16m 宽自南门直抵宫城，宫城居中 2×2 坊（正殿庑殿 L5 面阔 20-24 + 配殿 + 4 角亭 + 内墙），宫城区内御道两侧官署带 + 宫城后苑亭榭树丛，环城路 + 城墙四门四门楼（歇山/庑殿 L5）+ 四角攒尖亭，东市/西市摊群，城外树带。
2. **ancient_building 参数覆盖流**（`addons/ancient_building/nodes/ancient_building.gd`）：输入点携带 `ab_width/ab_depth/ab_roof_type/ab_bays_x/ab_bays_z/ab_material_style/ab_rafter_courses/ab_tile_coverage/ab_tile_course_width/ab_corner_rise_scale/ab_fence/ab_walls/ab_steps/ab_fence_lambda` 任意子集 → 逐点覆盖；**组合量化烘焙**：combo key = 量化后的 width/depth(quantum 0.5→4.0 逐级×2 直到 ≤variant_count) + roof + material，每组合只烘焙一次，同组合点共享网格 → 一个 MultiMesh 一条 draw call（对应 mueller2006 终端符号→MultiMesh 映射）。

### 7.2 等级体系（dong2021 落地）

`town_lots._params_for_level(level)`：L1 民居 5.5-8m 硬山/悬山/卷棚 1-2 开间（rural 时茅草/土木）；L2 8-10.5 悬山 3 开间；L3 9-13 歇山/悬山 3-5 开间；L4 11-17 歇山/庑殿 5 开间；L5 官署殿宇 13-21 庑殿 5-7 开间。城市里坊建筑等级 = 1+round(3×中心性)，离宫城越远等级越低（≈ Qin2023 f_p 等级函数）。9 种屋顶 C++ 枚举全部经 GDScript 可用（0 硬山…8 盔顶），亭=攒尖/圆/盔顶，门楼=歇山/庑殿。

### 7.3 实测数据（2026-09-26 修复后，seed 42）

| 预设 | 建筑组合烘焙数 | MMI 总数 | 实例总数 |
|---|---|---|---|
| 聚落 | ~11 | 11 | 38 |
| 村镇 | ~19 | 19 | 51 |
| 市集 | ~22 | 22 | 71 |
| 城市 | ≤24 | 26 | 553 |
| 四城同图 | — | 81 | 714 |

组合量化把城市从「每栋一烘焙」压到 ≤24 个网格，draw call 与网格内存不再随建筑数增长。（553/714 含宫城区官署带与后苑的补齐。）

### 7.4 运行方式

`Map_AncientTown.tscn` + env 变量：`TOWN_PRESET=hamlet|village|market|city|all`、`TOWN_SEED=n`、`SHOTS=1`（出图后退出，PNG 到 `Reference/Shots/AncientTown/`）。水墨材质链路复用 InkPainting 三件套（ink_surface→ink_outline_0→ink_outline_1），建筑顶点色经 shader `v_vertex_color` 保留，每栋 hsv 微调色相破「克隆城」。相机 rig 注意：`CameraRigController`（C++）的 `current_zoom` 未绑定为属性，程序化改焦距需直接设 `SpringArm.spring_length`。

### 7.5 后续衔接

- 阶段 G1 LOD：garland1998 QEM 边折叠应用到建筑网格 3 档简化；
- 远期 H1-H3（Qin2023 城市级）：town_lots 的里坊/等级函数是其 GDScript 原型；C++ 化时照 §3 阶段 F 参数层对齐。

### 7.6 几何修复记录（2026-09-26，17 项代码评审闭环）

代码评审发现 17 项问题，其中 2 HIGH / 8 MED，主要修复：

1. **道路朝向约定 bug（HIGH）**：道路条带是 `size=(len,0.1,width)` 的单位盒、长度沿**局部 X**，而 `_yaw_to(dir)`（局部 +Z 对齐）只适用于建筑。聚落小路与村镇侧巷全部 90° 错位横穿房屋。新增 `_road_yaw(dir) = atan2(-dir.z, dir.x)`，道路一律走该约定；
2. **村镇主街穿过庙宇（HIGH）**：主街西起 -L-1 原止于 L-7 = 庙中心。改为止于庙西面宽半减 2m；沿街房屋循环上界 L-14 → L-19 留出庙前院；
3. 村镇广场水井/摊位从路面移到路旁（路 z∈[-3,3]）；市集水井移出十字路口（o+(6,6)）；聚落起点水井移离小路；
4. 市集临街铺面朝向重写：面向广场一侧朝向广场（轴向），对角块最近角对角朝向广场，其余面朝较近外侧街道——同时消除 45° 屋檐戳出街区边缘的问题；
5. 城墙转角 3m 缺口：墙段长度 `(Ex-gate)/2` → `(2R-gate)/2`，四角亭不再悬空；
6. 宫城东西门位从 z∈[5,21]（与横街错开 8m 的「死门」）改为居中 z∈[-8,8]，对齐 W_MAIN 横街；
7. 村镇侧巷首栋 t 4→18（避开主街房屋纵深带 ~14m）+ 巷间距 12 候选取最大间隔；聚落支路首栋 t 3-8→8-11 且放主路背侧；
8. 同侧房屋间距抖动下限从 -1m 提到 +0.5m（防贴脸）；城市里坊退距 8→10m（台阶/栅栏/尺寸抖动余量）；
9. 十字路口两条路 slab 高度错开 2cm（按朝向 y=0.05/0.07）防共面闪烁；
10. `ancient_building.gd` 覆盖路径加固：要求 4 个身份流（width/depth/roof/material）齐备才走覆盖（防部分流 null 算术），缺失可选流回落 settings 默认，烘焙失败（mesh_by_key≠keys）显式 setError。

修复后重出全部 16 张验证截图（`Reference/Shots/AncientTown/`），视觉验收闭环见各轮 vision 评审。

11. **村镇主街中心重复加原点（自测发现）**：`o.x + (o.x - L - 1 + road_end) * 0.5` 在非零 origin 时把 o.x 算了两次 → 主街跑到村镇以东 330m（"all" 图包围盒被拉宽 213m）。修为 `(o.x - L - 1 + road_end) * 0.5`、长度 `road_end - o.x + L + 1`。教训：局部/绝对坐标混写时先统一为绝对坐标再算中点；
12. **宫城区填补**：御道两侧 4 行官署院落（L4 歇山，面向御道）+ 宫城后苑两亭榭与树丛，消除 2×2 坊中心大片空白。

### 7.7 视觉评审修复记录（2026-09-26，两轮 vision 评审 → 相机/光照闭环）

第 1 轮评审通过 5 图（lane/temple/palace/avenue/gate），第 2 轮复测发现 5 张 overview 全部 FAIL（主体被压在画面底部 1/3），根因与修复：

13. **自动取景预偏移公式错误（根因）**：相机由 SpringArm 看向**焦点本身**（画面中心=焦点），旧公式 `focus = center - (sinθ,0,cosθ)·cos(pitch)·zoom` 把焦点甩到主体以南 ~0.5·zoom，相机落点虽然回到主体正上方，但画面中心对准了主体外 359m 的空地 → 城镇只剩底部一条。修正：`focus = center + (sinθ,0,cosθ)·lead`，其中 `lead = center.y/tan(pitch)` 只补偿「焦点悬空高度导致视线落地点北偏」的几米量；
14. **框幅按近侧计算（透视非对称）**：俯拍时画面近侧覆盖的地面长度只有 `H·(cot(p)−cot(p+fov/2))`（p=60° 时 ≈0.332·zoom），远侧是 0.497·zoom——旧 `zoom=1.25·S` 会把主体靠近相机的一半切在下边缘外。改为 `zoom = S/2 / min(近侧,远侧) × 1.05`（p=60° 时 ≈1.58·S）。all_city 固定机位同理从 zoom 420 提到 895（否则南城墙整段出画）；
15. **远景雾/景深联动**：旧雾 300→1500、DOF 全糊起点 310m 恒定，鸟瞰（相机 350–1150m 高）全部泡在雾里（all_city 暗部像素 0%）。出图循环里按臂长缩放：`fog_begin=max(300, 1.4·zoom)`、`fog_end=max(1500, 4·zoom)`、`dof_far=max(220, 1.5·zoom)`；近景保持默认保留空气感。相机 far 1400→6000（四城同图对角 >1400m 会裁地面）；
16. **市集摊位棚顶近黑**：棚顶 quad 绕序反了（法线朝下），墨水 shader 是光照驱动的（`NdotL`），俯拍时棚顶 0 受光。翻转绕序 + 提亮 `AWNING_COL 0.68→0.76`、`TIMBER_DARK 0.24→0.30`；
17. **机位调整**：市集广场 (0,3,0)/-30°/38 → (0,2,0)/-35°/46（棚顶黑块少、广场完整）；摊位 (0,1.5,10)/-24°/16 → -30°/20（棚顶不再压满上半画面）；村镇街 焦点 30→40（填右侧空白区）；城市街景 -90° 朝东死胡同 → +90° 从东往西看，pitch -14→-16、zoom 36→40，视角沿大街穿过宫城门直抵西城门；城墙角楼 zoom 55→62 收紧。
18. `shot_output.gd` 增加 `SHOT_SUBDIR` env 重定向，供「旧图仍被评审 agent 读取时」并行出新批次。

### 7.8 视觉评审 round-4 闭环（2026-09-26，8 PASS / 8 MINOR / 0 FAIL）

round-4 对修复后的 16 图复测：5 张 overview 全部居中且四边无裁切（edgeClip 0.00%），雾/DOF 修复生效（city_overview edge30 0%→7.98%、dark 0%→1.5%），城市街景暗楔消失（dark% 0）。剩余问题与修复：

19. **南/西朝向面近黑（根因）**：墨水 shader 光照驱动（`NdotL`），单盏东北太阳下所有朝南/朝西的面 NdotL=0 → 渲染为近黑（实测 ~44）。市集摊位暗带、market_stalls 底部黑带、city_street 城门塔楼黑影同源。新增西南低角度**补光**（无阴影, rotation (-25,130,0), energy 0.32）——把暗面抬到可读的深墨 ~80-95，同时太阳 1.1→1.15、环境光 0.9→0.75 拉开墨色对比；
20. 聚落/村镇 overview 太淡（content 2-5%、dark 0）：yaw 0→180 从南侧拍背光面，墨色更足；
21. market_stalls 底缘黑带：机位 (0,11.5,-7.3)/-30° → (0,14.1,-8)/-35°、zoom 22，近处摊位移出画面底缘；
22. village_street 偏左（centroid x 0.33）：焦点 40→35；
23. all_city 南城墙贴底（edgeClip B 7.14%）：zoom 895→950 留出底缘；
24. city_corner 顶部 25% 空纸：pitch -30→-35。
