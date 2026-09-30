extends SceneTree
## Peach-specific controls, visible bearing wood, resource/worker integration and species isolation.

const OUTPUT := "E:/ProjectAbyss/Reference/TreeGenPeachRefine"
const OPTIONS := {"crossed_cards": true, "species_rules": true, "growth_debug": true, "radial_segments": 12}
var failures: Array[String] = []

func _initialize():
	call_deferred("run")

func check(condition: bool, message: String):
	if not condition:
		failures.append(message)
		push_error(message)

func make(params: ProceduralTreeGrowthParameters = null, seed_value := 0) -> Dictionary:
	var options := OPTIONS.duplicate()
	options.growth_parameters = params
	return SlowTreeGenerator.generate(6, seed_value, false, 1.0, options)

func wood(result: Dictionary) -> PackedByteArray:
	return var_to_bytes(result.mesh.surface_get_arrays(0))

func flower_count(mesh: Mesh) -> int:
	var uvs: PackedVector2Array = mesh.surface_get_arrays(1)[Mesh.ARRAY_TEX_UV]
	var count := 0
	for card in uvs.size() / 8:
		var uv := uvs[card * 8]
		count += int(int(uv.y * 4) == 3 and int(uv.x * 4) >= 2)
	return count

func run():
	var rows: Array[Dictionary] = []
	var reference := make()
	for seed_value in [0, 17, 42]:
		var result := make(null, seed_value)
		check(result.error.is_empty() and not result.truncated, "Peach failed its default budget")
		check(result.growth_stats.omitted_junctions == 0, "Peach dropped a visible junction")
		check(result.growth_stats.junctions == result.growth_stats.stems - 1, "Bearing shoots disappeared from the visible wood")
		check(result.triangle_count < 120000, "Peach exceeded the detailed tree geometry budget")
		var foliage: Array = result.mesh.surface_get_arrays(1)
		check(foliage[Mesh.ARRAY_VERTEX].size() == result.leaf_count * 8, "Crossed card vertex count changed")
		check(foliage[Mesh.ARRAY_INDEX].size() == result.leaf_count * 12, "Crossed card triangle count changed")
		check(flower_count(result.mesh) > 0, "Peach has no flowers")
		var repeated := make(null, seed_value)
		for surface in range(2):
			check(var_to_bytes(result.mesh.surface_get_arrays(surface)) == var_to_bytes(repeated.mesh.surface_get_arrays(surface)), "Peach is not deterministic")
		rows.append({"seed": seed_value, "triangles": result.triangle_count,
			"flower_cards": flower_count(result.mesh), "cards": result.leaf_count, "growth": result.growth_stats})

	var params := ProceduralTreeGrowthParameters.new()
	params.peach_blossom_density = 0.0
	var no_flowers := make(params)
	check(flower_count(no_flowers.mesh) == 0 and no_flowers.leaf_count > 0, "Zero flower density must keep summer leaves")
	params.peach_blossom_density = 0.4
	var sparse := make(params)
	params.peach_blossom_density = 1.5
	var dense := make(params)
	check(flower_count(sparse.mesh) < flower_count(reference.mesh) and flower_count(dense.mesh) > flower_count(reference.mesh), "Flower density does not control the flower count")
	check(wood(sparse) == wood(reference) and wood(dense) == wood(reference) and wood(no_flowers) == wood(reference), "Flower density changed wood")
	params.peach_blossom_density = 1.0
	params.peach_blossom_scale = 1.4
	var large := make(params)
	check(wood(large) == wood(reference) and large.leaf_count == reference.leaf_count, "Flower size changed topology or flower count")
	check(var_to_bytes(large.mesh.surface_get_arrays(1)) != var_to_bytes(reference.mesh.surface_get_arrays(1)), "Flower size did not affect geometry")
	params.peach_twig_density = 0.5
	var fewer_twigs := make(params)
	check(fewer_twigs.growth_stats.stems < reference.growth_stats.stems and wood(fewer_twigs) != wood(reference), "Twig density did not change the branch structure")

	var tree := ProceduralTree.new()
	tree.auto_regenerate = false
	tree.backend = 1
	tree.slowtree_preset = 6
	tree.growth_parameters = params
	root.add_child(tree)
	tree.auto_regenerate = true
	params.peach_twig_density = 1.2
	params.peach_blossom_density = 0.2
	params.peach_blossom_scale = 0.6
	params.peach_blossom_density = 1.4
	params.peach_blossom_scale = 1.15
	var deadline := Time.get_ticks_msec() + 10000
	while tree.is_preview_pending() and Time.get_ticks_msec() < deadline:
		await process_frame
	check(not tree.is_preview_pending(), "Peach worker timed out")
	var expected := make(params)
	for surface in range(2):
		check(var_to_bytes(tree.mesh.surface_get_arrays(surface)) == var_to_bytes(expected.mesh.surface_get_arrays(surface)), "Peach worker committed stale controls")
	var generation_count: int = tree.get_generation_count()
	tree.season = 1.0
	check(tree.get_generation_count() == generation_count and not tree.is_preview_pending(), "Season changes should only update the shader")
	var params_path := OUTPUT + "/validated_parameters.tres"
	var mesh_path := OUTPUT + "/validated_mesh.res"
	check(ResourceSaver.save(params, params_path) == OK, "Peach parameter save failed")
	check(ResourceSaver.save(tree.bake_mesh(), mesh_path) == OK, "Peach bake failed")
	var saved_params: Resource = ResourceLoader.load(params_path, "", ResourceLoader.CACHE_MODE_IGNORE)
	check(is_equal_approx(saved_params.peach_twig_density, 1.2) and is_equal_approx(saved_params.peach_blossom_density, 1.4) and is_equal_approx(saved_params.peach_blossom_scale, 1.15), "Peach controls did not survive saving")
	var baked: Mesh = ResourceLoader.load(mesh_path, "", ResourceLoader.CACHE_MODE_IGNORE)
	check(var_to_bytes(baked.surface_get_arrays(1)) == var_to_bytes(tree.mesh.surface_get_arrays(1)), "Baked peach lost its flower cards")
	tree.free()

	# These files were captured with the pre-refactor DLL; missing baselines must never be regenerated here.
	for preset in range(6):
		for seed_value in [0, 17, 42]:
			var path := OUTPUT + "/before_%d_%d.res" % [preset, seed_value]
			if not FileAccess.file_exists(path):
				check(false, "Missing original baseline: " + path)
				continue
			var previous: Mesh = load(path)
			var current := SlowTreeGenerator.generate(preset, seed_value, false, 2.0, OPTIONS)
			for surface in range(2):
				check(var_to_bytes(previous.surface_get_arrays(surface)) == var_to_bytes(current.mesh.surface_get_arrays(surface)), "Peach changed preset %d seed %d surface %d" % [preset, seed_value, surface])
			for slot in [BaseMaterial3D.TEXTURE_ALBEDO, BaseMaterial3D.TEXTURE_NORMAL]:
				var old_texture: Texture2D = previous.surface_get_material(0).get_texture(slot)
				var new_texture: Texture2D = current.mesh.surface_get_material(0).get_texture(slot)
				check((old_texture == null) == (new_texture == null), "Neighbour bark texture slot changed")
				if old_texture and new_texture:
					check(old_texture.get_image().get_data() == new_texture.get_image().get_data(), "Neighbour bark texture changed")
	var original: Mesh = load(OUTPUT + "/before_6_0.res")
	var old_atlas: Image = original.surface_get_material(1).get_shader_parameter("foliage_atlas").get_image()
	var atlas: Image = reference.mesh.surface_get_material(1).get_shader_parameter("foliage_atlas").get_image()
	for cell in range(14):
		var region := Rect2i((cell % 4) * 256, (cell / 4) * 256, 256, 256)
		check(old_atlas.get_region(region).get_data() == atlas.get_region(region).get_data(), "Non-blossom atlas cell changed: %d" % cell)
	atlas.save_png(OUTPUT + "/foliage_atlas.png")
	FileAccess.open(OUTPUT + "/validation.json", FileAccess.WRITE).store_string(JSON.stringify({"failures": failures, "cases": rows}, "\t"))
	print("TREEGEN_PEACH_VALIDATE ", "PASS" if failures.is_empty() else "FAIL", " ", failures)
	quit(0 if failures.is_empty() else 1)
