extends SceneTree
## Rendered benchmark. Optional: -- --output=C:/absolute/path/result.json --gpu-check

const REPEATS := 12
const OPTIONS := {"crossed_cards": true, "species_rules": true, "radial_segments": 12}
var results: Array = []
var keep_alive: Array[Mesh] = []
var frames: Array = []
var record_frames := false
var previous_frame_usec := 0

func _initialize() -> void:
	call_deferred("run")

func _process(_delta: float) -> bool:
	var now := Time.get_ticks_usec()
	if record_frames and previous_frame_usec > 0:
		frames.append(float(now - previous_frame_usec) / 1000.0)
	previous_frame_usec = now
	return false

func percentile(values: Array, fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(ceili(sorted.size() * fraction) - 1, 0, sorted.size() - 1)]

func run() -> void:
	root.size = Vector2i(1280, 800)
	root.msaa_3d = Viewport.MSAA_4X
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 60
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
	var world := Node3D.new()
	root.add_child(world)
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color(0.65, 0.70, 0.75)
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_energy = 0.5
	world.add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, -30, 0)
	light.shadow_enabled = true
	world.add_child(light)
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.current = true
	var failures: Array = []
	var presets := range(7)
	var season := 2.0
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--preset="):
			presets = [clampi(int(argument.trim_prefix("--preset=")), 0, 6)]
		elif argument.begins_with("--season="):
			season = clampf(float(argument.trim_prefix("--season=")), 0.0, 4.0)
	for preset in presets:
		var walls: Array = []
		var generation: Array = []
		var convert: Array = []
		var cold_ms := 0.0
		var last_result: Dictionary
		for iteration in range(-2, REPEATS):
			var started := Time.get_ticks_usec()
			last_result = SlowTreeGenerator.generate(preset, 100 + maxi(iteration, 0), false, season, OPTIONS)
			var elapsed := float(Time.get_ticks_usec() - started) / 1000.0
			if iteration == -2:
				cold_ms = elapsed
				keep_alive.append(last_result.mesh)
			if iteration >= 0:
				walls.append(elapsed)
				generation.append(last_result.generation_ms)
				convert.append(last_result.convert_ms)
			if last_result.error != "":
				failures.append(last_result.error)
		var tree = ClassDB.instantiate("ProceduralTree")
		tree.auto_regenerate = false
		tree.backend = 1
		tree.slowtree_preset = preset
		tree.season = season
		world.add_child(tree)
		tree.auto_regenerate = true
		var bounds: AABB = tree.mesh.get_aabb()
		var target := bounds.get_center()
		camera.position = target + Vector3(0.6, 0.2, 1.0).normalized() * bounds.size.length() * 1.4
		camera.look_at(target)
		for frame in 30:
			await process_frame
		var edits: Array = []
		var submits: Array = []
		var gpu_times: Array = []
		frames.clear()
		record_frames = true
		for iteration in REPEATS:
			var started := Time.get_ticks_usec()
			tree.seed = 100 + iteration
			while tree.is_preview_pending():
				await process_frame
			await RenderingServer.frame_post_draw
			edits.append(float(Time.get_ticks_usec() - started) / 1000.0)
			submits.append(tree.get_commit_ms())
			gpu_times.append(RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid()))
		var before_drag: int = tree.get_generation_count()
		for frame in 60:
			tree.seed = 200 + frame
			await process_frame
		while tree.is_preview_pending():
			await process_frame
		var drag_builds: int = tree.get_generation_count() - before_drag
		record_frames = false
		var record := {"name": SlowTreeGenerator.get_preset_name(preset), "cold_ms": cold_ms,
			"tessellation_backend": last_result.tessellation_backend,
			"wall_p50_ms": percentile(walls, 0.5), "wall_p95_ms": percentile(walls, 0.95),
			"generation_p50_ms": percentile(generation, 0.5), "convert_p50_ms": percentile(convert, 0.5),
			"input_to_draw_p50_ms": percentile(edits, 0.5), "input_to_draw_p95_ms": percentile(edits, 0.95),
			"main_submit_p95_ms": percentile(submits, 0.95), "frame_interval_p95_ms": percentile(frames, 0.95),
			"gpu_frame_p50_ms": percentile(gpu_times, 0.5), "commits_during_60_drag_frames": drag_builds,
			"triangles_seed111": last_result.triangle_count, "samples_ms": walls, "edit_samples_ms": edits}
		results.append(record)
		print("BENCH ", JSON.stringify(record))
		if drag_builds < 10:
			failures.append("Continuous edits starved preview: " + record.name)
		tree.free()
		await process_frame

	if "--gpu-check" in OS.get_cmdline_user_args():
		for preset in range(7):
			var cpu: Dictionary = SlowTreeGenerator.generate(preset, 12, false, season, OPTIONS)
			var gpu: Dictionary = SlowTreeGenerator.generate(preset, 12, true, season, OPTIONS)
			if gpu.tessellation_backend != "cpu_connected":
				failures.append("Connected preset did not select CPU fallback: %s" % preset)
			if gpu.error != "" or cpu.triangle_count != gpu.triangle_count or cpu.leaf_count != gpu.leaf_count:
				failures.append("GPU crossed foliage mismatch: %s" % preset)
			else:
				var cpu_leaves: Array = cpu.mesh.surface_get_arrays(1)
				var gpu_leaves: Array = gpu.mesh.surface_get_arrays(1)
				if var_to_bytes(cpu_leaves) != var_to_bytes(gpu_leaves):
					failures.append("GPU foliage arrays differ: %s" % preset)
		print("CONNECTED_BACKEND_CHECK ", failures)
	var report := {"renderer": RenderingServer.get_current_rendering_method(),
		"adapter": RenderingServer.get_video_adapter_name(), "msaa": "4x", "resolution": "1280x800",
		"target_fps": 60, "warmups": 2, "samples": REPEATS, "seed_range": "100..111", "season": season,
		"results": results, "failures": failures}
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):
			var file := FileAccess.open(arg.trim_prefix("--output="), FileAccess.WRITE)
			file.store_string(JSON.stringify(report, "\t"))
	print("TREEGEN_BENCHMARK ", "PASS" if failures.is_empty() else "FAIL")
	quit(0 if failures.is_empty() else 1)
