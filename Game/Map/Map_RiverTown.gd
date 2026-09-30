@tool
extends Node3D

# 水城 StyleShowcase: a small river town assembled entirely by PCG, used as the
# common subject for comparing NPR treatments. Reference composition:
# Reference/screenshot-20260928-123404.png (walled city with a tiered gate tower
# on the left bank, waterfront street on the right, arch bridge with a covered
# gallery between them).
#
# The script is @tool: opening the scene in the editor builds the town once
# (plain look, no NPR) and drops one transient "PreviewCam …" camera per framing
# — select one in the scene tree and tick the viewport's Preview checkbox.
# Nothing the preview adds has an owner, so saving the scene stays clean.
#
#   river_town_lots ─ Buildings ─ ancient_building ─ spawn_meshes
#                   ├ Roads / Walls / Props / Yard Walls ─ spawn_meshes
#                   ├ Trees ─ cached SlowTree Peach / Bamboo variants ─ spawn_meshes
#                   └ Structures (water, land, 驳岸, piers, bridge, streets, hills) ─ spawn_meshes
#
# Run: Engine/bin/godot.windows.editor.x86_64.console.exe --path Game/ res://Map/Map_RiverTown.tscn
#   RIVER_STYLE=plain|ink|outline|ink_outline|textured
#                                               NPR pass (default plain; see npr_style_kit.gd);
#                                               textured = ambientCG albedo + normal maps
#   RIVER_TERRAIN=1                             Terrain3D ground: mountain valley, river carved into
#                                               it, Terrain3D water (river_town_terrain.gd); 0 = the
#                                               old land/water sheets and backdrop ridges
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
const Vegetation := preload("res://Script/PCG/river_town_vegetation.gd")
const TownTextures := preload("res://Script/NPR/town_texture_library.gd")
const RiverTownLots := preload("res://addons/ancient_town/nodes/river_town_lots.gd")
const RiverTownTerrain := preload("res://Map/river_town_terrain.gd")

const AB_NODE_DIR := "res://addons/ancient_building/nodes"
const TOWN_NODE_DIR := "res://addons/ancient_town/nodes"
const PAPER := Color(0.898, 0.859, 0.824)
const WATER_DEPTH := 3.2
const TREE_WATER_CLEARANCE := 0.15

## Build the town in the editor too. Everything it adds is transient (no owner), so it never
## lands in the saved scene.
@export var editor_preview := true:
	set(value):
		editor_preview = value
		if Engine.is_editor_hint() and is_inside_tree():
			_rebuild_editor()

## Layout seed for the editor preview (game runs use RIVER_SEED).
@export var preview_seed := 7:
	set(value):
		preview_seed = value
		if Engine.is_editor_hint() and is_inside_tree() and editor_preview:
			_rebuild_editor()

## Editor preview look and ground (game runs use RIVER_STYLE / RIVER_TERRAIN).
@export_enum("plain", "ink", "outline", "ink_outline", "textured") var preview_style := "textured":
	set(value):
		preview_style = value
		if Engine.is_editor_hint() and is_inside_tree() and editor_preview:
			_rebuild_editor()
@export var preview_terrain := true:
	set(value):
		preview_terrain = value
		if Engine.is_editor_hint() and is_inside_tree() and editor_preview:
			_rebuild_editor()

@export_group("Bamboo Forest")
## Spacing between clumps; each shared bamboo mesh contains three culms.
@export_range(1.8, 5.0, 0.1) var bamboo_spacing := 2.4:
	set(value):
		bamboo_spacing = value
		if Engine.is_editor_hint() and is_inside_tree() and editor_preview:
			_rebuild_editor()
@export_range(15.0, 100.0, 1.0) var bamboo_forest_depth := 54.0:
	set(value):
		bamboo_forest_depth = value
		if Engine.is_editor_hint() and is_inside_tree() and editor_preview:
			_rebuild_editor()

var _flow: FlowGraphNode3D
var _anchors := {}
var _terrain: Terrain3D


