extends SceneTree

# =============================================================================
# AncientBuilding 「民居样板」固定机位截图（before / after 同脚本切换）
#
# 机位与光照全部取自 `ProjectAbyssWiki/documentation/systems/AncientBuilding_Handbooks/05_建筑描述契约.md` §4.4
# 的固定机位表；除本轮新增的 M4 / M4b / M4c（见下方各自理由）外，相机数值**逐字复制**
# 自表里登记的既有实测脚本（AncientBuildingRoofQuality.gd / AncientBuildingQuality.gd /
# AncientBuildingEaveRafters.gd / AncientBuildingGable.gd），一律 fov = 42。
# §4.4 约定：表中坐标是以建筑中心为原点的世界坐标；本脚本按该栋的放置偏移加上去，
# 去掉 offset 之外不改动任何数值（单栋验收取 offset = 0 的等价形式）。
#
# 场景（§4.7 的 PA / PB 预设）：
#   flush    硬山 ROOF_FLUSH_GABLE = 0，gable_decoration 3（排山勾滴 + 山花板）——主对象
#   overhang 悬山 ROOF_OVERHANGING_GABLE = 3，gable_decoration 2（博风板 + 悬鱼惹草）
#   rolled   卷棚 ROOF_ROUND_RIDGE = 4（配 §4.7 的 `_page_246_Picture_3.jpeg` 参考图）；
#            --rolled=0 可关掉
# 三栋共用同一套 §4.7 预设字段：width 9 / depth 6 / bays 3×2 / walls true / fence false /
# steps true / step_style 1 如意 / platform_style 1 阶条石 / wall_style 1 砖缝 /
# rafter_courses 5 / eave_rafter_style 2 / ridge_detail 1 / tile_style 0 / drip_tiles true /
# material_style 0；**相对 PA/PB 只多加一项 `paving true`**，因为「铺地」是 60_台基地面
# 验收参考图（`_page_245_Picture_5.jpeg`）与 20_墙体 R9 的可见判据项。
#
# -----------------------------------------------------------------------------
# 用法（**必须窗口模式，绝不加 --headless**：frame_post_draw 在 headless 下不触发会挂起）
#
# 拍 before（旧参数 = PA 预设 + 新参数全取旧行为值）：
#   ./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game ^
#     --log-file D:/VibeSpace/ProjectAbyss/Reference/Shots/dwelling_before.log ^
#     --script res://Develop/AncientBuildingDwelling.gd --resolution 1280x720 -- ^
#     --capture --tag=before --dado=0 --dado-trim=0 --platform-top-joints=0 ^
#     --platform-edge-lip=0 --paving-joint-geometry=0 --column-base=0
#
# 拍 after（民居参数；DLL 里没有的参数会被守卫跳过并打印在 skipped 列表里）：
#   ./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game ^
#     --log-file D:/VibeSpace/ProjectAbyss/Reference/Shots/dwelling_after.log ^
#     --script res://Develop/AncientBuildingDwelling.gd --resolution 1280x720 -- ^
#     --capture --tag=after --dado=0.25 --dado-trim=1 --platform-top-joints=1 ^
#     --platform-edge-lip=1 --paving-joint-geometry=1 --column-base=0.35
#
# 合成并排对比（左参考图 / 右渲染图，见 Develop/Tools/DwellingCompare.gd）：
#   ./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --headless ^
#     --script res://Develop/Tools/DwellingCompare.gd -- --all --tag=after
#
# 出图：Reference/Shots/AncientBuildingQuality/dwelling/<机位>_<对象>_<tag>.png
#   （对象的三种取值 flush / overhang / rolled；M1b 另有一张 group = 硬山+悬山同框；
#     M3_ridge / M4c 只拍悬山那栋 —— 博风板/悬鱼/惹草属 GABLE_DECOR_BARGE_AND_FISH 档）
# -----------------------------------------------------------------------------
# 参数（`--` 之后的 user args；全部可选）：
#   --capture            拍图并退出；不给则只建场景交互查看
#   --tag=NAME           文件名后缀，惯例 before / after（默认 after）
#   --rolled=0|1         是否建卷棚那栋（默认 1）
#   ---- 以下新参数按 `ProjectAbyssWiki/documentation/systems/.../20_墙体.md` §3 与 `60_台基地面.md` §3 的登记名，
#   缺失时用 `if "prop" in p:` 守卫跳过（照 AncientBuildingFrameSystems.gd:57 的写法），
#   不会崩；哪些生效 / 哪些被跳过会在 stdout 里逐项打印。
#   --dado=FLOAT                 DadoHeightRatio（下碱高 ÷ 墙高；旧行为 0 = 砖缝满墙）
#   --dado-trim=0|1              DadoTopTrim（上下区分界带；旧行为 off）
#   --platform-top-joints=0|1    PlatformTopJoints（台基顶面接缝网格；旧行为 off）
#   --platform-edge-lip=0|1      PlatformEdgeLip（台基边缘沿口；旧行为 off = 直落）
#   --paving-joint-geometry=0|1  PavingJointGeometry（铺地几何板缝；旧行为 off = 顶点色当缝）
#   --column-base=FLOAT          ColumnBaseHeightScale（柱础高 ÷ D；**该参数 DLL 已存在**）
#   --paving=0|1                 paving（铺地开关，既有参数）
#   --tile-detail=0|1|2          TileDetail（30_瓦作 瓦作细度；0 = 旧行为 = 现状）
#   --bedding=FLOAT              TileBeddingThickness（泥背厚/米；旧行为 0）
#   --lod=0|1|2                  LODLevel（ 距离档；0 = 近景 = 现状，逐字节相同的基线）
#   --pyramid=0|1                额外建一栋攒尖亭（四角攒尖），给 LOD 三角数表凑齐"三栋民居 + 攒尖类"；
#                                默认 0，故默认出图集与机位逐字不变
#   --ridge-cams=0|1             额外拍 M7 正脊近景 / M8 山面檐口收头（30_瓦作 判据 C：脊与瓦面接口）；
#                                默认 0，故默认出图集不变
#   --section-cams=0|1           额外拍 M9 正脊端断面 / M10 垂脊檐口端（50_脊饰 §1.1 判据 1/2：
#                                脊断面三级台阶 + 脊饰落座）；默认 0，故默认出图集不变
#   --ridge-detail=0|1           脊断面（50_脊饰 §3）：0 = 旧 7 点断面（对照基线）/ 1 = 分层断面
#                                （当沟条 + 脊身 + 盖脊筒）。**默认走脚本预设 = 1**
#   --ridge-ornaments=0|1        脊饰总开关（50_脊饰 R1-R4）：**默认 0 = 民居不长脊饰**
#   --finial-size=FLOAT          正吻占位块边长 ÷ D（默认 0.90）
#   --beast-size=FLOAT           垂兽 ÷ D（默认 0.50）
#   --walker-size=FLOAT          走兽 ÷ D（默认 0.26）
#   --walker-count=INT           每条垂脊的走兽数（默认 3）
#   --ornament-mesh=KIND:SHAPE   把某一类脊饰的占位 cube 换成内置图元，证明参数槽可替换：
#                                KIND ∈ finial/beast/walker，SHAPE ∈ box/sphere/cylinder/prism/capsule
# `ColumnBaseHeightScale` 是**已有参数**（AncientBuildingParameters.h:205，默认 0），
# 其余五个按 20_/60_ 手册本次核码为「全不存在」，要到 C++ 侧落地后才会生效。
#
# 瓦作三档（30_瓦作 §2）：
#   before 档（旧瓦面）：--tile-detail=0
#   排垄+叠压档：    --tile-detail=1
#   全档（檐口件+泥背）：--tile-detail=2 --bedding=0.03
# =============================================================================

