@tool
class_name ProceduralRockNodeSettings
extends NodeSettings

@export_group("Procedural Rock")

## Attribute the generated meshes are written to. Point `spawn_meshes` at the same name.
@export var mesh_attribute : String = "mesh"

## How many distinct rocks to generate. Points are assigned one at random, so a field
## of 40 rocks costs a handful of meshes rather than 40.
@export_range(1, 24, 1) var variant_count : int = 4

## Base seed. Variant i is generated with seed + i.
@export var seed : float = 880.0

@export_group("Form")
@export_enum("Boulder", "Pebble", "Slab") var form : int = 0

## Grid samples per axis; the mesh comes from (resolution-1)^3 cells. CPU-evaluated,
## so scrub this against the preview before raising it.
@export_range(16, 128, 1) var resolution : int = 48
@export_range(0.1, 20.0, 0.05, "or_greater") var scale : float = 2.5
@export_range(8, 72, 1) var steps : int = 20
@export_range(0.01, 0.2, 0.001) var smoothness : float = 0.05

@export_group("Surface")
@export_range(0.0, 1.0, 0.001) var displacement_scale : float = 0.15
@export_range(1.0, 10.0, 0.01) var displacement_spread : float = 10.0
## Y squash for Pebble / Y extent for Slab. Ignored by Boulder.
@export_range(0.3, 1.5, 0.01) var flatness : float = 0.7
## Corner radius for Slab, as a fraction of its half extent.
@export_range(0.0, 1.0, 0.01) var roundness : float = 0.25

@export_group("Ground")
@export var cut_ground : bool = false
@export_range(-0.5, 0.5, 0.01) var ground_cut : float = -0.3

@export_group("Colors")
@export var base_color : Color = Color(0.45, 0.46, 0.47)
@export var crevice_color : Color = Color(0.16, 0.17, 0.18)
