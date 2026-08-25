extends Node3D

# Close-up of a roof slope, to judge how the tiling actually reads rather than guessing from a
# whole-building shot.
#
# Run: godot --path Game/ res://Develop/RoofCloseup.tscn

func _ready() -> void:
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-38, -50, 0)
	light.light_energy = 1.6
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.63, 0.71, 0.81)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.55, 0.58, 0.62)
	e.ambient_light_energy = 0.7
	env.environment = e
	add_child(env)

	var p = ClassDB.instantiate("AncientBuildingParameters")
	p.roof_type = 1
	p.width = 11.0
	p.depth = 7.0

	var b = ClassDB.instantiate("AncientBuilding")
	b.auto_regenerate = false
	b.parameters = p
	add_child(b)
	b.generate()

	var camera := Camera3D.new()
	camera.current = true
	camera.fov = 35.0
	add_child(camera)

	# Frame the eave corner from just above and outside it: the angle a player sees a roof from,
	# and where tile articulation matters most.
	var eave_y: float = p.get_roof_base()
	var target := Vector3(2.2, eave_y + 1.2, 4.2)
	camera.position = target + Vector3(2.6, 2.0, 3.4)
	camera.look_at(target)

	for i in 8:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(
		ProjectSettings.globalize_path("res://Develop/RoofCloseup.png"))

	print("tris=%d tile_course_width=%.2f module=%.2f" % [
		b.get_triangle_count(), p.tile_course_width, p.get_module()])

	get_tree().quit()
