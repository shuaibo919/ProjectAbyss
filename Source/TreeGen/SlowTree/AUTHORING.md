# 写 .vtree 模板的设计规律

从上游默认模板 `VtreeIO.cpp: kDefaultTemplate`（HelloTree，SpeedTree Broadleaf 示例导出）逆向
提取，**每条都对着 `CylinderSegment.cpp` / `TreeGenerator.cpp` 核对过**。它是仓库里唯一一个
视觉合格的模板，所以它就是参考实现。

初版五个手写预设（柳/松/银杏/竹/水杉）几乎每条规律都踩反了，2026-08-27 按本文重调。

## 0. 怎么读一个 .vtree

```
NODE <id> <type> <editorX> <editorY>
```

- `type`: 0=Trunk 1=Roots 2=Branch 3=Twig 4=LeafCluster 5=Spine 6=Frond 7=Export
- **`id` 是任意的，层级顺序看 `editorX`。** HelloTree 的 id 顺序是 2→3→6→1→4→5，
  但 X 是 100→282→417→554→682→833。看 id 会读错结构。
- `LINK a b` = b 是 a 的子节点。

HelloTree 的实际链路：

```
Trunk(x=100) → Branch L1(282) → Branch L2(417) → Branch L3(554) → Twig(682) → Leaf(833)
             → Roots(282)
```

## 1. 叶数是逐层相乘的 —— 层数比参数重要

`buildLeafCluster` 每个**末端枝实例**调用一次，把 `leafCount` 片叶铺满整根枝。所以

```
总叶数 = branchCount(L1) × branchCount(L2) × branchCount(L3) × twigCount × leafCount
```

| | HelloTree | 初版银杏 |
|---|---|---|
| 链路 | Br×3 层 + Twig | Br×1 层 + Twig |
| 计算 | 12×8×4×7×10 | 7×5×15 |
| 叶数 | **26,880** | **525** |
| 三角面 | 356k | 6k |

**先数 Branch 层数，再动参数。** 少一层就少一个数量级，调 `leafSize` 救不回来。

## 2. 分枝数向外**递减**：12 → 8 → 4

大扇出放在靠树干的一层，往外收。不是持平、更不是递增。
12×8×4 = 384 根三级枝，是稠密但不爆炸的量。

## 3. `spreadAngle` 向外**递增**：33 → 50 → 50 → 65

一级枝贴着树干走（33°，接近直立），每往外一层张得更开。
**这是"圆润树冠"和"炸开的星形"的区别。** 一级枝就给 40°+ 会把树撑成扫帚。

## 4. `gravity 1.0` 只给一级枝，其余 0.18

实现（`CylinderSegment.cpp:128`）：每段 `dir = lerp(dir, down, gravity*0.5/lengthSegs)`，
累计总弯折约 `gravity*0.5` 朝正下方。

所以一级枝**先上扬再压弯成拱**，子枝保持挺直。**阔叶树的招牌剪影就靠这一个值。**
全树都给 0.3~0.4 的话，哪里都不拱，就是一把伞骨。

## 5. `taperPow < 1` = 细长枝；> 1 = 粗壮枝（容易搞反）

实现（`:143`）：`radius = lerp(radiusStart, radiusEnd, t^taperPow)`。

- `p < 1` → `t^p > t` → 更早接近 `radiusEnd` → **出干后迅速变细，通体细长**
- `p > 1` → `t^p < t` → 半径长时间贴着 `radiusStart` → **粗壮到末端才收尖**

HelloTree 一级枝 `taperPow 0.729`（细长）。初版预设全用 1.2~1.5，所以枝条是一根根
粗棒子 —— 松树那张图的褐色棍子就是这么来的。

躯干反过来：Trunk `taperPow 1.6`（要粗壮），Roots `1.8`（根盘饱满）。

## 6. `radiusScale` 只降一次，然后贴合：0.689 → 0.395 → 1.0 → 1.0

二级枝故意比父级附着点细得多（0.395），三级枝和细枝则完全贴合父级（1.0）。
一次明显的粗细台阶，之后连续。

## 7. `noiseAmount 90` 给大木头，30~35 给小枝

| | Trunk | L1 | L2 | L3 | Twig |
|---|---|---|---|---|---|
| noiseAmount | 90 | 90 | 90 | 30 | 35 |
| noiseFreq | 3.27 | 1.42 | 2.02 | 3.0 | 3.5 |

