# 植物学笔记：用于指导 PCG 植被生成

配套文档：
- `Source/TreeGen/SlowTree/AUTHORING.md` —— SlowTree `.vtree` 的参数级操作规律（从上游模板逆推）
- 本文 —— 上游那些数字**为什么**是那样的生物学依据，以及尚未用上的规律

**阅读约定**：标注「✅ 可直接用」的是能立刻映射到 SlowTree 参数的；标注「⚠️ 对不上」的是
生物学成立但本工具无法表达的，不要浪费时间。

---

## 1. 顶端优势 —— 单轴/合轴分枝的生物学原因

顶芽产生生长素（auxin / IAA），沿韧皮部下行扩散进侧芽，**抑制侧芽伸长**，使植株优先向上
争光。这就是分枝架构分化的机制。

| 顶端优势 | 形态 | 代表 | SlowTree |
|---|---|---|---|
| **强** | 单一挺拔主干 + 强烈分化的**水平**侧枝，锥形 | 针叶树；南洋杉科最极端 | `mode 6` Interval 轮生 + `sizeFalloff` 高 |
| **弱** | 侧枝不受压制，向外铺展，圆钝冠形 | 多数阔叶、垂枝落叶松 | Classic + L1 `gravity ~1.0` |

**✅ 可直接用**：这解释了 `AUTHORING.md §12` 为什么针叶和阔叶的参数要反着填 —— 不是风格
选择，是两种不同的生长调控。

**顶芽被去除后**，IAA 浓度下降，侧芽解除抑制并**互相竞争成为新的主导枝**。矮林作业
（coppicing）、截头（pollarding）、绿篱、盆景造型都在利用这一点。

**✅ 可直接用**：这正是**丛生（假二叉）**形态的来源 —— 竹、杜鹃、绣球、灌木丛。所以丛生
不是"多株挨着长"，而是"顶端优势缺失导致多个等势主轴"。做法见 `AUTHORING.md §18` 的多 Trunk。

---

## 2. Hallé–Oldeman 树木建筑模型 —— 23 个模型的判别键

Hallé、Oldeman、Tomlinson《Tropical Trees and Forests: An Architectural Analysis》(1978)。
原书已在 `Reference/Halle.pdf`（463 页 OCR 扫描本）。**不要整本读**，用这三个索引按需取：

| 文件 | 用途 |
|---|---|
| `Docs/reference/Halle_key.md` | **判别键全文**（9 KB，本节的来源），23 个模型的二歧检索表 |
| `Docs/reference/Halle_models.md` | 模型 → PDF 页范围，每个模型 4–10 页 |
| `Docs/reference/Halle_index.md` | 逐页 OCR 首行 + 页面字数，用于定位任意内容 |
| `Docs/reference/Halle_book.md` | 全文 OCR，带 `<!-- page N -->` 标记，供 grep |

生成脚本在同目录（`halle_to_markdown.py` / `halle_model_map.py` / `halle_extract_key.py`）。
**OCR 会毁标题**（"Holttum's Model" → "Holuum's Madel"），所以索引用去元音的骨架匹配，
不要指望精确搜词。

### 判别键用的六个判别性状 —— 全部可映射到生成器参数

一个二歧检索表本身就是一棵决策树，而它的判别轴恰好就是 PCG 需要的轴：

| 性状 | 含义 | SlowTree |
|---|---|---|
| **orthotropic / plagiotropic** | 轴向上辐射对称生长 / 水平背腹生长 | `spreadAngle` 小 / 接近 90 |
| **monopodial / sympodial** | 一个顶端持续延伸 / 由侧芽接替 | 单节点递归 / 需多层 Branch 接力 |
| **rhythmic / continuous** 生长 | 成批爆发出层 / 均匀连续 | **`mode 6` Interval / Classic** |
| **inflorescence terminal / lateral** | 顶生花序使轴**有限**（必须接替）/ 侧生花序轴可无限延伸 | 顶生 ⇒ 必须 sympodial 接力 |
| **long-lived / phyllomorphic 枝** | 永久枝 / 像复叶一样整体脱落的短命枝 | 后者 ⇒ 用 Spine→Frond |
| **basitonic / acrotonic** | 分枝在模块基部（常地下）/ 远端 | 前者 ⇒ **多 Trunk 丛生** |