func _ready() -> void:
	if Engine.is_editor_hint():
		if editor_preview and _flow == null:
			_build_editor_preview()
		return
	# TreeGen's fine masked leaf cards use alpha-to-coverage at town viewing distances.
	if get_viewport().msaa_3d == Viewport.MSAA_DISABLED:
		get_viewport().msaa_3d = Viewport.MSAA_4X
	var style := NprStyleKit.parse(_env_or("RIVER_STYLE", "plain"))
	var seed := _env_or("RIVER_SEED", "7").to_int()
	var terrain := _env_or("RIVER_TERRAIN", "1") != "0"

	var fills := _build_environment(style)
	var t0 := Time.get_ticks_msec()
	_build_town(seed, style, terrain)
	if terrain:
		await _build_terrain(seed)
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


# ---------------------------------------------------------------- 编辑器预览

## Builds the town once for the editor, in the plain look, and drops one transient camera per
## framing. FlowGraphNode3D only auto-executes in game runs, so here we call execute() ourselves.
func _build_editor_preview() -> void:
	var before := get_children()
	var style := NprStyleKit.parse(preview_style)
	var fills := _build_environment(style)
	_build_town(preview_seed, style, preview_terrain)
	_flow.execute()
	if preview_terrain:
		await _build_terrain(preview_seed)
	var stats := NprStyleKit.apply(_flow, style, {
		"fill_dirs": fills,
		"lod_world_width": 0.08,
	})
	_anchors = _find_anchors()
	_strip_owners(_flow)
	_add_preview_cameras()
	for child in get_children():
		# Cameras stay owned (they are saved, and rebuilt by name), everything else transient.
		if not before.has(child) and not String(child.name).begins_with("PreviewCam "):
			child.set_meta("river_preview", true)
	print("river_town: editor preview built (seed %d, %d anchors, styled %d)" % [
		preview_seed, _anchors.size(), stats.styled])


## spawn_meshes gives instances an owner where it can; stripping every owner under the flow node
## makes the whole preview transient, so saving the scene keeps only the .tscn nodes.
func _strip_owners(root: Node) -> void:
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		node.owner = null
		stack.append_array(node.get_children())


func _rebuild_editor() -> void:
	for child in get_children():
		if child.has_meta("river_preview"):
			child.free()
	_flow = null
	_anchors = {}
	if editor_preview:
		_build_editor_preview()


## One Camera3D per shot framing, named "PreviewCam <name>". Select one and tick the viewport's
## Preview checkbox to look through it. Unlike the town itself these are owned by the scene root:
## the Scene dock hides ownerless nodes, so without an owner the cameras would be invisible to
## click on. They are a handful of tiny nodes — safe to save — and are rebuilt by name, so a new
## seed never leaves stale ones behind.
func _add_preview_cameras() -> void:
	for child in get_children():
		if String(child.name).begins_with("PreviewCam "):
			child.free()
	var bridge: Transform3D = _anchors.get("bridge", Transform3D.IDENTITY)
	var n := bridge.basis.x.normalized()
	var t := bridge.basis.z.normalized()
	for shot in _shots():
		var origin := bridge.origin
		if shot.has("anchor"):
			if not _anchors.has(shot.anchor):
				continue
			origin = _anchor_origin(shot.anchor)
		var cam := Camera3D.new()
		cam.name = "PreviewCam " + String(shot.name)
		cam.fov = 40.0
		cam.far = 4000.0
		add_child(cam)
		cam.owner = self
		var c: Vector3 = shot.cam
		var l: Vector3 = shot.look
		cam.global_position = origin + n * c.x + Vector3.UP * c.y + t * c.z
		cam.look_at(origin + n * l.x + Vector3.UP * l.y + t * l.z)


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

func _build_town(seed: int, style: int, terrain: bool) -> void:
	FlowNodeRegistry.register_node_directory(AB_NODE_DIR)
	FlowNodeRegistry.register_node_directory(TOWN_NODE_DIR)

	_flow = FlowGraphNode3D.new()
	_flow.name = "RiverTownPCG"
	_flow.graph = _town_graph(seed, style, terrain)
	add_child(_flow)  # FlowGraphNode3D._ready() evaluates the graph in game runs


## The Terrain3D ground, from the terrain description river_town_lots published while the graph ran.
func _build_terrain(seed: int) -> void:
	if RiverTownLots.last_terrain.is_empty():
		push_error("river_town: the layout published no terrain description")
		return
	_terrain = await RiverTownTerrain.build(self, RiverTownLots.last_terrain, seed)
	_settle_on_terrain()