**90 是"看着像树"和"看着像示意图"的分界。** 初版预设用 25~55，太老实了。
注意 freq 反着走：大木头低频（大摆动），小枝高频（细抖动）。

## 8. `regionStart ≈ 0.4~0.5` 每一层都要

子枝只长在父级的**外半段**。这会逐层复合（三级枝长在二级枝外半段，而二级枝又长在
一级枝外半段），结果是**树冠内部是空的**，不会塞满交叉枝。这也是省面数的地方。

## 9. 不用 `sizeFalloff`

HelloTree 全图没有一处 `sizeFalloff`。它是给蕨类小叶向梢渐小用的，用在枝条上会让
上部树冠缩水。

## 10. 根系要**细而短**

HelloTree: `rootCount 5, length 1.413, radiusScale 0.34, taperPow 1.8, spreadAngle 90,
droop 1, noiseAmount 90, gnarl 17.5, lengthSegs 10`。

`radiusScale 0.34` 是关键 —— 根比树干基部细得多。初版预设用 0.7~0.8，粗了一倍，
配上 `spreadAngle 92` 就成了平铺的放射状扁片（"像板根尖刺而不是根"）。

**预览必须有地面**（`TreeGenPreviewSlowTree.gd` 已加）。根系按设计要扎到 y<0，
悬空看等于没法判断。

## 11. 材质贴图字段留空

上游模板的 `mat.*Tex` 指向设计者机器上的 SpeedTree Modeler 示例资源，本项目无资产，
靠顶点色。见 `UPSTREAM_SYNC.md` 第 8 条：无贴图下 `LeafCluster` 是不透明矩形，
`Frond` 自带轮廓收尖 —— 针叶树用一叶一四边形覆盖不了，那是 `useCutout` 要解决的。

## 12. 上面这些是**阔叶树**词汇，针叶/竹子要反着来

HelloTree 是 SpeedTree 的 Broadleaf 示例，所以它的规律有一部分是阔叶专属的。照搬到松树上
把清晰的轮生层次搅成了一团乱棍 —— 实测踩过。

| 规律 | 阔叶（HelloTree） | 针叶 / 竹 |
|---|---|---|
| §5 `taperPow` L1 | **0.75**（细长） | **1.15~1.4**（挺直粗壮） |
| §7 `noiseAmount` L1 | **90** | **14~20**（针叶枝是直的！） |
| `gnarl` L1 | 10 | 2~3 |
| §9 `sizeFalloff` | **不用** | **必须用**（松 0.85） |
| §2 分枝数递减 | 12→8→4 三层 | `mode 6` Interval 轮生 + 一层 L2 朝外 |
| `regionEnd` | 0.92 | **1.0**（否则顶端露出一截光杆主梢） |
| 叶子 | LeafCluster 叶卡 | **Spine→Frond 叶簇**（见 §17） |

**`sizeFalloff` 是圆锥的来源。** 没有它，各层轮生枝一样长，树冠是个球不是锥 —— 这是我把
松树调坏两轮才定位到的。松树的完整配方见 §17。

竹子同理：`noiseAmount 6~14`（竹竿是直的），靠 `jointCount`/`jointBulge` 做竹节，
`regionStart 0.45` 留出下段光竿。

## 13. 面数由**细枝管**主导，不是叶片

一根细枝 = `sides × lengthSegs × 2` 三角面。银杏 L1(11)×L2(8)×L3(4)×Twig(7) = 2464 根细枝，
按 sides 5 / segs 5 算就是 12 万面 —— **比叶片贵得多**。

所以压预算优先动这三个，按顺序：

1. **砍一层 Branch**（阔叶两层 + Twig 就够，HelloTree 的三层是英雄档）
2. **降细枝 `sides`/`lengthSegs`**（4/4 足够，细枝没人看截面）
3. 最后才动 `leafCount`

实测：银杏三层 Branch = 324k 面；砍到两层 + 细枝 4/4 = **60k**，观感几乎无损。

## 14. 预算参考（本项目）

| 档 | 面数 | 用途 |
|---|---|---|
| 散布档 | 40~70k | PCG 成片撒，当前五个预设都在这档 |
| 英雄档 | 300k+ | 单株近景，HelloTree/银杏三层版 |

## 14b. 一个可复制的阔叶骨架

