extends SceneTree
## Run with --headless --path Game --script res://Develop/TreeGenFoliageValidate.gd

var failures: Array[String] = []
var meshes: Array[ArrayMesh] = []

func _initialize() -> void:
	call_deferred("run")

func check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
		push_error(message)

func arrays_equal(left: Mesh, right: Mesh) -> bool:
	if left.get_surface_count() != right.get_surface_count():
		return false
	for surface in left.get_surface_count():
		if var_to_bytes(left.surface_get_arrays(surface)) != var_to_bytes(right.surface_get_arrays(surface)):
			return false
	return true

func wait_preview(tree: Node) -> void:
	var deadline = Time.get_ticks_msec() + 10000
	while tree.is_preview_pending() and Time.get_ticks_msec() < deadline:
		await process_frame
	check(not tree.is_preview_pending(), "Preview did not finish within ten seconds")

func foliage_bounds(mesh: Mesh) -> AABB:
	var vertices: PackedVector3Array = mesh.surface_get_arrays(1)[Mesh.ARRAY_VERTEX]
	var bounds := AABB(vertices[0], Vector3.ZERO)
	for vertex in vertices:
		bounds = bounds.expand(vertex)
	return bounds

func run() -> void:
	create_timer(30.0).timeout.connect(func():
		push_error("TreeGen validation timed out")
		quit(2))
	var options = {"crossed_cards": true, "species_rules": true}
	for preset in range(7):
		var legacy: Dictionary = SlowTreeGenerator.generate(preset, 0)
		var current: Dictionary = SlowTreeGenerator.generate(preset, 0, false, 2.0, options)
		check(current.error == "", "Preset %d failed: %s" % [preset, current.error])
		check(current.surface_count == 2, "Preset %d needs wood and foliage surfaces" % preset)
		if preset == 4:
			# Bamboo now has articulated culm nodes and a visible fine-branch tier: a different wood budget.
			check(current.triangle_count < 90000, "Detailed three-culm bamboo exceeded its geometry budget")
		elif preset in [1, 5, 6]:
			# Willow whips, redwood axes and peach flower-bearing shoots need visible wood.
			# Check the card reduction separately from that added woody structure.
			check(current.triangle_count < 120000, "Detailed tree exceeded its geometry budget")
			check(current.leaf_count * 4 < legacy.triangle_count * 0.6, "Foliage cards lost their triangle saving")
		else:
			check(current.triangle_count < legacy.triangle_count, "Preset %d did not reduce triangles" % preset)
		meshes.append(current.mesh)
		var foliage: Array = current.mesh.surface_get_arrays(1)
		check(foliage[Mesh.ARRAY_VERTEX].size() == current.leaf_count * 8, "Eight vertices per crossed cluster")
		check(foliage[Mesh.ARRAY_INDEX].size() == current.leaf_count * 12, "Four triangles per crossed cluster")
		for surface in current.mesh.get_surface_count():
			var arrays: Array = current.mesh.surface_get_arrays(surface)
			for vertex in arrays[Mesh.ARRAY_VERTEX]:
				check(vertex.is_finite(), "Non-finite vertex")
			for index in arrays[Mesh.ARRAY_INDEX]:
				check(index >= 0 and index < arrays[Mesh.ARRAY_VERTEX].size(), "Invalid index")
		var repeated: Dictionary = SlowTreeGenerator.generate(preset, 0, false, 2.0, options)
		check(arrays_equal(current.mesh, repeated.mesh), "Determinism failed for preset %d" % preset)
		print("FOLIAGE ", JSON.stringify({"preset": preset, "old_tris": legacy.triangle_count,
			"new_tris": current.triangle_count, "clusters": current.leaf_count,
			"generation_ms": current.generation_ms, "convert_ms": current.convert_ms}))

	var material: ShaderMaterial = meshes[0].surface_get_material(1)
	var atlas: Texture2D = material.get_shader_parameter("foliage_atlas")
	var image: Image = atlas.get_image()
	check(image.has_mipmaps(), "Mask atlas has no mipmaps")
	check(image.get_pixel(0, 0).a == 0.0, "Atlas gutter must be transparent")
	check(material.shader.code.contains("ALPHA_SCISSOR_THRESHOLD"), "Missing alpha test")
	check(material.shader.code.contains("alpha_to_coverage"), "Missing MSAA alpha antialiasing")
	for tree_mesh in meshes:
		check(tree_mesh.surface_get_material(1).get_shader_parameter("foliage_atlas") == atlas, "Atlas is not shared")

	# The supplied ginkgo reference has a continuous leader and a tall, narrow crown.
	# Guard the visible envelope across seeds, rather than asserting exact generated vertices.
	for ginkgo_seed in [0, 17, 42]:
		var ginkgo := SlowTreeGenerator.generate(3, ginkgo_seed, false, 3.0, options)
		var crown := foliage_bounds(ginkgo.mesh)
		check(crown.size.y > maxf(crown.size.x, crown.size.z) * 1.6,
			"Ginkgo seed %d lost its narrow crown" % ginkgo_seed)
		check(not ginkgo.truncated, "Reference ginkgo exhausted its default budget")

	var limited = options.duplicate()
	limited.max_leaves = 100
	var capped: Dictionary = SlowTreeGenerator.generate(1, 0, false, 2.0, limited)
	check(capped.leaf_count == 100, "Cluster budget must be exact")
	var full_bounds: AABB = foliage_bounds(meshes[1])
	var capped_bounds: AABB = foliage_bounds(capped.mesh)
	check(capped_bounds.size.x > full_bounds.size.x * 0.7 and capped_bounds.size.z > full_bounds.size.z * 0.7,
		"Leaf budget stripped one side of the crown")
	limited.generate_leaves = false
	var bare: Dictionary = SlowTreeGenerator.generate(1, 0, false, 2.0, limited)
	check(bare.leaf_count == 0 and bare.surface_count == 1, "Leaf visibility must remove foliage geometry")
	limited = options.duplicate()
	limited.leaf_density = 0.25
	var thinned: Dictionary = SlowTreeGenerator.generate(1, 0, false, 2.0, limited)
	check(thinned.leaf_count < meshes[1].surface_get_arrays(1)[Mesh.ARRAY_VERTEX].size() / 8, "Density has no effect")
	check(var_to_bytes(thinned.mesh.surface_get_arrays(0)) == var_to_bytes(meshes[1].surface_get_arrays(0)), "Density changed branch topology")
	var short_budget = options.duplicate()
	short_budget.max_segments = 100
	var stopped: Dictionary = SlowTreeGenerator.generate(1, 0, false, 2.0, short_budget)
	check(stopped.truncated, "Branch budget was ignored")

	var tree = ClassDB.instantiate("ProceduralTree")
	tree.auto_regenerate = false
	tree.backend = 1
	tree.slowtree_preset = 1
	root.add_child(tree)
	tree.auto_regenerate = true
	var before: int = tree.get_generation_count()
	for seed in range(1, 101):
		tree.seed = seed
	await wait_preview(tree)
	check(tree.get_generation_count() == before + 1, "Same-frame edits should coalesce to one build")
	var expected: Dictionary = SlowTreeGenerator.generate(1, 100, false, 2.0, options)
	check(arrays_equal(tree.mesh, expected.mesh), "Preview did not commit the latest seed")
	var previous_mesh: Mesh = tree.mesh
	before = tree.get_generation_count()
	tree.season = 3.2
	tree.wind_strength = 2.0
	await process_frame
	check(tree.mesh == previous_mesh and tree.get_generation_count() == before, "Material edits rebuilt geometry")
	check(is_equal_approx(tree.mesh.surface_get_material(1).get_shader_parameter("season"), 3.2), "Season uniform is stale")

	# A pending worker must not replace a newer explicit build.
	tree.seed = 101
	await process_frame
	tree.seed = 102
	tree.generate()
	previous_mesh = tree.mesh
	await wait_preview(tree)
	check(tree.mesh == previous_mesh, "Old worker overwrote explicit generate")
	tree.seed = 103
	await process_frame
	tree.backend = 0
	tree.generate()
	previous_mesh = tree.mesh
	await wait_preview(tree)
	check(tree.mesh == previous_mesh, "Old worker overwrote another backend")
	tree.backend = 1
	tree.seed = 104
	await process_frame
	tree.free()
	await process_frame

	# Baked meshes retain embedded shader and atlas, independent of the procedural node.
	var path = "res://Develop/TreeGenFoliageValidation.res"
	check(ResourceSaver.save(meshes[0], path) == OK, "Bake serialization failed")
	var baked: ArrayMesh = ResourceLoader.load(path, "ArrayMesh", ResourceLoader.CACHE_MODE_IGNORE)
	check(baked != null and baked.surface_get_material(1) is ShaderMaterial, "Baked foliage material missing")
	check(baked.surface_get_material(1).get_shader_parameter("foliage_atlas") != null, "Baked atlas missing")
	check(baked.get_meta("treegen_masked_foliage", false), "Baked mesh lost foliage metadata")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	var malformed_path := "res://Develop/TreeGenFoliageMalformed.vtree"
	var malformed := FileAccess.open(malformed_path, FileAccess.WRITE)
	malformed.store_string("VEGTOOL 1\nNODE 1 0 0 0\nlength invalid\nsides 999999999999999999999\nENDNODE\n")
	malformed.close()
	var parsed: Dictionary = SlowTreeGenerator.generate_from_file(ProjectSettings.globalize_path(malformed_path), 0)
	check(parsed.error == "" and parsed.vertex_count > 0, "Malformed scalar fields did not fall back to defaults")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(malformed_path))
	var npr = load("res://Script/NPR/npr_style_kit.gd")
	var styled_root := Node3D.new()
	root.add_child(styled_root)
	var styled_tree := MeshInstance3D.new()
	styled_tree.mesh = baked
	styled_root.add_child(styled_tree)
	var stale_outline := MeshInstance3D.new()
	stale_outline.set_meta("npr_outline", true)
	styled_tree.add_child(stale_outline)
	npr.apply(styled_root, npr.EStyle.INK_OUTLINE)
	check(styled_tree.material_override == null, "NPR replaced alpha-tested foliage with an opaque material")
	check(styled_tree.get_child_count() == 0, "NPR outlined card rectangles")
	styled_root.free()
	if "--full-self-test" in OS.get_cmdline_user_args():
		var self_test: Dictionary = SlowTreeSelfTest.run_all()
		check(self_test.failed == 0, "Legacy SlowTree CPU/GPU self-test failed")
		print("LEGACY_SELF_TEST ", self_test)
	print("TREEGEN_FOLIAGE_VALIDATE ", "PASS" if failures.is_empty() else "FAIL", " ", failures)
	quit(0 if failures.is_empty() else 1)
