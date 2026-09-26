extends SceneTree

# Geometry regression for the P4 檐下椽飞 increment (v2 P4): one shared 檐口断面 under every
# eave line, style 0 = none, 1 = 檐椽头, 2 = 檐椽头 + 飞椽 (the default). Run with
# --headless --script.
var failures: Array[String] = []

func check(ok: bool, message: String) -> void:
	if not ok:
		failures.append(message)
		push_error(message)

func bake(p: Resource) -> ArrayMesh:
	var building = ClassDB.instantiate("AncientBuilding")
	building.auto_regenerate = false
	building.parameters = p
	var result: ArrayMesh = building.bake_mesh()
	building.free()
	return result

func count_of(mesh: ArrayMesh) -> Vector2i:
	var arrays: Array = mesh.surface_get_arrays(0)
	return Vector2i(arrays[Mesh.ARRAY_VERTEX].size(), arrays[Mesh.ARRAY_INDEX].size() / 3)

func _initialize() -> void:
	# --- Per-roof: style ordering, footprint stability, mesh sanity ---
	var base = ClassDB.instantiate("AncientBuildingParameters")
	base.generate_fence = false
	for roof in range(9):
		base.roof_type = roof
		base.sides = 6 if roof >= 6 else 4
		var counts: Array[Vector2i] = []
		var meshes: Array[ArrayMesh] = []
		for style in range(3):
			base.eave_rafter_style = style
			meshes.append(bake(base))
			counts.append(count_of(meshes[style]))
		# Every rafter head is a whole sweep, so geometry grows strictly with the style.
		check(counts[2].x > counts[1].x and counts[1].x > counts[0].x,
			"roof %d vertex counts not ordered 2>1>0: %s" % [roof, str(counts)])
		check(counts[2].y > counts[1].y and counts[1].y > counts[0].y,
			"roof %d triangle counts not ordered 2>1>0: %s" % [roof, str(counts)])
		# The heads hang above the platform (y untouched); the square-cut end face leans out
		# over the eave line by at most H = 0.288 in plan, so x/z may grow by that per side.
		var a0: AABB = meshes[0].get_aabb()
		var a2: AABB = meshes[2].get_aabb()
		check(absf(a0.position.y - a2.position.y) < 1e-4 and absf(a0.size.y - a2.size.y) < 1e-4,
			"roof %d aabb height drifted with rafters" % roof)
		check(absf(a0.position.x - a2.position.x) <= 0.30 and absf(a0.position.z - a2.position.z) <= 0.30,
			"roof %d aabb origin drifted beyond the head lean: %s vs %s" % [roof, str(a0), str(a2)])
		check(absf(a0.size.x - a2.size.x) <= 0.60 and absf(a0.size.z - a2.size.z) <= 0.60,
			"roof %d aabb size grew beyond the head lean: %s vs %s" % [roof, str(a0), str(a2)])
		var arrays: Array = meshes[2].surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var norms: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		for i in verts.size():
			check(verts[i].is_finite() and norms[i].is_finite(),
				"nonfinite vertex/normal roof %d style 2" % roof)
			check(absf(norms[i].length() - 1.0) < 1e-3,
				"nonunit normal roof %d style 2" % roof)
		# Style 2 must put geometry strictly below the 望板 soffit, under the footprint —
		# that is the whole point of the heads.
		var found: bool = false
		for v in verts:
			if v.y < base.get_roof_base() - 0.05 and absf(v.x) < 8.0 and absf(v.z) < 8.0:
				found = true
				break
		check(found, "roof %d style 2 has no head below the soffit" % roof)
		print("roof=%d %s" % [roof, str(counts)])

	# --- Canonical pin: 歇山 style 2, count pin + determinism ---
	base.roof_type = 1
	base.sides = 4
	base.eave_rafter_style = 2
	var pin: Array = bake(base).surface_get_arrays(0)
	check(pin[Mesh.ARRAY_VERTEX].size() == 53586,
		"歇山 style 2 vertices drifted: %d" % pin[Mesh.ARRAY_VERTEX].size())
	check(pin[Mesh.ARRAY_INDEX].size() / 3 == 27230,
		"歇山 style 2 triangles drifted: %d" % (pin[Mesh.ARRAY_INDEX].size() / 3))
	var again: Array = bake(base).surface_get_arrays(0)
	check(again[Mesh.ARRAY_VERTEX] == pin[Mesh.ARRAY_VERTEX], "rafter bake not deterministic")
	check(again[Mesh.ARRAY_INDEX] == pin[Mesh.ARRAY_INDEX], "rafter bake indices not deterministic")

	print("AncientBuildingEaveRafter: %s (%d failures)" % ["PASS" if failures.is_empty() else "FAIL", failures.size()])
	quit(0 if failures.is_empty() else 1)