```
Trunk         length L, startRadius R, taperPow 1.6, noiseAmount 90, sides 8,  lengthSegs 16
 ├ Roots      rootCount 5~7, length 0.2L, radiusScale 0.34, taperPow 1.8, spreadAngle 90, droop 1
 └ Branch L1  count 12, lengthRatio 0.71, radiusScale 0.69, taperPow 0.73,
              spreadAngle 33, gravity 1.0,  regionStart 0.40, noiseAmount 90, noiseFreq 1.4
   └ Branch L2 count 8,  lengthRatio 0.39, radiusScale 0.40, taperPow 1.5,
              spreadAngle 50, gravity 0.18, regionStart 0.39, noiseAmount 90, noiseFreq 2.0
     └ Branch L3 count 4, lengthRatio 0.38, radiusScale 1.0, taperPow 1.5,
              spreadAngle 50, gravity 0.18, regionStart 0.51, noiseAmount 90, noiseFreq 3.0
       └ Twig  count 7,  lengthRatio 0.58, radiusScale 1.0, taperPow 1.3,
              spreadAngle 65, gravity 0.25, regionStart 0.20, noiseAmount 35, sides 5, lengthSegs 5
         └ LeafCluster leafCount 10, clusterRadius 0.05, leafSize 0.19, normalJitter 0.25
```

针叶树把 L1 换成 `mode 6`(Interval) + `intervalSpacing` / `branchesPerNode` 做轮生；
羽状复叶把 LeafCluster 换成 `Spine → Frond`。

## 15. Spine → Frond：面数效率高一个数量级

从 `Reference/SlowTree/samples/Sample.vtree` 挖出来的（`tools/split_vtree.py` 拆成单株，
`Game/Develop/VtreeSampleTest.tscn` 逐株出图）。实测：

| 株 | 叶子方式 | 面数 | 观感 |
|---|---|---|---|
| 棕榈（plant6） | Spine→Frond | **7,816** | 完整可用 |
| 地面莲座（plant4） | Spine→Frond | **3,064** | 完整可用 |
| 阔叶（plant1，≈HelloTree） | LeafCluster | 525,736 | 完整可用 |
| 我们的银杏 | LeafCluster | 59,960 | 尚可 |

**一张 Frond 叶带顶掉几十个四边形叶卡**，且无贴图下自带轮廓收尖（见 UPSTREAM_SYNC 第 8 条）。
所以：**能用 Frond 表达的物种就别用 LeafCluster** —— 棕榈、蕨、芭蕉、苏铁、羽状复叶，以及任何
"少量大叶"的植物。

### 样本里学到的七个手法（参数注释里都没写）

1. **`gravity` 可以远大于 1。** 样本用 **3 / 3.5**。累计弯折约 `gravity*0.5`，代码没有上限
   （`CylinderSegment.cpp:129`）。棕榈叶的大幅拱垂就靠它。

2. **`gravity ≤ 0` 是空操作，不是上扬。** 守卫是 `if (gravity > 0.0f)`，上下游一致。样本里那个
   `gravity -0.45` 作者大概想要上扬，实得"完全不下垂"。要直枝就填 0。

3. **退化 Trunk 当纯挂点**：莲座的 Trunk 是 `length 1 / startRadius 0.001 / endRadius 0 /
   noiseAmount 0`，几何上不存在，只提供附着点。**这是做无主干植物（草丛/蕨/莲座）的办法**，
   不需要新节点类型。

4. **同一父级挂多个平行 Spine，各用很窄的 `regionStart/regionEnd` 窗口**钉在特定高度带。棕榈用
   五组（`0.917~0.95`、`0.94~0.95`……），配不同 `spreadAngle`(21~35) 与 `gravity`(3~3.5)，得到
   有层次的叶冠而不是一圈整齐叶子。

5. **`Spine lengthRatio` 可以 > 1**：莲座 **2**，针叶那株 **3** —— 叶轴比父级长两三倍。短枝配长
   叶轴，是让叶簇远远探出枝条的办法。

6. **棕榈主干几乎不收分**：`0.097 → 0.072`（阔叶是 `0.3 → 0.075`）。`Spine radiusScale 0.05`，
   叶柄只有主干半径的 5%。

7. **`Frond width` 是单侧半宽、单位米**，可以很大：棕榈 `width 1`。等宽叶带
   （`widthBase 1 / widthTip 1`，见针叶那株）是另一种效果 —— 宽扁叶片带。

