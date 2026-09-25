@tool
class_name TownLotsSettings
extends NodeSettings

## Generator settings for the ancient-town settlement node. The node emits five
## point streams from one deterministic layout: buildings, roads, walls, props
## (stalls / wells / 牌坊) and trees.

@export_group("Settlement")

## 聚落 (hamlet) / 村镇 (village) / 市集 (market town) / 城市 (city).
@export_enum("Hamlet 聚落:0", "Village 村镇:1", "Market 市集:2", "City 城市:3") \
	var settlement_type : int = 3

## Origin of the settlement; all coordinates are relative to this point.
@export var origin : Vector3 = Vector3.ZERO

## Half-extent of the city (or overall scale anchor for the smaller types).
@export_range(100.0, 700.0, 10.0) var extent_x : float = 260.0
@export_range(100.0, 700.0, 10.0) var extent_z : float = 260.0

@export_group("City")

## Height of the city wall ring. Ignored by every type except 城市.
@export_range(2.0, 14.0, 0.5) var wall_height : float = 8.0
## Thickness of the city wall ring.
@export_range(0.4, 4.0, 0.1) var wall_thickness : float = 1.6
## Gap the four gate towers leave in the wall.
@export_range(4.0, 16.0, 0.5) var gate_width : float = 10.0

@export_group("Density")

## Scales building / stall / tree counts (0.5 sparse .. 1.5 dense).
@export_range(0.5, 1.5, 0.05) var density : float = 1.0

## Colour tint stream written per building, read by Spawn Meshes through
## `color_attribute`. Set Spawn Meshes' use_vertex_colors to true.
@export var color_attribute : String = "color"
