extends SceneTree
## Species proportions, generated materials, parameter snapshots and unchanged neighbouring presets.

const OPTIONS := {"crossed_cards": true, "species_rules": true, "growth_debug": true, "radial_segments": 12}
const BASELINE := "E:/ProjectAbyss/Reference/TreeGenBambooRefine"
var failures: Array[String] = []

func _initialize():
	call_deferred("run")

func check(condition: bool, message: String):
	if not condition:
		failures.append(message)
		push_error(message)

func make(growth: ProceduralTreeGrowthParameters = null) -> Dictionary:
	var options := OPTIONS.duplicate()
	options.growth_parameters = growth
	return SlowTreeGenerator.generate(4, 0, false, 2.0, options)

func wood(result: Dictionary) -> PackedByteArray:
	return var_to_bytes(result.mesh.surface_get_arrays(0))

func run():
	var reference := make()
	for seed_value in [0, 17, 42]:
		var result := SlowTreeGenerator.generate(4, seed_value, false, 2.0, OPTIONS)
		var skeleton: Dictionary = result.growth_skeleton
		check(not result.truncated and result.error.is_empty(), "Default bamboo failed its generation budget")
		check(result.growth_stats.omitted_junctions == 0, "Bamboo lost a visible branch junction")
		for stem in skeleton.parents.size():
			if skeleton.parents[stem] >= 0:
				continue
			var first: int = skeleton.offsets[stem]
			var last: int = skeleton.offsets[stem + 1]
			check(skeleton.radii[first] > 0.03 and skeleton.radii[first] < 0.06, "Culm diameter lost its slender proportion")
			var lengths: Array[float] = []
			for node in range(first + 3, last, 3):
				var length := 0.0
				for sample in range(node - 2, node + 1):
					length += skeleton.points[sample].distance_to(skeleton.points[sample - 1])
				lengths.append(length)
			check(lengths.max() / lengths.min() > 1.8, "Internodes became uniform again")
			check(lengths[0] < lengths[lengths.size() / 2] and lengths.back() < lengths[lengths.size() / 2], "Root/tip internodes must be shorter")
		var support_count := 0
		for role in skeleton.roles:
			support_count += int(role == 1)
		check(result.growth_stats.junctions >= support_count, "Fine bamboo supports disappeared from the wood surface")
		check(result.leaf_count > 800 and result.leaf_count < 4000, "Bamboo leaf fan budget changed unexpectedly")
		check(result.triangle_count < 90000, "Detailed three-culm mesh exceeds the preview budget")

	var params := ProceduralTreeGrowthParameters.new()
	params.bamboo_internode_length = 0.50
	var long_nodes := make(params)
	check(long_nodes.growth_skeleton.offsets[1] < reference.growth_skeleton.offsets[1], "Internode control did not reduce the node count")
	params.bamboo_internode_length = 0.28
	params.bamboo_node_definition = 0.0
	check(wood(make(params)) != wood(reference), "Node definition control was ignored")
	params.bamboo_node_definition = 1.0
	params.bamboo_leaf_scale = 0.5
	var small := make(params)
	params.bamboo_leaf_scale = 2.0
	var large := make(params)
	check(wood(small) == wood(reference) and wood(large) == wood(reference), "Leaf scale changed branch geometry")
	var small_leaves: PackedVector3Array = small.mesh.surface_get_arrays(1)[Mesh.ARRAY_VERTEX]
	var large_leaves: PackedVector3Array = large.mesh.surface_get_arrays(1)[Mesh.ARRAY_VERTEX]
	check(is_equal_approx(large_leaves[0].distance_to(large_leaves[1]) / small_leaves[0].distance_to(small_leaves[1]), 4.0), "Leaf scale did not preserve card proportions")
	var material: StandardMaterial3D = reference.mesh.surface_get_material(0)
	check(material.get_texture(BaseMaterial3D.TEXTURE_ALBEDO) != null, "Generated bamboo fibres missing")
	check(material.get_texture(BaseMaterial3D.TEXTURE_ALBEDO) == small.mesh.surface_get_material(0).get_texture(BaseMaterial3D.TEXTURE_ALBEDO), "Culm texture is not cached")
	var atlas: Image = reference.mesh.surface_get_material(1).get_shader_parameter("foliage_atlas").get_image()
	check(atlas.get_region(Rect2i(0, 512, 256, 256)).get_data() != atlas.get_region(Rect2i(256, 512, 256, 256)).get_data(), "Bamboo mask variants are identical")

	var tree := ProceduralTree.new()
	tree.auto_regenerate = false
	tree.backend = 1
	tree.slowtree_preset = 4
	tree.growth_parameters = params
	root.add_child(tree)
	tree.auto_regenerate = true
	params.bamboo_internode_length = 0.4
	params.bamboo_leaf_scale = 1.25
	params.bamboo_node_definition = 1.4
	var deadline := Time.get_ticks_msec() + 10000
	while tree.is_preview_pending() and Time.get_ticks_msec() < deadline:
		await process_frame
	check(not tree.is_preview_pending(), "Bamboo worker did not finish")
	check(var_to_bytes(tree.mesh.surface_get_arrays(0)) == wood(make(params)), "Bamboo worker committed stale settings")
	var baked: Mesh = tree.bake_mesh()
	var mesh_path := "res://Develop/TreeGenBambooValidation.res"
	var params_path := "res://Develop/TreeGenBambooValidation.tres"
	check(ResourceSaver.save(baked, mesh_path) == OK and ResourceSaver.save(params, params_path) == OK, "Bamboo save failed")
	var loaded: Mesh = ResourceLoader.load(mesh_path, "", ResourceLoader.CACHE_MODE_IGNORE)
	check(loaded.surface_get_material(0).get_texture(BaseMaterial3D.TEXTURE_ALBEDO) != null, "Baked culm lost its generated texture")
	check(var_to_bytes(loaded.surface_get_arrays(0)) == var_to_bytes(baked.surface_get_arrays(0)), "Bamboo mesh roundtrip changed colours or geometry")
	var loaded_params: Resource = ResourceLoader.load(params_path, "", ResourceLoader.CACHE_MODE_IGNORE)
	check(is_equal_approx(loaded_params.bamboo_internode_length, 0.4) and is_equal_approx(loaded_params.bamboo_leaf_scale, 1.25), "Bamboo controls did not survive saving")
	DirAccess.remove_absolute(mesh_path)
	DirAccess.remove_absolute(params_path)
	tree.free()

	var compared := 0
	for preset in [0, 1, 2, 3, 5, 6]:
		# Historical isolation check for the bamboo-only change, before subsequent species refinements.
		if "--compare-before" not in OS.get_cmdline_user_args():
			continue
		var path := BASELINE + "/before_%d_0.res" % preset
		if not FileAccess.file_exists(path):
			continue
		var previous: Mesh = load(path)
		var current := SlowTreeGenerator.generate(preset, 0, false, 2.0, OPTIONS)
		for surface in range(2):
			check(var_to_bytes(previous.surface_get_arrays(surface)) == var_to_bytes(current.mesh.surface_get_arrays(surface)), "Bamboo refinement changed preset %d surface %d" % [preset, surface])
		var previous_atlas: Image = previous.surface_get_material(1).get_shader_parameter("foliage_atlas").get_image()
		for cell in range(16):
			if cell in [8, 9]:
				continue
			var region := Rect2i((cell % 4) * 256, (cell / 4) * 256, 256, 256)
			check(previous_atlas.get_region(region).get_data() == atlas.get_region(region).get_data(), "Bamboo refinement altered another species mask")
		compared += 1
	print("BAMBOO_OTHER_PRESETS_COMPARED ", compared)
	print("TREEGEN_BAMBOO_VALIDATE ", "PASS" if failures.is_empty() else "FAIL", " ", failures)
	quit(0 if failures.is_empty() else 1)
