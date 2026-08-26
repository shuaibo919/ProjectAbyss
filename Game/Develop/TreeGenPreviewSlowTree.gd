extends Node3D

# SlowTree 后端截图集: 每个预设拍模板种子(seed=0)与一个种子变种, 供视觉验收。
# Stage 2 起每个镜头拍 CPU/GPU 两张(use_gpu_tessellation 开关), 供并排比对。
# Run with: godot --path Game/ res://Develop/TreeGenPreviewSlowTree.tscn

const OUT_DIR := "res://Develop/TreeGenShots"

var _shots: Array[Dictionary] = []


func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, -35, 0)
	light.light_energy = 1.5
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.55, 0.68, 0.85)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.5, 0.55, 0.6)
	e.ambient_light_energy = 0.6
	env.environment = e
	add_child(env)

	var camera := Camera3D.new()
	camera.current = true
	add_child(camera)

	for i in range(SlowTreeGenerator.get_preset_count()):
		var preset_name := SlowTreeGenerator.get_preset_name(i)
		for mode in ["cpu", "gpu"]:
			_shots.append({"name": "SlowTree_%s_s0_%s" % [preset_name, mode], "preset": i, "seed": 0, "gpu": mode == "gpu"})
			_shots.append({"name": "SlowTree_%s_s7_%s" % [preset_name, mode], "preset": i, "seed": 7, "gpu": mode == "gpu"})

	for entry in _shots:
		await _shoot(entry, camera)

	get_tree().quit()


func _shoot(entry: Dictionary, camera: Camera3D) -> void:
	var tree := ProceduralTree.new()
	tree.backend = ProceduralTree.BACKEND_SLOWTREE
	tree.slowtree_preset = entry["preset"]
	tree.seed = entry["seed"]
	tree.use_gpu_tessellation = entry.get("gpu", false)
	add_child(tree)

	# Frame the whole tree from its own AABB, so every species gets a comparable shot.
	var aabb: AABB = tree.get_aabb()
	var centre := aabb.get_center()
	var radius := maxf(aabb.size.length() * 0.6, 1.0)
	camera.position = centre + Vector3(0.35, 0.12, 1.0).normalized() * radius * 1.15
	camera.look_at(centre)

	for i in 6:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	var image := get_viewport().get_texture().get_image()
	image.save_png(ProjectSettings.globalize_path(OUT_DIR + "/" + entry["name"] + ".png"))

	print("%s: %d verts, %d tris, %d surfaces, truncated=%s, height=%.1fm" % [
		entry["name"],
		tree.get_vertex_count(),
		tree.get_triangle_count(),
		tree.get_segment_count(),
		tree.was_truncated(),
		aabb.size.y,
	])

	tree.queue_free()
	await get_tree().process_frame
