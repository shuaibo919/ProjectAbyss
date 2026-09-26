extends SceneTree

# P1 roof-curve visual comparison: fixed grazing light, 歇山 with partial tile coverage so the
# bare boarding shows its facet shading. Left = legacy polyline, right = continuous curve.
# --capture writes the shots and exits.
var camera: Camera3D
var root_3d: Node3D
var sun: DirectionalLight3D
var buildings: Array[Node3D] = []

func _initialize() -> void:
	call_deferred("build")

func build() -> void:
	root_3d = Node3D.new()
	root.add_child(root_3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.12, 0.15, 0.19)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.85, 0.87, 0.92)
	env.environment.ambient_light_energy = 0.35
	root_3d.add_child(env)
	# Grazing key from the north (the camera side of the +Z slope), so each slope band lands at
	# its own angle and any facet step between course bands shows as a luminance jump.
	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-25, 15, 0)
	sun.light_energy = 1.35
	sun.shadow_enabled = true
	root_3d.add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-25, -70, 0)
	fill.light_energy = 0.35
	fill.shadow_enabled = false
	root_3d.add_child(fill)
	var floor_node := MeshInstance3D.new()
	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(80, 80)
	floor_node.mesh = floor_mesh
	var floor_mat := StandardMaterial3D.new()
	floor_mat.albedo_color = Color(0.32, 0.34, 0.36)
	floor_mesh.material = floor_mat
	root_3d.add_child(floor_node)
	for i in range(2):
		var p = ClassDB.instantiate("AncientBuildingParameters")
		p.roof_type = 1
		p.generate_fence = false
		p.generate_walls = false
		p.generate_columns = false
		p.tile_coverage = 0.0
		p.roof_curve_mode = i
		var building = ClassDB.instantiate("AncientBuilding")
		building.parameters = p
		building.position.x = -9.0 if i == 0 else 9.0
		root_3d.add_child(building)
		buildings.append(building)
		print("%s vertices=%d triangles=%d" % ["legacy" if i == 0 else "continuous", building.get_vertex_count(), building.get_triangle_count()])
	camera = Camera3D.new()
	camera.fov = 42
	camera.position = Vector3(20, 13, 32)
	root_3d.add_child(camera)
	camera.look_at(Vector3(0, 3, 0))
	camera.current = true
	if "--capture" in OS.get_cmdline_user_args():
		await capture()

func capture() -> void:
	var directory := ProjectSettings.globalize_path("res://../Reference/Shots/AncientBuildingQuality")
	DirAccess.make_dir_recursive_absolute(directory)
	await save_shot(directory.path_join("roof_curve_comparison.png"))
	# Identical relative camera and lighting for the two close-ups, aimed up the +Z slope.
	for i in range(2):
		for j in range(2):
			buildings[j].visible = j == i
		var offset := -9.0 if i == 0 else 9.0
		camera.position = Vector3(offset + 1.0, 8.5, 12.0)
		camera.look_at(Vector3(offset, 5.0, 3.0))
		await save_shot(directory.path_join("roof_curve_%s.png" % ("legacy" if i == 0 else "continuous")))
	# Tier close-ups: the camera faces the 收山 tier head-on, where the legacy boarding was one
	# facet per course. A lower sun makes each band's NdotL step show.
	sun.rotation_degrees = Vector3(-15, 15, 0)
	for i in range(2):
		for j in range(2):
			buildings[j].visible = j == i
		var offset := -9.0 if i == 0 else 9.0
		camera.position = Vector3(offset + 1.0, 10.2, 8.0)
		camera.look_at(Vector3(offset, 10.2, 1.2))
		await save_shot(directory.path_join("roof_curve_tier_%s.png" % ("legacy" if i == 0 else "continuous")))
	quit()

func save_shot(path: String) -> void:
	for i in range(12):
		await process_frame
	await RenderingServer.frame_post_draw
	var result := root.get_texture().get_image().save_png(path)
	print("screenshot=%s result=%d" % [path, result])
