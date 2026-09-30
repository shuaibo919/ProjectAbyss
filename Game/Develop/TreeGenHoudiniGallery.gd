extends SceneTree
## Before/after views use identical cameras. Run --capture-before before rebuilding the extension.

const OUTPUT := "E:/ProjectAbyss/Reference/TreeGenHoudiniRefine"
var models: Array[Mesh] = []
var labels: Array[String] = []
var panel_dimensions := Vector2i(600, 400)
var columns := 3
var fit_each_panel := false

func _initialize():
	call_deferred("run")

func run():
	DirAccess.make_dir_recursive_absolute(OUTPUT)
	if "--overview" in OS.get_cmdline_user_args():
		await make_overview()
		quit()
		return
	var capture := "--capture-before" in OS.get_cmdline_user_args()
	var preset := 0
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--preset="):
			preset = int(argument.trim_prefix("--preset="))
	if "--details" in OS.get_cmdline_user_args():
		await make_details(preset)
		quit()
		return
	var season := 3.0 if preset == 3 else 2.0
	var season_suffix := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--season="):
			season = float(argument.trim_prefix("--season="))
			season_suffix = "_season_%.1f" % season
	if capture:
		for species in range(7):
			for seed_value in [0, 17, 42]:
				if FileAccess.file_exists(OUTPUT + "/before_%d_%d.res" % [species, seed_value]):
					continue
				var result := SlowTreeGenerator.generate(species, seed_value, false, 3.0 if species == 3 else 2.0,
					{"radial_segments": 12, "crossed_cards": true, "species_rules": true, "structural_branches": true})
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
			print("No stored baseline; comparing the legacy generator for preset ", preset)
			old = SlowTreeGenerator.generate(preset, seed_value, false, 3.0 if preset == 3 else 2.0,
				{"radial_segments": 12, "crossed_cards": true, "species_rules": true, "structural_branches": false}).mesh
		old = with_season(old, season)
		var result := SlowTreeGenerator.generate(preset, seed_value, false, season,
			{"radial_segments": 12, "crossed_cards": true, "species_rules": true, "growth_debug": true})
		assert(result.error.is_empty())
		print("BRANCH_GALLERY ", preset, " ", seed_value, " ", result.get("growth_stats", {}), " tris ", result.triangle_count)
		models.append(old)
		models.append(bare_mesh(result.mesh))
		models.append(result.mesh)
		labels.append("Before / seed %d" % seed_value)
		labels.append("Branch structure / seed %d" % seed_value)
		labels.append("Refined / seed %d" % seed_value)
	for index in models.size():
		make_panel(index, preset)
	for frame in 18:
		await process_frame
	await RenderingServer.frame_post_draw
	var suffix := "_close" if "--close" in OS.get_cmdline_user_args() else ""
	root.get_texture().get_image().save_png(OUTPUT + "/branches_%d%s%s.png" % [preset, suffix, season_suffix])
	print("BRANCH_GALLERY_SAVED")
	quit()

func make_overview():
	panel_dimensions = Vector2i(500, 600)
	columns = 4
	fit_each_panel = true
	root.size = Vector2i(2000, 1200)
	root.content_scale_size = root.size
	var presets := [3, 1, 2, 5, 0, 4, 6, 6]
	var names := ["Ginkgo / autumn", "Willow", "Pine", "Metasequoia", "HelloTree", "Bamboo / 3 culms", "Peach / summer", "Peach / spring"]
	for index in presets.size():
		var season := 3.0 if presets[index] == 3 else (1.0 if index == 7 else 2.0)
		var result := SlowTreeGenerator.generate(presets[index], 0, false, season,
			{"radial_segments": 12, "crossed_cards": true, "species_rules": true})
		assert(result.error.is_empty())
		models.append(result.mesh)
		labels.append("%s\nSeed 0 | %s triangles" % [names[index], result.triangle_count])
		make_panel(index, presets[index])
	for frame in 18:
		await process_frame
	await RenderingServer.frame_post_draw
	root.get_texture().get_image().save_png(OUTPUT + ("/overview_before.png" if "--before" in OS.get_cmdline_user_args() else "/overview.png"))
	print("ALL_SPECIES_OVERVIEW_SAVED")