### 与本项目直接相关的七条

**① 松树 = Rauh's model**（p221，例种字面就是 *Pinus caribaea*）：全轴 orthotropic、
花序侧生（枝 monopodial）、主干**节律性**生长。
**我给松树写的配方（Interval 轮生 + monopodial）正好落在这个模型上** —— 属于事后印证。

**② 南洋杉 = Massart's model**（p191），**不是 Rauh**。差别在枝的性质：
Rauh 的枝是 **orthotropic**（趋上、辐射对称），Massart 的枝**严格 plagiotropic**（扁平背腹）。
所以我松树用的 `spreadAngle 85`（近水平）其实更像 Massart。真正的松（Rauh）枝该更抬起一些。
云杉/冷杉那种平展层次才是 Massart。

**③ 竹 = McClure's model**（p139，例种字面就是 *Bambusa arundinacea*）：
**basitonic 分枝，产生新的、通常在地下的主干**。这就是根状茎（rhizome）。
**这是"竹必须是一丛"的植物学定论**，而且新竿从地下来 —— 印证了 `AUTHORING.md §18` 的多 Trunk。

**④ 真二歧分枝（Schoute's model, p128）在真树里几乎不存在** —— 例种只有棕榈科
（doum palm、nipa palm），因为它要求**顶端分生组织真正等分**。

所以丁香/紫荆/绣球那类"看起来二叉"的，实际是 **Leeuwenberg's model**（p145）：
每模块出**两个或更多**分枝、三维合轴、**顶生花序**。例种是龙血树、蓖麻、木薯。
**机制是"顶生花序终止主轴，迫使侧芽接替"，不是真分叉。**

**⚠️ 这修正了我之前的说法** —— 我原来打算用 `branchCount 2` + `rotateOffset 180` 模拟"二叉"。
方向大致可用，但正确的心智模型是**合轴接力**：每层 `branchCount 2~3`，逐层递归，
且**每个轴都必须终止**（这一点 SlowTree 表达不了，只能靠层数有限来近似）。

**⑤ 垂枝形态 = Champagnat's model**（p238，例种 Bougainvillea）：
全轴 orthotropic，**次生地因重力弯曲**。这正是 SlowTree `gravity` 的语义（朝下 lerp）。
柳树的长枝下垂属于这一类。

**⑥ Troll's model**（p242）：全轴 **plagiotropic，次生地变直立**，常在落叶后发生。
例种：番荔枝、杨桃、**凤凰木**。这是温带阔叶树里最常见的模型之一。

**⑦ Aubréville vs Fagerlind 的差别是"分生组织是否有限"**（原书 p183 图 43，已核对）：
Fagerlind 顶生花序 ⇒ 分生组织**有限**，新叶簇只能长在层的**外缘**；
Aubréville 侧生花序 ⇒ 分生组织**无限**，每个都能继续产叶簇，**光合面积大得多**。

**✅ 可直接用**：这是"叶子只长在枝端外缘"还是"整根枝都有叶"的判据 ——
对应 SlowTree 的 `regionStart` 取高值（0.7+，只外缘）还是常规值（0.2~0.4）。

### 完整判别路径（PDF 页码见 `Halle_models.md`）

