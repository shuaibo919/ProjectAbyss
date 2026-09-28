extends Node3D

# 水城 StyleShowcase: a small river town assembled entirely by PCG, used as the
# common subject for comparing NPR treatments. Reference composition:
# Reference/screenshot-20260928-123404.png (walled city with a tiered gate tower
# on the left bank, waterfront street on the right, arch bridge with a covered
# gallery between them).
#
#   river_town_lots ─ Buildings ─ ancient_building ─ spawn_meshes
#                   ├ Roads / Walls / Props / Trees ─ spawn_meshes (whitebox meshes)
#                   └ Structures (water, land, 驳岸, piers, bridge, hills) ─ spawn_meshes
#
# Run: Engine/bin/godot.windows.editor.x86_64.console.exe --path Game/ res://Map/Map_RiverTown.tscn
#   RIVER_STYLE=plain|ink|outline|ink_outline   NPR pass (default plain; see npr_style_kit.gd)
#   RIVER_SEED=7                                layout seed
#   RIVER_LOD=0.08                              outline line LOD in world units, 0 = off
#   SHOTS=1                                     render every framing to Reference/Shots/RiverTown and quit
#   SHOT=hero                                   with SHOTS, only this framing
#
# Controls: WASD move, right-drag orbit, wheel zoom, Q/E yaw, R/F pitch.

const FlowGraphBuilder := preload("res://Script/PCG/flow_graph_builder.gd")
const ShotOutput := preload("res://Develop/Tools/shot_output.gd")
const NprStyleKit := preload("res://Script/NPR/npr_style_kit.gd")
const TownMeshes := preload("res://addons/ancient_town/town_meshes.gd")
const PropMeshes := preload("res://Script/PCG/pcg_prop_meshes.gd")

const AB_NODE_DIR := "res://addons/ancient_building/nodes"
const TOWN_NODE_DIR := "res://addons/ancient_town/nodes"
const PAPER := Color(0.898, 0.859, 0.824)
const WATER_DEPTH := 3.2

var _flow: FlowGraphNode3D
var _anchors := {}


func _ready() -> void:
	var style := NprStyleKit.parse(_env_or("RIVER_STYLE", "plain"))
	var seed := _env_or("RIVER_SEED", "7").to_int()

	var fills := _build_environment(style)
	var t0 := Time.get_ticks_msec()
	_build_town(seed)
	var t_gen := Time.get_ticks_msec() - t0
	var stats := NprStyleKit.apply(_flow, style, {
		"fill_dirs": fills,
		# Line LOD on by default: at town distance the tile seams otherwise ink every
		# roof into a black mass (see CLAUDE.md, OutlineGen). Inert up close.
		"lod_world_width": _env_or("RIVER_LOD", "0.08").to_float(),
	})
	_anchors = _find_anchors()
	_report(t_gen, stats, style)
	_build_camera_rig(_anchor_origin("bridge"))

	if OS.has_environment("SHOTS"):
		await _shoot(NprStyleKit.STYLE_NAMES[style])
		get_tree().quit()


func _env_or(key: String, fallback: String) -> String:
	var v := OS.get_environment(key)
	return v if v != "" else fallback


# ---------------------------------------------------------------- 环境

## Returns the fill-light directions for the ink shader's bypass.
func _build_environment(style: int) -> Array:
	var ink := NprStyleKit.has_ink(style)
	var sun := DirectionalLight3D.new()
	sun.name = "SunLight"
	sun.rotation_degrees = Vector3(-40, -32, 0)
	sun.light_energy = 1.0 if ink else 0.8
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 600.0
	add_child(sun)

	# Shadowless fill from the opposite side: the ink shader is light-driven, so
	# back faces under a single sun go near-black. Same rig as Map_AncientTown.
	var fill := DirectionalLight3D.new()
	fill.name = "FillLight"
	fill.rotation_degrees = Vector3(-45, 130, 0)
	fill.light_energy = 0.45 if ink else 0.2
	add_child(fill)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = PAPER
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.86, 0.84, 0.82)
	# The plain pass is the reference the NPR looks are judged against, so it
	# must not clip: sun + fill + ambient stays under 1 on the palest albedo.
	e.ambient_light_energy = 0.3 if ink else 0.32
	e.fog_enabled = true
	e.fog_mode = Environment.FOG_MODE_DEPTH
	e.fog_light_color = PAPER
	e.fog_depth_begin = 260.0
	e.fog_depth_end = 1300.0
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.environment = e
	add_child(env)

	return [fill.global_transform.basis.z.normalized()]


