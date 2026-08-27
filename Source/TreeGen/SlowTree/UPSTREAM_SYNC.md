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
