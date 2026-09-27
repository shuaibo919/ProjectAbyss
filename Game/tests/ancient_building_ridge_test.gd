extends SceneTree

# -- --snapshot=PATH writes the legacy arrays' digest before/after a native rebuild.
# Normal run checks the ridge material surface, independently of the other slots.
var failures: Array[String] = []
var checked_triangles := 0

func check_surface(arrays: Array, label: String, expected_runs: int) -> void:
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var parents := {}
	var edges := {}
	for i in range(0, indices.size(), 3):
		var a := vertices[indices[i]]
		var b := vertices[indices[i + 1]]
		var c := vertices[indices[i + 2]]
		var front := -(b - a).cross(c - a)
		if not a.is_finite() or not b.is_finite() or not c.is_finite() or front.length_squared() < 1e-16:
			failures.append(label + ": nonfinite/degenerate ridge triangle")
		elif front.dot(normals[indices[i]] + normals[indices[i + 1]] + normals[indices[i + 2]]) <= 0.0:
			failures.append(label + ": inward triangle versus shading normal")
		checked_triangles += 1
		if expected_runs == 0:
			continue
		var keys := [a.snapped(Vector3.ONE * 0.00001), b.snapped(Vector3.ONE * 0.00001), c.snapped(Vector3.ONE * 0.00001)]
		for key in keys:
			if not parents.has(key):
				parents[key] = key
		for j in 3:
			var u: Vector3 = keys[j]
			var v: Vector3 = keys[(j + 1) % 3]
			var edge := str(u) + ":" + str(v) if u < v else str(v) + ":" + str(u)
			edges[edge] = edges.get(edge, 0) + 1
			parents[find_root(parents, u)] = find_root(parents, v)
	if expected_runs > 0:
		var roots := {}
		for key in parents:
			roots[find_root(parents, key)] = true
		if roots.size() != expected_runs:
			failures.append("%s: expected %d continuous ridge runs, got %d" % [label, expected_runs, roots.size()])
		for count in edges.values():
			if count != 2:
				failures.append(label + ": open or multiply covered ridge edge")
				break

func find_root(parents: Dictionary, key: Vector3) -> Vector3:
	while parents[key] != key:
		parents[key] = parents[parents[key]]
		key = parents[key]
	return key

func _initialize() -> void:
	var rows := {}
	for roof in 9:
		for lod in 3:
			for detail in 2:
				var p = ClassDB.instantiate("AncientBuildingParameters")
				p.roof_type = roof
				p.lod_level = lod
				p.ridge_detail = detail
				p.tile_detail = 2
				p.tile_bedding_thickness = 0.03
				var b = ClassDB.instantiate("AncientBuilding")
				b.auto_regenerate = false
				b.parameters = p
				var mesh: ArrayMesh = b.bake_mesh()
				var hash := HashingContext.new()
				hash.start(HashingContext.HASH_SHA256)
				hash.update(var_to_bytes(mesh.surface_get_arrays(0)))
				rows["%d/%d/%d" % [roof, lod, detail]] = {
					"sha256": hash.finish().hex_encode(), "triangles": b.get_slot_triangle_count(4)}
				if detail == 1:
					var marker := StandardMaterial3D.new()
					b.set_slot_material(4, marker)
					mesh = b.bake_mesh()
					for surface in mesh.get_surface_count():
						if mesh.surface_get_material(surface) == marker:
							check_surface(mesh.surface_get_arrays(surface), "%d/%d" % [roof, lod], 3 if roof in [0, 3] else (5 if roof == 5 else 0))
				b.free()
	# A steep roof must not let the verge miter project through the main crown.
	for courses in [3, 5, 9, 13]:
		for curve in [0, 1]:
			var p = ClassDB.instantiate("AncientBuildingParameters")
			p.ridge_detail = 1
			p.rafter_courses = courses
			p.roof_curve_mode = curve
			var b = ClassDB.instantiate("AncientBuilding")
			b.auto_regenerate = false
			b.parameters = p
			var marker := StandardMaterial3D.new()
			b.set_slot_material(4, marker)
			var mesh: ArrayMesh = b.bake_mesh()
			var ceiling: float = p.get_roof_base() + p.get_roof_height() + p.get_module() * 1.35 * 0.75
			for surface in mesh.get_surface_count():
				if mesh.surface_get_material(surface) != marker:
					continue
				var arrays := mesh.surface_get_arrays(surface)
				check_surface(arrays, "pitch %d curve %d" % [courses, curve], 3)
				for vertex in arrays[Mesh.ARRAY_VERTEX]:
					if vertex.y > ceiling + 0.001:
						failures.append("verge above main crown: courses=%d curve=%d" % [courses, curve])
						break
			b.free()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--snapshot="):
			var file := FileAccess.open(arg.trim_prefix("--snapshot="), FileAccess.WRITE)
			file.store_string(JSON.stringify(rows, "\t"))
	print("ridge mesh snapshots: %d" % rows.size())
	print("ridge triangles checked: %d, failures: %d" % [checked_triangles, failures.size()])
	var grouped := {}
	for message in failures:
		grouped[message] = grouped.get(message, 0) + 1
	print("failure categories: ", grouped)
	for message in failures.slice(0, 20):
		push_error(message)
	quit(0 if failures.is_empty() else 1)
