extends Node3D

# Three AncientBuilding material styles side by side: 官式 / 茅草 / 土木.
# Same 硬山 geometry, different palettes + per-piece colour mottle.
# Run: godot --path Game/ res://Develop/StyleShowcase.tscn [-- --out=Name]

const ShotOutput := preload("res://Develop/Tools/shot_output.gd")


func _ready() -> void:
	var out := "building_styles"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.substr(6)

	# Ink environment (paper background + flat light), same as the validation map.
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-42, -38, 0)
	light.light_energy = 1.1
	light.shadow_enabled = true
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.898, 0.859, 0.824)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.86, 0.84, 0.82)
	e.ambient_light_energy = 0.9
	env.environment = e
	add_child(env)

	var cam := Camera3D.new()
	cam.position = Vector3(0, 22, 34)
	cam.current = true
	add_child(cam)
	cam.look_at(Vector3(0, 3, 0))

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(200, 200)
	ground.mesh = plane
	add_child(ground)

	for style in 3:
		var building = ClassDB.instantiate("AncientBuilding")
		building.name = "Style_%d" % style
		building.position = Vector3(-18.0 + 18.0 * style, 0, 0)
		var params = ClassDB.instantiate("AncientBuildingParameters")
		params.material_style = style
		building.parameters = params
		add_child(building)

	for i in 12:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	get_viewport().get_texture().get_image().save_png(
		ShotOutput.file("Ink", "%s.png" % out))
	print("saved %s" % out)
	get_tree().quit()
