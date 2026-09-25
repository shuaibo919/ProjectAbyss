extends Node3D

# Validation + visual check for the baked mesh-edge outline (Source/OutlineGen/
# CartoonOutlineBuilder + Assets/Shaders/InkPainting/ink_mesh_edge_outline.gdshader,
# a port of Reference/UnitySimpleCartoonLine). Builds outlines for a box (exact edge
# count is knowable), a sphere (smooth surface: silhouettes only, no creases), a
# prism (creases between roof faces) and a ProceduralRock, screenshots the set to
# Reference/Shots/Outline/ via the shared ShotOutput helper.
#
# Run: Engine/bin/godot.windows.editor.x86_64.console.exe --path Game/ res://Develop/OutlineValidate.tscn

const ShotOutput := preload("res://Develop/Tools/shot_output.gd")
const InkBrush := preload("res://Develop/Tools/ink_brush.gd")
const OUTLINE_SHADER := preload("res://Assets/Shaders/InkPainting/ink_mesh_edge_outline.gdshader")

# A BoxMesh has 12 triangles / 24 vertices (per-face normals), but welded by
# position the unique edges are: 12 border edges + 6 face diagonals = 18.
const BOX_EXPECTED_EDGES := 18


func _ready() -> void:
	# The outline shader's AA is alpha-to-coverage, which needs MSAA on the
	# viewport; without it the alpha is ignored and lines render fully opaque.
	get_viewport().msaa_3d = Viewport.MSAA_4X

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-48, -35, 0)
	light.light_energy = 1.1
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.35, 0.38, 0.42)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.6, 0.6, 0.6)
	e.ambient_light_energy = 1.0
	env.environment = e
	add_child(env)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(10, 10)
	ground.mesh = plane
	var ground_mat := StandardMaterial3D.new()
	ground_mat.albedo_color = Color(0.25, 0.23, 0.2)
	ground.material_override = ground_mat
	add_child(ground)

	var failures: Array[String] = []
	var subjects: Array[MeshInstance3D] = []

	var box := MeshInstance3D.new()
	box.name = "Box"
	var box_mesh := BoxMesh.new()
	box_mesh.size = Vector3(0.8, 0.8, 0.8)
	box.mesh = box_mesh
	box.position = Vector3(-2.2, 0.4, 0)
	_add_outlined(box, Color(0.75, 0.55, 0.35), failures, "Box", BOX_EXPECTED_EDGES)
	subjects.append(box)

	var sphere := MeshInstance3D.new()
	sphere.name = "Sphere"
	var sphere_mesh := SphereMesh.new()
	sphere_mesh.radius = 0.5
	sphere_mesh.height = 1.0
	sphere_mesh.radial_segments = 24
	sphere_mesh.rings = 12
	sphere.mesh = sphere_mesh
	sphere.position = Vector3(-0.7, 0.5, 0)
	_add_outlined(sphere, Color(0.5, 0.65, 0.75), failures, "Sphere")
	subjects.append(sphere)

	var prism := MeshInstance3D.new()
	prism.name = "Prism"
	var prism_mesh := PrismMesh.new()
	prism_mesh.size = Vector3(0.9, 0.7, 0.9)
	prism.mesh = prism_mesh
	prism.position = Vector3(0.7, 0.35, 0)
	_add_outlined(prism, Color(0.7, 0.4, 0.4), failures, "Prism")
	subjects.append(prism)

	var rock: MeshInstance3D = ClassDB.instantiate("ProceduralRock")
	var rock_params = ClassDB.instantiate("ProceduralRockParameters")
	rock_params.seed = 42.0
	rock.parameters = rock_params
	rock.name = "Rock"
	rock.position = Vector3(2.2, 0.0, 0)
	# ProceduralRock generates on _ready; outline is attached after one frame below.
	rock.material_override = _flat_material(Color(0.55, 0.55, 0.58))
	add_child(rock)
	subjects.append(rock)

	# Open wavy ribbons: every edge is a boundary edge, so the two long sides bake
	# into two long strokes — the direct demo for stroke parameterization. Three
	# brush styles stacked vertically: bristle + tapered ends, burnt-ink grain +
	# dry tail, bristle + pulsing width.
	var brush_styles := [
		{"name": "RibbonBristle", "brush": InkBrush.make_brush_texture(), "amount": 0.8,
			"profile": InkBrush.make_width_profile(&"taper_both")},
		{"name": "RibbonGrain", "brush": InkBrush.make_grain_texture(), "amount": 0.9,
			"profile": InkBrush.make_width_profile(&"tail")},
		{"name": "RibbonPulse", "brush": InkBrush.make_brush_texture(13), "amount": 0.5,
			"profile": InkBrush.make_width_profile(&"pulse")},
	]
	for i in brush_styles.size():
		var style: Dictionary = brush_styles[i]
		var ribbon := MeshInstance3D.new()
		ribbon.name = style["name"]
		ribbon.mesh = _make_ribbon_mesh()
		ribbon.position = Vector3(-2.0, 1.3 + i * 0.45, -0.8)
		ribbon.material_override = _flat_material(Color(0.85, 0.82, 0.75))
		add_child(ribbon)
		subjects.append(ribbon)
		var outline := _attach_outline(ribbon, style["name"], failures)
		if outline == null:
			continue
		var brush_mat := outline.material_override as ShaderMaterial
		brush_mat.set_shader_parameter("line_width_px", 6.0)
		brush_mat.set_shader_parameter("width_jitter", 0.3)
		brush_mat.set_shader_parameter("brush_tex", style["brush"])
		brush_mat.set_shader_parameter("brush_amount", style["amount"])
		brush_mat.set_shader_parameter("brush_tile_size", 0.9)
		brush_mat.set_shader_parameter("width_profile_tex", style["profile"])
		if i == 0:
			# Chaining check: single-edge chains would cap u at one segment (~0.14);
			# the ribbon sides must chain into strokes several units long.
			var uvs: PackedVector2Array = outline.mesh.surface_get_arrays(0)[Mesh.ARRAY_TEX_UV]
			var max_u := 0.0
			for uv in uvs:
				max_u = max(max_u, uv.x)
			if max_u < 2.0:
				failures.append("Ribbon: max stroke arc length %.2f, chaining broken" % max_u)

	var cam := Camera3D.new()
	cam.position = Vector3(0, 2.2, 4.2)
	cam.current = true
	add_child(cam)
	cam.look_at(Vector3(0, 0.3, 0))

	await get_tree().process_frame

	# The rock only has a mesh after its _ready generate.
	var rock_outline := _attach_outline(rock, "Rock", failures)
	if rock_outline != null:
		var rock_verts: PackedVector3Array = rock_outline.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
		var rock_edges := rock_verts.size() / 6
		if rock_edges < 100:
			failures.append("Rock: only %d edges, suspiciously low for a rock" % rock_edges)

	for i in 12:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	get_viewport().get_texture().get_image().save_png(ShotOutput.file("Outline", "overview.png"))

	for subject in subjects:
		# Frame each subject off its own AABB (same pattern as GrassGenValidate).
		var aabb: AABB = subject.get_aabb()
		var focus: Vector3 = subject.position + aabb.get_center()
		var reach: float = aabb.size.length() * 0.9 + 0.2
		cam.position = focus + Vector3(reach * 0.4, reach * 0.45, reach)
		cam.look_at(focus)
		await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(
			ShotOutput.file("Outline", "%s.png" % subject.name.to_lower()))

	var report: Array[String] = []
	if failures.is_empty():
		report.append("OUTLINE_VALIDATE: PASS")
	else:
		report.append("OUTLINE_VALIDATE: FAIL")
		for f in failures:
			report.append("  - %s" % f)

	var report_text := "\n".join(report)
	print(report_text)
	var f := FileAccess.open(ShotOutput.file("Outline", "report.txt"), FileAccess.WRITE)
	f.store_string(report_text)
	f.close()

	get_tree().quit(0 if failures.is_empty() else 1)


