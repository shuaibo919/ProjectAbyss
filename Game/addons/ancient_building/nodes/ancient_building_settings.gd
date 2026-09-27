@tool
class_name AncientBuildingNodeSettings
extends NodeSettings

@export_group("Ancient Building")

## Attribute the generated meshes are written to. Point `spawn_meshes` at the same name.
@export var mesh_attribute : String = "mesh"

## How many distinct buildings to generate. Points are assigned one at random, so a village
## of 40 houses costs 4 meshes rather than 40 — the whole reason to cap this. With point
## overrides enabled this is the cap on distinct parameter combinations instead.
@export_range(1, 48, 1) var variant_count : int = 4

## Base seed. Variant i is generated with seed + i.
@export var seed : int = 0

## When the input carries `ab_*` override streams (width, depth, roof_type...), bake one
## variant per distinct parameter combination instead of random variants. Points sharing a
## combination share one mesh — the town generator relies on this for MultiMesh efficiency.
@export var use_point_overrides : bool = true

@export_group("Plan")
## 通面阔. Every other dimension descends from this via the Table 1 module.
@export_range(2.0, 40.0, 0.1, "or_greater") var width : float = 9.0
@export_range(2.0, 40.0, 0.1, "or_greater") var depth : float = 6.0
## Randomly varies width and depth per variant, as a fraction.
@export_range(0.0, 0.8, 0.01) var size_jitter : float = 0.18
@export_range(1, 9, 1) var bays_x : int = 3
@export_range(1, 9, 1) var bays_z : int = 2

@export_group("Roof")
## All nine C++ roof types (AncientBuildingParameters.ERoofType). A square plan with Hip
## degenerates to a 攒尖 pyramid (Eq 8).
@export_enum("硬山 Flush Gable:0", "歇山 Gable and Hip:1", "庑殿 Hip:2",
	"悬山 Overhanging Gable:3", "卷棚 Round Ridge:4", "盝顶 Hollow:5",
	"攒尖 Pyramidal:6", "圆攒尖 Round:7", "盔顶 Helmet:8") var roof_type : int = 1
## Picks a roof type per variant instead of using roof_type for all of them.
@export var randomize_roof_type : bool = false
@export_range(3, 13, 1) var rafter_courses : int = 5
@export_range(0.0, 1.0, 0.01) var tile_coverage : float = 1.0
## 起翘, as a multiple of the module D. Ignored by 硬山, which has no corner to lift.
@export_range(0.0, 5.0, 0.01) var corner_rise_scale : float = 1.6

@export_group("Detail")
@export var generate_fence : bool = true
@export var generate_steps : bool = true
@export var generate_walls : bool = true
@export_range(0, 3, 1) var fence_lambda : int = 1
## Larger tiles mean fewer 瓦垄 sweeps, which is the main cost knob.
@export_range(0.1, 2.0, 0.01) var tile_course_width : float = 0.34

@export_group("民居形制 (2026-09-26)")
## 下方形制参数**作用到哪些建筑**。城里有民居也有庙宇，如果全城一律套用，
## 庑殿/歇山这些官式建筑也会长出民居的下碱与柱础 —— 所以默认只作用到民居屋顶。
@export_enum("关闭:0", "仅民居屋顶 硬山/悬山/卷棚:1", "全部建筑:2")
var dwelling_style_scope : int = 1
## 下列参数由 「民居样板」新增（Source/AncientBuilding/）。**全部默认 = 旧行为**，
## 所以资源不动就不会改变任何现有产物的外观。
## 注：它们目前是**节点级设置（全城统一）**，尚未接成 `ab_*` 点属性；要让每栋不同，
## 需接入 `_collect_overrides` + `_combo_key`（见 05 契约 §一：影响网格的语义必须入键）。
## 老 DLL 上这些属性不存在，赋值走 `_apply_if_present` 守卫，不会报错。

