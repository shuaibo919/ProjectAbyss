extends Node3D

# Close-up of a roof slope, to judge how the tiling actually reads rather than guessing from a
# whole-building shot.
#
# Run: godot --path Game/ res://Develop/RoofCloseup.tscn

func _ready() -> void:
	# Tile geometry is fine enough to alias badly at this resolution. Without MSAA the crown of a
	# converging roof reads as noise and it is impossible to tell a shape problem from a sampling
	# one, so judge the geometry antialiased and treat aliasing as the separate LOD question it is.
	get_viewport().msaa_3d = Viewport.MSAA_8X

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
	p.roof_type = int(OS.get_environment("ROOF")) if OS.has_environment("ROOF") else 1
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

	# Two framings: the eave corner from close range, and the whole building from a normal
	# viewing distance, because tile detail that only works in macro is not worth its triangles.
	# Derive the framings from the actual mesh bounds rather than guessing at the overhang.
	var box: AABB = b.get_aabb()
	var eave_y: float = p.get_roof_base()
	var eave_z: float = box.end.z
	var shots := {
		"RoofCloseup": [Vector3(2.2, eave_y + 1.2, 4.2), Vector3(2.6, 2.0, 3.4)],
		"RoofDistant": [Vector3(0.0, box.position.y + box.size.y * 0.55, 0.0), Vector3(17.0, 12.0, 22.0)],
		# Level with the eave and outside it: the face-on view of the drip line, which is what the
		# 瓦当 and 滴水 exist to shape.
		"RoofEave": [Vector3(0.0, eave_y - 0.1, eave_z), Vector3(1.6, 0.5, 5.5)],
		# Square on to the gable end: 山花, 博风板 and the hip end all live here, and it is the
		# elevation that shows whether the roof closes at its sides.
		"RoofSide": [Vector3(box.end.x - 1.0, eave_y + 1.6, 0.0), Vector3(13.0, 1.5, 2.0)],
	}

	for name in shots:
		var target: Vector3 = shots[name][0]
		camera.position = target + shots[name][1]
		camera.look_at(target)
		for i in 4:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(
			ProjectSettings.globalize_path("res://Develop/%s.png" % name))

	print("tris=%d tile_course_width=%.2f module=%.2f" % [
		b.get_triangle_count(), p.tile_course_width, p.get_module()])

	get_tree().quit()