const FOV := 42.0
const OUT_DIR := "D:/VibeSpace/ProjectAbyss/Reference/Shots/AncientBuildingQuality/dwelling"

# ---- 05 §4.4 固定机位表（建筑置于原点；实测出处写在注释里）----
const CAM_M1 := {"pos": Vector3(1.0, 8.5, 12.0), "look": Vector3(0.0, 5.0, 3.0)}          # RoofQuality.gd:77-78
const CAM_M1B := {"pos": Vector3(20.0, 13.0, 32.0), "look": Vector3(0.0, 3.0, 0.0)}        # Quality/RoofQuality/EaveRafters 三脚本一致
const CAM_M2_UNDER := {"pos": Vector3(0.0, 3.2, 8.0), "look": Vector3(0.0, 6.6, 2.5)}      # EaveRafters.gd:73-74
const CAM_M2_CLOSE := {"pos": Vector3(0.4, 4.6, 6.8), "look": Vector3(0.1, 6.9, 3.6)}      # EaveRafters.gd:81-82
const CAM_M2_CORNER := {"pos": Vector3(9.5, 3.6, 9.5), "look": Vector3(3.0, 6.4, 3.0)}     # EaveRafters.gd:89-90
const CAM_M3 := {
	"flush": {"pos": Vector3(15.0, 11.0, 8.5), "look": Vector3(4.5, 8.5, 0.0)},            # Gable.gd:106-107
	# 卷棚在 bb:3924 走「悬山式出挑」分支，故 M3 沿用悬山那一组的参数。
	"overhang": {"pos": Vector3(16.0, 11.0, 9.0), "look": Vector3(5.5, 8.5, 0.0)},          # Gable.gd:90-91
}
const CAM_M3_RIDGE := {"pos": Vector3(12.5, 10.2, 1.2), "look": Vector3(6.0, 9.6, 0.0)}    # Gable.gd:98-99（悬鱼）

