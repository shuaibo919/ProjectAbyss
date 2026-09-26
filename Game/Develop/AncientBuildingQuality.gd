extends SceneTree

# Isolated standard-material review; --capture writes fixed-camera comparison shots and exits.
var camera: Camera3D
var root_3d: Node3D
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
	env.environment.ambient_light_color = Color(0.72, 0.80, 1.0)
	env.environment.ambient_light_energy = 0.45
	root_3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-28, -55, 0)
	sun.light_energy = 1.2
	sun.shadow_enabled = true
	root_3d.add_child(sun)
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
		p.column_sides = 10 if i == 0 else 24
		p.smooth_columns = i == 1
		p.column_base_height_scale = 0.0 if i == 0 else 0.65
		var building = ClassDB.instantiate("AncientBuilding")
		building.parameters = p
		building.position.x = -7.0 if i == 0 else 7.0
		root_3d.add_child(building)
		buildings.append(building)
		print("%s vertices=%d triangles=%d" % ["flat / no plinth" if i == 0 else "smooth / plinth", building.get_vertex_count(), building.get_triangle_count()])
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
	await save_shot(directory.path_join("comparison.png"))
	# Identical relative camera and lighting for the two support close-ups.
	for i in range(2):
		for j in range(2):
			buildings[j].visible = j == i
		var offset := -7.0 if i == 0 else 7.0
		camera.position = Vector3(offset - 3.2, 2.9, 7.3)
		camera.look_at(Vector3(offset - 4.5, 1.8, 3.0))
		await save_shot(directory.path_join("column_%s.png" % ("before" if i == 0 else "after")))
	quit()

func save_shot(path: String) -> void:
	for i in range(12):
		await process_frame
	await RenderingServer.frame_post_draw
	var result := root.get_texture().get_image().save_png(path)
	print("screenshot=%s result=%d" % [path, result])
