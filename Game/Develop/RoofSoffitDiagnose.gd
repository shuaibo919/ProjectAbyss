extends Node3D

# Answers one question about the roof: is the missing underside a winding bug, or is there simply
# no downward-facing geometry there?
#
# Those have opposite fixes, and they look identical from the one angle you normally check from.
# A flipped normal would also make the surface vanish when viewed from ABOVE, so the histogram
# below is the discriminator: it counts triangles by the sign of their normal's Y.
#
# Run: godot --path Game/ res://Develop/RoofSoffitDiagnose.tscn

func _ready() -> void:
	var p = ClassDB.instantiate("AncientBuildingParameters")
	p.roof_type = int(OS.get_environment("ROOF")) if OS.has_environment("ROOF") else 1
	p.width = 11.0
	p.depth = 7.0

	var b = ClassDB.instantiate("AncientBuilding")
	b.auto_regenerate = false
	b.parameters = p
	add_child(b)
	b.generate()

	var mesh: ArrayMesh = b.mesh
	var arrays: Array = mesh.surface_get_arrays(0)
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var norms: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]

	var roof_base: float = p.get_roof_base()

	# Bucket every triangle above the eave line — i.e. the roof — by which way it faces.
	var up := 0
	var down := 0
	var side := 0
	var lowest_down := INF

	for t in range(0, idx.size(), 3):
		var a: Vector3 = verts[idx[t]]
		var bb: Vector3 = verts[idx[t + 1]]
		var c: Vector3 = verts[idx[t + 2]]
		var centre: Vector3 = (a + bb + c) / 3.0
		if centre.y < roof_base - 0.3:
			continue

		var n: Vector3 = (norms[idx[t]] + norms[idx[t + 1]] + norms[idx[t + 2]]).normalized()
		if n.y > 0.25:
			up += 1
		elif n.y < -0.25:
			down += 1
			lowest_down = min(lowest_down, centre.y)
		else:
			side += 1

	print("ROOF TRIANGLES  up=%d  down=%d  side=%d" % [up, down, side])
	print("down/up ratio = %.3f" % (float(down) / max(float(up), 1.0)))
	if down == 0:
		print("VERDICT: no downward-facing roof geometry at all -> missing surface, not bad winding")
	else:
		print("VERDICT: %d downward triangles exist, lowest at y=%.2f" % [down, lowest_down])

	# And a look from directly underneath, which is the view that exposed this.
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

	var camera := Camera3D.new()
	camera.current = true
	camera.fov = 55.0
	add_child(camera)

	var box: AABB = b.get_aabb()
	var target := Vector3(0.0, roof_base + 1.0, box.end.z - 1.0)
	camera.position = Vector3(0.0, p.get_platform_height() + 0.6, box.end.z + 2.0)
	camera.look_at(target)

	for i in 6:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(
		ProjectSettings.globalize_path("res://Develop/RoofSoffit.png"))

	get_tree().quit()