### 可复制的骨架

```
# 棕榈 / 芭蕉（~8k 面）
Trunk        length 7.2, startRadius 0.10, endRadius 0.072, noiseAmount 23   # 几乎不收分
 └ Spine ×5  lengthRatio 0.5, radiusScale 0.05, gravity 3~3.5, spreadAngle 21~35,
             regionStart 0.92~0.94, regionEnd 0.95, spineCount 5~9, lengthSegs 12
   └ Frond   width 1, widthBase 0.15, widthTip 0, profilePow 0.6, segsPerSide 1

# 地面莲座 / 蕨（~3k 面）
Trunk        length 1, startRadius 0.001, endRadius 0, noiseAmount 0          # 纯挂点
 ├ Spine     lengthRatio 2, gravity 3, regionStart 0, regionEnd 0.075, spineCount 10
 │ └ Frond   width 0.65, widthBase 0.18, widthTip 0, profilePow 1.05
 └ Spine     lengthRatio 1.3, gravity 2, regionStart 0.24, regionEnd 0.26, spineCount 3,
             noiseAmount 90                                                   # 少量杂乱内层
   └ Frond   width 0.40, 其余同上
```

## 16. 外部 .vtree 直接可用，不需要导入器

`.vtree` 是纯文本（`NODE id type x y` / `LINK a b` / KV 行）。
`SlowTreeGenerator.generate_from_file(path, seed, use_gpu)` 直接吃绝对路径 —— 任何来源的 `.vtree`
都能加载，实测上游 6 株样本零改动跑通。

**SpeedTree 参数图无法导入**：上游只做 SpeedTree **FBX 网格**导入（ufbx，`ImportTrunk`/
`ImportLeaf`/`Scatter`，为 Nanite Assembly 管线服务），`.spm` 是专有二进制。那三个节点在本项目
关在 `SLOWTREE_FULL_NODES` 后面（带资产，与无资产契约冲突）。

多株工程用 `python Source/TreeGen/SlowTree/tools/split_vtree.py <in.vtree> <outdir>` 拆单株
（会把 `posX/posZ` 归零，并打印每株的层级摘要）。

## 17. 针叶树配方（含知乎 SpeedTree 松树笔记的可迁移部分）

来源：`Reference/zhihu-scraper/知乎归档/SPEEDTREE学习笔记—松树简易制作流程（单轴分支）/`。
是 SpeedTree 流程，但**结构性建议可以直接搬**，参数名对应如下。

| 文章说法 | SlowTree 对应 | 说明 |
|---|---|---|
| §03「大树枝更**平**，远端稍微被压弯」 | `spreadAngle 85` + `gravity 0.35` | 近水平 + 只有梢部压弯，不是整根下垂 |
| §03「长度要**下长上短**，需要调整曲线」 | **`sizeFalloff 0.85`** | **这就是圆锥的来源。** 没它每层一样长 = 球 |
| §03「Gen 下 First/Last 控制生成范围」 | `regionStart` / `regionEnd` | 针叶要 `regionEnd 1.0`，否则顶端露光杆主梢 |
| §04「二级分支复制一级，**朝向都向外**」 | L2 `spreadAngle 55` | 比 L1 收一点，朝外 |
| §02「RadialAmount 大噪波 + FineAmount 小噪波」 | `noiseAmount` / `noiseFreq` | 无 displacement 贴图，只有样条噪声 |
| §02「FlaresRadius 底部更粗」 | `baseFlare` | |
| §02「Spline 是骨，Skin 是皮」 | —— | 好用的心智模型：想改长度改"骨"，想改表面改"皮" |

### 一处**没有**照搬的地方

§05 说「松针也是用**树枝节点**制作，每一根树枝就是一根松针」。**我们不这么做**，因为
§06 自己就承认了代价：「生成数量过度，会导致软件崩溃」。

实测对比（等覆盖面积）：

| 方案 | 单位成本 |
|---|---|
| 一根针 = 一个 Twig（sides 3 / segs 2） | 12 面 × ~15 根 = **~180 面** |
| 一簇针 = 一个 Spine+Frond（sides 3/segs 6, segsPerSide 2） | **~60 面** |

**Frond 叶簇是三分之一的成本，剪影一样，而 `serrate` 边缘正好给出针叶梳齿感。**
文章 §06 是从另一头撞上了同一个问题。

