@tool
extends NodeSettings

## Settings for the Town Layout node: a flat town on a square grid of streets, filled with
## courtyards — the frame-free counterpart of River Town Lots.

@export_group("Settlement")

## Centre of the town.
@export var origin : Vector3 = Vector3.ZERO

## Half the town's extent along X / Z; the streets fill this rectangle.
@export_range(30.0, 600.0, 10.0) var extent_x : float = 120.0
@export_range(30.0, 600.0, 10.0) var extent_z : float = 120.0

## Scales prop / tree counts (0.5 sparse .. 1.5 dense).
@export_range(0.5, 1.5, 0.05) var density : float = 1.0

@export_group("Streets")

## Qin 2023 symmetry factor SYM for the lanes about the main axis: 1 = mirror image.
@export_range(0.0, 1.0, 0.05) var symmetry : float = 0.6

## gridF snap distance (Qin 2023 Eq 1 scale), in metres.
@export_range(2.0, 20.0, 0.5) var grid : float = 6.0

@export_group("Output")

## Colour tint stream written per building, read by Spawn Meshes through
## `color_attribute`. Set Spawn Meshes' use_vertex_colors to true.
@export var color_attribute : String = "color"
## Resource attribute carrying the one-off meshes on the Structures output.
@export var structure_mesh_attribute : String = "mesh"