## Place trees on dry terrain, excluding old courtyard points that fall over the river.
## Buildings are only checked: the town stamp keeps their ground flat.
func _settle_on_terrain() -> void:
	var water_y: float = RiverTownLots.last_terrain.get("water_y", 0.0)
	var worst := 0.0
	var over_water := 0
	var rejected_trees := 0
	for child in _flow.get_children():
		var mmi := child as MultiMeshInstance3D
		if mmi == null or mmi.multimesh == null or mmi.multimesh.mesh == null:
			continue
		var mm := mmi.multimesh
		var is_tree := mm.mesh.has_meta("town_tree")
		var is_building := mm.mesh.get_surface_count() > 1
		if is_tree:
			rejected_trees += _settle_vegetation(mmi, water_y)
			continue
		if not is_building:
			continue
		for i in mm.instance_count:
			var xf := mmi.global_transform * mm.get_instance_transform(i)
			var h := _terrain.data.get_height(xf.origin)
			if is_nan(h):
				continue
			if h < water_y:
				over_water += 1  # 水榭 stand on piles in the river by design
			else:
				worst = maxf(worst, absf(h - xf.origin.y))
	print("river_town: buildings sit within %.2f m of the terrain (%d stilted over water)" % [worst, over_water])
	if rejected_trees > 0:
		print("river_town: removed %d vegetation points without dry ground" % rejected_trees)


func _settle_vegetation(mmi: MultiMeshInstance3D, water_y: float) -> int:
	var mm := mmi.multimesh
	var original_count := mm.instance_count
	var transforms: Array[Transform3D] = []
	var colors := PackedColorArray()
	var custom_data := PackedColorArray()
	var to_local := mmi.global_transform.affine_inverse()
	for index in original_count:
		var xf := mmi.global_transform * mm.get_instance_transform(index)
		var height := _terrain.data.get_height(xf.origin)
		if is_nan(height) or height <= water_y + TREE_WATER_CLEARANCE:
			continue
		xf.origin.y = height
		transforms.append(to_local * xf)
		if mm.use_colors:
			colors.append(mm.get_instance_color(index))
		if mm.use_custom_data:
			custom_data.append(mm.get_instance_custom_data(index))
	# Resizing clears the GPU buffer, so preserve instance attributes before compaction.
	if transforms.size() != original_count:
		mm.instance_count = transforms.size()
	for index in transforms.size():
		mm.set_instance_transform(index, transforms[index])
		if mm.use_colors:
			mm.set_instance_color(index, colors[index])
		if mm.use_custom_data:
			mm.set_instance_custom_data(index, custom_data[index])
	return original_count - transforms.size()