# ---------------------------------------------------------------- 城镇 (PCG)

func _build_town(seed: int) -> void:
	FlowNodeRegistry.register_node_directory(AB_NODE_DIR)
	FlowNodeRegistry.register_node_directory(TOWN_NODE_DIR)

	_flow = FlowGraphNode3D.new()
	_flow.name = "RiverTownPCG"
	_flow.graph = _town_graph(seed)
	add_child(_flow)  # FlowGraphNode3D._ready() evaluates the graph in game runs


func _town_graph(seed: int) -> FlowGraphResource:
	var b := FlowGraphBuilder.new()

	var lots := b.AddNode("river_town_lots", {
		"random_seed": seed,
		"water_depth": WATER_DEPTH,
	}, Vector2(0, 0))

	# One baked mesh per distinct parameter combination. No size jitter: tiered
	# towers and the bridge gallery are stacked from exact Table 1 heights.
	var buildings := b.AddNode("ancient_building", {
		"mesh_attribute": "mesh",
		"variant_count": 64,
		"seed": seed + 7,
		"size_jitter": 0.0,
		"ridge_detail": 1,
	}, Vector2(320, 0), { "Points": 0 })

	var spawns := [
		[buildings, 0, { "mesh_attribute": "mesh" }],
		[lots, 1, { "mesh": TownMeshes.road() }],
		[lots, 2, { "mesh": TownMeshes.unit_box(TownMeshes.CITY_WALL_COL) }],
		[lots, 3, {
			"mesh_variants": [TownMeshes.stall(), TownMeshes.well(), TownMeshes.archway(),
				TownMeshes.merlon(), TownMeshes.boat_wupeng(), TownMeshes.boat_cargo(),
				TownMeshes.figure(Color(0.30, 0.33, 0.38)), TownMeshes.figure(Color(0.66, 0.58, 0.44)),
				TownMeshes.river_steps(3.2, WATER_DEPTH, 4.5)],
			"mesh_selector_attribute": "prop_type",
		}],
		[lots, 4, {
			"mesh_variants": [PropMeshes.conifer(), PropMeshes.broadleaf(), PropMeshes.broadleaf(4.5, 3)],
			"mesh_variant_weights": [1.0, 2.0, 1.5],
			"randomize_mesh_variants": true,
		}],
		[lots, 5, { "mesh_attribute": "mesh" }],
	]
	var row := 0
	for entry in spawns:
		var settings: Dictionary = entry[2]
		settings.merge({
			"clear_previous_instances": true,
			"color_attribute": "color",
			"use_vertex_colors": true,
			"random_seed": seed + 20 + row,
		})
		var spawn := b.AddNode("spawn_meshes", settings, Vector2(640, row * 200), { "In": 0 })
		b.Connect(entry[0], entry[1], spawn, 0)
		row += 1
	b.Connect(lots, 0, buildings, 0)
	return b.Build()


## Landmarks by `town_role` mesh meta → world transform of their first instance.
func _find_anchors() -> Dictionary:
	var out := {}
	for child in _flow.get_children():
		var mmi := child as MultiMeshInstance3D
		if mmi == null or mmi.multimesh == null or mmi.multimesh.mesh == null:
			continue
		var mesh := mmi.multimesh.mesh
		if mesh.has_meta("town_role") and mmi.multimesh.instance_count > 0:
			out[mesh.get_meta("town_role")] = mmi.global_transform * mmi.multimesh.get_instance_transform(0)
	return out


func _anchor_origin(role: String) -> Vector3:
	return (_anchors[role] as Transform3D).origin if _anchors.has(role) else Vector3.ZERO


