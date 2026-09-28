extends Node3D

# Multi-storey (多层) visual check: 重檐歇山, 重檐庑殿, 三层楼 (叉柱造 + 平座), 三层阁 (收分 + 平座 +
# 攒尖). Same cases as tests/ancient_building_storey_test.gd, which does the numeric checks.
# Run: godot --path Game/ res://Develop/StoreyValidate.tscn   (renders, so not --headless)
# Shots land in Reference/Shots/Storey/. `-- --cull` also renders every shot with back-face
# culling off, for the RoofCullDiagnose-style diff.

const ShotOutput := preload("res://Develop/Tools/shot_output.gd")

const CASES := [
	["double_eave_xieshan", {
		"width": 15.0, "depth": 10.0, "bays_x": 5, "bays_z": 3, "roof_type": 1,
		"storey_count": 2, "storey_setback_bays": 1, "upper_column_height_scale": 0.45,
		"ridge_detail": 1, "column_base_height_scale": 0.35 }],
	["double_eave_wudian", {
		"width": 17.0, "depth": 11.0, "bays_x": 7, "bays_z": 3, "roof_type": 2,
		"storey_count": 2, "storey_setback_bays": 1, "upper_column_height_scale": 0.45,
		"ridge_detail": 1, "tile_color": Color(0.55, 0.42, 0.18) }],
	["lou_three_stacked", {
		"width": 10.0, "depth": 8.0, "bays_x": 3, "bays_z": 3, "roof_type": 1,
		"storey_count": 3, "storey_setback_bays": 0, "storey_balcony": true,
		"upper_column_height_scale": 0.8, "ridge_detail": 1 }],
	["ge_three_setback", {
		"width": 12.0, "depth": 12.0, "bays_x": 5, "bays_z": 5, "roof_type": 6,
		"storey_count": 3, "storey_setback_bays": 1, "storey_balcony": true,
		"upper_column_height_scale": 0.75, "tile_color": Color(0.22, 0.31, 0.40) }],
	["double_eave_octagon_pavilion", {
		"width": 7.0, "depth": 7.0, "sides": 8, "roof_type": 6, "generate_walls": false,
		"storey_count": 2, "storey_setback_bays": 1, "upper_column_height_scale": 0.4,
		"fence_lambda": 0 }],
	["hexagon_ge", {
		"width": 9.0, "depth": 9.0, "sides": 6, "roof_type": 8,
		"storey_count": 3, "storey_setback_bays": 0, "storey_balcony": true,
		"upper_column_height_scale": 0.7, "tile_color": Color(0.30, 0.36, 0.30) }],
]

const SPACING := 30.0


func _ready() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X
	var cull := "--cull" in OS.get_cmdline_user_args()

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
	plane.size = Vector2(400, 400)
	ground.mesh = plane
	add_child(ground)

	var buildings := []
	for i in CASES.size():
		var p = ClassDB.instantiate("AncientBuildingParameters")
		for key in CASES[i][1]:
			p.set(key, CASES[i][1][key])
		var b = ClassDB.instantiate("AncientBuilding")
		b.name = CASES[i][0]
		b.parameters = p
		b.position = Vector3((i - (CASES.size() - 1) * 0.5) * SPACING, 0, 0)
		add_child(b)
		buildings.append(b)

	var cam := Camera3D.new()
	cam.fov = 38.0
	cam.far = 1000.0
	add_child(cam)
	cam.current = true

	var shots := [["overview", Vector3(0, 55, 150), Vector3(0, 10, 0)]]
	for b in buildings:
		var o: Vector3 = b.position
		shots.append([b.name + "_three_quarter", o + Vector3(22, 14, 26), o + Vector3(0, 11, 0)])
		shots.append([b.name + "_under_eave", o + Vector3(9, 5.5, 14), o + Vector3(0, 13, 0)])

	for pass_cull in ([false, true] if cull else [false]):
		for b in buildings:
			if pass_cull:
				var m := StandardMaterial3D.new()
				m.vertex_color_use_as_albedo = true
				m.cull_mode = BaseMaterial3D.CULL_DISABLED
				b.material_override = m
		for s in shots:
			cam.position = s[1]
			cam.look_at(s[2])
			for f in 8:
				await get_tree().process_frame
			await RenderingServer.frame_post_draw
			var file: String = s[0] + ("_nocull" if pass_cull else "") + ".png"
			get_viewport().get_texture().get_image().save_png(ShotOutput.file("Storey", file))
			print("shot " + file)
	get_tree().quit()
