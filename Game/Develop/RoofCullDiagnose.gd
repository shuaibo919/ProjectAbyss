extends Node3D

# Decides whether a surface that looks missing is single-sided (a winding/one-layer problem) or
# genuinely absent, by rendering the same camera twice — once as shipped, once with backface
# culling switched off — and diffing the two images.
#
# Anything that appears only in the culling-off pass is a one-sided surface being viewed from
# behind. Anything still missing in both is geometry that was never generated. The diff image
# marks the difference in red so it is obvious which.
#
# Run: godot --path Game/ res://Develop/RoofCullDiagnose.tscn   (ROOF=n to pick a roof type)

func _shot(path: String) -> Image:
	for i in 4:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var img: Image = get_viewport().get_texture().get_image()
	img.save_png(ProjectSettings.globalize_path(path))
	return img

func _ready() -> void:
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

	var roof_type := int(OS.get_environment("ROOF")) if OS.has_environment("ROOF") else 1
	var p = ClassDB.instantiate("AncientBuildingParameters")
	p.roof_type = roof_type
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

	var box: AABB = b.get_aabb()
	var eave_y: float = p.get_roof_base()
	var target := Vector3(box.end.x - 1.0, eave_y + 1.6, 0.0)
	camera.position = target + Vector3(13.0, 1.5, 2.0)
	camera.look_at(target)

	var before: Image = await _shot("res://Develop/RoofCullOn.png")

	# Now the same frame with every face two-sided.
	var mat: BaseMaterial3D = (b.mesh as ArrayMesh).surface_get_material(0)
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	var after: Image = await _shot("res://Develop/RoofCullOff.png")

	# Diff: pixels that changed are surfaces only the two-sided pass could draw.
	var diff := Image.create(before.get_width(), before.get_height(), false, Image.FORMAT_RGB8)
	var changed := 0
	for y in before.get_height():
		for x in before.get_width():
			var c0: Color = before.get_pixel(x, y)
			var c1: Color = after.get_pixel(x, y)
			if abs(c0.r - c1.r) + abs(c0.g - c1.g) + abs(c0.b - c1.b) > 0.06:
				diff.set_pixel(x, y, Color(1, 0, 0))
				changed += 1
			else:
				diff.set_pixel(x, y, c1 * 0.45)
	diff.save_png(ProjectSettings.globalize_path("res://Develop/RoofCullDiff.png"))

	var pixels := before.get_width() * before.get_height()
	print("ROOF=%d  changed pixels=%d / %d  (%.2f%%)" % [
		roof_type, changed, pixels, 100.0 * float(changed) / float(pixels)])
	if changed * 400 < pixels:
		print("VERDICT: culling is not hiding anything meaningful here")
	else:
		print("VERDICT: %.2f%% of the frame is one-sided surfaces seen from behind" % [
			100.0 * float(changed) / float(pixels)])

	get_tree().quit()