```
1 茎不分枝（单轴树）
  ├ 2a 顶生花序 ................ Holttum      （贝叶棕；开花即死）
  └ 2b 侧生花序 ................ Corner       （椰子、油棕、番木瓜）
1 茎分枝（多轴树）
  ├ 3a 各轴等价、多为 orthotropic 且模块化
  │   ├ 4a basitony（基部分枝，常地下）..... Tomlinson （香蕉）
  │   └ 4b acrotony（远端分枝）
  │       ├ 5a 顶端等分二歧 ............... Schoute    （doum palm、nipa palm）
  │       └ 5b 腋生分枝
  │           ├ 6a 每模块一枝，线性合轴 ... Chamberlain（雄苏铁、朱蕉）
  │           └ 6b 每模块两枝以上，三维 ... Leeuwenberg（龙血树、蓖麻、木薯）
  └ 3b 主干与枝明显不同
      ├ 7a 轴异质（orthotropic + plagiotropic 并存）
      │   ├ 8a basitonic，产生地下新干 .... McClure    （竹！）
      │   └ 8b acrotonic
      │       ├ 9a 模块化 + 顶生花序
      │       │   ├ 10a 高生长合轴
      │       │   │   ├ 11a 模块初等后分化 . Koriba     （沙箱树）
      │       │   │   └ 11b 模块自始不等 ... Prévost
      │       │   └ 10b 高生长单轴
      │       │       ├ 12a 节律性 ......... Fagerlind  （四照花）
      │       │       └ 12b 连续 ........... Petit      （棉）
      │       └ 9b 非模块化，花序侧生
      │           ├ 13a 主干为 orthotropic 合轴 . Nozeran（可可）
      │           └ 13b 主干为 orthotropic 单轴
      │               ├ 14a 主干节律性生长
      │               │   ├ 15a 枝 plagiotropic by apposition . Aubréville（榄仁）
      │               │   └ 15b 枝 plagiotropic 非 apposition .. Massart（南洋杉、木棉、肉豆蔻）
      │               └ 14b 主干连续/弥散生长
      │                   ├ 17a 枝长命，不似复叶 ........ Roux（咖啡、巴西栗）
      │                   └ 17b 枝短命、phyllomorphic .... Cook（橡胶）
      └ 7b 轴同质
          ├ 18a 全 orthotropic
          │   ├ 19a 顶生花序（枝合轴）
          │   │   ├ 20a 主干节律性 ......... Scarrone   （芒果、露兜树）
          │   │   └ 20b 主干连续 ........... Stone
          │   └ 19b 侧生花序（枝单轴）
          │       ├ 21a 主干节律性 ......... Rauh       （松！橡胶树）
          │       └ 21b 主干连续 ........... Attims     （红树）
          └ 18b 全混合
              ├ 22a 初生生长即混合（近端直立、远端水平）. Mangenot
              └ 22b 次生变化造成的混合
                  ├ 23a 全 orthotropic，次生因重力弯曲 .. Champagnat（叶子花）
                  └ 23b 全 plagiotropic，次生变直立 ..... Troll（番荔枝、杨桃、凤凰木）
```

**⚠️ 已读范围**：我读了完整判别键（PDF p102–115）并核对了 Aubréville 一章（p201–208）。
其余 22 章的正文**未读**，需要时按 `Halle_models.md` 的页范围取。
上面的例种和判别语来自判别键本身，可信；细节描述要回原书。


---

## 3. 达·芬奇分枝律 —— `radiusScale` 的物理依据

达·芬奇《绘画论》原话：

> "All the branches of a tree at every stage of its height when put together are equal in
> thickness to the trunk"
> （树在任一高度上所有分枝合起来，与树干等粗）

即**横截面积守恒**。一个父枝分成若干子枝时：

```
d_parent² = Σ d_child²          （直径指数 = 2）
```

维基给的几何说法：父枝分两支时，三者直径构成**直角三角形**（勾股关系）。生物力学模拟
与该规律吻合；一种解释是这种几何更能抗强风。

**✅ 可直接用（二叉分枝）**：n 个等粗子枝在**同一点**分出时

```
radiusScale = 1 / √n
n=2 → 0.707    n=3 → 0.577    n=4 → 0.500
```

**一个印证**：HelloTree 的 L1 用 `radiusScale 0.689`，与两分叉的理论值 `1/√2 = 0.707`
**只差 3%**。作者大概是眼调的，但落在物理正确值上。

