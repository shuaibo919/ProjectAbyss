extends SceneTree
## Topology, attachment, determinism and resource/worker integration checks.

const OPTIONS := {"crossed_cards": true, "species_rules": true, "growth_debug": true}
var failures: Array[String] = []

func _initialize():
	call_deferred("run")

func check(condition: bool, message: String):
	if not condition:
		failures.append(message)
		push_error(message)

func verify_skeleton(result: Dictionary, context: String):
	var skeleton: Dictionary = result.growth_skeleton
	var points: PackedVector3Array = skeleton.points
	var radii: PackedFloat32Array = skeleton.radii
	var offsets: PackedInt32Array = skeleton.offsets
	var parents: PackedInt32Array = skeleton.parents
	check(parents[0] == -1 and offsets.size() == parents.size() + 1, context + " root/offsets")
	var valid_attachment := true
	var valid_radius := true
	var valid_length := true
	for stem in parents.size():
		if parents[stem] >= 0:
			var parent: int = parents[stem]
			valid_attachment = valid_attachment and parent >= 0 and parent < stem
			var parent_point: int = offsets[parent] + skeleton.attachments[stem]
			valid_attachment = valid_attachment and points[offsets[stem]].distance_to(points[parent_point]) < 0.000001
			if skeleton.roles[stem] != 5:
				valid_radius = valid_radius and radii[offsets[stem]] <= radii[parent_point] + 0.000001
		else:
			valid_attachment = valid_attachment and skeleton.roles[stem] == 0
		for index in range(offsets[stem], offsets[stem + 1]):
			valid_radius = valid_radius and is_finite(radii[index]) and radii[index] > 0.0
			valid_length = valid_length and points[index].is_finite()
			if index > offsets[stem]:
				valid_length = valid_length and points[index].distance_to(points[index - 1]) > 0.000001
	check(valid_attachment, context + " disconnected skeleton attachment")
	check(valid_radius, context + " invalid radius or child thicker than parent")
	check(valid_length, context + " invalid/zero-length skeleton segment")

func verify_surface(mesh: Mesh, context: String, expected_components: int = 1):
	var arrays: Array = mesh.surface_get_arrays(0)
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var edges := {}
	var adjacency := {}
	var degenerate := 0
	var backwards := 0
	var welded := {}
	var canonical := PackedInt32Array()
	for vertex in vertices:
		if not welded.has(vertex):
			welded[vertex] = welded.size()
		canonical.append(welded[vertex])
	var signed_volume := 0.0
	for triangle in range(0, indices.size(), 3):
		var a := indices[triangle]
		var b := indices[triangle + 1]
		var c := indices[triangle + 2]
		var face := (vertices[b] - vertices[a]).cross(vertices[c] - vertices[a])
		signed_volume += vertices[a].dot(vertices[b].cross(vertices[c])) / 6.0
		if face.length_squared() < 1e-18:
			degenerate += 1
		# Godot uses clockwise front faces. Vertex normals face away from the winding cross product.
		if face.dot(normals[a] + normals[b] + normals[c]) > 0.000000001:
			backwards += 1
			if "--probe" in OS.get_cmdline_user_args() and backwards <= 4:
				print("NORMAL_PROBE ", context, " ", [a, b, c], " ", [vertices[a], vertices[b], vertices[c]], " ", face.normalized().dot((normals[a] + normals[b] + normals[c]).normalized()))
		# A shading crease duplicates normals/UVs, not the geometric boundary.
		for pair in [Vector2i(canonical[a], canonical[b]), Vector2i(canonical[b], canonical[c]), Vector2i(canonical[c], canonical[a])]:
			var key := Vector2i(mini(pair.x, pair.y), maxi(pair.x, pair.y))
			var edge: Vector2i = edges.get(key, Vector2i.ZERO)
			edges[key] = edge + Vector2i(1, 1 if pair.x < pair.y else -1)
			if not adjacency.has(pair.x):
				adjacency[pair.x] = []
			adjacency[pair.x].append(pair.y)
	var boundary := 0
	var nonmanifold := 0
	var reversed_edges := 0
	for edge: Vector2i in edges.values():
		boundary += int(edge.x == 1)
		nonmanifold += int(edge.x > 2)
		reversed_edges += int(edge.y != 0)
	var visited := {}
	var components := 0
	for start in adjacency:
		if visited.has(start):
			continue
		components += 1
		var pending := [start]
		while not pending.is_empty():
			var vertex: int = pending.pop_back()
			if visited.has(vertex):
				continue
			visited[vertex] = true
			pending.append_array(adjacency[vertex])
	check(boundary == 0 and nonmanifold == 0 and reversed_edges == 0,
		context + " invalid edge topology " + str([boundary, nonmanifold, reversed_edges]))
	check(components == expected_components, context + " detached/missing wood component: " + str(components))
	check(adjacency.size() - edges.size() + indices.size() / 3 == 2 * expected_components,
		context + " unexpected handle/pinched topology, Euler=" + str(adjacency.size() - edges.size() + indices.size() / 3))
	check(degenerate == 0, context + " degenerate triangles")
	check(signed_volume < -0.000001, context + " inside-out closed surface")
	check(backwards == 0, context + " inverted winding/normals: " + str(backwards))
	print("WOOD_TOPOLOGY ", context, " ", {"triangles": indices.size() / 3, "boundary": boundary,
		"nonmanifold": nonmanifold, "reversed_edges": reversed_edges, "backwards": backwards, "components": components})

