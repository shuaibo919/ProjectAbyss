# SlowTree 移植说明

本目录是从 [SlowTree](https://github.com/Puluomiyuhun/SlowTree)（SpeedTree 风格节点图植被工具，
MIT 许可）拷贝改造而来的生成核心。项目决策：**只取核心，不追踪上游更新**；数学库由 glm
改写为 Godot 数学库（godot::Vector3/Vector2 等），**效果近似即可，不做位级对拍**。

## 来源

- 仓库: https://github.com/Puluomiyuhun/SlowTree.git
- 锚点 commit: `10e6c66a1dbbb1cc6af5a6f787a73b55be7a6838`
- 本地镜像: `Reference/SlowTree/`（完整上游源码保留在仓库外目录，仅作参考）

## 文件对应与改动

| 本目录文件 | 上游文件 | 改动 |
|---|---|---|
| `SlowTreeTypes.h` | `src/graph/NodeTypes.h` | glm 类型 → godot（Vector3/Vector2/Quaternion/Vector4/Vector4i） |
| `SlowTreeMeshData.h` | `src/renderer/Renderer.h:15-114` | 抽取 MeshBatch/TreeMeshData/LightingParams/WindParams；glm → godot |
| `CylinderSegment.{h,cpp}` | `src/generator/CylinderSegment.{h,cpp}` | glm → godot |
| `NodeGraph.{h,cpp}` | `src/graph/NodeGraph.{h,cpp}` | 删 `drawProperties()`(ImGui)；`rootNode()` 按 id 取最小（上游取 unordered_map 首命中，不确定）；`buildDefaultTemplate()` → `VtreeIO::loadDefaultTemplate`；Vector2 → godot |
| `Nodes.{h,cpp}` | `src/graph/Nodes.{h,cpp}` | 剥离 `drawProperties()` 实现（ImGui 面板、Win32 文件对话框） |
| `TreeGenerator.{h,cpp}` | `src/generator/TreeGenerator.{h,cpp}` | ① 删 4 个应用导出入口 `generateSubtree/generateSpecimen/generateChain/measureSpecimenParent`（标本/祖先链/测量成员保留恒为初始值，各 build 内分支逐字保留）；② `generate()` 根遍历按 id 排序（上游按 unordered_map 迭代序，不确定；单根 Trunk 工程行为一致）；③ `afterAppend()` 空实现（拾取/高亮是应用视口功能，不影响 batches）；④ Custom/ImportTrunk/ImportLeaf/Scatter 四 builder 及分派置 `#ifdef SLOWTREE_FULL_NODES` 门后；⑤ glm → godot |
| `VtreeIO.{h,cpp}` | `src/io/ProjectIO.{h,cpp}` 解析侧 | 仅 `load/loadDefaultTemplate` + KV 解析；写入侧/OBJ/FBX/USD 导出不移植（转换器在 Python 侧）；glm → godot |

## 与上游的有意差异

1. **数学库**：glm → Godot 数学库，浮点结果与上游可能存在 ULP 级差异（项目接受"效果差不多就行"）。
2. **多根 Trunk 工程的根处理顺序**：按 id 升序（上游依赖 unordered_map 桶序）。单株（所有预设）行为相同。
3. **拾取/高亮不生成**：`TreeMeshData::pickTris/hlVerts/hlIdx` 恒空；`batches` 是唯一比对对象。
4. **v1 不支持 Custom(Lua)/ImportTrunk/ImportLeaf/Scatter**：`SLOWTREE_FULL_NODES` 未定义时这些 builder 不编译；含这些节点的 .vtree 由 `SlowTreeGenerator` 校验层报清晰错误。
5. **材质贴图路径**：.vtree 内贴图路径是应用机器绝对路径，Godot 侧按 res:// 约定 + basename 搜索兜底，缺图降级纯色（见 SlowTreeMaterials）。
6. **Frond 端点浮点噪声**：上游 `buildFrond` 的 `halfWidthAt` 在 t=1.0 处 `pow(sin(π·t), profilePow)` 因 π 浮点误差得 `pow(极小负数, 非整数)=NaN`（widthTip=0 时末行整行 NaN）。本移植将 sin 输出 clamp ≥0，只消除端点噪声、不改变曲线形状。
7. **预设模板是本项目自己写的，不是上游资产**（2026-08-27 重调）。`SlowTreePresets.cpp` 里
   五个物种模板最初只为覆盖生成路径，每个只有 *一层* Branch，而上游默认模板有三层。叶数逐层
   相乘，所以少一层少一个数量级：银杏 525 片 / 6k 面 vs 默认模板 26880 片 / 356k 面。已按默认
   模板的层级结构重调。**改模板时先数 Branch 层数，再动参数。**
8. **无贴图下 LeafCluster 与 Frond 的观感差距很大**：`LeafCluster` 的叶卡是四边形，缺 alpha
   贴图时就是不透明矩形；`Frond` 是沿脊线的连续叶带，自带轮廓收尖，无贴图也读作有机叶形。
   针叶树(松/水杉)改用 `Spine→Frond` 叶簇解决。
9. **`useCutout` 已启用**（2026-08-27）：`SlowTreeGenerator::FillLeafCutouts` 用 TreeGen 的
   `TreeLeafOutline` 给每个 LeafCluster 填归一化剪影，CPU/GPU 两条路径都填（否则对拍挂）。
   **只做 LeafCluster，不动 Frond** —— Frond 自带宽度曲线与 serrate 裂片，加 cutout 会覆盖掉。
   代价：一片叶 7 三角 vs 四边形 2 三角。上游 Mesh Cutout 的本意是省透明像素 overdraw，本项目
   无 alpha 贴图，所以省不到填充率，纯属形状开销 —— 预设叶数已相应下调。

## 自检

### 2026-09-29：本项目的实时叶簇路径

`SlowTreeFoliage.*` 是本项目新增代码：物种枝架修正、垂柳末级枝弧线、十字叶簇、程序化图集及 Alpha Scissor 材质。`TreeGenerator` 保留原几何路径，并新增叶簇描述输出、细管与枝领省略、枝段预算及取消检查。合并上游时应保留这些入口；不要将图集材质覆盖成不透明单材质。

`SlowTreeGenerator` 现在直接填 Packed 数组，并将后台准备与主线程提交分离。`ProceduralTree` 自动预览使用 CPU 工作线程，显式生成仍可使用 GPU。数值解析使用 `strtof/strtol`，避免在关闭 C++ 异常展开的构建中依赖 `stof/stoi` 的异常处理。

详细边界和验证方式见 [TreeGen_Foliage_Optimization.md](../../../ProjectAbyssWiki/documentation/systems/TreeGen/TreeGen_Foliage_Optimization.md)。

`SlowTreeSelfTest` 为**结构自检**（网格非空 / 无 NaN / AABB 合理 / 顶点预算 / 确定性），
无位级 golden。若未来需要与上游对拍，恢复 `Reference/SlowTree` 并参照旧协议（dumpMesh + SHA256）。

## 上游 bug 修正（本移植已修）

1. **`emitGpuLeafCard` 的 cutout 索引预留少乘 3**（`TreeGenerator.cpp`）。`idxCount` 的单位是
   索引（四边形分支给 `6`），但 `CountCutoutTris` 返回三角形数。少乘 3 → GPU 只预留实际所需
   索引的三分之一 → 之后每片叶索引区错位 → GPU/CPU 对拍报「batch N 索引与 CPU 路径位级不一致」。
   **这条分支在上游和本移植里都从未被执行过**：项目里没有任何东西设过 `useCutout`，直到
   2026-08-27 加了叶片轮廓填充。属于随功能启用才暴露的潜伏 bug。

2. **叶顶点布局读错，导致所有叶片被压暗偏色**（原 `AddBatchSurface`）。实际布局是
   `pos(0-2) normal(3-5) uv(6-7) **albedo(8-10) wind(11-12)** anchor(13-15)`
   （见 `TreeGenerator` 的 `emitVert` 与 `leaf_card.comp` 的 stride 头注），
   而装配代码按 `wind(8-9) colour(10-12)` 读，于是每片叶的顶点色成了
   `(col.b, windW, leafPhase)`。`LeafCluster` 的 `windW` 恒为 `1.0`，顶点色又以**乘法**叠在
   材质 albedo 上 —— 所以银杏 `(0.55,0.62,0.2)` 实际渲染成 `(0.11,0.62,0.2·phase)`，
   一直是偏暗的深绿。之前记录的"针叶颜色偏灰绿"就是它。
   **2026-08-27 重写 `ConvertToGodotMesh` 时一并修正**（改单 surface 时必须重读布局，才发现）。
## 2026-09-30：参考驱动的形态修订

柳树在下垂枝之前增加中间支撑枝级；银杏采用持续中轴、窄冠和短枝扇叶簇。这两种树的枝条实例按父枝位置派生随机种子，减少不同父枝上重复的小枝排列。遮罩图集每种形状含两个图格，柳树和银杏使用不同布局，其余树种暂复制各自的原遮罩。参考来源、实施范围和验证记录见 `../../../ProjectAbyssWiki/documentation/systems/TreeGen/TreeGen_SpeedTree_Study.md`。

## 2026-09-30：骨架与连接网格

`SlowTreeGrowth.*` 和 `ProceduralTreeGrowthParameters.*` 为项目新增代码。银杏、柳树的树种规则与十字叶簇模式默认使用「父子骨架 → 支撑需求与半径 → 局部接头网格 → 既有叶簇」路径；`structural_branches = false` 可返回上一轮。曲线资源在主线程采样后传给工作线程。

该路径始终使用 CPU，显式请求 GPU 时也回退 CPU，结果标记 `tessellation_backend = cpu_connected`。没有移植 JZTREES 的 LGPL VEX/HDA 代码，也不依赖 Houdini。其他预设、外部 `.vtree` 和旧几何模式保持原生成器。验证与性能见 `../../../ProjectAbyssWiki/documentation/systems/TreeGen/TreeGen_Branch_Rebuild.md`。

## 2026-09-30：全部 SlowTree 预设接入连接枝架

后续一轮将连接路径扩展到编号 0–6 的全部内置预设。`SlowTreeGrowth` 新增默认阔叶、松、竹、水杉和桃的分枝布局；多 Trunk 按 ID 稳定生成并共享骨架预算，诊断数据新增 `trunks`。竹秆使用节间半径与竹节细分，不沿用树木的主干支撑需求锥度。所有 LeafCluster 子节点都会保留，避免桃树花簇被叶簇覆盖；Spine/Frond 转为末级细梢和对应遮罩叶簇。

松的两个遮罩格现在具有不同的双针束排列。其余新增预设继续复用原有叶形遮罩，调整簇尺寸和沿枝朝向。银杏、柳树保留上一轮规则。

内置预设使用 CPU 连接路径，外部 `.vtree` 明确保留旧生成器，旧几何模式和 Weber–Penn 不变。合并上游时应保留这些路由条件、主线程曲线快照和工作线程取消检查。实现、参考和验证记录见 [TreeGen_AllSpecies.md](../../../ProjectAbyssWiki/documentation/systems/TreeGen/TreeGen_AllSpecies.md)。

## 2026-09-30：竹子精修

`SlowTreeGrowth` 为 Bamboo 增加不等长节间、节环与箨痕、纵槽和可见细枝层。仅竹秆使用非均匀弧长样本，其他预设维持原采样。`MeshBatch.WoodColors` 和 `bBambooCulm` 为项目扩展，保持上游木质顶点的 10 浮点布局；准备阶段复制顶点色，提交阶段绑定缓存的程序化纤维纹理。

竹叶图集的两个图格改为不同的小叶组，仍使用每簇 4 三角形的交叉卡片。生长资源新增竹节间距、竹节起伏和叶片尺寸，均进入后台快照。不要在上游同步时丢失这些字段或在工作线程创建纹理。参考、精度开销和验证见 [TreeGen_Bamboo_Refinement.md](../../../ProjectAbyssWiki/documentation/systems/TreeGen/TreeGen_Bamboo_Refinement.md)。

## 2026-09-30：其余六类树形与外观精修

`SlowTreeGrowth`、`SlowTreeFoliage` 调整六类预设的枝梢分布、叶簇大小及蒙版形状，竹子的对应结果由历史网格和图格回归保护。木质支撑新增枝级颜色、切线与复用的 UV 接缝顶点。`MeshBatch.BarkPreset`、`WoodTangents` 以及准备结果中的 `BarkPreset` 为项目扩展，不改变上游 10 浮点木质顶点布局；提交阶段使用 `SlowTreeMaterials` 的缓存程序树皮颜色与法线纹理。

图集全部形状现在具有不同的两个变体。连接路径的叶簇按实际支撑轴附着，蒙版的根位置包含图格留白的 UV 换算；竹子继续使用已验收的专用路径。没有导入 Houdini 工程或教程源码。完整参考、边界与复现见 [TreeGen_Houdini_Refinement.md](../../../ProjectAbyssWiki/documentation/systems/TreeGen/TreeGen_Houdini_Refinement.md)。

## 2026-09-30：桃树枝架与花冠重构

Peach 使用独立的主枝、中枝、上扬承花细枝布局，前两级均支持分叉；承花细枝纳入连续木质网格，并按细轴减少径向分段。`TreeGrowthSettings` / `ProceduralTreeGrowthParameters` 新增 `PeachTwigDensity`、`PeachBlossomDensity`、`PeachBlossomScale`，应保留资源绑定、范围限制、变更通知及后台快照。

Blossom 图格改为带细轴、圆瓣、侧花和花苞的程序花枝，仍使用每簇 4 三角形的十字面片。花量计算纳入正数 `LeafCount` 与密度系数，花簇尺寸独立控制；盛花期推迟绿叶展开。其他预设的网格、树皮及非花图格经历史基线比较保持一致。未移植 SpeedTree 代码或导入视频中的模型、贴图。实现和验证见 [TreeGen_Peach_Refinement.md](../../../ProjectAbyssWiki/documentation/systems/TreeGen/TreeGen_Peach_Refinement.md)。

后续接头修正为 Peach 主枝预留较宽的主干接口，并按接口中心对齐截面相位；`WoodSurface::RefineJunctions` 只对主干与粗枝根部进行一次共享边细分及局部平滑。应保留共享中点、外侧零权重、纹理接缝插值、取消检查和最终法线单次计算，避免开裂、改变花冠或重复消耗生成时间。其他树种不进入此细分路径。近景与性能记录同上。