## 台基有无（地基）。**默认 true = 旧行为**。关掉时不生成台基块、踏步与栏杆，
## 且整栋下移落到地面（PlatformHeight 参与柱底/墙底/屋顶基准的派生）。
@export var generate_platform : bool = true
## 下碱高 ÷ 墙高（20_墙体 R15）。0 = 关闭（旧行为）。>0 时墙体分区：
## 上部抹灰 + 下部砌块带，砖缝几何只在下碱带内生成。
@export_range(0.0, 0.9, 0.01) var dado_height_ratio : float = 0.0
## 分界带高，单位 = 模数 D。0 = 关闭（旧行为）。
@export_range(0.0, 2.0, 0.01) var dado_top_trim : float = 0.0
## 台基顶面接缝网格（60_台基地面 R6）。默认关。
@export var platform_top_joints : bool = false
## 台基边缘凸出沿口（60 R6）。默认关。
@export var platform_edge_lip : bool = false
## 方砖铺地（60 R9）。默认关。
@export var paving : bool = false
## 铺地**几何板缝**（仅 `paving` 开时生效）。默认关 = 顶点色缝。
@export var paving_joint_geometry : bool = false
## 踏步两侧简单侧挡（60 R8）。默认关。
@export var step_side_cheek : bool = false
## 方形石础（60 R12；关 = 旧的车削圆础盘）。高度仍由 `column_base_height_scale` 给。
@export var column_base_square : bool = false
## 柱础高 = 该值 × D（既有参数，非新增）。0 = 无础（旧行为）。
@export_range(0.0, 2.0, 0.01) var column_base_height_scale : float = 0.0

@export_group("瓦作 / 距离档 ()")
## 瓦作 detail level（30_瓦作 §2）。**0 = 旧行为**（既有资源字节不变）。
## 1 = 排垄与叠压；2 = 1 + 檐口件（瓦当/滴水）与泥背层。
@export_enum("旧 Legacy:0", "排垄与叠压:1", "檐口件与泥背:2") var tile_detail : int = 0
## 泥背层厚（米，R17）。**0 = 旧行为**（瓦面直接坐在望板上）。
## 它抬升的是整张瓦面，因此**所有脊的高度链都由它决定** —— 不同值不能共用网格。
@export_range(0.0, 0.2, 0.001) var tile_bedding_thickness : float = 0.0
## 距离档。**0 = 近景 / 旧行为**；中景与远景会丢掉细节（含脊断面降为占位块）。
@export_enum("近景:0", "中景:1", "远景:2") var lod_level : int = 0
## 脊体截面与接头（2026-09-27 起**默认 1**，用户裁定）。
## 1 = 收分脊身、圆弧盖脊、山尖连续接头（正脊与垂脊不再互相穿插）；0 = 旧 7 点断面，
## 仅作**回归基线**保留 —— C++ 侧的参数默认值仍是 0，所以既有资源与 4 套测试的字节锚点
## 不受影响；"默认"在这里、在消费方。
@export_enum("旧 Legacy (回归基线):0", "分层圆脊 / 连续接头:1") var ridge_detail : int = 1
## 以上四项可由点属性 `ab_tile_detail` / `ab_tile_bedding_thickness` / `ab_lod_level` / `ab_ridge_detail` 逐点覆盖
## （地形/地块生成器写了流就以流为准）。**写了流的点位会进 `ab_*` 组合键**，所以不同档不共享网格。

@export_group("Material")
## 官式 / 茅草 / 土木. Selecting 茅草 or 土木 overwrites the colour palette below; hand-tune
## afterwards as needed. The geometry (硬山) is unchanged.
@export_enum("Traditional 官式:0", "Thatched 茅草:1", "Earthen 土木:2") var material_style : int = 0

@export_group("Colors")
@export var stone_color : Color = Color(0.60, 0.58, 0.54)
@export var timber_color : Color = Color(0.40, 0.15, 0.12)
@export var plaster_color : Color = Color(0.74, 0.70, 0.63)
@export var tile_color : Color = Color(0.26, 0.29, 0.31)
