extends Node3D

# Validation + visual check for ProceduralGrass (Source/GrassGen/). Instantiates all
# four species side by side, asserts the invariants noted in
# Docs/ProceduralGrass_Spec.md §6, and screenshots each species plus an overview to
# Reference/Shots/Grass/ via the shared ShotOutput helper (dev renders must not land
# under Game/ — see Game/Develop/Tools/shot_output.gd).
#
# Run: Engine/bin/godot.windows.editor.x86_64.console.exe --path Game/ res://Develop/GrassGenValidate.tscn

const ShotOutput := preload("res://Develop/Tools/shot_output.gd")

const SPECIES_NAMES := ["Thatch", "Foxtail", "Short", "Weed"]
const SPECIES_SPACING := 1.2


func _ready() -> void:
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-48, -35, 0)
	light.light_energy = 1.1
	light.shadow_enabled = true
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.35, 0.38, 0.42)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.55, 0.55, 0.55)
	e.ambient_light_energy = 1.0
	env.environment = e
	add_child(env)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(6, 6)
	ground.mesh = plane
	var ground_mat := StandardMaterial3D.new()
	ground_mat.albedo_color = Color(0.22, 0.2, 0.16)
	ground.material_override = ground_mat
	add_child(ground)

	var failures: Array[String] = []
	var clumps: Array[Node3D] = []

	for i in SPECIES_NAMES.size():
		var clump = ClassDB.instantiate("ProceduralGrass")
		clump.name = "Grass_%s" % SPECIES_NAMES[i]
		var params = ClassDB.instantiate("ProceduralGrassParameters")
		params.species = i
		params.seed = 100.0 + i * 7.0
		clump.parameters = params
		clump.position = Vector3(-1.5 * SPECIES_SPACING + i * SPECIES_SPACING, 0, 0)
		add_child(clump)
		clumps.append(clump)

		if clump.get_triangle_count() <= 0:
			failures.append("%s: no geometry generated" % SPECIES_NAMES[i])
			continue

		if not _is_mesh_finite(clump.mesh):
			failures.append("%s: non-finite vertex/normal in generated mesh" % SPECIES_NAMES[i])

	# Determinism: regenerating the same params must reproduce the exact vertex count.
	var short_clump: Node3D = clumps[2]
	var short_verts_before: int = short_clump.get_vertex_count()
	short_clump.generate()
	if short_clump.get_vertex_count() != short_verts_before:
		failures.append("Short: same-seed regenerate produced a different vertex count "
			+ "(%d -> %d)" % [short_verts_before, short_clump.get_vertex_count()])

	# Variation: a different seed on the same species must not reproduce it exactly.
	var alt_params = ClassDB.instantiate("ProceduralGrassParameters")
	alt_params.species = 2
	alt_params.seed = 999.0
	var alt_clump = ClassDB.instantiate("ProceduralGrass")
	alt_clump.parameters = alt_params
	add_child(alt_clump)
	if alt_clump.get_vertex_count() == short_clump.get_vertex_count():
		failures.append("Short: seed 100 and seed 999 produced the same vertex count "
			+ "(%d) -- suspiciously seed-independent" % alt_clump.get_vertex_count())
	alt_clump.queue_free()

	var cam := Camera3D.new()
	cam.position = Vector3(0, 1.6, 2.6)
	cam.current = true
	add_child(cam)
	cam.look_at(Vector3(0, 0.3, 0))

	for i in 12:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	get_viewport().get_texture().get_image().save_png(ShotOutput.file("Grass", "overview.png"))

	for i in SPECIES_NAMES.size():
		# Auto-frame per species: Short's 5-15cm blades and Thatch's 0.6-1.4m blades
		# need very different camera distances, so size the shot off the clump's own
		# generated AABB instead of one fixed offset for all four.
		var aabb: AABB = clumps[i].get_aabb()
		var focus: Vector3 = clumps[i].position + aabb.get_center()
		var reach: float = aabb.size.length() * 0.75 + 0.05
		cam.position = focus + Vector3(0, reach * 0.35, reach)
		cam.look_at(focus)
		await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(
			ShotOutput.file("Grass", "%s.png" % SPECIES_NAMES[i].to_lower()))

	var report: Array[String] = []
	if failures.is_empty():
		report.append("GRASSGEN_VALIDATE: PASS (%s)" % ", ".join(
			SPECIES_NAMES.map(func(n): return "%s ok" % n)))
	else:
		report.append("GRASSGEN_VALIDATE: FAIL")
		for f in failures:
			report.append("  - %s" % f)

	for i in SPECIES_NAMES.size():
		report.append("%s: %d verts / %d tris" % [
			SPECIES_NAMES[i], clumps[i].get_vertex_count(), clumps[i].get_triangle_count()])

	var report_text := "\n".join(report)
	print(report_text)
	var f := FileAccess.open(ShotOutput.file("Grass", "report.txt"), FileAccess.WRITE)
	f.store_string(report_text)
	f.close()

	get_tree().quit(0 if failures.is_empty() else 1)


func _is_mesh_finite(mesh: Mesh) -> bool:
	if mesh == null:
		return false
	var arrays := mesh.surface_get_arrays(0)
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	for v in verts:
		if not (is_finite(v.x) and is_finite(v.y) and is_finite(v.z)):
			return false
	for n in normals:
		if not (is_finite(n.x) and is_finite(n.y) and is_finite(n.z)):
			return false
	return true