func _report(t_gen: int, stats: Dictionary, style: int) -> void:
	var mmis := 0
	var instances := 0
	var building_meshes := 0
	var tris := 0
	for child in _flow.get_children():
		var mmi := child as MultiMeshInstance3D
		if mmi == null or mmi.multimesh == null:
			continue
		mmis += 1
		var count := mmi.multimesh.instance_count
		instances += count
		var mesh := mmi.multimesh.mesh
		if mesh is ArrayMesh:
			var mesh_tris := 0
			for s in mesh.get_surface_count():
				var arrays := mesh.surface_get_arrays(s)
				var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX] if arrays[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
				mesh_tris += idx.size() / 3 if idx.size() > 0 else (arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array).size() / 3
			tris += mesh_tris * count
			if mesh_tris > 1500 and not mesh.has_meta("town_role") and not mesh.has_meta("town_water"):
				building_meshes += 1
	print("river_town: gen=%d ms  mmis=%d instances=%d  building_variants~%d  tris=%.2fM  style=%s styled=%d outlined=%d anchors=%s" % [
		t_gen, mmis, instances, building_meshes, tris / 1.0e6, NprStyleKit.STYLE_NAMES[style],
		stats.styled, stats.outlined, _anchors.keys()])


# ---------------------------------------------------------------- 相机

func _build_camera_rig(focus: Vector3) -> void:
	var player := Node3D.new()
	player.name = "CameraFocus"
	player.position = focus
	add_child(player)
	var yaw := Node3D.new()
	yaw.name = "YawPivot"
	player.add_child(yaw)
	var pitch := Node3D.new()
	pitch.name = "PitchPivot"
	yaw.add_child(pitch)
	var arm := SpringArm3D.new()
	arm.name = "SpringArm"
	arm.spring_length = 120.0
	arm.collision_mask = 0
	pitch.add_child(arm)
	var camera := Camera3D.new()
	camera.name = "Camera3D"
	camera.current = true
	camera.fov = 40.0
	camera.far = 4000.0
	arm.add_child(camera)

	if ClassDB.class_exists("CameraRigController"):
		var rig = ClassDB.instantiate("CameraRigController")
		rig.name = "CameraRig"
		rig.player_path = NodePath("../CameraFocus")
		rig.yaw_pivot_path = NodePath("../CameraFocus/YawPivot")
		rig.pitch_pivot_path = NodePath("../CameraFocus/YawPivot/PitchPivot")
		rig.spring_arm_path = NodePath("../CameraFocus/YawPivot/PitchPivot/SpringArm")
		rig.default_pitch = -35.0
		rig.zoom_default = 120.0
		rig.zoom_min = 12.0
		rig.zoom_max = 600.0
		add_child(rig)


## Framings in the bridge's frame: x toward the city bank, y up, z upstream.
## Each entry is [camera position, look target], both in that frame, or anchored
## to another landmark with a third element naming it.
func _shots() -> Array:
	return [
		{ "name": "hero", "cam": Vector3(-30, 100, -175), "look": Vector3(12, 6, 30) },
		{ "name": "overview", "cam": Vector3(-20, 240, -230), "look": Vector3(0, 0, 40) },
		{ "name": "gate", "cam": Vector3(-6, 20, -62), "look": Vector3(0, 15, 0), "anchor": "gate_pier" },
		{ "name": "bridge", "cam": Vector3(-6, 3.5, -58), "look": Vector3(0, 4, 0) },
		{ "name": "street", "cam": Vector3(-8.8, 5.0, -48), "look": Vector3(-8.8, 2.5, 12), "anchor": "bridgehead" },
		{ "name": "city", "cam": Vector3(-45, 55, -85), "look": Vector3(45, 6, 0), "anchor": "gate_pier" },
	]


func _shoot(style_name: String) -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X
	var only := OS.get_environment("SHOT")
	var bridge: Transform3D = _anchors.get("bridge", Transform3D.IDENTITY)
	var n := bridge.basis.x.normalized()
	var t := bridge.basis.z.normalized()

	var cam := Camera3D.new()
	cam.fov = 40.0
	cam.far = 4000.0
	add_child(cam)
	cam.current = true

	for shot in _shots():
		if only != "" and shot.name != only:
			continue
		var origin := bridge.origin
		if shot.has("anchor"):
			origin = _anchor_origin(shot.anchor)
		var c: Vector3 = shot.cam
		var l: Vector3 = shot.look
		cam.global_position = origin + n * c.x + Vector3.UP * c.y + t * c.z
		cam.look_at(origin + n * l.x + Vector3.UP * l.y + t * l.z)
		for i in 10:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		var path := ShotOutput.file("RiverTown", "%s_%s.png" % [style_name, shot.name])
		get_viewport().get_texture().get_image().save_png(path)
		print("shot %s -> %s" % [shot.name, path])
