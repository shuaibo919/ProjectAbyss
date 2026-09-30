extends SceneTree

var output_directory := "res://Develop/TreeGenShots"
var records: Array = []
var meshes: Array[Mesh] = []

func _initialize():
	call_deferred("run")

func mesh_metrics(mesh: Mesh) -> Dictionary:
	var vertices = 0
	var triangles = 0
	var reversed_duplicates = 0
	for s in mesh.get_surface_count():
		var arrays = mesh.surface_get_arrays(s)
		vertices += arrays[Mesh.ARRAY_VERTEX].size()
		var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		triangles += idx.size() / 3
		var seen = {}
		for i in range(0, idx.size(), 3):
			var a = idx[i]
			var b = idx[i + 1]
			var c = idx[i + 2]
			var key = Vector3i(mini(a, mini(b, c)), a + b + c - mini(a, mini(b, c)) - maxi(a, maxi(b, c)), maxi(a, maxi(b, c)))
			if seen.has(key):
				reversed_duplicates += 1
			else:
				seen[key] = true
	return {"vertices": vertices, "triangles": triangles, "duplicate_index_triangles": reversed_duplicates, "surfaces": mesh.get_surface_count(), "aabb": str(mesh.get_aabb())}

func run():
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):
			output_directory = arg.trim_prefix("--output=")
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(output_directory))
	for preset in range(1, 7):
		var chosen_preset := preset
		var chosen_seed := 0
		var chosen_season := 2.0
		if "--orbit" in OS.get_cmdline_user_args():
			chosen_preset = 1
			chosen_seed = 0 if preset < 5 else 21
		if "--seasons" in OS.get_cmdline_user_args():
			chosen_preset = 3 if preset < 5 else 6
			chosen_season = float(preset - 1) if preset < 5 else (1.0 if preset == 5 else 2.0)
		if "--ginkgo" in OS.get_cmdline_user_args():
			chosen_preset = 3
			chosen_seed = [0, 17, 42, 0, 0, 0][preset - 1]
			chosen_season = 3.0 if preset < 4 or preset == 6 else (0.0 if preset == 4 else 2.0)
		var result = SlowTreeGenerator.generate(chosen_preset, chosen_seed, false, chosen_season, {"crossed_cards": true, "species_rules": true})
		if result.error != "":
			push_error(result.error)
			quit(1)
			return
		var mesh: Mesh = result.mesh
		meshes.append(mesh)
		var info = mesh_metrics(mesh)
		info["name"] = SlowTreeGenerator.get_preset_name(chosen_preset) + " / seed %d / season %.0f" % [chosen_seed, chosen_season]
		info["generation_ms"] = result.generation_ms
		info["convert_ms"] = result.convert_ms
		records.append(info)
		print(JSON.stringify(info))
	var suffix := "-orbit" if "--orbit" in OS.get_cmdline_user_args() else ("-seasons" if "--seasons" in OS.get_cmdline_user_args() else "")
	if "--ginkgo" in OS.get_cmdline_user_args():
		suffix = "-ginkgo"
	var file = FileAccess.open(output_directory + "/after-metrics" + suffix + ".json", FileAccess.WRITE)
	file.store_string(JSON.stringify(records, "\t"))
	file.close()
	root.size = Vector2i(1800, 1080)
	root.content_scale_size = Vector2i(1800, 1080)
	for i in meshes.size():
		make_panel(i)
	for frame in 20:
		await process_frame
	await RenderingServer.frame_post_draw
	root.get_texture().get_image().save_png(output_directory + "/slowtree-after" + suffix + ".png")
	print("AUDIT_RENDER_SAVED")
	quit()

func make_panel(i: int):
	var panel = SubViewportContainer.new()
	panel.position = Vector2((i % 3) * 600, (i / 3) * 540)
	panel.size = Vector2(600, 540)
	root.add_child(panel)
	var viewport = SubViewport.new()
	viewport.size = Vector2i(600, 540)
	viewport.own_world_3d = true
	viewport.msaa_3d = Viewport.MSAA_4X
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	panel.add_child(viewport)
	var world = Node3D.new()
	viewport.add_child(world)
	var tree = MeshInstance3D.new()
	tree.mesh = meshes[i]
	world.add_child(tree)
	var environment = WorldEnvironment.new()
	var env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.83, 0.85, 0.86)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.85, 0.88, 1.0)
	env.ambient_light_energy = 0.65
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.environment = env
	world.add_child(environment)
	var light = DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, -35, 0)
	light.light_energy = 1.1
	light.shadow_enabled = true
	world.add_child(light)
	var ground = MeshInstance3D.new()
	var plane = PlaneMesh.new()
	plane.size = Vector2(100, 100)
	ground.mesh = plane
	var material = StandardMaterial3D.new()
	material.albedo_color = Color(0.49, 0.51, 0.45)
	material.roughness = 1.0
	ground.material_override = material
	world.add_child(ground)
	var aabb = tree.mesh.get_aabb()
	var focus = aabb.get_center()
	focus.y = aabb.size.y * 0.46
	var extent = maxf(aabb.size.y, maxf(aabb.size.x, aabb.size.z))
	var cam = Camera3D.new()
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = extent * 1.35
	cam.position = focus + Vector3(0.8, 0.3, 1.5).normalized() * extent * 2.5
	if "--orbit" in OS.get_cmdline_user_args():
		var angle := float(i % 4) * PI * 0.5
		var tilt := deg_to_rad(55.0) if i >= 4 else deg_to_rad(12.0)
		cam.position = focus + Vector3(sin(angle) * cos(tilt), sin(tilt), cos(angle) * cos(tilt)) * extent * 2.5
	if "--ginkgo" in OS.get_cmdline_user_args() and i == 5:
		cam.position = focus + Vector3(0.4, 1.43, 0.9).normalized() * extent * 2.5
	world.add_child(cam)
	cam.look_at(focus)
	cam.current = true
	var label = Label.new()
	label.position = panel.position + Vector2(18, 12)
	label.text = "%s  |  %s tris" % [records[i].name, records[i].triangles]
	label.add_theme_font_size_override("font_size", 17)
	label.add_theme_color_override("font_color", Color(0.06, 0.08, 0.08))
	root.add_child(label)
