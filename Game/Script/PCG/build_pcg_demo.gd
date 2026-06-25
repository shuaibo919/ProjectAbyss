extends SceneTree

## Headless verification of programmatic PCG (path B) — NO GUI involved.
##
## Run with:
##   Engine/bin/godot.windows.editor.x86_64.console.exe --headless \
##     --path Game/ --script res://Script/PCG/build_pcg_demo.gd
##
## Steps:
##   1. Build a self-contained graph in code:  grid -> transform -> spawn_meshes
##   2. Save it to res://Develop/PCG/generated_grid.tres
##   3. Attach it to a FlowGraphNode3D, add to the tree, execute() it
##   4. Inspect the spawned MultiMeshInstance3D children to prove generation
##
## Everything here is engine-only: FlowNodeRegistry resolves node scripts from
## the addon's nodes/ dir via ResourceLoader (no plugin-enable required), and
## FlowNodeIO.evaluate_graph() is the same path FlowGraphNode3D runs at startup.

const FlowGraphBuilder := preload( "res://Script/PCG/flow_graph_builder.gd" )

const OUTPUT_GRAPH_PATH := "res://Develop/PCG/generated_grid.tres"

# Grid extent we expect to generate (5 * 1 * 5 = 25 points).
const GRID_X := 5
const GRID_Y := 1
const GRID_Z := 5
const EXPECTED_POINTS := GRID_X * GRID_Y * GRID_Z


func _init() -> void:
	print( "=== PCG path-B headless build ===" )


# Use _initialize() (not _init): the SceneTree's `root` window only exists once
# the main loop is initialized, and FlowGraphNode3D.execute() needs to be in the
# tree so spawn_meshes can call get_tree().
var _graph: FlowGraphResource = null
var _did_run := false


func _initialize() -> void:
	var builder := FlowGraphBuilder.new()

	# 1) grid generator — self-contained, no input ports.
	var grid := builder.AddNode( "grid", {
		"x": GRID_X,
		"y": GRID_Y,
		"z": GRID_Z,
		"step": Vector3( 2.0, 0.0, 2.0 ),
		"origin": Vector3.ZERO,
		"random_seed": 1337,
	}, Vector2( 0, 0 ) )

	# 2) transform — random yaw + slight scale jitter per point.
	var xform := builder.AddNode( "transform", {
		"rotation_min": Vector3( 0, 0, 0 ),
		"rotation_max": Vector3( 0, 360, 0 ),
		"scale_min": Vector3( 0.8, 0.8, 0.8 ),
		"scale_max": Vector3( 1.2, 1.2, 1.2 ),
		"uniform_scale": true,
		"random_seed": 4242,
	}, Vector2( 200, 0 ), { "In": 0 } )

	# 3) spawn_meshes — default unit_cube mesh, one MultiMesh for all points.
	var spawn := builder.AddNode( "spawn_meshes", {
		"clear_previous_instances": true,
		"random_seed": 9001,
	}, Vector2( 400, 0 ), { "In": 0 } )

	builder.Connect( grid, 0, xform, 0 )
	builder.Connect( xform, 0, spawn, 0 )

	var save_err := builder.SaveTo( OUTPUT_GRAPH_PATH )
	if save_err != OK:
		push_error( "Failed to save graph: %d" % save_err )
		quit( 1 )
		return
	print( "Saved graph -> %s" % OUTPUT_GRAPH_PATH )

	# Reload from disk to prove the serialized .tres is self-sufficient.
	var graph: FlowGraphResource = load( OUTPUT_GRAPH_PATH )
	if graph == null:
		push_error( "Failed to reload saved graph" )
		quit( 1 )
		return
	print( "Reloaded graph: %d node(s), %d link(s)" % [
		graph.data.get( "nodes", [] ).size(),
		graph.data.get( "links", [] ).size(),
	] )

	# Defer execution to _process: with a SceneTree loaded via --script, `root`
	# is only fully attached to the tree after _initialize returns.
	_graph = graph


func _process( _delta: float ) -> bool:
	if _did_run:
		return true  # already done; signal main loop to quit
	_did_run = true

	# Execute through FlowGraphNode3D. In a non-editor run, FlowGraphNode3D._ready()
	# auto-calls execute() on add_child — so we must NOT call execute() again, or
	# spawn_meshes runs twice (clear_previous_instances clears within a single MMI
	# group, not across re-entrant passes). Adding to the tree is the trigger.
	var flow_node := FlowGraphNode3D.new()
	flow_node.name = "PCGRoot"
	flow_node.graph = _graph
	root.add_child( flow_node )

	# Verify: spawn_meshes added MultiMeshInstance3D children with our points.
	var total_instances := 0
	var mmi_count := 0
	for child in flow_node.get_children():
		var mmi := child as MultiMeshInstance3D
		if mmi != null and mmi.multimesh != null:
			mmi_count += 1
			total_instances += mmi.multimesh.instance_count

	print( "--- result ---" )
	print( "MultiMeshInstance3D children: %d" % mmi_count )
	print( "Total spawned instances:     %d (expected %d)" % [ total_instances, EXPECTED_POINTS ] )

	if total_instances == EXPECTED_POINTS and mmi_count > 0:
		print( "PCG_VERIFY: SUCCESS" )
	else:
		print( "PCG_VERIFY: FAILURE" )

	quit( 0 if ( total_instances == EXPECTED_POINTS and mmi_count > 0 ) else 1 )
	return true