**⚠️ 注意适用范围**：SlowTree 的 Branch 是「N 根子枝**沿父级轴线分布**」，不是在一点分叉，
父干还继续往上走。所以截面积守恒对**每个附着点**成立，而不是对整层成立 ——
`radiusScale = 1/√branchCount` **对 Classic 模式是错的**（会太细）。
它只对真正的二叉/假二叉（一点分叉）直接适用。

沿轴分布的情况下更实用的读法：`AUTHORING.md §6` 观察到的
「只降一次（0.69 → 0.40）然后贴合（1.0）」——那个 0.69 就是两分叉值，之后子枝已足够细，
不必再逐层收。

---

## 4. 叶序 / 分枝方位角 —— `rotateOffset` 的物种表

叶序（phyllotaxis）用**离顶角**（相邻叶片绕茎的旋转角）描述，常写成整圈的分数。分子分母
通常是斐波那契数及其隔位后继（1,1,2,3,5,8,13）。Bravais 兄弟 1837 年建立了这个联系。

| 分数 | 角度 | 物种 |
|---|---|---|
| 1/2 | **180°** | 二列（distichous），两纵列如扇 |
| 1/3 | **120°** | 山毛榛、榛的直立枝 |
| 2/5 | **144°** | **栎（橡）、杏** |
| 3/8 | **135°** | 向日葵、**杨、梨** |
| 5/13 | **138.46°** | **柳、杏仁** |
| 黄金角 | **137.508°** | SlowTree `rotateOffset` 默认值 |

**✅ 可直接用**：这是一张现成的 `rotateOffset` 物种表。柳树用 138.46°（我们现在填的
137.5，几乎一样，可以不动）；杏/桃属于杏那一支 → **144°**；杨/梨 → **135°**。

排布术语与 SlowTree 的对应：

| 术语 | 含义 | SlowTree |
|---|---|---|
| 互生 / 螺旋 alternate | 每节一叶，逐节旋转 | `rotateOffset 137.5`（默认） |
| 对生 opposite | 同一节两叶，相对 | `rotateOffset 180` |
| **交互对生 decussate** | 相邻的对生叶对**相差 90°** | 需要"每节两枝 + 逐节转 90°"，Classic 表达不了（见下） |
| 轮生 whorled | 同一节多叶同高 | **`mode 6` Interval** + `branchesPerNode` |
| 二列 distichous | 两纵列，扇形 | `rotateOffset 180` + `alternating 1` |

**⚠️ 对不上**：**交互对生**（丁香、绣球、多数木犀科都是）在 SlowTree 里没有直接表达 ——
Classic 是"沿轴按固定角步进"，做不到"成对 + 对间转 90°"。近似做法：`rotateOffset 90`
配 `branchCount` 取偶数，得到近似的四列排布。真要做对需要新的 BranchMode。

---

## 5. 叶的构造 —— 什么时候用 LeafCluster、什么时候用 Spine→Frond

**单叶**（simple）叶片不分裂；**复叶**（compound）叶片完全分裂成小叶（leaflet），
沿主脉或侧脉分离。

关键术语对应：

| 植物学 | 定义（维基原文） | SlowTree |
|---|---|---|
| **叶轴 rachis** | "The middle vein of a compound leaf **or a frond**" | **就是 `Spine` 节点** |
| 小叶 leaflet | 复叶的一个分区 | `Frond` 的锯齿裂片，或 Spine 上的 LeafCluster |
| 叶柄 petiole | 把叶机械连到植株 | Spine 的基部 |

复叶类型：

| 类型 | 小叶排布 | 代表 | SlowTree |
|---|---|---|---|
| **掌状复叶** palmate | 从叶柄顶端**一点**辐射，如手指 | 七叶树、大麻 | LeafCluster，`planar 0`、`clusterRadius` 小 |
| **羽状复叶** pinnate | 沿叶轴两侧排布 | 白蜡（奇数羽状，有顶小叶）、桃花心木（偶数羽状，无顶小叶） | **Spine → Frond**（`serrate` 出裂片） |
| **二回羽状** bipinnate | 二次分裂，小叶长在叶轴的次级轴上 | **合欢** *Albizia* | **Spine → Spine → Frond** |
| 三出复叶 trifoliate | 只三片小叶 | 车轴草、金链花、毒藤 | LeafCluster，`leafCount 3` |

