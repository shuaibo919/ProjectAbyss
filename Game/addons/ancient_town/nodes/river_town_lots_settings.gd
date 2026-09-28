@tool
class_name RiverTownLotsSettings
extends NodeSettings

## Generator settings for the river-town (水城) settlement node: a river through
## the middle, a walled city with a gate tower on one bank, a waterfront street
## of 河房 on the other, and an arch bridge with a covered gallery joining them.

@export_group("Settlement")

## Origin of the settlement; the river's midpoint lands here.
@export var origin : Vector3 = Vector3.ZERO

## Length of the built-up stretch along the river. The river itself runs
## `river_length`, so its ends stay out of frame.
@export_range(80.0, 600.0, 10.0) var town_length : float = 300.0

## Scales house / boat / figure / tree counts (0.5 sparse .. 1.5 dense).
@export_range(0.5, 1.5, 0.05) var density : float = 1.0

@export_group("River")

## Total river length. Ignored when a River Path is connected.
@export_range(200.0, 1600.0, 10.0) var river_length : float = 700.0
@export_range(16.0, 120.0, 1.0) var river_width : float = 46.0
## Lateral swing of the default meander. 0 gives a straight canal.
@export_range(0.0, 80.0, 1.0) var meander_amplitude : float = 22.0
## Water surface below the quay (the quay is y = 0).
@export_range(0.5, 8.0, 0.1) var water_depth : float = 3.2

@export_group("City Bank")

## City wall along the +side bank. Both banks get houses either way.
@export var city_wall : bool = true
@export_range(3.0, 14.0, 0.5) var wall_height : float = 8.0
@export_range(0.8, 4.0, 0.1) var wall_thickness : float = 2.4
## Tiers of the gate tower (楼阁 over the gate pier).
@export_range(1, 3, 1) var gate_tower_tiers : int = 3
## The inner city climbs this much behind the wall, so its roofs read over it.
@export_range(0.0, 20.0, 0.5) var terrace_rise : float = 7.0

@export_group("Bridge")

@export var bridge : bool = true
## Bridge position along the town, 0 = downstream end, 1 = upstream end.
@export_range(0.0, 1.0, 0.01) var bridge_position : float = 0.55
@export_range(1, 7, 2) var bridge_arches : int = 3
## Covered gallery (廊桥) on the deck.
@export var bridge_gallery : bool = true

@export_group("Waterfront Bank")

@export_range(4.0, 14.0, 0.5) var street_width : float = 7.5
## Rows of houses behind the waterfront street (the river row not counted).
@export_range(0, 3, 1) var back_rows : int = 2

@export_group("Output")

## Colour tint stream written per building, read by Spawn Meshes through
## `color_attribute`. Set Spawn Meshes' use_vertex_colors to true.
@export var color_attribute : String = "color"
## Resource attribute carrying the one-off whitebox meshes on the Structures output.
@export var structure_mesh_attribute : String = "mesh"