func make_details(preset: int):
	root.size = Vector2i(1800, 1000)
	root.content_scale_size = root.size
	var season := 3.0 if preset == 3 else 2.0
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--season="):
			season = float(argument.trim_prefix("--season="))
	var result := SlowTreeGenerator.generate(preset, 0, false, season,
		{"radial_segments": 12, "crossed_cards": true, "species_rules": true, "growth_debug": true})
	var previous := with_season(load(OUTPUT + "/before_%d_0.res" % preset), season)
	var bounds := previous.get_aabb().merge(result.mesh.get_aabb())
	var height := maxf(bounds.size.y * 1.12, bounds.size.x * 1.8)
	var center := bounds.get_center()
	detail_panel(previous, Rect2i(0, 0, 600, 1000), center, Vector3(0.5, 0.14, 1), height, "Before / seed 0")
	detail_panel(result.mesh, Rect2i(600, 0, 600, 1000), center, Vector3(0.5, 0.14, 1), height, "Refined / seed 0 | %d triangles" % result.triangle_count)
	var arrays: Array = result.mesh.surface_get_arrays(1)
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var uvs: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
	var target_shape := 7 if preset == 6 and season < 1.5 else preset
	var chosen := 0
	for card in vertices.size() / 8:
		var uv := uvs[card * 8]
		var shape := (int(uv.x * 4) + int(uv.y * 4) * 4) / 2
		if shape == target_shape:
			chosen = card * 8
			break
	var isolated := ArrayMesh.new()
	var card_arrays := []
	card_arrays.resize(Mesh.ARRAY_MAX)
	for slot in [Mesh.ARRAY_VERTEX, Mesh.ARRAY_NORMAL, Mesh.ARRAY_COLOR, Mesh.ARRAY_TEX_UV]:
		card_arrays[slot] = arrays[slot].slice(chosen, chosen + 8)
	card_arrays[Mesh.ARRAY_CUSTOM0] = arrays[Mesh.ARRAY_CUSTOM0].slice(chosen * 2, (chosen + 8) * 2)
	card_arrays[Mesh.ARRAY_CUSTOM1] = arrays[Mesh.ARRAY_CUSTOM1].slice(chosen * 3, (chosen + 8) * 3)
	card_arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7])
	var flags := (Mesh.ARRAY_CUSTOM_RG_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT) | (Mesh.ARRAY_CUSTOM_RGB_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM1_SHIFT)
	isolated.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, card_arrays, [], {}, flags)
	isolated.surface_set_material(0, result.mesh.surface_get_material(1))
	var card_center := isolated.get_aabb().get_center()
	var up := (vertices[chosen + 3] - vertices[chosen]).normalized()
	var right := (vertices[chosen + 1] - vertices[chosen]).normalized()
	var direction := (right.cross(up) + right * 0.12 + up * 0.15).normalized()
	detail_panel(isolated, Rect2i(1200, 0, 600, 500), card_center, direction,
		maxf(isolated.get_aabb().size.length() * 1.1, 0.22), "Leaf spray / 4 triangles", up)
	var skeleton: Dictionary = result.growth_skeleton
	var first_end: int = skeleton.offsets[1]
	var sample := maxi(1, int(first_end * (0.36 if preset == 6 else 0.14)))
	var bark_center: Vector3 = skeleton.points[sample]
	var wood_only := ArrayMesh.new()
	wood_only.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, result.mesh.surface_get_arrays(0), [], {}, flags)
	wood_only.surface_set_material(0, result.mesh.surface_get_material(0))
	detail_panel(wood_only, Rect2i(1200, 500, 600, 500), bark_center, Vector3(0.5, 0.10, 1),
		maxf(skeleton.radii[sample] * 5.0, 0.65), "Generated bark / albedo + normal")
	for frame in 18:
		await process_frame
	await RenderingServer.frame_post_draw
	var suffix := "_spring" if preset == 6 and season < 1.5 else ""
	root.get_texture().get_image().save_png(OUTPUT + "/details_%d%s.png" % [preset, suffix])
	print("HOUDINI_DETAILS_SAVED ", preset)

