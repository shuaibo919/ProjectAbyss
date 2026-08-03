@tool
extends Node3D
class_name PcgScatterToolNode

## Area-scatter PCG tool — the "房屋散布/植被/岩石" workflow. Drop the node,
## size its region, pick a [member scatter_type], and it populates the ground
## beneath with houses / trees / rocks:
##
##   grid (region around this node, lifted high)
##     → FBM noise density (clumping)
##     → ray_cast straight down onto ANY collider (e.g. a PcgTerrainToolNode
##       island) → keep hits only
##     → height band filter (decompose position → y in [min,max]) so houses
##       stay near the shore and trees on the mid slopes
##     → slope filter (normal_to_density → density_filter) so nothing spawns
##       on cliffs
##     → relax (Lloyd) for organic, non-overlapping spacing
##     → transform (yaw + scale jitter) → spawn_meshes (weighted variants)
##
## Mesh slots are native Inspector drag-drop (empty = built-in greybox
## placeholders per type). Everything regenerates on change, in-editor and at
## runtime — chained PCG: the terrain below is itself procedurally generated.

const FlowGraphBuilder := preload( "res://Script/PCG/flow_graph_builder.gd" )
const PcgVillageMeshes := preload( "res://Script/PCG/pcg_village_meshes.gd" )
const PcgPropMeshes := preload( "res://Script/PCG/pcg_prop_meshes.gd" )

enum ScatterType { HOUSES, TREES, ROCKS }

@export var scatter_type : ScatterType = ScatterType.HOUSES:
	set( v ):
		scatter_type = v
		_mark_dirty()

## Turn generation on/off.
@export var enabled : bool = true:
	set( v ):
		enabled = v
		_mark_dirty()

## Region footprint (m) centred on this node in which candidates are laid.
@export var region_size : Vector2 = Vector2( 60, 60 ):
	set( v ):
		region_size = Vector2( maxf( 2.0, v.x ), maxf( 2.0, v.y ) )
		_mark_dirty()

## Candidate grid spacing (m). Effective density also depends on the clump
## threshold and relax padding.
@export var spacing : float = 6.0:
	set( v ):
		spacing = maxf( 0.5, v )
		_mark_dirty()

## Meshes to scatter (weighted random per point). Empty = built-in greybox set
## for the chosen scatter_type.
@export var meshes : Array[Mesh] = []:
	set( v ):
		meshes = v
		_mark_dirty()
## Per-variant weights (empty = even).
@export var mesh_weights : Array[float] = []:
	set( v ):
		mesh_weights = v
		_mark_dirty()

@export_group( "Placement Filters" )
## Keep only ground hits whose height (global y) falls inside this band —
## e.g. houses [0.5, 6] hug the shore terraces; trees [2, 18] take the slopes.
@export var height_band : Vector2 = Vector2( 0.3, 8.0 ):
	set( v ):
		height_band = v
		_mark_dirty()
## Ground flatness required, 0..1 (0 = only dead-flat, 1 = anything). Maps to
## the normal_to_density tolerance; houses want ~0.15, trees ~0.35.
@export_range( 0.0, 1.0 ) var slope_tolerance : float = 0.18:
	set( v ):
		slope_tolerance = clampf( v, 0.0, 1.0 )
		_mark_dirty()
## Clump threshold 0..1 — points below this noise density are culled (higher =
## sparser, patchier coverage).
@export_range( 0.0, 1.0 ) var clump_threshold : float = 0.45:
	set( v ):
		clump_threshold = clampf( v, 0.0, 1.0 )
		_mark_dirty()
## Minimum spacing enforced between survivors (relax padding). Houses need
## their footprint (~6m); trees ~2m.
@export var min_separation : float = 6.0:
	set( v ):
		min_separation = maxf( 0.0, v )
		_mark_dirty()

@export_group( "Variation" )
@export var seed : int = 3:
	set( v ):
		seed = v
		_mark_dirty()
## Uniform scale jitter range applied per instance.
@export var scale_range : Vector2 = Vector2( 0.9, 1.15 ):
	set( v ):
		scale_range = v
		_mark_dirty()