func verify_species(result: Dictionary, preset: int, context: String):
	var skeleton: Dictionary = result.growth_skeleton
	var offsets: PackedInt32Array = skeleton.offsets
	var points: PackedVector3Array = skeleton.points
	var roots := 0
	var pairs := {}
	for stem in skeleton.parents.size():
		var parent: int = skeleton.parents[stem]
		if parent < 0:
			roots += 1
			if preset == 6:
				check(points[offsets[stem + 1] - 1].y < 1.1, context + " peach retained a tall central leader")
			continue
		if preset == 4 and skeleton.roles[parent] == 0:
			check(skeleton.attachments[stem] % 3 == 0, context + " bamboo branch between culm nodes")
		if (preset == 4 and skeleton.roles[parent] == 0) or (preset == 5 and skeleton.roles[stem] == 6):
			var key := Vector2i(parent, skeleton.attachments[stem])
			if not pairs.has(key):
				pairs[key] = []
			pairs[key].append(stem)
	check(roots == (3 if preset == 4 else 1), context + " lost a preset trunk")
	if preset in [2, 4, 5]:
		check(result.growth_stats.forks == 0, context + " unexpected broadleaf fork rule")
	if preset in [0, 1, 3, 6]:
		check(result.growth_stats.forks > 0, context + " missing unequal forks")
	if preset == 4:
		for key in pairs:
			check(pairs[key].size() == 2, context + " bamboo node lost its paired branches")
			if pairs[key].size() == 2:
				var left: int = pairs[key][0]
				var right: int = pairs[key][1]
				var left_axis := (points[offsets[left] + 1] - points[offsets[left]]).normalized()
				var right_axis := (points[offsets[right] + 1] - points[offsets[right]]).normalized()
				check(left_axis.dot(right_axis) > 0.0, context + " bamboo branches point to opposite sides")
	if preset == 5:
		for key in pairs:
			check(pairs[key].size() % 2 == 0, context + " dawn redwood lost opposite branchlet pairs")
	if preset == 6:
		var arrays: Array = result.mesh.surface_get_arrays(1)
		var shapes := {}
		for uv: Vector2 in arrays[Mesh.ARRAY_TEX_UV]:
			var cell := floori(uv.y * 4.0) * 4 + floori(uv.x * 4.0)
			shapes[cell / 2] = true
		check(shapes.has(6) and shapes.has(7), context + " peach lost leaves or blossoms")

