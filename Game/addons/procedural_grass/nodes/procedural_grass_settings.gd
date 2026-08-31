@tool
class_name ProceduralGrassNodeSettings
extends NodeSettings

@export_group("Procedural Grass")

## Attribute the generated meshes are written to. Point `spawn_meshes` at the same name.
@export var mesh_attribute : String = "mesh"

## How many distinct clumps to generate. Points are assigned one at random, so a field
## of hundreds of grass clumps costs a handful of meshes rather than hundreds.
@export_range(1, 24, 1) var variant_count : int = 4

## Base seed. Variant i is generated with seed + i.
@export var seed : float = 1.0

@export_group("Species")
@export_enum("Thatch", "Foxtail", "Short", "Weed") var species : int = 2

@export_group("Shape")
@export_range(0.1, 5.0, 0.05, "or_greater") var scale : float = 1.0
@export_range(0.02, 1.0, 0.01, "or_greater") var clump_radius : float = 0.25
## 0 = pick a species-appropriate random count per variant.
@export_range(0, 60, 1) var blade_count : int = 0
@export_range(0.0, 3.0, 0.01) var curvature : float = 1.0
@export_range(0.0, 60.0, 0.5) var lean_angle : float = 0.0
@export_range(0.0, 360.0, 1.0) var lean_azimuth : float = 0.0

@export_group("Color")
@export_range(0.0, 1.0, 0.01) var color_variance : float = 0.15
## When true (the default) each species uses its own built-in palette and the two
## colors below are ignored.
@export var use_species_colors : bool = true
@export var base_color : Color = Color(0.20, 0.42, 0.16)
@export var tip_color : Color = Color(0.55, 0.72, 0.22)