## Physics frames to wait before (re)generating — the terrain collider below
## must be registered for the down-rays to hit.
@export var settle_frames : int = 8

## Inspector button: tick to force a rebuild now.
@export var regenerate_now : bool = false:
	set( v ):
		regenerate_now = false
		if v:
			regenerate()


var _flow : FlowGraphNode3D = null
var _dirty : bool = false
var _regenerating : bool = false


func _ready() -> void:
	_mark_dirty()


func _mark_dirty() -> void:
	if not is_inside_tree():
		return
	_dirty = true
	if not _regenerating:
		_deferred_regenerate.call_deferred()


func _deferred_regenerate() -> void:
	if _regenerating:
		return
	_regenerating = true
	while _dirty:
		_dirty = false
		for i in range( maxi( 1, settle_frames ) ):
			await get_tree().physics_frame
		regenerate()
	_regenerating = false


## Rebuild the scatter graph from current parameters and run it.
func regenerate() -> void:
	if not is_inside_tree():
		return
	_ensure_flow_child()
	if not enabled:
		_clear_output()
		return
	_flow.graph = _build_graph()
	if Engine.is_editor_hint():
		_evaluate_in_editor()
	else:
		_flow.execute()


# --- graph ------------------------------------------------------------------

func _default_meshes() -> Array[Mesh]:
	match scatter_type:
		ScatterType.HOUSES:
			return [
				PcgVillageMeshes.house_hut(),
				PcgVillageMeshes.house_timber(),
				PcgVillageMeshes.house_two_story(),
				PcgVillageMeshes.house_stilt(),
			]
		ScatterType.TREES:
			return [
				PcgPropMeshes.conifer( 4.5 ),
				PcgPropMeshes.conifer( 3.2 ),
				PcgPropMeshes.broadleaf( 3.6 ),
			]
		_:
			return [
				PcgPropMeshes.rock( 1.0, seed + 1 ),
				PcgPropMeshes.rock( 0.6, seed + 2 ),
				PcgPropMeshes.rock( 1.6, seed + 3 ),
			]