func with_season(source: Mesh, season: float) -> Mesh:
	# Baselines retain their original geometry and atlas, but both views need the same season.
	var mesh := source.duplicate() as ArrayMesh
	if mesh.get_surface_count() > 1:
		var original := mesh.surface_get_material(1) as ShaderMaterial
		if original:
			var material := original.duplicate() as ShaderMaterial
			material.set_shader_parameter("season", season)
			mesh.surface_set_material(1, material)
	return mesh

func detail_panel(mesh: Mesh, region: Rect2i, center: Vector3, direction: Vector3, height: float, text: String, camera_up := Vector3.UP):
	var container := SubViewportContainer.new()
	container.position = region.position
	container.size = region.size
	root.add_child(container)
	var viewport := SubViewport.new()
	viewport.size = region.size
	viewport.own_world_3d = true
	viewport.msaa_3d = Viewport.MSAA_8X
	container.add_child(viewport)
	var model := MeshInstance3D.new()
	model.mesh = mesh
	viewport.add_child(model)
	var world := WorldEnvironment.new()
	world.environment = Environment.new()
	world.environment.background_mode = Environment.BG_COLOR
	world.environment.background_color = Color(0.78, 0.80, 0.82)
	world.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	world.environment.ambient_light_color = Color(0.85, 0.90, 1.0)
	world.environment.ambient_light_energy = 0.60
	viewport.add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-38, -35, 0)
	light.light_energy = 1.25
	light.shadow_enabled = true
	viewport.add_child(light)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.keep_aspect = Camera3D.KEEP_HEIGHT
	camera.size = height
	camera.position = center + direction.normalized() * 35
	viewport.add_child(camera)
	camera.look_at(center, camera_up)
	camera.current = true
	var label := Label.new()
	label.position = Vector2(region.position) + Vector2(12, 10)
	label.text = text
	label.add_theme_font_size_override("font_size", 19)
	label.add_theme_color_override("font_color", Color(0.08, 0.09, 0.11))
	root.add_child(label)

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
	panel.position = Vector2(index % columns * panel_dimensions.x, index / columns * panel_dimensions.y)
	panel.size = panel_dimensions
	root.add_child(panel)
	var viewport := SubViewport.new()
	viewport.size = panel_dimensions
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
	var bounds := models[index].get_aabb()
	if not fit_each_panel:
		for item in models:
			bounds = bounds.merge(item.get_aabb())
	var center := bounds.get_center()
	if closeup:
		center = Vector3(0, 2.35 if preset != 6 else 0.8, 0)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.keep_aspect = Camera3D.KEEP_HEIGHT
	camera.size = 3.1 if closeup else maxf(bounds.size.y * 1.22,
		maxf(bounds.size.x, bounds.size.z) * 1.28 * float(panel_dimensions.y) / panel_dimensions.x)
	var direction := Vector3(0.06, 0.14, 1.0) if preset == 4 else Vector3(0.5, 0.14, 1.0)
	camera.position = center + direction.normalized() * 35
	viewport.add_child(camera)
	camera.look_at(center)
	camera.current = true
	var label := Label.new()
	label.position = panel.position + Vector2(12, 10)
	label.text = labels[index]
	label.add_theme_font_size_override("font_size", 19)
	label.add_theme_color_override("font_color", Color(0.08, 0.09, 0.11))
	root.add_child(label)