# ---- M4 / M4b：本轮新增（§4.4 表里没有民居整栋机位），参数自定，理由如下 ----
#
# M4 民居整栋（与图鉴参考图并排用）：**三分之四侧视**，方位角 45°。
#   为什么是 45°：正立面（含踏步）朝 +Z、山面（含抹灰/下碱带/博风板/排山勾滴）朝 ±X
#   （bb:3712-3713 / bb:3664-3665），45° 是唯一能**同时**看清这两个面的角度；
#   而默认 sun（rotation (-25,15,0)，光行进方向 ≈ (-0.23,-0.42,-0.88)）正好从 +X/+Z 打来，
#   这两个面都在受光侧，抹灰 vs 下碱的明暗差异与台基顶面接缝才读得出来。
#   仰角 ≈ 14.5°：太低看不到台基顶面与铺地网格，太高变成鸟瞰、山墙立面被压扁。
#   距离：PA 预设（width 9 / depth 6）D = 9×0.8/11 = 0.6545 m →
#     台基 2D=1.31 / 檐高 11D=7.2 / 铺作 0.85D=0.556 → 屋顶基准 7.756 / 举架高
#     1.3×6/((5−1)×0.5)=3.9 → 总高 ≈ 11.66 m；屋面出檐 2.6D=1.70，踏步再向 ±Z 外伸
#     1.1×3.2D ≈ 2.30 m（h:507-511）。
#   相机 (21.0, 12.0, 21.0) → (0, 5.4, 0)：斜距 ≈ 30.4 m，42° 竖直 FOV 下可见高度
#     ≈ 2×30.4×tan21° ≈ 23.3 m；实测（脚本自带的 frame= 打印）该栋 13.06×12.82×12.10 的
#     AABB 占画面高约 3/4、宽约 4/10 —— 竖着框得住整栋（含台基前的铺地），横着的空档
#     正好让并排图里「参考图 : 渲染图」的楼身宽度接近 1:1。目标高度 5.4 m 取在台基顶
#     (1.31) 与檐口(7.2)之间偏上，给近侧地面/铺地留出下边距。
#   注：本机位是「整栋比例 + 表面分区」的比较位，**不用于判细节可见性**（§4.2 三层验收）。
const CAM_M4 := {"pos": Vector3(21.0, 12.0, 21.0), "look": Vector3(0.0, 5.4, 0.0)}

# M4b 山面近景（§4.2 的近景层，视距 ≈ 9.7 m，落在 5–10 m 区间）：给
# 悬鱼 / 惹草 / 博风板（40_山花 的三件）与排山勾滴做并排对比用 —— M4 的 30 m 视距
# 下这些件的形制判不了。方位角约 25°（不是纯正投影），博风板的下缘线脚与板厚才看得出
# 立体；瞄准点 (4.9, 9.3, 0) 取在檐口(7.2)与脊(≈12.8)之间，可见高度 ≈ 7.4 m，
# 刚好把整块山面（含脊心的悬鱼）收进画面。
const CAM_M4B := {"pos": Vector3(12.5, 9.3, 6.0), "look": Vector3(4.9, 9.3, 0.0)}

# M4c 博风板构件微距（只拍悬山那栋）：给惹草（卷草浮雕件 + 中央开光）与博风板下缘线脚
# 做并排对比用。M4b 的 9.7 m 视距下，一枚惹草只有 ~25 px，判不了纹样（§4.1 的像素换算：
# 720p / 9.7 m 下 1 px ≈ 10 mm）。40_山花 把惹草挂在博风板路径的 0.45 / 0.72 两处，
# 博风板本身在 (Z,Y) 平面内从檐口 (Z≈4.7, Y≈7.2) 斜到脊 (Z≈0, Y≈11.7)，故 t=0.45 的
# 站位约在 (X≈5.6, Y≈9.2, Z≈2.5)。相机退到 3.7 m（可见高度 ≈ 2.8 m），
# 一枚约 0.3 m 的惹草占画面高 ~11% ⇒ 720p 下 ~78 px，够判「卷草 + 中央开光」的形制。
const CAM_M4C := {"pos": Vector3(8.8, 9.7, 4.3), "look": Vector3(5.7, 9.2, 2.4)}

# ---- M5 / M6：本轮新增（30_瓦作 判据 A/B/C 与自检 12 的檐口/叠压对照位）----
# 只在 `--tile-cams=1` 时使用，默认出图路径与机位**逐字不变**。
#
# 既有的 §4.4 机位表里没有「看得见瓦面」的位置：M2 三兄弟瞄的是檐下椽飞（P4 的主题），
# 瓦当/滴水只在画面最上缘扫到一点点；M1/M4 的 12–30 m 视距下，一枚 0.126 m 的瓦当只有
# 几像素。判据 A（瓦件剖面）与自检 12（瓦当端面圆盘 / 滴水裙边 / 叠压节奏）都需要更近的
# 正视檐口，故新增两个机位；两者都按 §4.1 的像素换算定距（0.5 px 弦误差预算）：
#   720p / fov 42° 下 f_px = 720/(2·tan21°) ≈ 937.8；
#   M5 距离 2.0 m ⇒ 1 px ≈ 2.1 mm：瓦当直径 0.126 m ≈ 59 px（够判「筒瓦体 + 端面圆盘」），
#   滴水裙边 0.29 m ≈ 137 px（够判宽裙边与下垂）。
#   M6 距离 3.3 m ⇒ 1 px ≈ 3.5 mm：叠压节奏周期 q = 0.48·p = 0.171 m ≈ 49 px（够数）。
#
# M5 檐口正近景：站在檐口外 2 m、比檐口低 0.6 m 处仰视——瓦当的端面圆盘正对镜头，
# 滴水裙边在瓦当之间下垂，檐口断面的「连檐→滴水→瓦当」层次就是这个角度读的。
# 目标点取檐口线上方 0.3 m，避免画面被台基/柱子占满。
const CAM_M5_EAVE := {"pos": Vector3(0.0, 6.6, 6.7), "look": Vector3(0.0, 7.5, 4.2)}
# M6 沿坡擦视：同样在檐外，但视点抬高到檐口略下方、沿屋面坡度方向平视——叠压台阶面朝下坡，
# 只有这个方向能看到它们的「逐片叠压」节奏（正上方俯视会把台阶压没）。
const CAM_M6_SLOPE := {"pos": Vector3(0.6, 6.35, 7.6), "look": Vector3(0.2, 7.45, 4.2)}