func _build_graph() -> FlowGraphResource:
	var b := FlowGraphBuilder.new()

	var origin: Vector3 = global_transform.origin
	var cast_height := 80.0
	var cells_x := maxi( 2, int( region_size.x / spacing ) )
	var cells_z := maxi( 2, int( region_size.y / spacing ) )

	# Candidate grid lifted above everything, centred on this node.
	var src := b.AddNode( "grid", {
		"x": cells_x, "y": 1, "z": cells_z,
		"step": Vector3( spacing, 0.0, spacing ),
		"origin": origin + Vector3( -0.5 * cells_x * spacing, cast_height, -0.5 * cells_z * spacing ),
		"size": 1.0,
	} )

	# Clump noise BEFORE the raycast (cheap cull earlier would be nicer, but the
	# density value rides along and is thresholded after all hard filters).
	var dens := b.AddNode( "noise", {
		"out_name": "density",
		"in_scale": 0.05,
		"noise_type": 4,            # Simplex
		"fractal_type": 1,          # FBM
		"fractal_octaves": 4,
		"random_seed": seed,
		"mode": 0,                  # Override
		"sample_space": 1,          # XZ2D
	}, Vector2( 200, 0 ), { "In": 0 } )

	var ray := b.AddNode( "ray_cast", {
		"dir": Vector3( 0, -1, 0 ),
		"max_distance": cast_height + 120.0,
		"collision_mask": 1,
		"out_result_attribute": "hit",
		"out_normal_attribute": "normal",
		"out_position_attribute": "position",
		"out_rotation_attribute": "",       # keep zero yaw; transform adds it
	}, Vector2( 400, 0 ), { "In": 0 } )

	var hit_filter := b.AddNode( "filter", {
		"in_nameA": "hit", "in_nameB": "True", "condition": 0,
	}, Vector2( 600, 0 ), { "In A": 0 } )

	# Height band: position → y float → keep y in [band.x, band.y].
	var decomp := b.AddNode( "decompose_vector", {
		"in_attribute": "position",
		"y_attribute": "y",
	}, Vector2( 800, 0 ), { "In": 0 } )
	var band := b.AddNode( "attribute_filter_range", {
		"attribute_name": "y",
		"min_value": height_band.x,
		"max_value": height_band.y,
	}, Vector2( 1000, 0 ), { "In": 0 } )

	# Slope mask: flat ground keeps density, steep loses it (Minimum-combine).
	var slope := b.AddNode( "normal_to_density", {
		"normal_to_compare": Vector3( 0, 1, 0 ),
		"offset": slope_tolerance,
		"strength": 1.0,
		"density_mode": 1,          # Minimum
	}, Vector2( 1200, 0 ), { "In": 0 } )

	var cull := b.AddNode( "density_filter", {
		"lower_bound": clump_threshold,
		"upper_bound": 1.0,
	}, Vector2( 1400, 0 ), { "In": 0 } )

	var relax := b.AddNode( "relax", {
		"num_iterations": 8,
		"strength": 0.55,
		"padding": min_separation,
	}, Vector2( 1600, 0 ), { "In": 0 } )

	var xform := b.AddNode( "transform", {
		"rotation_min": Vector3( 0, 0, 0 ),
		"rotation_max": Vector3( 0, 360, 0 ),
		"scale_min": Vector3.ONE * scale_range.x,
		"scale_max": Vector3.ONE * scale_range.y,
		"uniform_scale": true,
		"random_seed": seed + 5,
	}, Vector2( 1800, 0 ), { "In": 0 } )

	var use_meshes := _clean( meshes )
	if use_meshes.is_empty():
		use_meshes = _default_meshes()
	var weights: Array[float] = mesh_weights.duplicate()
	if weights.is_empty():
		weights.resize( use_meshes.size() )
		weights.fill( 1.0 )
	var spawn := b.AddNode( "spawn_meshes", {
		"mesh": use_meshes[0],
		"mesh_variants": use_meshes,
		"mesh_variant_weights": weights,
		"randomize_mesh_variants": true,
		"random_seed": seed + 9,
		"use_vertex_colors": false,
		"clear_previous_instances": true,
	}, Vector2( 2000, 0 ), { "In": 0 } )

	b.Connect( src, 0, dens, 0 )
	b.Connect( dens, 0, ray, 0 )
	b.Connect( ray, 0, hit_filter, 0 )
	b.Connect( hit_filter, 0, decomp, 0 )
	b.Connect( decomp, 0, band, 0 )
	b.Connect( band, 0, slope, 0 )
	b.Connect( slope, 0, cull, 0 )
	b.Connect( cull, 0, relax, 0 )
	b.Connect( relax, 0, xform, 0 )
	b.Connect( xform, 0, spawn, 0 )
	return b.Build()


func _clean( provided : Array[Mesh] ) -> Array[Mesh]:
	var cleaned : Array[Mesh] = []
	for m in provided:
		if m != null:
			cleaned.append( m )
	return cleaned


# --- internal flow child ------------------------------------------------------

func _ensure_flow_child() -> void:
	if _flow != null and is_instance_valid( _flow ):
		return
	for child in get_children():
		if child is FlowGraphNode3D:
			_flow = child
			return
	_flow = FlowGraphNode3D.new()
	_flow.name = "FlowGraph"
	add_child( _flow )
	if not Engine.is_editor_hint():
		_flow.owner = null


func _clear_output() -> void:
	if _flow == null or not is_instance_valid( _flow ):
		return
	for child in _flow.get_children():
		if child is MultiMeshInstance3D:
			child.queue_free()


func _evaluate_in_editor() -> void:
	var io = load( "res://addons/flow_nodes_editor/flow_nodes_io.gd" )
	var ctx = load( "res://addons/flow_nodes_editor/flow_data.gd" ).EvaluationContext.new()
	ctx.owner = _flow
	ctx.eval_id = 0
	ctx.gedit_nodes_by_name = {}
	ctx.runtime_params = {}
	io.evaluate_graph( _flow.graph, {}, ctx, {}, 0 )
