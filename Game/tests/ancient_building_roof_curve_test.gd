extends SceneTree

# Geometry regression for the P1 continuous roof curve increment (v2 P1.1-P1.3).
# Run with --headless --script.
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

# Key anchors must match tightly: the ground plane never moves, and the ridge top is the curve's
# end anchor (identical by construction). Two kinds of legitimate drift get a looser bound:
#  - plan extents: the hip-ridge sweeps reach their X/Z extreme at a knot the profile samples
#    slightly differently between modes (measured 0.77-2.76mm at the corners), a sweep
#    displacement rather than an anchor drift;
#  - roof height on 盝顶: its flat cap sits at the 收山 break, where legacy interpolated the
#    chord (up to ~8mm above the true curve) — continuous mode puts the cap on the curve, which
#    is the documented v2 P1.2 mode upgrade, not a regression.
func aabb_close(a: AABB, b: AABB) -> bool:
	return absf(a.position.x - b.position.x) <= 0.005 \
		and absf(a.position.y - b.position.y) <= 0.0001 \
		and absf(a.position.z - b.position.z) <= 0.005 \
		and absf(a.size.x - b.size.x) <= 0.01 \
		and absf(a.size.y - b.size.y) <= 0.01 \
		and absf(a.size.z - b.size.z) <= 0.01

# v2 P1.2: y(t) = Rise * [a*t + (b-a)*t^2/2] / [(a+b)/2]
func curve_y(eave: float, ridge: float, rise: float, t: float) -> float:
	var mean: float = (eave + ridge) * 0.5
	if absf(mean) < 1e-9:
		return 0.0
	return rise * (eave * t + (ridge - eave) * t * t * 0.5) / mean

func dist_point_segment(p: Vector2, a: Vector2, b: Vector2) -> float:
	var chord: Vector2 = b - a
	var length: float = chord.length()
	if length < 1e-9:
		return p.distance_to(a)
	return absf(chord.x * (p.y - a.y) - chord.y * (p.x - a.x)) / length

