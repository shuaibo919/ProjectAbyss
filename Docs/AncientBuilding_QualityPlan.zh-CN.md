# AncientBuilding 质量升级计划（第一轮：歇山质量样板）

> 依据 astra 评审意见（2026-09-25），逐条核实后制定。**详细执行版见 `AncientBuilding_ExecutionPlan.zh-CN.md`**（论文原文已到位并映射到任务卡；本文档保留评审核实记录与基准表）。
> 实施原则：**解耦先行、分阶段提交、每阶段可独立验收**；所有 C++ 改动需重编 GDExtension 后验证。

---

## 1. 评审核实记录

| # | 意见 | 核实结论 | 证据 | 备注（已存在项） |
|---|------|----------|------|------------------|
| 1 | `rafter_courses` 同时控制分段数与屋顶高度 | **属实**，最严重 | `AncientBuildingParameters.h:286-291`：`RoofHeight = 1.3×Depth/((Courses-1)×0.5)`，5→9 高度精确减半；`BuildingBuilder.cpp:287-321` 分段数 = Courses | 曲线形状已与高度解耦（rise ratios 先算后整体缩放）；望板/瓦面/脊已共享同一 profile 采样 |
| 1 | 柱顶高度固定为面阔 0.8 倍，明间/次间等宽 | **属实** | `AncientBuildingParameters.h:251-266`：`D = Width×0.8/11`，柱顶 = 11D；`BuildingBuilder.cpp:69-79` BayPositions 均匀划分 | — |
| 2 | 檐下椽子未实现，斗拱为盒体堆叠 | **属实** | `Docs/AncientBuilding_Spec.md:658-659` 自述未实现；`BuildingBuilder.cpp:660-717` "Boxes only" | 连檐板、望板厚度、板门进深、阶条石帽已存在 |
| 3 | 瓦面无纵向搭接 | **属实** | `TileSkin.cpp:346-446`：每垄是连续 quad strip，无沿坡向台阶 | 筒瓦/板瓦截面+解析法线、瓦当、滴水、每垄色差已实现且质量好 |
| 3 | PCG 成本旋钮 = 瓦宽 | **属实** | `ancient_building_settings.gd:42` "Larger tiles mean fewer 瓦垄 sweeps" | 歇山/庑殿裙部翼角切瓦已有（`BuildingBuilder.cpp:1244-1259`） |
| 4 | 单顶点色材质、粗糙度 0.88、UV 重复 0-1、无切线、柱面平面法线、色差不稳定 | **全部属实** | `AncientBuilding.cpp:81-90` 材质；`BuildingBuilder.cpp:137-143` UV 每三角形 0-1；`AncientBuilding.cpp:173-179` 无 ARRAY_TANGENT；`BuildingBuilder.cpp:254-283` AddColumn→AddQuad 平面法线；`BuildingBuilder.cpp:99-121` PieceCounter 递增计数 | TileSkin 的 CourseTint 用垄号做哈希，是稳定的 |
| 5 | 无构件替换接口 | **属实** | 单入口 `BuildBuilding` 直出单网格（`BuildingBuilder.cpp:1893-1957`），无构件定位点输出 | — |
| 6 | PCG 只开 3 种屋顶、无 LOD | **属实** | `ancient_building_settings.gd:28` 枚举 3 种；C++ 枚举 9 种（`AncientBuildingParameters.h:38-58`）；`AncientBuilding.cpp:153-188` 单 surface 直出 | 变体一次生成、MultiMesh 实例化已有（`ancient_building.gd:60-77`） |

## 2. 前置条件

1. **GDExtension 重编流程跑通**（OutlinePass 已合并、`Game/bin` DLL 已过期；uv+scons 构建，见本仓库构建记忆）
2. **PCG Ancient 样例**（基准场景/参考图）——待用户提供
3. **原文**（评审原文/论文原文）——待用户提供，填入 §7 参考资料

## 3. 第一轮阶段划分（每阶段独立提交）

### 阶段 A：参数解耦 + 自适应采样（地基）
- 拆三层参数：
  - **建筑结构**：步架数、举架比例、屋脊高度、出檐长度（保留 `RafterCourses` 语义，改名 `RafterCourses`→结构层）
  - **造型控制**：屋面曲线、翼角起翘、收山位置（沿用现有 rise-ratio 体系）
  - **网格质量**：新增曲线误差容限、最大分段长度、截面细分（现有分段数全部改用该层驱动）
- `GetRoofHeight()` 与 Courses 彻底解绑：高度由结构层参数直接给出
- 屋面剖面按曲率自适应采样：平缓区少分，翼角/卷棚顶/轮廓转折处多分（`BuildRoofProfile` 输出从"步架节"改为"采样节"，步架节保留为结构语义）
- 新增独立柱高、明间/次间宽度；预留"大殿/民居/亭子"预设（比例体系）
- **涉及**：`AncientBuildingParameters.h/.cpp`、`BuildingBuilder.cpp`（BuildRoofProfile 及所有消费方）、`ancient_building_settings.gd`
- **验收**：改网格精度参数不改变建筑轮廓（对比三机位截图）；旧参数组合可换算迁移

