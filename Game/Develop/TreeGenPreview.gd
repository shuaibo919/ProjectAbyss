extends Node3D

# Renders one screenshot per tree preset so the generated geometry can be inspected.
# Run with: godot --path Game/ res://Develop/TreeGenPreview.tscn

const OUT_DIR := "res://Develop/TreeGenShots"

var _presets := [
	{"name": "Ginkgo", "preset": 5},
	{"name": "Peach", "preset": 6},
	{"name": "Camphor", "preset": 7},
	{"name": "Pine", "preset": 8},
	{"name": "ChineseFir", "preset": 9},
	{"name": "Willow", "preset": 10},
	{"name": "Apple", "preset": 1},
	{"name": "Sassafras", "preset": 2},
	{"name": "Palm", "preset": 3},
	{"name": "Tamarack", "preset": 4},
]

# Peach carries blossom, Ginkgo the autumn gold, so between them the season code is covered.
var _seasons := [
	{"name": "Peach_s1.2_bloom", "preset": 6, "season": 1.2},
	{"name": "Ginkgo_s3.1_autumn", "preset": 5, "season": 3.1},
	{"name": "Camphor_s4.0_winter", "preset": 7, "season": 4.0},
]


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

	for entry in _presets:
		await _shoot(entry, camera)

	for entry in _seasons:
		await _shoot(entry, camera)

	get_tree().quit()


func _shoot(entry: Dictionary, camera: Camera3D) -> void:
	var tree := ProceduralTree.new()
	tree.seed = 3
	tree.apply_preset(entry["preset"])
	if entry.has("season"):
		tree.season = entry["season"]
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

	print("%s: %d verts, %d tris, %d segments, %d leaves, truncated=%s, height=%.1fm" % [
		entry["name"],
		tree.get_vertex_count(),
		tree.get_triangle_count(),
		tree.get_segment_count(),
		tree.get_leaf_count(),
		tree.was_truncated(),
		aabb.size.y,
	])

	tree.queue_free()
	await get_tree().process_frame
