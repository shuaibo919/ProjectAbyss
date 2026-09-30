extends SceneTree

## Render the same dense forest with automatic mesh LOD on/off, in the actual town.
## Launch with --resolution 1600x900 --script res://Develop/RiverTownForestProfile.gd.
const OUTPUT := "E:/ProjectAbyss/Reference/RiverTownForest"
const SHOTS := "E:/ProjectAbyss/Reference/Shots/RiverTownForest"

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	OS.set_environment("RIVER_STYLE", "textured")
	OS.set_environment("RIVER_TERRAIN", "1")
	OS.set_environment("RIVER_SEED", "7")
	OS.unset_environment("SHOTS")
	OS.low_processor_usage_mode = false
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	DirAccess.make_dir_recursive_absolute(OUTPUT)
	DirAccess.make_dir_recursive_absolute(SHOTS)
	var scene: Node3D = load("res://Map/Map_RiverTown.tscn").instantiate()
	root.add_child(scene)
	while scene.get_node_or_null("CameraFocus") == null:
		await process_frame
	var camera := Camera3D.new()
	camera.fov = 40.0
	camera.far = 4000.0
	scene.add_child(camera)
	camera.current = true
	scene._terrain.set_camera(camera)
	var viewport := root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(viewport, true)
	var bridge: Transform3D = scene._anchors.bridge
	var n := bridge.basis.x.normalized()
	var t := bridge.basis.z.normalized()
	var report := []
	for shot in scene._shots():
		if shot.name not in ["bamboo_forest", "bamboo_walk", "hero"]:
			continue
		var origin: Vector3 = scene._anchor_origin(shot.get("anchor", "bridge"))
		camera.global_position = origin + n * shot.cam.x + Vector3.UP * shot.cam.y + t * shot.cam.z
		camera.look_at(origin + n * shot.look.x + Vector3.UP * shot.look.y + t * shot.look.z)
		for threshold in [1.0, 0.0]:
			root.mesh_lod_threshold = threshold
			for warmup in 25:
				await process_frame
			var gpu := PackedFloat64Array()
			var cpu := PackedFloat64Array()
			for frame in 30:
				await process_frame
				gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(viewport))
				cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(viewport) + RenderingServer.get_frame_setup_time_cpu())
			gpu.sort()
			cpu.sort()
			var sample := {"view": shot.name, "lod": threshold > 0.0,
				"gpu_ms_median": gpu[gpu.size() / 2], "cpu_render_ms_median": cpu[cpu.size() / 2],
				"visible_triangles": root.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_PRIMITIVES_IN_FRAME),
				"visible_draw_calls": root.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME),
				"resolution": root.size, "msaa": root.msaa_3d}
			report.append(sample)
			print("FOREST_PROFILE ", JSON.stringify(sample))
			await RenderingServer.frame_post_draw
			root.get_texture().get_image().save_png(SHOTS.path_join("%s_%s.png" % [shot.name, "lod" if threshold > 0.0 else "full"]))
	FileAccess.open(OUTPUT.path_join("profile.json"), FileAccess.WRITE).store_string(JSON.stringify(report, "\t"))
	print("FOREST_PROFILE COMPLETE")
	quit()