func _town_graph(seed: int, style: int, terrain: bool) -> FlowGraphResource:
	var b := FlowGraphBuilder.new()

	var lots := b.AddNode("river_town_lots", {
		"random_seed": seed,
		"water_depth": WATER_DEPTH,
		"terrain_ground": terrain,
		"bamboo_spacing": bamboo_spacing,
		"bamboo_forest_depth": bamboo_forest_depth,
	}, Vector2(0, 0))

	# One baked mesh per distinct parameter combination. No size jitter: tiered
	# towers and the bridge gallery are stacked from exact Table 1 heights.
	var building_settings := {
		"mesh_attribute": "mesh",
		# Courtyards bring many more distinct sizes than the old rows; past this budget the node
		# widens its size bins and the planned gaps between neighbours would drift.
		"variant_count": 192,
		"seed": seed + 7,
		"size_jitter": 0.0,
		"ridge_detail": 1,
	}
	if style == NprStyleKit.EStyle.TEXTURED:
		building_settings["slot_materials"] = TownTextures.building_slot_materials()
	var buildings := b.AddNode("ancient_building", building_settings, Vector2(320, 0), { "Points": 0 })

	var road := TownMeshes.road()
	road.set_meta("town_material", "paving")
	var wall := TownMeshes.unit_box(TownMeshes.CITY_WALL_COL)
	wall.set_meta("town_material", "brick")
	var yard_wall := TownMeshes.yard_wall()
	yard_wall.set_meta("town_material", "plaster")
	var trees := Vegetation.meshes()

	var spawns := [
		[buildings, 0, { "mesh_attribute": "mesh" }],
		[lots, 1, { "mesh": road }],
		[lots, 2, { "mesh": wall }],
		[lots, 3, {
			"mesh_variants": [TownMeshes.stall(), TownMeshes.well(), TownMeshes.archway(),
				TownMeshes.merlon(), TownMeshes.boat_wupeng(), TownMeshes.boat_cargo(),
				TownMeshes.figure(Color(0.30, 0.33, 0.38)), TownMeshes.figure(Color(0.66, 0.58, 0.44)),
				TownMeshes.river_steps(3.2, WATER_DEPTH, 4.5), TownMeshes.yard_gate()],
			"mesh_selector_attribute": "prop_type",
		}],
		[lots, 4, {
			"mesh_variants": trees,
			"mesh_selector_attribute": Vegetation.VARIANT_ATTRIBUTE,
			"instance_cell_size": Vegetation.INSTANCE_CELL_SIZE,
		}],
		[lots, 5, { "mesh_attribute": "mesh" }],
		[lots, 6, { "mesh": yard_wall }],
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
	var triangle_counts := {}
	var vegetation := {"peach": 0, "bamboo": 0}
	for child in _flow.get_children():
		var mmi := child as MultiMeshInstance3D
		if mmi == null or mmi.multimesh == null:
			continue
		mmis += 1
		var count := mmi.multimesh.instance_count
		instances += count
		var mesh := mmi.multimesh.mesh
		if mesh != null and mesh.has_meta("town_tree_species"):
			vegetation[mesh.get_meta("town_tree_species")] += count
		if mesh is ArrayMesh:
			# Many spatial batches share each tree. Decode its geometry only once for diagnostics.
			if not triangle_counts.has(mesh):
				var total := 0
				for s in mesh.get_surface_count():
					var arrays := mesh.surface_get_arrays(s)
					var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX] if arrays[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
					total += idx.size() / 3 if idx.size() > 0 else (arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array).size() / 3
				triangle_counts[mesh] = total
			var mesh_tris: int = triangle_counts[mesh]
			tris += mesh_tris * count
			if mesh_tris > 1500 and not mesh.has_meta("town_role") and not mesh.has_meta("town_water") and not mesh.has_meta("town_tree"):
				building_meshes += 1
	print("river_town: gen=%d ms  mmis=%d instances=%d  building_variants~%d  tris=%.2fM  style=%s styled=%d outlined=%d anchors=%s" % [
		t_gen, mmis, instances, building_meshes, tris / 1.0e6, NprStyleKit.STYLE_NAMES[style],
		stats.styled, stats.outlined, _anchors.keys()])
	print("river_town vegetation: %d peach trees, %d bamboo clumps (%d culms), 6 shared meshes" % [
		vegetation.peach, vegetation.bamboo, vegetation.bamboo * 3])


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
	if _terrain != null:
		_terrain.set_camera(camera)

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
		# 街巷 / 院落 (2026-09-30): eye height in a waterfront lane looking inland, and a bird's-eye
		# over the 衙署 courtyards.
		{ "name": "lane", "cam": Vector3(4.0, 1.7, 0.0), "look": Vector3(-30.0, 2.5, 0.0), "anchor": "lane" },
		{ "name": "courtyard", "cam": Vector3(-38, 42, -34), "look": Vector3(0, 2, 0), "anchor": "office" },
		{ "name": "bamboo", "cam": Vector3(-14, 6, -18), "look": Vector3(0, 3, 0), "anchor": "bamboo_grove" },
		{ "name": "bamboo_forest", "cam": Vector3(-74, 48, -98), "look": Vector3(-14, 5, 30), "anchor": "bamboo_grove" },
		{ "name": "bamboo_walk", "cam": Vector3(-3, 1.7, -7), "look": Vector3(-25, 3.0, 14), "anchor": "bamboo_grove" },
		{ "name": "peach", "cam": Vector3(-5.5, 2.7, -8.5), "look": Vector3(0, 1, 0), "anchor": "peach_garden" },
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
	if _terrain != null:
		# The clipmap follows the camera it was given, not whichever one is current.
		_terrain.set_camera(cam)

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