func _initialize() -> void:
	# --- Sampler fidelity: the polyline is the analytic curve within chord error ---
	var params = ClassDB.instantiate("AncientBuildingParameters")
	var profile: PackedVector2Array = params.sample_roof_curve(0.5, 0.9, 3.9, 6.0, 0.005, 0.5)
	check(profile.size() >= 2, "sampler returned fewer than 2 points")
	check((profile[0] - Vector2(6.0, 0.0)).length() < 1e-4, "sampler eave anchor off")
	check((profile[-1] - Vector2(0.0, 3.9)).length() < 1e-4, "sampler ridge anchor off")
	for i in range(profile.size() - 1):
		check(profile[i].distance_to(profile[i + 1]) <= 0.5 + 1e-4, "sampler segment exceeds max length")
		check(profile[i].x > profile[i + 1].x, "sampler profile not monotonic in span")
	for pt in profile:
		var t: float = 1.0 - pt.x / 6.0
		check(absf(pt.y - curve_y(0.5, 0.9, 3.9, t)) < 1e-4, "sample off the analytic curve")
	for k in range(401):
		var t: float = float(k) / 400.0
		var q: Vector2 = Vector2(6.0 * (1.0 - t), curve_y(0.5, 0.9, 3.9, t))
		var best: float = INF
		for i in range(profile.size() - 1):
			best = minf(best, dist_point_segment(q, profile[i], profile[i + 1]))
		check(best <= 0.005 + 1e-4, "polyline deviates from the curve by more than chord error")
	# The curve passes through every legacy node (midpoint-rule identity).
	var courses: int = 5
	var run: float = 6.0 / courses
	var total: float = 0.0
	for i in range(courses):
		total += run * (0.5 + 0.4 * (float(i) + 0.5) / float(courses))
	var height: float = 0.0
	var distance: float = 6.0
	for i in range(courses):
		height += run * (0.5 + 0.4 * (float(i) + 0.5) / float(courses)) * (3.9 / total)
		distance -= run
		var t: float = 1.0 - distance / 6.0
		check(absf(curve_y(0.5, 0.9, 3.9, t) - height) < 1e-4, "curve misses legacy node %d" % i)
	# Degenerate ramp (a + b = 0) must fall back to the flat profile, like legacy's zero scale.
	var flat: PackedVector2Array = params.sample_roof_curve(0.5, -0.5, 3.9, 6.0, 0.005, 0.5)
	for pt in flat:
		check(absf(pt.y) < 1e-4, "degenerate ramp not flat")

	# --- Legacy regression pin: the P2 column sample bakes the recorded numbers ---
	var legacy_pin = ClassDB.instantiate("AncientBuildingParameters")
	legacy_pin.roof_type = 1
	legacy_pin.generate_fence = false
	legacy_pin.generate_walls = false
	legacy_pin.column_sides = 10
	legacy_pin.smooth_columns = false
	var pin_arrays: Array = bake(legacy_pin).surface_get_arrays(0)
	check(pin_arrays[Mesh.ARRAY_VERTEX].size() == 44704,
		"legacy baseline vertices drifted: %d" % pin_arrays[Mesh.ARRAY_VERTEX].size())
	check(pin_arrays[Mesh.ARRAY_INDEX].size() / 3 == 22590,
		"legacy baseline triangles drifted: %d" % (pin_arrays[Mesh.ARRAY_INDEX].size() / 3))
	check(bake(legacy_pin).surface_get_arrays(0)[Mesh.ARRAY_VERTEX] == pin_arrays[Mesh.ARRAY_VERTEX],
		"legacy bake not deterministic")

	# --- Continuous vs legacy: anchors, mesh sanity, determinism, coverage extremes ---
	var base = ClassDB.instantiate("AncientBuildingParameters")
	base.generate_fence = false
	for roof in range(9):
		base.roof_type = roof
		base.sides = 6 if roof >= 6 else 4
		base.roof_curve_mode = 0
		var legacy_mesh: ArrayMesh = bake(base)
		base.roof_curve_mode = 1
		var curve_mesh: ArrayMesh = bake(base)
		var la: Array = legacy_mesh.surface_get_arrays(0)
		var ca: Array = curve_mesh.surface_get_arrays(0)
		check(legacy_mesh.get_aabb().is_equal_approx(curve_mesh.get_aabb()) \
			or aabb_close(legacy_mesh.get_aabb(), curve_mesh.get_aabb()),
			"roof %d aabb drifted between modes" % roof)
		check(ca[Mesh.ARRAY_VERTEX].size() > 0, "empty continuous roof %d" % roof)
		# Continuous boarding is smooth-shaded (4 vertices per quad) while legacy uses the flat
		# emitter (6 vertices per quad), so vertex counts are not comparable across modes.
		# Triangle counts measure the geometry: continuous must sample no less densely.
		check(ca[Mesh.ARRAY_INDEX].size() >= la[Mesh.ARRAY_INDEX].size(),
			"continuous roof %d has fewer triangles than legacy" % roof)
		for i in ca[Mesh.ARRAY_VERTEX].size():
			check(ca[Mesh.ARRAY_VERTEX][i].is_finite() and ca[Mesh.ARRAY_NORMAL][i].is_finite(),
				"nonfinite vertex/normal roof %d" % roof)
			check(absf(ca[Mesh.ARRAY_NORMAL][i].length() - 1.0) < 1e-3,
				"nonunit normal roof %d" % roof)
		for index in ca[Mesh.ARRAY_INDEX]:
			check(index >= 0 and index < ca[Mesh.ARRAY_VERTEX].size(), "invalid index roof %d" % roof)
		print("roof=%d legacy=%d/%d continuous=%d/%d" % [roof,
			la[Mesh.ARRAY_VERTEX].size(), la[Mesh.ARRAY_INDEX].size() / 3,
			ca[Mesh.ARRAY_VERTEX].size(), ca[Mesh.ARRAY_INDEX].size() / 3])
	base.roof_type = 1
	base.sides = 4
	base.roof_curve_mode = 1
	var d1: Array = bake(base).surface_get_arrays(0)
	var d2: Array = bake(base).surface_get_arrays(0)
	check(d1[Mesh.ARRAY_VERTEX] == d2[Mesh.ARRAY_VERTEX], "continuous bake not deterministic")
	# Coverage extremes bake cleanly; partial coverage inserts its boundary point.
	for coverage in [0.0, 0.33, 0.5, 1.0]:
		base.tile_coverage = coverage
		var mesh: ArrayMesh = bake(base)
		check(mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX].size() > 0,
			"coverage %.2f broke the roof" % coverage)
	base.tile_coverage = 1.0

	# --- Smooth boarding normals: the P1.2 shading contract ---
	# Adjacent boarding bands share their boundary profile node exactly, so two bands meet at the
	# same (y, z) position. In continuous mode that position carries one normal — the analytic
	# curve normal, which both bands sample identically. Legacy carries two: the adjacent course
	# facets. The window keeps the lower tier boundary (above the skirt break, below the ridge
	# caps); coverage 0 bares the boarding, and the (y, z) key keeps skirt-top and ridge vertices
	# from colliding with tier vertices.
	base.tile_coverage = 0.0
	var split_count: Array[int] = []
	for mode in [0, 1]:
		base.roof_curve_mode = mode
		var arrays: Array = bake(base).surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var norms: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		var groups := {}
		for i in verts.size():
			# The y floor sits above the skirt's top rings (their corner wedges legitimately carry
			# different azimuth-tilted normals at the same y,z on opposite sides of the building).
			if verts[i].y > base.get_roof_base() + 1.55 and verts[i].y < base.get_roof_base() + 2.1 \
					and verts[i].z > 0.05 and verts[i].z < 2.5 \
					and norms[i].y > 0.5 and norms[i].z > 0.3:
				var key: String = "%.4f,%.4f" % [verts[i].y, verts[i].z]
				if not groups.has(key):
					groups[key] = {}
				groups[key]["%s,%s,%s" % [snappedf(norms[i].x, 0.0001), snappedf(norms[i].y, 0.0001), snappedf(norms[i].z, 0.0001)]] = true
		var split: int = 0
		for key in groups.keys():
			if groups[key].size() > 1:
				split += 1
		split_count.append(split)
	base.tile_coverage = 1.0
	check(split_count[0] > 0, "legacy tier has no facet-normal splits at band boundaries")
	check(split_count[1] == 0, "continuous tier still splits normals at band boundaries: %d" % split_count[1])

	print("AncientBuildingRoofCurve: %s (%d failures)" % ["PASS" if failures.is_empty() else "FAIL", failures.size()])
	quit(0 if failures.is_empty() else 1)
