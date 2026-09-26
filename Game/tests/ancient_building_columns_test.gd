extends SceneTree

# Geometry regression for the P2 column/plinth increment. Run with --headless --script.
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

func column_arrays(p: Resource) -> Array:
	p.generate_columns = false
	var without := bake(p).surface_get_arrays(0)
	p.generate_columns = true
	var with_columns := bake(p).surface_get_arrays(0)
	var a: PackedVector3Array = without[Mesh.ARRAY_VERTEX]
	var b: PackedVector3Array = with_columns[Mesh.ARRAY_VERTEX]
	var start := 0
	while start < a.size() and a[start] == b[start]:
		start += 1
	var count := b.size() - a.size()
	check(count > 0, "columns must add geometry")
	var arrays: Array = []
	for slot in [Mesh.ARRAY_VERTEX, Mesh.ARRAY_NORMAL, Mesh.ARRAY_COLOR]:
		arrays.append(with_columns[slot].slice(start, start + count))
	var indices: PackedInt32Array = with_columns[Mesh.ARRAY_INDEX]
	var vertices: PackedVector3Array = with_columns[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = with_columns[Mesh.ARRAY_NORMAL]
	for i in range(0, indices.size(), 3):
		var ia := indices[i]
		if ia < start or ia >= start + count:
			continue
		var ib := indices[i + 1]
		var ic := indices[i + 2]
		var geometric := -(vertices[ib] - vertices[ia]).cross(vertices[ic] - vertices[ia])
		check(geometric.length_squared() > 1e-14, "degenerate support triangle")
		check(geometric.dot(normals[ia] + normals[ib] + normals[ic]) > 0,
			"support triangle faces against its normals")
	# Keyed support colours must not shift unrelated roof/beam colours.
	check(with_columns[Mesh.ARRAY_COLOR].slice(start + count) == without[Mesh.ARRAY_COLOR].slice(start),
		"column toggle changed unrelated component colours")
	return arrays

func _initialize() -> void:
	var p = ClassDB.instantiate("AncientBuildingParameters")
	p.roof_type = 1
	p.generate_fence = false
	p.column_sides = 24
	var baseline := bake(p)
	p.column_base_height_scale = 0.65
	var detailed := bake(p)
	check(baseline.get_aabb().is_equal_approx(detailed.get_aabb()), "plinth changed overall building bounds")
	var a := column_arrays(p)
	var positions: PackedVector3Array = a[0]
	var normals: PackedVector3Array = a[1]
	for i in positions.size():
		check(positions[i].is_finite() and normals[i].is_finite(), "nonfinite support vertex/normal")
		check(absf(normals[i].length() - 1.0) < 0.0001, "nonunit support normal")
	var column_top: float = p.get_platform_height() + p.get_column_height()
	var max_y := -INF
	for point in positions:
		max_y = maxf(max_y, point.y)
	check(absf(max_y - column_top) < 0.0001, "plinth moved the column top")
	# Check analytic taper normals on first column at the original southwest bay pivot.
	var pivot := Vector3(-p.width * 0.5, p.get_platform_height(), -p.depth * 0.5)
	var radius: float = p.get_module() * p.column_radius_scale
	var expected_slope: float = radius * 0.12 / p.get_column_height()
	var shaft_normals := 0
	for i in positions.size():
		var delta := positions[i] - pivot
		if Vector2(delta.x, delta.z).length() <= radius + 0.0001 and normals[i].y > 0 and normals[i].y < 0.1:
			var expected := Vector3(delta.x, 0, delta.z).normalized()
			expected.y = expected_slope
			check(normals[i].dot(expected.normalized()) > 0.9999, "incorrect tapered shaft normal")
			shaft_normals += 1
	check(shaft_normals > 40, "analytic shaft normals were not found")
	# Tessellation must not change a support's colour or structural heights.
	p.column_sides = 12
	var coarse := column_arrays(p)
	check(a[2][0] == coarse[2][0], "plinth tint changed with tessellation")
	check(a[2][-1] == coarse[2][-1], "shaft tint changed with tessellation")
	p.generate_fence = true
	var fenced := column_arrays(p)
	check(coarse[2] == fenced[2], "support colours changed with unrelated fence geometry")
	p.generate_fence = false
	# Negative input must behave as disabled; extreme height must leave a usable shaft.
	p.column_base_height_scale = -1.0
	var negative := bake(p)
	p.column_base_height_scale = 0.0
	check(negative.surface_get_arrays(0)[Mesh.ARRAY_VERTEX] == bake(p).surface_get_arrays(0)[Mesh.ARRAY_VERTEX],
		"negative base height was not clamped")
	p.column_base_height_scale = 100.0
	check(bake(p).get_aabb().is_equal_approx(baseline.get_aabb()), "extreme base height moved roof")
	# Existing nine roof families, plus a genuine polygonal body.
	p.column_base_height_scale = 0.65
	for roof in range(9):
		p.roof_type = roof
		p.sides = 6 if roof >= 6 else 4
		var mesh := bake(p)
		var data := mesh.surface_get_arrays(0)
		check(data[Mesh.ARRAY_VERTEX].size() > 0, "empty roof %d" % roof)
		for index in data[Mesh.ARRAY_INDEX]:
			check(index >= 0 and index < data[Mesh.ARRAY_VERTEX].size(), "invalid index roof %d" % roof)
		print("roof=%d vertices=%d triangles=%d" % [roof, data[Mesh.ARRAY_VERTEX].size(), data[Mesh.ARRAY_INDEX].size() / 3])
	print("AncientBuildingColumns: %s (%d failures)" % ["PASS" if failures.is_empty() else "FAIL", failures.size()])
	quit(0 if failures.is_empty() else 1)
