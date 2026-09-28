extends Node3D

# 砖石作 visual check: 城门楼 (城台 + 三层楼), 箭楼 (brick lower storey), 角楼 (攒尖 on a 城台),
# a crenellated wall run with a 水门, and a three-arch bridge — all from the one arched-slab
# primitive. Numeric checks live in tests/ancient_building_masonry_test.gd.
# Run: godot --path Game/ res://Develop/MasonryValidate.tscn   (renders, so not --headless)
# Shots land in Reference/Shots/Masonry/.

const ShotOutput := preload("res://Develop/Tools/shot_output.gd")

const BUILDINGS := [
	["gate_tower", Vector3(-46, 0, 0), {
		"width": 16.0, "depth": 10.0, "bays_x": 5, "bays_z": 3, "roof_type": 1,
		"base_kind": 1, "masonry_base_height": 8.0, "masonry_base_margin": 5.0, "base_arch_count": 1,
		"storey_count": 3, "storey_setback_bays": 1, "storey_balcony": true,
		"upper_column_height_scale": 0.7, "ridge_detail": 1, "tile_color": Color(0.22, 0.31, 0.40) }],
	["arrow_tower", Vector3(-10, 0, 0), {
		"width": 14.0, "depth": 9.0, "bays_x": 5, "bays_z": 3, "roof_type": 1,
		"storey_count": 2, "masonry_storeys": 1, "storey_setback_bays": 0,
		"upper_column_height_scale": 0.55, "ridge_detail": 1 }],
	["corner_pavilion", Vector3(22, 0, 0), {
		"width": 7.0, "depth": 7.0, "sides": 8, "roof_type": 6, "generate_walls": false,
		"base_kind": 1, "masonry_base_height": 6.0, "masonry_base_margin": 2.5, "base_arch_count": 0,
		"storey_count": 2, "storey_setback_bays": 1, "upper_column_height_scale": 0.4 }],
]


func _ready() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-38, -35, 0)
	sun.light_energy = 0.85
	sun.shadow_enabled = true
	add_child(sun)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.86, 0.85, 0.82)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.9, 0.88, 0.85)
	env.environment.ambient_light_energy = 0.45
	add_child(env)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(500, 500)
	ground.mesh = plane
	add_child(ground)

	for entry in BUILDINGS:
		var p = ClassDB.instantiate("AncientBuildingParameters")
		for key in entry[2]:
			p.set(key, entry[2][key])
		var b = ClassDB.instantiate("AncientBuilding")
		b.name = entry[0]
		b.parameters = p
		b.position = entry[1]
		add_child(b)

	# Wall run either side of a 水门, and a bridge.
	var wall_values := { "length": 30.0, "thickness": 5.0, "height": 8.0, "batter_faces": 0.08,
		"arch_count": 0, "parapet": 1 }
	for i in 2:
		_add_masonry(wall_values, Vector3(-6 + i * 44, 0, 40))
	_add_masonry({ "length": 14.0, "thickness": 6.0, "height": 8.0, "batter_faces": 0.08, "arch_count": 1,
		"arch_height_ratio": 0.6, "parapet": 1, "arch_profile": 1 }, Vector3(16, 0, 40))
	_add_masonry({ "length": 40.0, "thickness": 7.0, "height": 6.0, "arch_count": 3, "arch_height_ratio": 0.8,
		"batter_faces": 0.0, "parapet": 2, "plinth_height": 0.0 }, Vector3(-50, 0, 44))

	var cam := Camera3D.new()
	cam.fov = 40.0
	cam.far = 1000.0
	add_child(cam)
	cam.current = true
	var shots := [
		["overview", Vector3(-8, 60, 120), Vector3(-8, 6, 15)],
		["gate_tower", Vector3(-20, 16, 38), Vector3(-46, 13, 0)],
		["gate_passage", Vector3(-46, 3.5, 24), Vector3(-46, 3.2, 0)],
		["arrow_tower", Vector3(8, 9, 26), Vector3(-10, 8, 0)],
		["corner_pavilion", Vector3(36, 10, 20), Vector3(22, 8, 0)],
		["wall_and_water_gate", Vector3(16, 9, 68), Vector3(16, 4, 40)],
		["bridge", Vector3(-50, 4, 76), Vector3(-50, 3, 44)],
	]
	for s in shots:
		cam.position = s[1]
		cam.look_at(s[2])
		for f in 8:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(ShotOutput.file("Masonry", s[0] + ".png"))
		print("shot " + s[0])
	get_tree().quit()


func _add_masonry(values: Dictionary, at: Vector3) -> void:
	var m = ClassDB.instantiate("AncientMasonry")
	for key in values:
		m.set(key, values[key])
	m.position = at
	add_child(m)