### 骨架（~90k 面，比散布档略高）

```
Trunk        length 9, startRadius 0.32, endRadius 0.05, taperPow 1.6, noiseAmount 85
 ├ Roots     标准细短根
 └ L1 Branch mode 6 Interval, intervalSpacing 0.1, branchesPerNode 5,
             spreadAngle 85, gravity 0.35, sizeFalloff 0.85, regionStart 0.15, regionEnd 1.0,
             lengthRatio 0.34, taperPow 1.4, noiseAmount 20, gnarl 3       # 平 + 下长上短
   └ L2 Branch count 4, spreadAngle 55, gravity 0.25, sizeFalloff 0.3,
             lengthRatio 0.45, radiusScale 0.55, sides 4, lengthSegs 4     # 朝外
     └ Spine  spineCount 3, lengthRatio 1.1, radiusScale 0.12, spreadAngle 26,
             gravity 0.35, sides 3, lengthSegs 6                           # 叶轴, 管子要细
       └ Frond width 0.26, widthBase 0.85, widthTip 0.45, profilePow 0.3,
             segsPerSide 2, serrate 1, serrateDepth 0.65                   # 宽而少 > 窄而多
```

**"宽而少"胜过"窄而多"**：`spineCount 4 / width 0.085` 时叶片是一片片孤立的细条（78k 面还是
稀的）；改成 `spineCount 3 / width 0.26` 立刻成团（89k 面但成立）。因为每根 Spine 都要付一根
看不见的管子钱，管子数要压，单叶要放大。

## 18. 四种分枝方式 → SlowTree 参数对照

来源：三维赵金祥《部分树木的枝干特点》(牛客网 discuss/903740077119979520)。这是**植物学的分枝
架构分类**，比按物种一棵棵试有用得多 —— 先判断物种属于哪种分枝方式，再套对应骨架。

| 分枝方式 | 代表树木 | SlowTree 实现 | 状态 |
|---|---|---|---|
| **单轴** monopodial | 松、杉、柏、银杏幼树 | `mode 6` Interval 轮生 + `sizeFalloff 0.85` + `regionEnd 1.0` | **已有**，见 §17 |
| **合轴** sympodial | 枫、杨、柳、果树、多数景观阔叶 | Classic，`branchCount` 12→8→4，L1 `gravity ~1.0`，`noiseAmount 90`，`regionStart 0.4` | **已有**，见 §14b |
| **二叉** dichotomous | 丁香、槲寄生、紫荆、丛生灌木 | Classic + `branchCount 2` + `rotateOffset 180`，逐层递归；**必须开 Variance** | 待做 |
| **假二叉** pseudo-dichotomous | 竹、杜鹃、绣球、丛生花树、灌木丛 | **多个 Trunk 节点**，`posX`/`posZ` 错开 = 丛生 | 待做 |

原文的风格化要点，逐条对应：

| 原文 | 参数 |
|---|---|
| 单轴「侧枝分层环状排布，上下错落不重叠」 | `mode 6` + `intervalSpacing`（层间距）+ `branchesPerNode`（每层根数） |
| 单轴「侧枝长度自上而下逐渐缩短，收束成锥形」 | **`sizeFalloff`** —— 圆锥的唯一来源，见 §12 |
| 合轴「弱化僵硬直干，保留枝干轻微曲折韵律」 | Trunk `noiseAmount 85~90` + `gnarl 14` |
| 合轴「主枝向外舒展延伸，没有严格对称」 | L1 `gravity ~1.0`（先扬后拱）+ `rotateOffset 137.5`（黄金角） |
| 合轴「控制上疏下密的节奏」 | `regionStart 0.4`（子枝只长外半段，逐层复合）；阔叶**不要**用 `sizeFalloff` |
| 二叉「保留核心对称分叉，但避免绝对机械对称」 | `branchCount 2` + `rotateOffset 180`，再用 Variance 破对称（下方） |
| 二叉「统一每组枝丫的长短比例」 | `lengthRatioVar` 给小值（±0.05），不要给大 |
| 假二叉「减弱主干结构，统一丛生枝干的粗细高度」 | 多 Trunk，各株 `length`/`startRadius` 接近、`seed` 不同 |
| 假二叉「把控外轮廓疏密，避免枝干扎堆」 | 各 Trunk 的 `posX`/`posZ` 撒开 |

