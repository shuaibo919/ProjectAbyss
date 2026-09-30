extends SceneTree
## Before/after views use identical cameras. Run --capture-before before rebuilding the extension.

const OUTPUT := "E:/ProjectAbyss/Reference/TreeGenBranches"
var models: Array[Mesh] = []
var labels: Array[String] = []

func _initialize():
	call_deferred("run")

func run():
	DirAccess.make_dir_recursive_absolute(OUTPUT)
	var capture := "--capture-before" in OS.get_cmdline_user_args()
	var preset := 1 if "--willow" in OS.get_cmdline_user_args() else 3
	if capture:
		for species in [1, 3]:
			for seed_value in [0, 17, 42]:
				var result := SlowTreeGenerator.generate(species, seed_value, false, 3.0 if species == 3 else 2.0,
					{"crossed_cards": true, "species_rules": true, "structural_branches": false})
				assert(result.error.is_empty())
				assert(ResourceSaver.save(result.mesh, OUTPUT + "/before_%d_%d.res" % [species, seed_value]) == OK)
		print("BRANCH_BASELINE_SAVED")
		quit()
		return
	root.size = Vector2i(1800, 1200)
	root.content_scale_size = root.size
	for seed_value in [0, 17, 42]:
		var before_path := OUTPUT + "/before_%d_%d.res" % [preset, seed_value]
		var old: Mesh
		if FileAccess.file_exists(before_path):
			old = load(before_path)
		else:
			old = SlowTreeGenerator.generate(preset, seed_value, false, 3.0 if preset == 3 else 2.0,
				{"crossed_cards": true, "species_rules": true, "structural_branches": false}).mesh
		var result := SlowTreeGenerator.generate(preset, seed_value, false, 3.0 if preset == 3 else 2.0,
			{"crossed_cards": true, "species_rules": true, "growth_debug": true})
		assert(result.error.is_empty())
		print("BRANCH_GALLERY ", preset, " ", seed_value, " ", result.get("growth_stats", {}), " tris ", result.triangle_count)
		models.append(bare_mesh(old))
		models.append(bare_mesh(result.mesh))
		models.append(result.mesh)
		labels.append("Before / seed %d" % seed_value)
		labels.append("Rebuilt branches / seed %d" % seed_value)
		labels.append("With foliage / seed %d" % seed_value)
	for index in models.size():
		make_panel(index, preset)
	for frame in 18:
		await process_frame
	await RenderingServer.frame_post_draw
	var suffix := "_close" if "--close" in OS.get_cmdline_user_args() else ""
	root.get_texture().get_image().save_png(OUTPUT + "/branches_%d%s.png" % [preset, suffix])
	print("BRANCH_GALLERY_SAVED")
	quit()

func bare_mesh(source: Mesh) -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var arrays := source.surface_get_arrays(0)
	for slot in [Mesh.ARRAY_CUSTOM0, Mesh.ARRAY_CUSTOM1, Mesh.ARRAY_CUSTOM2, Mesh.ARRAY_CUSTOM3]:
		arrays[slot] = null
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.40, 0.33, 0.26)
	material.roughness = 0.9
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	mesh.surface_set_material(0, material)
	return mesh

func make_panel(index: int, preset: int):
	var panel := SubViewportContainer.new()
	panel.position = Vector2(index % 3 * 600, index / 3 * 400)
	panel.size = Vector2(600, 400)
	root.add_child(panel)
	var viewport := SubViewport.new()
	viewport.size = Vector2i(600, 400)
	viewport.own_world_3d = true
	viewport.msaa_3d = Viewport.MSAA_8X
	panel.add_child(viewport)
	var model := MeshInstance3D.new()
	model.mesh = models[index]
	viewport.add_child(model)
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color(0.78, 0.80, 0.82)
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color = Color(0.85, 0.90, 1.0)
	environment.environment.ambient_light_energy = 0.60
	viewport.add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-38, -35, 0)
	light.light_energy = 1.25
	light.shadow_enabled = true
	viewport.add_child(light)
	var closeup := "--close" in OS.get_cmdline_user_args()
	var center := Vector3(0, 2.35 if closeup else (5.9 if preset == 3 else 4.7), 0)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.keep_aspect = Camera3D.KEEP_HEIGHT
	camera.size = 3.1 if closeup else (13.7 if preset == 3 else 11.3)
	camera.position = center + Vector3(0.5, 0.14, 1.0).normalized() * 35
	viewport.add_child(camera)
	camera.look_at(center)
	camera.current = true
	var label := Label.new()
	label.position = panel.position + Vector2(12, 10)
	label.text = labels[index]
	label.add_theme_font_size_override("font_size", 19)
	label.add_theme_color_override("font_color", Color(0.08, 0.09, 0.11))
	root.add_child(label)
