@tool
class_name AncientBuildingNodeSettings
extends NodeSettings

@export_group("Ancient Building")

## Attribute the generated meshes are written to. Point `spawn_meshes` at the same name.
@export var mesh_attribute : String = "mesh"

## How many distinct buildings to generate. Points are assigned one at random, so a village
## of 40 houses costs 4 meshes rather than 40 — the whole reason to cap this.
@export_range(1, 24, 1) var variant_count : int = 4

## Base seed. Variant i is generated with seed + i.
@export var seed : int = 0

@export_group("Plan")
## 通面阔. Every other dimension descends from this via the Table 1 module.
@export_range(2.0, 40.0, 0.1, "or_greater") var width : float = 9.0
@export_range(2.0, 40.0, 0.1, "or_greater") var depth : float = 6.0
## Randomly varies width and depth per variant, as a fraction.
@export_range(0.0, 0.8, 0.01) var size_jitter : float = 0.18
@export_range(1, 9, 1) var bays_x : int = 3
@export_range(1, 9, 1) var bays_z : int = 2

@export_group("Roof")
## 硬山 / 歇山 / 庑殿. A square plan with Hip degenerates to a 攒尖 pyramid.
@export_enum("Flush Gable:0", "Gable and Hip:1", "Hip:2") var roof_type : int = 1
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

@export_group("Material")
## 官式 / 茅草 / 土木. Selecting 茅草 or 土木 overwrites the colour palette below; hand-tune
## afterwards as needed. The geometry (硬山) is unchanged.
@export_enum("Traditional 官式:0", "Thatched 茅草:1", "Earthen 土木:2") var material_style : int = 0

@export_group("Colors")
@export var stone_color : Color = Color(0.60, 0.58, 0.54)
@export var timber_color : Color = Color(0.40, 0.15, 0.12)
@export var plaster_color : Color = Color(0.74, 0.70, 0.63)
@export var tile_color : Color = Color(0.26, 0.29, 0.31)