### Variance 参数：已实现，但当前预设一个都没用

`spreadAngleVar` / `lengthRatioVar` / `gravityVar` / `radiusScaleVar` / `endRatioVar`，语义是
**绝对 ± 范围**（与被抖动的参数同单位，0 = 关闭）。实现处：Branch `TreeGenerator.cpp:630`、
Twig `:867`、Roots `:959`、Spine `:1211`，四处都生效。

**这是"打破机械感"的正规手段**，比手调 seed 有效。二叉/假二叉尤其依赖它 —— 完美对称的 Y 形
一眼假。建议起点：`spreadAngleVar 6`（度）、`lengthRatioVar 0.06`、`gravityVar 0.1`。

### BranchMode 只有 Interval 实现了

`TreeGenerator.cpp:595` 只对 `BranchMode::Interval` 分支，其余全部落到 Classic。
所以 `Bifurcation`(二叉) / `Phyllotaxy`(叶序) / `Proportional`(正比父级长度) 等**都是枚举占位**
（`SlowTreeTypes.h` 自己也写了"目前仅 Classic 生效"）。

**二叉分枝要用 Classic 模拟**：`branchCount 2` + `rotateOffset 180`，每层重复，靠 Variance 破
对称。想要真正的末端二叉分叉，得去实现 `Bifurcation` 模式。

### 多 Trunk = 丛生（能力已验证）

一个 `.vtree` 可以有多个 Trunk 根节点，各自带 `posX`/`posZ`。`Reference/SlowTree/samples/Sample.vtree`
就是六株同文件（用 `tools/split_vtree.py` 可拆开）。移植说明第 2 条：多根按 id 升序处理。

**所以竹子应该是一丛而不是一根** —— 当前 Bamboo 预设是单竿，这是个明确的改进项。

---

**生物学依据见 `Docs/PlantBotany_ForPCG.md`** —— 顶端优势为什么导致单轴/合轴分化、
达·芬奇分枝律给出的 `radiusScale` 理论值、叶序分数对应的 `rotateOffset` 物种表、
复叶类型决定用 LeafCluster 还是 Spine→Frond。本文管"怎么填"，那篇管"为什么"。

## 19. 花树配方（桃）—— 以及"用两个 LeafCluster 造花"的手法

SlowTree **没有"花"这个节点**。做法是在同一根细枝上挂**两个 LeafCluster**：一个粉色小卡当花，
一个绿色窄卡当叶。桃的实现见 `tools/retune_presets.py` 的 `PEACH`。

### 结构（由植物学推出，不是试出来的）

维基 "Peach" 给的事实 → 走 Hallé 判别键（`Docs/PlantBotany_ForPCG.md` §2）：

| 事实 | 推论 |
|---|---|
| 花序**侧生**（腋生花芽长在一年生枝上）| 枝 monopodial → 键 19b |
| 主干**节律性**生长，轴同质且 orthotropic | → 键 21a → **Rauh's model** |
| "branches tend to droop over time" | 次生重力弯曲（Champagnat 式）→ 就是 `gravity` |
| **树高仅 3–4 m**，冠幅≈树高 | 主干要短要细，靠 `lengthRatio` 高的枝撑出冠 |
| 叶 7–15 × 2–4.5 cm | `leafSize 0.075` / `leafAspect 0.32` |
| 花 2–3.5 cm，粉，**先花后叶** | 花卡为主、叶卡极少 |
| 李属叶序 2/5 | **`rotateOffset 144`**（不是默认的 137.5） |

```
Trunk        length 2.6, startRadius 0.13, endRadius 0.04     # 小树, 干短且细
 ├ Roots     length 0.55, radiusScale 0.34
 └ L1 Branch count 7, lengthRatio 0.85, spreadAngle 48, gravity 0.9, regionStart 0.3,
             rotateOffset 144                                  # 高 lengthRatio 补短干
   └ L2      count 6, lengthRatio 0.5, spreadAngle 58, gravity 0.55, radiusScale 0.45
     └ Twig  count 7, lengthRatio 0.6, spreadAngle 66, gravity 0.4, 偏红 albedo
       ├ LeafCluster 花: leafCount 26, leafSize 0.08, leafAspect 1.0, normalSoften 0.85,
       │                 albedo 0.95 0.51 0.62
       └ LeafCluster 叶: leafCount 3,  leafSize 0.075, leafAspect 0.32, planar 1
```

