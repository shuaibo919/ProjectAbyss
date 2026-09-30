extends Node3D

# TypeGallery — one framed shot per building type AncientBuilding supports, written to
# Reference/Shots/TypeGallery/<key>.png, then tiled by Develop/Tools/build_type_gallery.py
# into a labelled summary sheet. Run:
#   Engine/bin/godot.windows.editor.x86_64.console.exe --path Game/ --resolution 800x600 res://Develop/TypeGallery.tscn

const PAPER := Color(0.898, 0.859, 0.824)
const SPACING := 80.0

var _entries: Array[Dictionary] = []


func _ready() -> void:
	_build_environment()
	_define_entries()

	var camera := Camera3D.new()
	camera.fov = 40.0
	camera.far = 4000.0
	add_child(camera)
	camera.current = true

	var out_dir := ProjectSettings.globalize_path("res://").path_join("../Reference/Shots/TypeGallery")
	DirAccess.make_dir_recursive_absolute(out_dir)

	for i in _entries.size():
		var entry: Dictionary = _entries[i]
		var node: MeshInstance3D = entry.builder.call()
		node.position = Vector3(float(i % 5) * SPACING, 0.0, float(i / 5) * SPACING)
		add_child(node)
		await get_tree().process_frame

		var aabb := node.get_aabb()
		var centre := node.position + aabb.get_center()
		var size := aabb.size
		var dist := (maxf(size.x, size.z) * 0.5 + size.y * 0.5) * 2.3 + 4.0
		var dir := Vector3(0.85, 0.62, 1.0).normalized()
		camera.global_position = centre + dir * dist
		camera.look_at(centre + Vector3(0.0, size.y * 0.12, 0.0))
		for j in 8:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		var path := out_dir.path_join("%02d_%s.png" % [i, entry.key])
		get_viewport().get_texture().get_image().save_png(path)
		print("tile %s -> %s" % [entry.key, path])
		node.queue_free()
		await get_tree().process_frame

	get_tree().quit()


func _build_environment() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, -32, 0)
	sun.light_energy = 0.8
	sun.shadow_enabled = true
	add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-45, 130, 0)
	fill.light_energy = 0.2
	add_child(fill)
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = PAPER
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.86, 0.84, 0.82)
	e.ambient_light_energy = 0.32
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.environment = e
	add_child(env)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(4000, 4000)
	ground.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.80, 0.78, 0.72)
	mat.roughness = 0.95
	ground.material_override = mat
	add_child(ground)


func _building(params: Dictionary) -> MeshInstance3D:
	var b = ClassDB.instantiate("AncientBuilding")
	var p = ClassDB.instantiate("AncientBuildingParameters")
	for key in params:
		p.set(key, params[key])
	b.parameters = p
	return b


func _archetype(index: int) -> MeshInstance3D:
	var b = ClassDB.instantiate("AncientBuilding")
	var p = ClassDB.instantiate("AncientBuildingParameters")
	p.apply_archetype(index)
	b.parameters = p
	return b


func _compound(index: int) -> MeshInstance3D:
	var c = ClassDB.instantiate("AncientBuildingCompound")
	c.apply_preset(index)
	var mi := MeshInstance3D.new()
	mi.mesh = c.bake_mesh()
	c.free()
	return mi


func _masonry(values: Dictionary) -> MeshInstance3D:
	var m = ClassDB.instantiate("AncientMasonry")
	for key in values:
		m.set(key, values[key])
	var mi := MeshInstance3D.new()
	mi.mesh = m.bake_mesh()
	m.free()
	return mi


func _define_entries() -> void:
	var hall := {"width": 12.0, "depth": 8.0, "bays_x": 5, "bays_z": 3, "fence": true, "steps": true}
	var roofs := [
		["roof_yingshan", 0], ["roof_xuanshan", 3], ["roof_wudian", 2], ["roof_xieshan", 1],
		["roof_juanpeng", 4], ["roof_luding", 5], ["roof_cuanjian", 6], ["roof_yuancuanjian", 7],
		["roof_kuiding", 8],
	]
	for r in roofs:
		var params: Dictionary = hall.duplicate()
		params["roof_type"] = r[1]
		if int(r[1]) >= 5:
			# Centralised family: square plan, no bay counts (Eq 8).
			params["depth"] = params["width"]
		_entries.append({"key": r[0], "builder": _building.bind(params)})

	_entries.append({"key": "storey_chongyan_xieshan", "builder": _building.bind({
		"width": 14.0, "depth": 9.0, "bays_x": 5, "bays_z": 3, "roof_type": 1,
		"storey_count": 2, "storey_setback": 1, "storey_balcony": true, "upper_column_scale": 0.7,
		"fence": true, "steps": true})})
	_entries.append({"key": "storey_sanceng_ge", "builder": _building.bind({
		"width": 10.0, "depth": 10.0, "roof_type": 6, "sides": 6,
		"storey_count": 3, "storey_setback": 1, "storey_balcony": true, "upper_column_scale": 0.75,
		"steps": true})})

	for i in 6:
		_entries.append({"key": "arch_%d" % i, "builder": _archetype.bind(i)})

	_entries.append({"key": "masonry_gate_tower", "builder": _building.bind({
		"width": 14.0, "depth": 9.0, "bays_x": 5, "bays_z": 3, "roof_type": 1,
		"base_kind": 1, "base_height": 6.0, "base_margin": 3.0, "base_arches": 1, "base_parapet": 1,
		"storey_count": 2, "storey_setback": 1, "upper_column_scale": 0.7})})
	_entries.append({"key": "masonry_wall", "builder": _masonry.bind({
		"length": 24.0, "height": 7.0, "thickness": 3.0, "batter_faces": 0.08, "parapet": 1,
		"plinth_height": 0.6, "arch_count": 0, "brick_color": Color(0.42, 0.41, 0.40),
		"stone_color": Color(0.5, 0.49, 0.46)})})
	_entries.append({"key": "masonry_bridge", "builder": _masonry.bind({
		"length": 22.0, "thickness": 7.0, "height": 4.5, "arch_count": 3, "arch_width": 5.0,
		"arch_pitch": 6.6, "arch_height_ratio": 0.55, "parapet": 0, "ring_thickness": 0.45,
		"brick_color": Color(0.48, 0.47, 0.45), "stone_color": Color(0.55, 0.54, 0.51)})})

	for i in 5:
		_entries.append({"key": "compound_%d" % i, "builder": _compound.bind(i)})
