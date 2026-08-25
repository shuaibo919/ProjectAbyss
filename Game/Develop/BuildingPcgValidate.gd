extends Node3D

# Verifies the ancient_building plugin end to end: register the node directory, build a Flow
# graph in code that scatters points and pipes them through Ancient Building into Spawn Meshes,
# evaluate it, and confirm real MultiMesh instances came out.
#
# Run: godot --path Game/ res://Develop/BuildingPcgValidate.tscn

const OUT_DIR := "res://Develop/BuildingShots"
const NODE_DIR := "res://addons/ancient_building/nodes"

const FlowGraphBuilder := preload("res://Script/PCG/flow_graph_builder.gd")


func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))

	# --- 1. The node must be discoverable through the registry API ---
	FlowNodeRegistry.register_node_directory(NODE_DIR)
	var script_path: String = FlowNodeRegistry.get_node_script_path("ancient_building")
	print("VERIFY node resolves through registry: %s (%s)"
		% ["PASS" if script_path != "" else "FAIL", script_path])

	# --- 2. The generator must produce a mesh without touching the scene ---
	var params = ClassDB.instantiate("AncientBuildingParameters")
	params.roof_type = 1
	var probe = ClassDB.instantiate("AncientBuilding")
	probe.auto_regenerate = false
	probe.parameters = params
	var baked: Mesh = probe.bake_mesh()
	var surfaces: int = baked.get_surface_count() if baked != null else 0
	probe.free()
	print("VERIFY bake_mesh outside the tree: %s (%d surfaces)"
		% ["PASS" if surfaces > 0 else "FAIL", surfaces])

	# --- 3. The node must work inside a real evaluated graph ---
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-48, -36, 0)
	light.light_energy = 1.5
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.63, 0.71, 0.81)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.56, 0.59, 0.63)
	e.ambient_light_energy = 0.8
	env.environment = e
	add_child(env)

	var graph := _build_graph()
	var flow := FlowGraphNode3D.new()
	flow.graph = graph
	add_child(flow)
	flow.execute()

	for i in 8:
		await get_tree().process_frame

	var instances := 0
	var multimeshes := 0
	for child in flow.get_children():
		if child is MultiMeshInstance3D and child.multimesh != null:
			multimeshes += 1
			instances += child.multimesh.instance_count
	print("VERIFY graph spawned buildings: %s (%d MultiMeshInstance3D, %d instances)"
		% ["PASS" if instances > 0 else "FAIL", multimeshes, instances])

	var camera := Camera3D.new()
	camera.current = true
	add_child(camera)
	var bounds := _bounds_of(flow)
	var centre := bounds.get_center()
	var radius := maxf(bounds.size.length() * 0.6, 5.0)
	camera.position = centre + Vector3(0.6, 0.45, 1.0).normalized() * radius * 1.25
	camera.look_at(centre)

	for i in 6:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(
		ProjectSettings.globalize_path(OUT_DIR + "/pcg_village.png"))

	get_tree().quit()


func _build_graph() -> FlowGraphResource:
	var builder := FlowGraphBuilder.new()

	# A 3x3 grid of sites, each given a random yaw so the village is not a regular lattice.
	var grid := builder.AddNode("grid", {
		"x": 3, "y": 1, "z": 3,
		"step": Vector3(26, 0, 26),
	})
	var transform := builder.AddNode("transform", {
		"rotation_min": Vector3(0, 0, 0),
		"rotation_max": Vector3(0, 360, 0),
	})
	var buildings := builder.AddNode("ancient_building", {
		"mesh_attribute": "mesh",
		"variant_count": 4,
		"randomize_roof_type": true,
		"width": 9.0,
		"depth": 6.0,
		"seed": 7,
	})
	var spawn := builder.AddNode("spawn_meshes", {
		"mesh_attribute": "mesh",
	})

	builder.Connect(grid, 0, transform, 0)
	builder.Connect(transform, 0, buildings, 0)
	builder.Connect(buildings, 0, spawn, 0)

	return builder.Build()


func _bounds_of(root: Node) -> AABB:
	var result := AABB()
	var first := true
	for child in root.get_children():
		if child is VisualInstance3D:
			var box: AABB = child.get_aabb()
			box.position += child.global_position
			if first:
				result = box
				first = false
			else:
				result = result.merge(box)
	if first:
		return AABB(Vector3(-20, 0, -20), Vector3(40, 10, 40))

	return result