### 阶段 B：平滑法线与局部倒角
- 柱面平滑径向法线（`AddColumn` 改为按径向解析法线 + 可选边缘倒角）
- 梁、柱础、台阶可见边小倒角；新增柱础构件（现无）
- 色差随机数改用稳定构件 ID（替换 `PieceCounter` 哈希链）
- **涉及**：`BuildingBuilder.cpp`（MeshAccumulator::AddColumn/AddBox 路径）、`AncientBuildingParameters.h`
- **验收**：柱面受光连续无棱角折线；细分参数变化后颜色分布不变

### 阶段 C：檐下结构（地面视角高收益）
- 按优先级：**椽头、飞椽、连檐层次**（沿檐口按间距实例化，与现有连檐板/瓦当/滴水衔接）→ **斗拱剪影**（拱臂下缘曲线、区分柱头与转角组合，替换纯盒体）→ 台基收边细化
- **涉及**：`BuildingBuilder.cpp` 新增 BuildEaveStructure / 改 AddBracketSet；`AncientBuildingParameters.h` 新增椽径/椽距/飞椽起翘参数
- **验收**：檐下仰视可见连续结构节奏；三角形数增量记录在案

### 阶段 D：四类材质响应 + 数据升级
- 四类材质响应（同一 mesh 内按部位分流）：木构（顺纹理方向 UV+程序化笔触）、瓦面（瓦片级色差、独立粗糙度、少量积灰）、粉墙（大尺度明暗晕染、墙脚潮痕）、石台基（块面差异、接缝、边缘磨损）
- 底层数据：统一米制 UV 密度 + 构件方向；生成切线（ARRAY_TANGENT）；(阶段 B 的平滑法线已覆盖)
- 色彩控制保持"大块明暗 + 清楚轮廓"，避免高频噪声
- **涉及**：`AncientBuilding.cpp`（材质/切线/UV）、`MeshAccumulator`（UV 策略）、`AncientBuildingParameters.h`（材质细分参数）
- **验收**：三机位下木/石/瓦/墙读得开；帧时间对比记录

### 阶段 E：近景瓦片搭接（纵向）
- 沿坡面按弧长排列瓦片：新增瓦长、搭接长度、瓦唇厚度参数
- 近景真实搭接台阶；中景连续瓦面 + 法线/颜色变化（为 LOD 档位预留）
- 檐口第一排保留实体厚度，瓦当/滴水轮廓优先保留
- **涉及**：`TileSkin.cpp`（每垄沿弧长重采样 + 台阶）、`BuildingBuilder.cpp`（列生成）、参数层
- **验收**：檐下平视可见逐片搭接；瓦片真实尺寸不变（**禁用增大瓦宽降成本**——该旋钮转由 LOD 档位承担）

## 4. 基准测试方法

- **场景**：用户提供的 PCG Ancient 样例；固定光照（无昼夜/天气变化）
- **三机位**：
  1. 地面檐下平视（看椽头/瓦当/滴水/台基）
  2. 地面仰视（看斗拱剪影/翼角）
  3. 屋顶俯视（看瓦垄节奏/脊）
- **指标**：三角形数、生成耗时（ms）、GPU 帧时间（ms）；每阶段前后各测一轮，记录到下表

| 阶段 | 机位 | 三角形数 | 生成耗时 | GPU 帧时间 | 截图对比 |
|------|------|----------|----------|------------|----------|
| 基线 | 檐下/仰视/俯视 | — | — | — | — |
| A | 三机位 | | | | |
| … | | | | | |

## 5. 后续轮次（本轮不做）

- **LOD 三档**（近景搭接/中景连续瓦面/远景轮廓色块）——依赖阶段 E 的瓦片分级；Godot ArrayMesh LOD 与独立网格档位方案二选一，PCG 侧同步
- **PCG 开放 9 种屋顶** + 攒尖分区收垄
- **MultiMesh 空间分块**
- **构件替换接口**（#5）：参数生成柱网与屋面 → 输出构件定位点 → 装配程序化或美术构件 → 烘焙变体；优先开放斗拱、瓦当、脊端装饰、门窗四类

## 6. 待办输入

- [ ] PCG Ancient 样例（用户提供）
- [ ] 原文（用户提供；填入 §7）
- [ ] 优先级确认（阶段顺序 A→E 是否调整）

## 7. 参考资料

- astra 评审意见原文（待附）
- Hu & Qin 2020 论文：`Reference/hu2020.pdf`（原文）→ 已转换 `Reference/MD/hu2020/hu2020/hu2020.md`
- MDPI Buildings 2020 古建建模综述：`Reference/buildings-15-01652.pdf` → 已转换 `Reference/MD/buildings-15-01652/buildings-15-01652/buildings-15-01652.md`
- 古城市程序化布局论文：`Reference/Procedural-modeling-and-layout-method-for-a-generic-ancient-Chinese-city.pdf` → `Reference/MD/city/...md`
- `Docs/AncientBuilding_Spec.md`（现行规格，含已记录缺口）

> 注：`Reference/` 在 .gitignore 中，上述 md 为本地转换产物；如需 agents/astra 读取或入库，需先移到 `Docs/` 下（待用户确认）。