# ---- M9 / M10：第三轮第二批新增（50_脊饰 §1.1 判据 1/2：脊断面层次与脊饰落座）----
# 只在 `--section-cams=1` 时使用，默认出图路径与机位逐字不变。
#
# 「脊断面不再像半圆棒」只有**沿脊轴正视断面**才读得出来：M7 是斜上方看脊身，三级台阶被压扁。
#   M9 正脊端断面：沿 +X 轴正视正脊端头。悬山那栋脊端在 x = 5.743（山墙 4.5，脊挑出到 5.74），
#     硬山那栋脊端与山墙齐平在 4.5 —— 同一机位两栋都看得到：从 (7.8, 11.95, 0) 朝 (5.0, 11.85, 0)
#     的射线正好穿过两个脊端。视距 ≈ 2.8 m ⇒ 720p / fov 42° 下 1 px ≈ 3.0 mm
#     （f_px = 720/(2·tan21°) ≈ 937.8），断面 0.88 m 宽 ≈ 293 px、0.75 单位高 ≈ 220 px，
#     三级各占 0.16 / 0.28 / 0.31 单位 ⇒ 数十像素，够判「当沟条 / 脊身 / 盖脊筒」。
#     高度：屋面基准 RoofBase = 11D + 0.85D = 7.756，举架高 1.3×6/((5−1)×0.5) = 3.9 ⇒ 脊心 11.656；
#     瞄准点取 11.85，画面竖直可见高 ≈ 1.53 m，断面与正吻都收得进。
#   M10 垂脊断面 / 檐口端：悬山与卷棚的垂脊在 x = ±5.743（硬山 ±4.5），沿坡从檐口(7.756)升到脊。
#     站在檐外 45° 侧前方 (6.6, 8.6, 7.4) 瞄 (5.743, 8.15, 4.55)：视距 ≈ 3.0 m ⇒ 1 px ≈ 3.2 mm，
#     垂兽占位块 0.5D = 0.327 m ≈ 100 px、走兽 0.26D = 0.170 m ≈ 53 px，够判「坐落」与成列。
# 坐标是建筑自身的局部坐标（`_place` 会把该栋的放置偏移加回去）。
const CAM_M9_RIDGE_END := {"pos": Vector3(8.4, 12.6, 1.9), "look": Vector3(5.744, 12.0, 0.1)}
const CAM_M10_VERGE_END := {"pos": Vector3(7.0, 8.45, 6.6), "look": Vector3(5.744, 8.2, 4.62)}

# ---- M7 / M8：本轮新增（30_瓦作 判据 C：脊/博风板与瓦面的接口）----
# 只在 `--ridge-cams=1` 时使用，默认出图路径与机位逐字不变。
#
# M5/M6 看的是檐口（瓦当/滴水/叠压），看不到**脊与瓦面的接口**；判据 C 要看的是：脊的底面与
# 抬升后的瓦面之间有没有缝、有没有穿出、山面有没有没裁掉的瓦件伸出。故新增两个机位：
#   M7 正脊近景：站在檐外、比檐高 2.2 m 处沿坡向上看脊——脊的底面与两侧瓦垄的交线就是这个
#     角度读的（**从山面外侧看不行：硬山的山墙正好挡在那条视线后面**）。距离 ≈ 4.9 m ⇒
#     1 px ≈ 5.3 mm（720p / fov 42°），泥背 0.03 m ≈ 6 px，够判「陷进去/悬空」。
#   M8 山面收头：斜看山面边界（垂脊）与最外一垄瓦的关系，距离 ≈ 3.1 m ⇒ 1 px ≈ 3.3 mm；
#     判据是「瓦件不得伸出屋面轮廓」，即最外一件瓦（滴水裙边 0.42p ≈ 0.145 m）应落在垂脊的
#     覆盖带里、而不是越出山面平面。
# 坐标是建筑自身的局部坐标（`_place` 会把该栋的放置偏移加回去）。硬山山面在 x=±4.5，
# 悬山/卷棚的屋顶出挑后到 ±5.74，故 M8 取 x≈5.0 一档，三栋都能同框判读。
const CAM_M7_RIDGE := {"pos": Vector3(0.6, 10.0, 5.0), "look": Vector3(0.5, 11.5, 0.3)}
const CAM_M8_VERGE := {"pos": Vector3(7.4, 10.6, 4.1), "look": Vector3(5.0, 9.7, 2.4)}