func wait_preview(tree: Node):
	var deadline := Time.get_ticks_msec() + 10000
	while tree.is_preview_pending() and Time.get_ticks_msec() < deadline:
		await process_frame
	check(not tree.is_preview_pending(), "Worker did not complete")

func run():
	for preset in range(7):
		for seed_value in [0, 17, 42]:
			var result := SlowTreeGenerator.generate(preset, seed_value, false, 2.0, OPTIONS)
			var context := "%d/%d" % [preset, seed_value]
			check(result.error.is_empty() and not result.truncated, context + " failed or exhausted budget")
			verify_species(result, preset, context)
			verify_skeleton(result, context)
			verify_surface(result.mesh, context, result.growth_stats.trunks)
			print("BRANCH_STATS ", context, " ", result.growth_stats)
		var low := OPTIONS.duplicate()
		low.radial_segments = 3
		var coarse := SlowTreeGenerator.generate(preset, 7, false, 2.0, low)
		verify_surface(coarse.mesh, "%d/radial3" % preset, coarse.growth_stats.trunks)
		var gpu := SlowTreeGenerator.generate(preset, 7, true, 2.0, low)
		check(gpu.tessellation_backend == "cpu_connected", "Connected topology backend is not reported")
		check(var_to_bytes(gpu.mesh.surface_get_arrays(0)) == var_to_bytes(coarse.mesh.surface_get_arrays(0)), "Backend selection changed connected geometry")
		var stress := ProceduralTreeGrowthParameters.new()
		stress.trunk_bend = 3.0
		stress.branch_bend = 3.0
		stress.forking = 2.0
		stress.radius_power = 1.8
		stress.junction_shape = 2.0
		var stress_options := OPTIONS.duplicate()
		stress_options.growth_parameters = stress
		var extreme := SlowTreeGenerator.generate(preset, 19, false, 2.0, stress_options)
		verify_skeleton(extreme, "%d/stress" % preset)
		verify_surface(extreme.mesh, "%d/stress" % preset, extreme.growth_stats.trunks)
		var low_budget := OPTIONS.duplicate()
		low_budget.max_segments = 100
		var capped := SlowTreeGenerator.generate(preset, 0, false, 2.0, low_budget)
		check(capped.truncated and capped.growth_stats.segments <= 100, "Skeleton budget exceeded")
		verify_surface(capped.mesh, "%d/budget100" % preset, capped.growth_stats.trunks)
		var dense := SlowTreeGenerator.generate(preset, 11, false, 2.0, OPTIONS)
		var thin_options := OPTIONS.duplicate()
		thin_options.leaf_density = 0.2
		var thin := SlowTreeGenerator.generate(preset, 11, false, 2.0, thin_options)
		check(var_to_bytes(dense.mesh.surface_get_arrays(0)) == var_to_bytes(thin.mesh.surface_get_arrays(0)),
			"Preset %d foliage density changed support radii" % preset)
		var growth := ProceduralTreeGrowthParameters.new()
		var lengths := Curve.new()
		lengths.max_value = 3.0
		lengths.add_point(Vector2(0, 1.3))
		lengths.add_point(Vector2(1, 1.3))
		growth.length_by_height = lengths
		var custom_options := OPTIONS.duplicate()
		custom_options.growth_parameters = growth
		var custom := SlowTreeGenerator.generate(preset, 11, false, 2.0, custom_options)
		check(var_to_bytes(dense.growth_skeleton.points) != var_to_bytes(custom.growth_skeleton.points),
			"Preset %d ignored its growth curve" % preset)
	var settings := ProceduralTreeGrowthParameters.new()
	settings.forking = 0.0
	settings.trunk_bend = 0.0
	var options := OPTIONS.duplicate()
	options.growth_parameters = settings
	var straight := SlowTreeGenerator.generate(3, 0, false, 2.0, options)
	check(straight.growth_stats.forks == 0, "Fork control is ignored")
	var skeleton: Dictionary = straight.growth_skeleton
	for point in range(skeleton.offsets[1]):
		check(Vector2(skeleton.points[point].x, skeleton.points[point].z).length() < 0.000001, "Trunk bend zero is ignored")
	var tree := ProceduralTree.new()
	tree.auto_regenerate = false
	tree.backend = 1
	tree.slowtree_preset = 3
	tree.growth_parameters = settings
	root.add_child(tree)
	tree.auto_regenerate = true
	await wait_preview(tree)
	var original := var_to_bytes(tree.mesh.surface_get_arrays(0))
	var density := Curve.new()
	density.add_point(Vector2(0, 0.4))
	density.add_point(Vector2(1, 0.4))
	settings.density_by_height = density
	await wait_preview(tree)
	check(var_to_bytes(tree.mesh.surface_get_arrays(0)) != original, "Nested resource change did not regenerate")
	var count := tree.get_generation_count()
	for value in range(10):
		density.set_point_value(0, 0.5 + float(value) * 0.04)
		settings.trunk_bend = 0.5 + float(value) * 0.05
	await wait_preview(tree)
	var expected := SlowTreeGenerator.generate(3, tree.seed, false, tree.season, options)
	check(tree.get_generation_count() == count + 1, "Curve edits did not coalesce")
	check(var_to_bytes(tree.mesh.surface_get_arrays(0)) == var_to_bytes(expected.mesh.surface_get_arrays(0)), "Worker used stale curve snapshot")
	var save_path := "res://Develop/TreeGenGrowthValidation.tres"
	check(ResourceSaver.save(settings, save_path) == OK, "Growth resource save failed")
	var loaded := ResourceLoader.load(save_path, "", ResourceLoader.CACHE_MODE_IGNORE)
	check(loaded != null and loaded.density_by_height != null and is_equal_approx(loaded.trunk_bend, settings.trunk_bend), "Growth resource reload failed")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(save_path))
	var disabled := OPTIONS.duplicate()
	disabled.structural_branches = false
	var legacy := SlowTreeGenerator.generate(3, 0, false, 2.0, disabled)
	check(legacy.growth_stats.stems == 0 and legacy.tessellation_backend == "cpu", "Legacy branch option ignored")
	for preset in [0, 4, 2, 5, 1, 3, 6]:
		tree.slowtree_preset = preset
	await wait_preview(tree)
	var switched := SlowTreeGenerator.generate(6, tree.seed, false, tree.season, options)
	check(var_to_bytes(tree.mesh.surface_get_arrays(0)) == var_to_bytes(switched.mesh.surface_get_arrays(0)),
		"Rapid species changes committed an obsolete worker result")
	# An arbitrary imported graph must not silently acquire the HelloTree growth recipe.
	var fixture_path := "res://Develop/TreeGenImportValidation.vtree"
	var fixture := FileAccess.open(fixture_path, FileAccess.WRITE)
	fixture.store_string("""VEGTOOL 1
NODE 1 0 0 0
length 3
startRadius 0.2
ENDNODE
NODE 2 2 0 0
branchCount 3
ENDNODE
NODE 3 4 0 0
leafCount 8
ENDNODE
LINK 1 2
LINK 2 3
""")
	fixture.close()
	var imported := SlowTreeGenerator.generate_from_file(ProjectSettings.globalize_path(fixture_path), 0, false, 2.0, OPTIONS)
	var imported_legacy := SlowTreeGenerator.generate_from_file(ProjectSettings.globalize_path(fixture_path), 0, false, 2.0, disabled)
	check(imported.error.is_empty() and imported_legacy.error.is_empty(), "Import regression fixture failed")
	if imported.error.is_empty() and imported_legacy.error.is_empty():
		check(var_to_bytes(imported.mesh.surface_get_arrays(0)) == var_to_bytes(imported_legacy.mesh.surface_get_arrays(0)),
			"Imported graph was overridden by the default growth recipe")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(fixture_path))
	tree.free()
	print("TREEGEN_BRANCH_VALIDATE ", "PASS" if failures.is_empty() else "FAIL", " ", failures)
	quit(0 if failures.is_empty() else 1)