实测 **4.5 m / 42,172 面 / 7 surface**，自检与 GPU 对拍通过。

### 两条可迁移的经验

**① 花卡不能按真实花径给。** 第一版按实测 2–3.5 cm 填 `leafSize 0.035`，结果 4.5 m 的树上
每朵花约 4 像素，抗锯齿一平均就成了灰白麻点，整棵树像枯木长了地衣。改成 `0.08`（一张卡代表
"一花+一蕾"，桃花本就常成对）立刻成片。**这是 §17「宽而少胜过窄而多」的又一例。**

**② 花团要把 `normalSoften` 开大（0.85）。** 叶卡是平面卡，法线随机时约一半背向光源变黑；
`normalSoften` 让法线从叶簇轴心向外呈球形，花团才会像一个体积而不是一堆黑白碎片。

### 已知不足

**花卡是明显的方块。** 花卡比叶卡大且正面朝人，所以方形轮廓比任何其它预设都刺眼 ——
**这是 `useCutout` 收益最大的地方**（五瓣花轮廓 vs 方块），比针叶树更值得先做。

**先花后叶做不到真正切换。** 现在是"花为主 + 少量新叶"，对应盛花末期。真正的
「先花后叶 → 满叶 → 红叶 → 落尽」需要 SlowTree 缺的 season 参数。

## 20. 能力补齐（2026-08-27）

| 能力 | 状态 | 要点 |
|---|---|---|
| **叶片轮廓 `useCutout`** | ✅ | `SlowTreeGenerator::FillLeafCutouts` 用 `TreeGen::BuildLeafCutout` 填归一化剪影。**CPU/GPU 两条路径都要填**，否则对拍挂。**只做 LeafCluster，不动 Frond**（Frond 自带宽度曲线 + serrate，加 cutout 会覆盖）。 |
| **季节 `season`** | ✅ | 复用 `TreeMath.h` 的 `GetSeasonLeafColor`（0/4 冬，2 夏）。在装配单 surface 时按**逐叶锚点哈希**着色 —— 种子必须逐叶不是逐顶点，否则一片叶上出现渐变。 |
| **常绿判别** | ✅ | `SlowTreePresets::IsEvergreen()` 是一张**表**（松、竹）—— 颜色推不出针叶/阔叶。**水杉是落叶针叶树**，不在表里。 |
| **花不变色** | ✅ | 按 `albedo.r > albedo.g` 自动认出（叶绿色主导，花不是），走 `GetSeasonLeafColor` 的 needle/blossom 分支直接返回夏色。 |
| **单 surface 顶点色** | ✅ | 原来每 batch 一个 surface（桃 7 个），现在恒为 1。枝干无顶点色通道，用该 batch 的材质 albedo 填。 |
| **Variance 参数** | ❌ 未铺 | 已实现且四处生效，但预设全留 0。见 §18。 |
| **叶量预算重标定** | ❌ 未做 | 无 `max_leaves` 式机制。 |

### 单 surface 的已知代价

**逐节点的 `roughness`/`metallic`/`normalTex`/`sssStrength` 全部丢弃**，只保留 albedo 进顶点色。
这是无资产契约的必然结果，也是彩墨 NPR 想要的方向 —— 但是有意识的取舍，不是疏漏。

材质统一 `CULL_DISABLED`：叶卡是薄片必须双面，枝干是闭合管、双面只多花填充率不出错，
所以没为此拆回两个 surface。

### 验证

`Game/Develop/SeasonTest.tscn` 直接采样顶点色而不是看渲图。**方法上有个坑**：
不能每个季节各自按颜色筛叶片顶点 —— 深秋叶是暗红 `(0.27,0.05,0.01)`，会被"绿色主导"的筛选
条件全部排除，报出 `(0,0,0)`。几何在各季节完全相同，所以要**在夏季挑一次索引，再用同一批
索引读其它季节**。实测：

```
Ginkgo  s0.5=(0.66,0.75,0.24)  s2.0=(0.61,0.68,0.22)  s3.2=(0.31,0.10,0.02)  s3.9=(0.27,0.05,0.01)
Pine    s0.5=(0.15,0.34,0.15)  s2.0=(0.15,0.34,0.15)  s3.2=(0.15,0.34,0.15)  s3.9=(0.15,0.34,0.15)
```