**✅ 可直接用**：
- **`Spine` 在植物学上就是 rachis**（维基对 rachis 的定义直接把 compound leaf 和 frond 并列），
  所以「羽状复叶 → Spine→Frond」不是类比，是同一个东西。水杉那个预设方向是对的。
- **二回羽状 → Spine 挂 Spine**。合欢是中国园林常见树，这是它的正确画法。
- **掌状复叶 ≠ Frond**。七叶树、槭（枫）那类要用 LeafCluster 从一点辐射，不能用叶带。
  这修正了我之前"枫叶要 useCutout"的说法 —— 枫是**单叶掌状裂**，不是掌状复叶，
  但两者都不该用 Frond。

---

## 6. 对现有预设的直接影响

按上面的结论，当前预设有这些可改项（未实施，仅记录）：

| 预设 | 问题 | 依据 |
|---|---|---|
| **竹** | 是单竿，应为**一丛** | §1 顶端优势缺失 → 丛生；`AUTHORING.md §18` 多 Trunk |
| ~~**桃**~~ **已做** | `rotateOffset 144` + Rauh 骨架 + 双 LeafCluster 造花；见 `AUTHORING.md §19` | §2 ① / §4 |
| **杨/梨**（待做） | `rotateOffset` 应为 **135°**（3/8） | §4 |
| **柳** | 理论值 138.46°，现填 137.5，**差异可忽略，不必改** | §4 |
| **合欢**（待做） | 需要 **Spine → Spine → Frond** 二回羽状 | §5 |
| **丁香/绣球**（待做） | 交互对生表达不了，只能近似 | §4 ⚠️ |
| 二叉分枝（待做） | `radiusScale` 用 **1/√n**（两分叉 0.707） | §3 |
| **松** | 枝该更抬起（Rauh 的枝是 orthotropic）；现在 `spreadAngle 85` 更像 Massart/云杉 | §2 ② |
| **凤凰木**（待做） | Troll's model —— 全轴 plagiotropic 后次生直立 | §2 ⑥ |

**不要做的**：
- 不要把 `radiusScale = 1/√branchCount` 套到 Classic 模式的沿轴分布上，那是误用达·芬奇律（§3 ⚠️）。
- 不要按"真二歧分枝"去做丁香/绣球 —— 真二歧只存在于棕榈科（Schoute），
  那些灌木是 Leeuwenberg 的合轴接力（§2 ④）。

---

## 来源

- [Da Vinci branching rule](https://en.wikipedia.org/wiki/Da_Vinci_branching_rule) —— 截面积守恒、直角三角形几何、生物力学验证
- [Branch](https://en.wikipedia.org/wiki/Branch) —— 分枝角度、分形性质
- [Phyllotaxis](https://en.wikipedia.org/wiki/Phyllotaxis) —— 叶序分数表、斐波那契关系、排布术语
- [Apical dominance](https://en.wikipedia.org/wiki/Apical_dominance) —— auxin/IAA 机制、强弱顶端优势的形态后果、去顶效应
- [Leaf](https://en.wikipedia.org/wiki/Leaf) —— 单叶/复叶分类、rachis 定义、各复叶类型代表种
- [Aubreville's model](https://en.wikipedia.org/wiki/Aubreville%27s_model) —— 建筑模型实例、Hallé/Oldeman/Tomlinson 1978 出处
- [Tree allometry](https://en.wikipedia.org/wiki/Tree_allometry) —— 尺度关系（本次未深入）
- [Plant morphology](https://en.wikipedia.org/wiki/Plant_morphology) —— 总览

### 原书

Hallé, Oldeman & Tomlinson, *Tropical Trees and Forests: An Architectural Analysis*,
Springer 1978 —— `Reference/Halle.pdf`（463 页 OCR 扫描）。判别键已提取，见 §2 及
`Docs/reference/`。**23 章正文只核对了 Aubréville 一章，其余按需读。**
