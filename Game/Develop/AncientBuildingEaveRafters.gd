extends SceneTree

# P4 檐下椽飞 visual evidence: 歇山 under-eave shots, style 0 (no heads) vs style 2
# (檐椽头 + 飞椽头 + 连檐) side by side, plus a 翼角 corner view where the flipped eave
# carries the corner heads. --capture writes the shots and exits.
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
	env.environment.ambient_light_energy = 0.4
	root_3d.add_child(env)
	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-25, 15, 0)
	sun.light_energy = 1.2
	sun.shadow_enabled = true
	root_3d.add_child(sun)
	# Low bounce so the eave underside and the rafter heads read instead of going black.
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(38, -95, 0)
	fill.light_energy = 0.55
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
		p.eave_rafter_style = 0 if i == 0 else 2
		var building = ClassDB.instantiate("AncientBuilding")
		building.parameters = p
		building.position.x = -9.0 if i == 0 else 9.0
		root_3d.add_child(building)
		buildings.append(building)
		print("%s vertices=%d triangles=%d" % ["style0" if i == 0 else "style2", building.get_vertex_count(), building.get_triangle_count()])
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
	# Underside: low camera outside the front eave, looking up the slope so the head row,
	# the step between 檐椽 and 飞椽, and the 连檐 board in front of them all read.
	for i in range(2):
		for j in range(2):
			buildings[j].visible = j == i
		var offset := -9.0 if i == 0 else 9.0
		camera.position = Vector3(offset, 3.2, 8.0)
		camera.look_at(Vector3(offset, 6.6, 2.5))
		await save_shot(directory.path_join("eave_rafter_underside_%s.png" % ("style0" if i == 0 else "style2")))
	# Close-up between two heads, centred on the 连檐 so the stepped section silhouettes.
	for i in range(2):
		for j in range(2):
			buildings[j].visible = j == i
		var offset := -9.0 if i == 0 else 9.0
		camera.position = Vector3(offset + 0.4, 4.6, 6.8)
		camera.look_at(Vector3(offset + 0.1, 6.9, 3.6))
		await save_shot(directory.path_join("eave_rafter_closeup_%s.png" % ("style0" if i == 0 else "style2")))
	# 翼角: from the front-left, the flipped corner eave must carry its heads up with it.
	for i in range(2):
		for j in range(2):
			buildings[j].visible = j == i
		var offset := -9.0 if i == 0 else 9.0
		camera.position = Vector3(offset + 9.5, 3.6, 9.5)
		camera.look_at(Vector3(offset + 3.0, 6.4, 3.0))
		await save_shot(directory.path_join("eave_rafter_corner_%s.png" % ("style0" if i == 0 else "style2")))
	quit()

func save_shot(path: String) -> void:
	for i in range(12):
		await process_frame
	await RenderingServer.frame_post_draw
	var result := root.get_texture().get_image().save_png(path)
	print("screenshot=%s result=%d" % [path, result])
