extends SceneTree

# Run with --headless --script res://tests/ancient_building_winding_test.gd.
# Optional GPU captures: omit --headless and append -- --capture=ABSOLUTE_DIRECTORY.
# Test outward orientation as well as normal/winding agreement: an inside-out box
# can have perfectly consistent normals and indices and still disappear outside.
var failures: Array[String] = []
var cases := 0
var preview_mesh: ArrayMesh
var preview_box: ArrayMesh


func check(ok: bool, message: String) -> void:
	if not ok:
		failures.append(message)


func bake(p: Resource) -> ArrayMesh:
	var building = ClassDB.instantiate("AncientBuilding")
	building.auto_regenerate = false
	building.parameters = p
	var result: ArrayMesh = building.bake_mesh()
	building.free()
	return result


func check_box(arrays: Array, label: String) -> void:
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	check(indices.size() == 36, label + ": expected twelve triangles")
	if vertices.is_empty() or indices.size() != 36:
		return
	var bounds := AABB(vertices[0], Vector3.ZERO)
	for vertex in vertices:
		bounds = bounds.expand(vertex)
	var centre := bounds.get_center()
	var face_triangles := [0, 0, 0, 0, 0, 0]
	for i in range(0, indices.size(), 3):
		var a := indices[i]
		var b := indices[i + 1]
		var c := indices[i + 2]
		var midpoint := (vertices[a] + vertices[b] + vertices[c]) / 3.0
		var front := -(vertices[b] - vertices[a]).cross(vertices[c] - vertices[a])
		check(front.length_squared() > 1e-14, "%s: degenerate triangle %d" % [label, i / 3])
		front = front.normalized()
		check(front.dot(midpoint - centre) > 0.0,
			"%s: triangle %d faces into the solid" % [label, i / 3])
		for index in [a, b, c]:
			check(normals[index].dot(midpoint - centre) > 0.0,
				"%s: triangle %d has inward shading normal" % [label, i / 3])
			check(front.dot(normals[index]) > 0.999,
				"%s: triangle %d normal disagrees with winding" % [label, i / 3])
		var axis := front.abs().max_axis_index()
		face_triangles[axis * 2 + (1 if front[axis] > 0.0 else 0)] += 1
	check(face_triangles == [2, 2, 2, 2, 2, 2], label + ": missing box face")
	cases += 1


func platform_box(mesh: ArrayMesh) -> Array:
	# BuildBuilding emits the platform body first via AddBox. It has 36 unshared
	# vertices; inspect that known convex solid independently of the whole building.
	var all_arrays := mesh.surface_get_arrays(0)
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	for slot in [Mesh.ARRAY_VERTEX, Mesh.ARRAY_NORMAL, Mesh.ARRAY_TEX_UV, Mesh.ARRAY_COLOR]:
		arrays[slot] = all_arrays[slot].slice(0, 36)
	arrays[Mesh.ARRAY_INDEX] = all_arrays[Mesh.ARRAY_INDEX].slice(0, 36)
	return arrays


func _initialize() -> void:
	# Godot's own primitive is the independent winding convention reference.
	var reference := BoxMesh.new()
	check_box(reference.surface_get_arrays(0), "Godot BoxMesh")
	for size in [Vector2(9.0, 6.0), Vector2(3.0, 11.0)]:
		for scale in [0.5, 1.0, 2.0]:
			var p = ClassDB.instantiate("AncientBuildingParameters")
			p.width = size.x
			p.depth = size.y
			p.platform_height_scale = scale
			p.generate_fence = false
			p.generate_steps = false
			var mesh := bake(p)
			var arrays := platform_box(mesh)
			check_box(arrays, "platform %s height_scale=%s" % [size, scale])
			if preview_box == null:
				preview_box = ArrayMesh.new()
				# Normalize the extracted production box to a cube for a clear visual probe.
				var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
				var bounds := AABB(vertices[0], Vector3.ZERO)
				for vertex in vertices:
					bounds = bounds.expand(vertex)
				for i in vertices.size():
					vertices[i] = (vertices[i] - bounds.get_center()) / bounds.size * 2.0
				arrays[Mesh.ARRAY_VERTEX] = vertices
				preview_box.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var detail = ClassDB.instantiate("AncientBuildingParameters")
	detail.generate_fence = false
	detail.dado_height_ratio = 0.25
	detail.dado_top_trim = 1.0
	detail.platform_top_joints = true
	detail.platform_edge_lip = true
	detail.column_base_square = true
	detail.column_base_height_scale = 0.35
	preview_mesh = bake(detail)
	check_box(platform_box(preview_mesh), "detailed dwelling platform")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--capture="):
			call_deferred("capture", arg.trim_prefix("--capture="))
			return
	finish()


func finish() -> void:
	for failure in failures.slice(0, 12):
		push_error(failure)
	print("AncientBuilding winding: %d box cases, %d failures" % [cases, failures.size()])
	quit(0 if failures.is_empty() else 1)


func capture(directory: String) -> void:
	if DisplayServer.get_name() == "headless":
		check(false, "capture requires a rendering display")
		finish()
		return
	DirAccess.make_dir_recursive_absolute(directory)
	var scene := Node3D.new()
	root.add_child(scene)
	var world := WorldEnvironment.new()
	world.environment = Environment.new()
	world.environment.background_mode = Environment.BG_COLOR
	world.environment.background_color = Color(0.10, 0.12, 0.16)
	world.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	world.environment.ambient_light_color = Color.WHITE
	world.environment.ambient_light_energy = 0.5
	scene.add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, -30, 0)
	scene.add_child(sun)
	var camera := Camera3D.new()
	camera.fov = 42.0
	scene.add_child(camera)
	camera.make_current()
	var building := MeshInstance3D.new()
	building.mesh = preview_mesh
	scene.add_child(building)
	# Same outside-facing material for every probe; backface culling stays enabled.
	var material := StandardMaterial3D.new()
	material.vertex_color_use_as_albedo = true
	material.cull_mode = BaseMaterial3D.CULL_BACK
	building.material_override = material
	camera.position = Vector3(3.7, 3.8, 7.0)
	camera.look_at(Vector3(2.8, 2.9, 3.0))
	await save_frame(directory.path_join("wall.png"))
	building.visible = false
	var cube := MeshInstance3D.new()
	cube.mesh = preview_box
	cube.material_override = material
	scene.add_child(cube)
	camera.position = Vector3(4.0, 3.0, 5.0)
	camera.look_at(Vector3.ZERO)
	await save_frame(directory.path_join("cube.png"))
	scene.free()
	finish()


func save_frame(path: String) -> void:
	for frame in range(4):
		await process_frame
	await RenderingServer.frame_post_draw
	check(root.get_texture().get_image().save_png(path) == OK, "capture failed: " + path)
	print("capture=" + path)
