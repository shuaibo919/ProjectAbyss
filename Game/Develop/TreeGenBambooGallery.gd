extends SceneTree
## Run --capture-before with the previous DLL; renders always compare identical framing.

const OUTPUT := "E:/ProjectAbyss/Reference/TreeGenBambooRefine"
const OPTIONS := {"crossed_cards": true, "species_rules": true, "growth_debug": true, "radial_segments": 12}
var models: Array[Mesh] = []
var main_skeleton: Dictionary

func _initialize():
	call_deferred("run")

func run():
	DirAccess.make_dir_recursive_absolute(OUTPUT)
	if "--capture-before" in OS.get_cmdline_user_args():
		for preset in range(7):
			for seed_value in ([0, 17, 42] if preset == 4 else [0]):
				var path := OUTPUT + "/before_%d_%d.res" % [preset, seed_value]
				if not FileAccess.file_exists(path):
					var result := SlowTreeGenerator.generate(preset, seed_value, false, 2.0, OPTIONS)
					assert(result.error.is_empty())
					assert(ResourceSaver.save(result.mesh, path) == OK)
		print("BAMBOO_BASELINE_SAVED")
		quit()
		return
	var details := "--details" in OS.get_cmdline_user_args()
	var macro := "--macro" in OS.get_cmdline_user_args()
	root.size = Vector2i(1800, 900 if macro else (1200 if details else 1800))
	root.content_scale_size = root.size
	for seed_value in [0, 17, 42]:
		var result := SlowTreeGenerator.generate(4, seed_value, false, 2.0, OPTIONS)
		assert(result.error.is_empty())
		models.append(result.mesh)
		if seed_value == 0:
			main_skeleton = result.growth_skeleton
		print("BAMBOO_GALLERY seed=", seed_value, " tris=", result.triangle_count, " cards=", result.leaf_count, " growth=", result.get("growth_stats", {}))
	if macro:
		var node_center: Vector3 = main_skeleton.points[18]
		var leaf_center := Vector3(1, 5, 0.4)
		var best_distance := INF
		for stem in main_skeleton.parents.size():
			if main_skeleton.roles[stem] != 6:
				continue
			var point: Vector3 = main_skeleton.points[main_skeleton.offsets[stem]]
			var score := point.distance_squared_to(Vector3(1, 5, 0.4))
			if score < best_distance:
				leaf_center = point
				best_distance = score
		make_panel(0, models[0], "Culm node / sheath scar / fibres", node_center, 0.45, 0.2, Vector2i(900, 900), 2)
		make_panel(1, models[0], "Lanceolate leaves / crossed alpha cards", leaf_center, 0.70, 0.2, Vector2i(900, 900), 2)
	elif details:
		var bounds := models[0].get_aabb()
		var projected_width := bounds.size.x * cos(0.2) + bounds.size.z * sin(0.2)
		make_panel(0, models[0], "Refined bamboo / seed 0", bounds.get_center(), maxf(bounds.size.y * 1.14, projected_width * 2.3), 0.2, Vector2i(600, 1200), 3)
		make_panel(1, models[0], "Nodes and culm surface", Vector3(0, 1.45, 0), 1.65, 0.25, Vector2i(600, 600), 3)
		make_panel(2, models[0], "Upper branches and leaf sprays", Vector3(0, 5.5, 0), 3.4, 0.2, Vector2i(600, 600), 3)
		make_panel(4, models[0], "Node branch junctions", Vector3(0, 4.3, 0), 1.8, 0.2, Vector2i(600, 600), 3)
		make_panel(5, models[0], "Side view / seed 17", models[1].get_aabb().get_center(), 9.3, 1.7, Vector2i(600, 600), 3, models[1])
	else:
		for index in range(3):
			var seed_value: int = [0, 17, 42][index]
			var old: Mesh = load(OUTPUT + "/before_4_%d.res" % seed_value)
			var bounds := old.get_aabb().merge(models[index].get_aabb())
			for column in range(2):
				var mesh := old if column == 0 else models[index]
				make_panel(index * 2 + column, mesh, ("Before" if column == 0 else "Refined") + " / seed %d" % seed_value,
					bounds.get_center(), bounds.size.y * 1.15, 0.2, Vector2i(900, 600), 2)
	for frame in 18:
		await process_frame
	await RenderingServer.frame_post_draw
	var filename := "/macro.png" if macro else ("/details.png" if details else "/comparison.png")
	assert(root.get_texture().get_image().save_png(OUTPUT + filename) == OK)
	print("BAMBOO_GALLERY_SAVED")
	quit()

func make_panel(index: int, mesh: Mesh, title: String, center: Vector3, height: float, yaw: float, dimensions: Vector2i, columns: int, override_mesh: Mesh = null):
	var panel := SubViewportContainer.new()
	panel.position = Vector2(index % columns * dimensions.x, index / columns * dimensions.y)
	panel.size = dimensions
	root.add_child(panel)
	var viewport := SubViewport.new()
	viewport.size = dimensions
	viewport.own_world_3d = true
	viewport.msaa_3d = Viewport.MSAA_8X
	panel.add_child(viewport)
	var model := MeshInstance3D.new()
	model.mesh = override_mesh if override_mesh else mesh
	viewport.add_child(model)
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color(0.79, 0.81, 0.80)
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color = Color(0.85, 0.90, 1.0)
	environment.environment.ambient_light_energy = 0.65
	viewport.add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-38, -35, 0)
	light.light_energy = 1.2
	light.shadow_enabled = true
	viewport.add_child(light)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.keep_aspect = Camera3D.KEEP_HEIGHT
	camera.size = height
	camera.position = center + Vector3(sin(yaw), 0.08, cos(yaw)) * 35
	viewport.add_child(camera)
	camera.look_at(center)
	camera.current = true
	var label := Label.new()
	label.position = panel.position + Vector2(14, 12)
	label.text = title
	label.add_theme_font_size_override("font_size", 20)
	label.add_theme_color_override("font_color", Color(0.08, 0.09, 0.11))
	root.add_child(label)