# 05 §4.4 / AncientBuildingGable.gd:25-42 登记的光照与环境（三个脚本一致）
const ENV_BG := Color(0.12, 0.15, 0.19)
const ENV_AMBIENT := Color(0.85, 0.87, 0.92)
const ENV_AMBIENT_ENERGY := 0.4
const SUN_ROT := Vector3(-25, 15, 0)
const SUN_ENERGY := 1.2
const FILL_ROT := Vector3(38, -95, 0)
const FILL_ENERGY := 0.55
const FLOOR_SIZE := Vector2(120, 120)
const FLOOR_ALBEDO := Color(0.32, 0.34, 0.36)

# 主体：key -> 放置偏移 / 形制。roof_type 值取自 h:38-58 的 ERoofType。
const SUBJECTS := [
	{"key": "flush", "x": -9.0, "roof_type": 0, "gable_decoration": 3, "cam3": "flush",
		"note": "硬山 民居"},
	{"key": "overhang", "x": 9.0, "roof_type": 3, "gable_decoration": 2, "cam3": "overhang",
		"note": "悬山"},
	{"key": "rolled", "x": -27.0, "roof_type": 4, "gable_decoration": 0, "cam3": "overhang",
		"note": "卷棚"},
]

# CLI 名 -> [AncientBuildingParameters 属性名, 类型]。属性名 = C++ 成员的 snake_case
# （与已存在的 `column_base_height_scale` 同规则，AncientBuildingParameters.cpp:230）。
const OVERRIDES := {
	"dado": ["dado_height_ratio", "float"],
	"dado-trim": ["dado_top_trim", "float"],   # 单位 = 模数 D；2026-09-26 由 bool 改 float，与实现一致
	"platform-top-joints": ["platform_top_joints", "bool"],
	"platform-edge-lip": ["platform_edge_lip", "bool"],
	"paving-joint-geometry": ["paving_joint_geometry", "bool"],
	"column-base": ["column_base_height_scale", "float"],
	"paving": ["paving", "bool"],
	"step-side-cheek": ["step_side_cheek", "bool"],       # 2026-09-26 新增
	"column-base-square": ["column_base_square", "bool"], # 2026-09-26 新增
	"platform": ["generate_platform", "bool"],            # 台基有无（地基）；默认 true
	# 30_瓦作 ：瓦作细度档（0 旧行为 / 1 排垄与叠压 / 2 +檐口件与泥背）与泥背厚（米）
	"tile-detail": ["tile_detail", "int"],
	"bedding": ["tile_bedding_thickness", "float"],
	#  距离档：0 近景（= 现状）/ 1 中景（半剖面数 + 叠压台阶合并为一段斜面）/ 2 远景（单层瓦面 + 檐口条 + 脊占位块）
	"lod": ["lod_level", "int"],
	# 50_脊饰 （2026-09-27）：脊断面档与脊饰开关 / 尺寸。脊断面 0 = 旧 7 点断面（对照基线），
	# 1 = 分层断面；脊饰默认关（民居不长脊饰），开了才有占位 cube。
	"ridge-detail": ["ridge_detail", "int"],
	"ridge-ornaments": ["ridge_ornaments", "bool"],
	"finial-size": ["ridge_finial_size", "float"],
	"beast-size": ["ridge_beast_size", "float"],
	"walker-size": ["ridge_walker_size", "float"],
	"walker-count": ["ridge_walker_count", "int"],
}

# 脊饰 mesh 槽的替换演示（只在 `--ornament-mesh=` 给出时生效）：把某一类脊饰的占位 cube
# 换成 Godot 内置图元，用来证明「参数槽可替换任何 mesh」而不是只换尺寸。
const ORNAMENT_KINDS := {"finial": 0, "beast": 1, "walker": 2}

var camera: Camera3D
var root_3d: Node3D
var buildings: Array[Node3D] = []
var subjects: Array = []
var args := {}
var override_ops: Array = []      # [prop_name, value]
var applied: Array[String] = []
var skipped: Array[String] = []


func _initialize() -> void:
	args = _parse_args()
	if DisplayServer.get_name() == "headless":
		push_error("AncientBuildingDwelling: 必须窗口模式运行 —— frame_post_draw 在 headless 下不触发，会挂起。去掉 --headless 重跑。")
		quit(1)
		return
	call_deferred("build")


func _parse_args() -> Dictionary:
	var out := {}
	for arg in OS.get_cmdline_user_args():
		if not arg.begins_with("--"):
			continue
		var body := arg.substr(2)
		var eq := body.find("=")
		if eq == -1:
			out[body] = "1"
		else:
			out[body.substr(0, eq)] = body.substr(eq + 1)
	return out


func _arg_str(key: String, fallback: String) -> String:
	return str(args[key]) if args.has(key) else fallback


func _arg_bool(key: String, fallback: bool) -> bool:
	if not args.has(key):
		return fallback
	var v := str(args[key]).to_lower()
	return v == "1" or v == "true" or v == "yes" or v == "on"


func _arg_float(key: String, fallback: float) -> float:
	return float(str(args[key])) if args.has(key) else fallback


func _arg_int(key: String, fallback: int) -> int:
	return int(str(args[key])) if args.has(key) else fallback


