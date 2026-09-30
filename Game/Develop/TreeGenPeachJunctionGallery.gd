extends "res://Develop/TreeGenPeachGallery.gd"
## Same-camera joint inspection with neutral clay and the production bark material.

const JOINT_OUTPUT := "E:/ProjectAbyss/Reference/TreeGenPeachJunctions"
const JOINT_OPTIONS := {"radial_segments": 12, "crossed_cards": true, "species_rules": true, "growth_debug": true}

func run():
	DirAccess.make_dir_recursive_absolute(JOINT_OUTPUT)
	if "--capture-before" in OS.get_cmdline_user_args():
		for seed_value in [0, 17, 42]:
			var result := SlowTreeGenerator.generate(6, seed_value, false, 1.0, JOINT_OPTIONS)
			var path := JOINT_OUTPUT + "/before_%d.res" % seed_value
			if not FileAccess.file_exists(path):
				assert(ResourceSaver.save(result.mesh, path) == OK)
		print("PEACH_JOINT_BASELINE_SAVED")
		quit()
		return
	root.size = Vector2i(1800, 1200)
	root.content_scale_size = root.size
	var seed_value := 0
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--seed="):
			seed_value = int(arg.trim_prefix("--seed="))
	var result := SlowTreeGenerator.generate(6, seed_value, false, 1.0, JOINT_OPTIONS)
	var previous: Mesh = load(JOINT_OUTPUT + "/before_%d.res" % seed_value)
	var center := Vector3(0, 0.45, 0)
	var skeleton: Dictionary = result.growth_skeleton
	center = skeleton.points[int(skeleton.offsets[1] * 0.65)]
	var directions := [Vector3(0.5, 0.20, 1), Vector3(-0.8, 0.30, 0.6), Vector3(0.8, 0.15, -1)]
	var clay := StandardMaterial3D.new()
	clay.albedo_color = Color(0.55, 0.45, 0.32)
	clay.roughness = 1.0
	for row in range(3):
		for col in range(3):
			var source: Mesh = previous if col == 0 else result.mesh
			var mesh := ArrayMesh.new()
			var arrays := source.surface_get_arrays(0)
			for slot in [Mesh.ARRAY_CUSTOM0, Mesh.ARRAY_CUSTOM1, Mesh.ARRAY_CUSTOM2, Mesh.ARRAY_CUSTOM3]:
				arrays[slot] = null
			mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			mesh.surface_set_material(0, clay if col < 2 else source.surface_get_material(0))
			var label: String = ["Before / clay", "Refined / clay", "Refined / bark"][col]
			detail_panel(mesh, Rect2i(col * 600, row * 400, 600, 400), center, directions[row], 0.95, label)
	for frame in 18:
		await process_frame
	await RenderingServer.frame_post_draw
	root.get_texture().get_image().save_png(JOINT_OUTPUT + "/joints_%d.png" % seed_value)
	print("PEACH_JOINT_GALLERY ", result.growth_stats, " triangles ", result.triangle_count)
	quit()