# Flat sine-wave strip in the XY plane (faces the default camera). Open on all
# sides, so every edge is a boundary edge and the two long sides chain into two
# long strokes.
func _make_ribbon_mesh() -> ArrayMesh:
	const SEGMENTS := 40
	const LENGTH := 4.0
	const HALF_WIDTH := 0.07

	var verts := PackedVector3Array()
	var indices := PackedInt32Array()
	for i in SEGMENTS + 1:
		var t := float(i) / SEGMENTS
		var x := t * LENGTH
		var y := sin(t * TAU * 1.5) * 0.35
		verts.append(Vector3(x, y - HALF_WIDTH, 0))
		verts.append(Vector3(x, y + HALF_WIDTH, 0))
	for i in SEGMENTS:
		var a := i * 2
		indices.append_array([a, a + 1, a + 2, a + 1, a + 3, a + 2])

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _flat_material(albedo: Color) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = albedo
	mat.roughness = 0.9
	return mat


# Parents the box/sphere/prism cases: flat material on the source, outline child on top.
func _add_outlined(instance: MeshInstance3D, albedo: Color, failures: Array[String],
		label: String, expected_edges: int = -1) -> void:
	instance.material_override = _flat_material(albedo)
	add_child(instance)
	var outline := _attach_outline(instance, label, failures)
	if outline == null:
		return
	if expected_edges >= 0:
		var outline_verts: PackedVector3Array = outline.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
		var verts := outline_verts.size()
		if verts != expected_edges * 6:
			failures.append("%s: expected %d edges (%d verts), got %d verts" % [
				label, expected_edges, expected_edges * 6, verts])


func _attach_outline(instance: MeshInstance3D, label: String, failures: Array[String]) -> MeshInstance3D:
	if instance.mesh == null:
		failures.append("%s: no mesh to outline" % label)
		return null
	var outline_mesh: ArrayMesh = CartoonOutlineBuilder.build_outline_mesh(instance.mesh)
	if outline_mesh == null or outline_mesh.get_surface_count() == 0:
		failures.append("%s: outline mesh is empty" % label)
		return null
	var outline := MeshInstance3D.new()
	outline.name = "%sOutline" % label
	outline.mesh = outline_mesh
	var mat := ShaderMaterial.new()
	mat.shader = OUTLINE_SHADER
	mat.set_shader_parameter("line_width_px", 2.5)
	outline.material_override = mat
	instance.add_child(outline)
	return outline