func build() -> void:
	root_3d = Node3D.new()
	root.add_child(root_3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = ENV_BG
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = ENV_AMBIENT
	env.environment.ambient_light_energy = ENV_AMBIENT_ENERGY
	root_3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = SUN_ROT
	sun.light_energy = SUN_ENERGY
	sun.shadow_enabled = true
	root_3d.add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = FILL_ROT
	fill.light_energy = FILL_ENERGY
	fill.shadow_enabled = false
	root_3d.add_child(fill)
	var floor_node := MeshInstance3D.new()
	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = FLOOR_SIZE
	floor_node.mesh = floor_mesh
	var floor_mat := StandardMaterial3D.new()
	floor_mat.albedo_color = FLOOR_ALBEDO
	floor_mesh.material = floor_mat
	root_3d.add_child(floor_node)

	var want_rolled := _arg_bool("rolled", true)
	# 攒尖亭（可选）：只为 LOD 三角数表凑"攒尖类"一格，默认不建，故默认出图集不变。
	var extra: Array = []
	if _arg_bool("pyramid", false):
		extra.append({"key": "pyramid", "x": 27.0, "roof_type": 5, "gable_decoration": 0,
			"cam3": "flush", "sides": 4, "note": "攒尖亭"})
	print("=== AncientBuildingDwelling  tag=%s  fov=%.1f ===" % [_arg_str("tag", "after"), FOV])
	_collect_overrides()
	_report_overrides()
	for subject in SUBJECTS + extra:
		if subject["key"] == "rolled" and not want_rolled:
			continue
		subjects.append(subject)
		buildings.append(_make_building(subject, _make_parameters(subject)))

	camera = Camera3D.new()
	camera.fov = FOV
	root_3d.add_child(camera)
	camera.current = true
	if _capture_requested():
		await capture()


## 存在才设：P5 时代的参数名在 2026-09-26 回滚后已从 98b96ae 删除，
## 直接赋值会让 `_make_parameters` 运行时报错并中断整个脚本。
## 用 `in` 守卫后，缺失的档位被静默跳过，脚本在 P4 与未来版本上都能跑。
func _set_if(p: Resource, prop: String, value) -> void:
	if prop in p:
		p.set(prop, value)


func _make_parameters(subject: Dictionary) -> Resource:
	var p = ClassDB.instantiate("AncientBuildingParameters")
	# §4.7 的民居预设（PA 硬山 / PB 悬山 / 卷棚同基座）
	p.width = 9.0
	p.depth = 6.0
	p.bays_x = 3
	p.bays_z = 2
	p.roof_type = subject["roof_type"]
	p.generate_walls = true
	p.generate_fence = false
	p.generate_steps = true
	p.rafter_courses = 5
	p.eave_rafter_style = 2   # 檐椽头 + 飞椽头 + 连檐
	p.material_style = 0      # 官式配色
	# 形制档位写入：下列参数名属 P5 五子任务，**随 2026-09-26 回滚已从 98b96ae 删除**。
	# 逐项守卫（存在才设），使同一脚本在 P4 基线与未来重建版本上都能跑：
	# 缺失时静默跳过 —— 该档位今天表达不出来，属已知缺口，不是脚本错误。
	_set_if(p, "gable_decoration", subject["gable_decoration"])
	_set_if(p, "step_style", 1)          # 如意踏跺（P5 名，待重建）
	_set_if(p, "platform_style", 1)      # 阶条石（P5 名，待重建）
	_set_if(p, "wall_style", 1)          # 砖缝分块（P5 名，待重建）
	_set_if(p, "ridge_detail", 1)
	_set_if(p, "tile_style", 0)          # 筒瓦
	_set_if(p, "drip_tiles", true)
	_set_if(p, "paving", true)           # 方砖铺地：图鉴参考图里是可见项
	if subject.has("sides"):
		p.sides = subject["sides"]
	for op in override_ops:
		p.set(op[0], op[1])
	return p


func _collect_overrides() -> void:
	# 守卫：新参数可能还没编译进 DLL（20_/60_ 手册本次核码为零命中）。
	# 照 AncientBuildingFrameSystems.gd:57 的 `if "prop" in p:` 写法，缺失就跳过而不是崩。
	# 用一次性探针实例做存在性判定，判定结果对三栋建筑复用（否则三栋会把同一份
	# applied/skipped 名单重复打印三遍）。
	var probe = ClassDB.instantiate("AncientBuildingParameters")
	for key in OVERRIDES:
		if not args.has(key):
			continue
		var prop: String = OVERRIDES[key][0]
		var kind: String = OVERRIDES[key][1]
		if not (prop in probe):
			skipped.append("%s(%s)" % [key, prop])
			continue
		var value = _arg_bool(key, false) if kind == "bool" \
			else (_arg_int(key, 0) if kind == "int" else _arg_float(key, 0.0))
		override_ops.append([prop, value])
		applied.append("%s(%s)=%s" % [key, prop, str(value)])


func _report_overrides() -> void:
	if applied.is_empty() and skipped.is_empty():
		print("overrides: (none) —— 全部走 DLL 默认值")
		return
	print("overrides applied: %s" % ("; ".join(applied) if not applied.is_empty() else "(none)"))
	print("overrides SKIPPED (该 DLL 里没有此属性，本次形状不受它影响): %s" %
		("; ".join(skipped) if not skipped.is_empty() else "(none)"))


## `--ornament-mesh=KIND:SHAPE`：把 KIND（finial / beast / walker）的脊饰换成内置图元
## SHAPE（box / sphere / cylinder / prism / capsule）。不替换的类仍走生成器的 cube 占位。
## 生成器按「mesh 自身包围盒的底面中心」落座（`AddPlacedTriangles`），所以图元不必改 pivot：
## 不管内置图元的原点在中心还是在底部，它的底面都会贴在与占位 cube 底面相同的位置。
func _apply_ornament_mesh(building: Node3D) -> void:
	var spec := _arg_str("ornament-mesh", "")
	if spec == "":
		return
	var parts := spec.split(":")
	if parts.size() != 2 or not ORNAMENT_KINDS.has(parts[0]):
		push_warning("--ornament-mesh 用法: KIND:SHAPE，KIND ∈ %s，SHAPE ∈ box/sphere/cylinder/prism/capsule"
			% str(ORNAMENT_KINDS.keys()))
		return
	var mesh: Mesh
	match parts[1]:
		"sphere":
			mesh = SphereMesh.new()
		"cylinder":
			mesh = CylinderMesh.new()
		"prism":
			mesh = PrismMesh.new()
		"capsule":
			mesh = CapsuleMesh.new()
		_:
			mesh = BoxMesh.new()
	var kind: int = ORNAMENT_KINDS[parts[0]]
	building.set_ridge_ornament_mesh(kind, mesh)
	print("ornament mesh swap: %s -> %s (kind=%d), triangles/instance=%d" %
		[parts[0], parts[1], kind, mesh.get_faces().size() / 3])


func _make_building(subject: Dictionary, p: Resource) -> Node3D:
	var building = ClassDB.instantiate("AncientBuilding")
	building.parameters = p
	building.position = Vector3(subject["x"], 0.0, 0.0)
	root_3d.add_child(building)
	_apply_ornament_mesh(building)
	print("%-9s %-14s roof_type=%d gable_decoration=%d vertices=%d triangles=%d" %
		[subject["key"], subject["note"], subject["roof_type"], subject["gable_decoration"],
		building.get_vertex_count(), building.get_triangle_count()])
	return building


func _capture_requested() -> bool:
	return "--capture" in OS.get_cmdline_user_args() or args.has("capture")


func show_only(indices: Array) -> void:
	for i in buildings.size():
		buildings[i].visible = i in indices


func _place(subject: Dictionary, cam: Dictionary) -> void:
	var offset := Vector3(subject["x"], 0.0, 0.0)
	camera.position = (cam["pos"] as Vector3) + offset
	camera.look_at((cam["look"] as Vector3) + offset)


func capture() -> void:
	var tag := _arg_str("tag", "after")
	DirAccess.make_dir_recursive_absolute(OUT_DIR)

	# --tile-cams=1：只拍瓦作新增的两个机位（M5 檐口正近景 / M6 沿坡擦视），
	# 用来做 30_瓦作 判据 A 与自检 12 的对照图；不给这个开关时本函数与旧版逐字相同。
	if _arg_bool("tile-cams", false):
		for i in subjects.size():
			var subject: Dictionary = subjects[i]
			show_only([i])
			await _shot(subject, CAM_M5_EAVE, "M5_eave_%s_%s" % [subject["key"], tag])
			await _shot(subject, CAM_M6_SLOPE, "M6_slope_%s_%s" % [subject["key"], tag])
		print("tile-cams done -> %s" % OUT_DIR)
		quit()
		return

	# --section-cams=1：第三轮第二批的两个断面近景（M9 正脊端 / M10 垂脊檐口端）。
	# 与 tile-cams / ridge-cams 同理：不给这个开关时下面这段不执行，默认出图集与旧版逐字相同。
	# 断面图的有效性靠 tag 区分档位（--ridge-detail=0 是旧 7 点断面的对照基线，
	# --ridge-detail=1 是新分层断面；轴测/俯视机位都读不出三级台阶，只有正视端头才行）。
	if _arg_bool("section-cams", false):
		for i in subjects.size():
			var subject: Dictionary = subjects[i]
			show_only([i])
			await _shot(subject, CAM_M9_RIDGE_END, "M9_ridge_end_%s_%s" % [subject["key"], tag])
			await _shot(subject, CAM_M10_VERGE_END, "M10_verge_end_%s_%s" % [subject["key"], tag])
		print("section-cams done -> %s" % OUT_DIR)
		quit()
		return

	# --ridge-cams=1：判据 C 的两个近景（脊/瓦面接口、山面收头）。与 tile-cams 同理，
	# 不给这个开关时下面这段不执行，默认出图集与旧版逐字相同。
	if _arg_bool("ridge-cams", false):
		for i in subjects.size():
			var subject: Dictionary = subjects[i]
			show_only([i])
			await _shot(subject, CAM_M7_RIDGE, "M7_ridge_%s_%s" % [subject["key"], tag])
			await _shot(subject, CAM_M8_VERGE, "M8_verge_%s_%s" % [subject["key"], tag])
		print("ridge-cams done -> %s" % OUT_DIR)
		quit()
		return

	for i in subjects.size():
		var subject: Dictionary = subjects[i]
		show_only([i])
		# ---- M1 近景：装配 / 砖缝 / 瓦剖面 ----
		await _shot(subject, CAM_M1, "M1_%s_%s" % [subject["key"], tag])
		# ---- M1b 总览（单栋，机位与既有脚本同值）----
		await _shot(subject, CAM_M1B, "M1b_%s_%s" % [subject["key"], tag])
		# ---- M2 檐下 / 檐口（三个登记子机位）----
		await _shot(subject, CAM_M2_UNDER, "M2_underside_%s_%s" % [subject["key"], tag])
		await _shot(subject, CAM_M2_CLOSE, "M2_closeup_%s_%s" % [subject["key"], tag])
		await _shot(subject, CAM_M2_CORNER, "M2_corner_%s_%s" % [subject["key"], tag])
		# ---- M3 侧面（硬山/悬山两组登记参数之一）----
		await _shot(subject, CAM_M3[subject["cam3"]], "M3_%s_%s" % [subject["key"], tag])
		# ---- M4 民居整栋（新增，与图鉴参考图并排）----
		await _shot(subject, CAM_M4, "M4_%s_%s" % [subject["key"], tag])
		# ---- M4b 山面近景（新增，悬鱼/惹草/博风板/排山勾滴并排）----
		await _shot(subject, CAM_M4B, "M4b_%s_%s" % [subject["key"], tag])
		if subject["key"] == "overhang":
			# 以下两个只有悬山那栋有意义：博风板 / 悬鱼 / 惹草都是
			# GABLE_DECOR_BARGE_AND_FISH(2) 这一档的构件，硬山那栋（decor 3）没有博风板。
			# 悬山脊心特写：悬鱼挂在博风板交汇点正下方，只有这个机位看得见。
			await _shot(subject, CAM_M3_RIDGE, "M3_ridge_%s_%s" % [subject["key"], tag])
			# 博风板构件微距：惹草纹样与下缘线脚。
			await _shot(subject, CAM_M4C, "M4c_%s_%s" % [subject["key"], tag])

	# ---- M1b 总览同框：硬山 + 悬山同时可见，offset 取 0（= §4.4 登记的原场景形态）----
	var group: Array = []
	for i in subjects.size():
		if subjects[i]["key"] != "rolled":
			group.append(i)
	if group.size() > 0:
		show_only(group)
		camera.position = CAM_M1B["pos"]
		camera.look_at(CAM_M1B["look"])
		await _save(OUT_DIR.path_join("M1b_group_%s.png" % tag))

	print("capture done -> %s" % OUT_DIR)
	quit()


func _shot(subject: Dictionary, cam: Dictionary, filename: String) -> void:
	_place(subject, cam)
	await _save(OUT_DIR.path_join("%s.png" % filename))
	_report_framing(subject, cam, filename)


func _report_framing(subject: Dictionary, cam: Dictionary, label: String) -> void:
	# 打印该栋 AABB 在画面里的投影占比，用来核对「机位是否还框得住」——
	# 改了 width/depth/rafter_courses 之后，先看这行的 out= 再判图。
	# 整栋机位（M1b/M4）报 frame= 占比；近景机位（M1/M2/M3/M4b）本来就只看局部，
	# AABB 远大于画面、四角还可能落到相机背后，那几行只报 aabb + dist，不报占比。
	var index: int = subjects.find(subject)
	var box: AABB = buildings[index].get_mesh().get_aabb()
	var origin := Vector3(subject["x"], 0.0, 0.0)
	var prefix := "  %-30s aabb=%.2f x %.2f x %.2f" % [label, box.size.x, box.size.y, box.size.z]
	var dist := camera.position.distance_to((cam["look"] as Vector3) + origin)
	var viewport: Vector2 = root.size
	if viewport.x <= 0.0 or viewport.y <= 0.0:
		return
	var corners: Array[Vector3] = []
	for corner in 8:
		var world := box.position + Vector3(
			box.size.x * float(corner & 1),
			box.size.y * float((corner >> 1) & 1),
			box.size.z * float((corner >> 2) & 1)) + origin
		if camera.is_position_behind(world):
			print("%s  dist=%.1fm  (近景特写：机位在 AABB 内)" % [prefix, dist])
			return
		corners.append(world)
	var lo := Vector2(INF, INF)
	var hi := Vector2(-INF, -INF)
	for world in corners:
		var px := camera.unproject_position(world)
		lo = lo.min(px)
		hi = hi.max(px)
	var uv := Rect2(lo / viewport, (hi - lo) / viewport)
	if uv.size.x > 2.0 or uv.size.y > 2.0:
		print("%s  dist=%.1fm  (近景特写：AABB 大于画面，占比不适用)" % [prefix, dist])
		return
	var outside := lo.x < 0.0 or lo.y < 0.0 or hi.x > viewport.x or hi.y > viewport.y
	print("%s  dist=%.1fm  frame=%.0f%%x%.0f%% at (%.2f,%.2f)  out=%s" %
		[prefix, dist, uv.size.x * 100.0, uv.size.y * 100.0, uv.position.x, uv.position.y,
		"YES" if outside else "no"])


func _save(path: String) -> void:
	for i in range(12):
		await process_frame
	await RenderingServer.frame_post_draw
	var result := root.get_texture().get_image().save_png(path)
	print("screenshot=%s result=%d" % [path, result])
