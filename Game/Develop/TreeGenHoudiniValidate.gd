extends SceneTree
## Botanical refinement regression: the approved bamboo is the fixed reference.

const OUTPUT := "E:/ProjectAbyss/Reference/TreeGenHoudiniRefine"
const OPTIONS := {"radial_segments": 12, "crossed_cards": true, "species_rules": true, "growth_debug": true}
var failures: Array[String] = []

func _initialize():
	call_deferred("run")

func check(condition: bool, message: String):
	if not condition:
		failures.append(message)
		push_error(message)

func run():
	for preset in range(7):
		for seed_value in [0, 17, 42]:
			if not FileAccess.file_exists(OUTPUT + "/before_%d_%d.res" % [preset, seed_value]):
				check(false, "Historical baseline missing; do not generate it from the revised DLL")
				quit(1)
				return
	var rows: Array[Dictionary] = []
	var references: Array[Mesh] = []
	for preset in range(7):
		for seed_value in [0, 17, 42]:
			var result := SlowTreeGenerator.generate(preset, seed_value, false, 2.0, OPTIONS)
			check(result.error.is_empty(), "Generation failed %d/%d" % [preset, seed_value])
			check(not result.get("truncated", false), "Default tree exhausted the segment budget")
			check(result.triangle_count < 130000, "Preview mesh exceeded 130k triangles")
			var mesh: ArrayMesh = result.mesh
			references.append(mesh)
			var wood := mesh.surface_get_arrays(0)
			var leaves := mesh.surface_get_arrays(1)
			check(leaves[Mesh.ARRAY_VERTEX].size() == result.leaf_count * 8, "Crossed cards no longer use eight vertices")
			check(leaves[Mesh.ARRAY_INDEX].size() == result.leaf_count * 12, "Crossed cards no longer use four triangles")
			check(result.growth_stats.omitted_junctions == 0, "Default tree dropped a branch junction")
			var previous: Mesh = load(OUTPUT + "/before_%d_%d.res" % [preset, seed_value])
			var previous_triangles := 0
			for surface in previous.get_surface_count():
				previous_triangles += previous.surface_get_arrays(surface)[Mesh.ARRAY_INDEX].size() / 3
			if preset == 4:
				for surface in range(2):
					check(var_to_bytes(mesh.surface_get_arrays(surface)) == var_to_bytes(previous.surface_get_arrays(surface)), "Approved bamboo geometry/colour changed %d/%d" % [seed_value, surface])
				var old_bark: StandardMaterial3D = previous.surface_get_material(0)
				var new_bark: StandardMaterial3D = mesh.surface_get_material(0)
				check(old_bark.get_texture(BaseMaterial3D.TEXTURE_ALBEDO).get_image().get_data() == new_bark.get_texture(BaseMaterial3D.TEXTURE_ALBEDO).get_image().get_data(), "Approved bamboo fibres changed")
				var old_atlas: Image = previous.surface_get_material(1).get_shader_parameter("foliage_atlas").get_image()
				var new_atlas: Image = mesh.surface_get_material(1).get_shader_parameter("foliage_atlas").get_image()
				for cell in [8, 9]:
					var region := Rect2i((cell % 4) * 256, (cell / 4) * 256, 256, 256)
					check(old_atlas.get_region(region).get_data() == new_atlas.get_region(region).get_data(), "Approved bamboo leaf mask changed")
			else:
				check(var_to_bytes(mesh.surface_get_arrays(0)) != var_to_bytes(previous.surface_get_arrays(0)), "Refinement did not reach preset %d" % preset)
				var tangents: PackedFloat32Array = wood[Mesh.ARRAY_TANGENT]
				var normals: PackedVector3Array = wood[Mesh.ARRAY_NORMAL]
				check(tangents.size() == normals.size() * 4, "Bark tangent count mismatch")
				for vertex in normals.size():
					var tangent := Vector3(tangents[vertex * 4], tangents[vertex * 4 + 1], tangents[vertex * 4 + 2])
					if not tangent.is_finite() or absf(tangent.length() - 1.0) > 0.015 or absf(tangent.dot(normals[vertex])) > 0.015:
						check(false, "Invalid bark tangent basis %d/%d" % [preset, seed_value])
						break
				var bark: StandardMaterial3D = mesh.surface_get_material(0)
				check(bark.normal_enabled and bark.normal_texture != null and bark.albedo_texture != null, "Bark detail material missing")
				var bare_options := OPTIONS.duplicate()
				bare_options.leaf_density = 0.18
				var sparse := SlowTreeGenerator.generate(preset, seed_value, false, 2.0, bare_options)
				check(var_to_bytes(wood) == var_to_bytes(sparse.mesh.surface_get_arrays(0)), "Leaf budget changed the woody scaffold")
				var path := "res://Develop/TreeGenHoudiniValidation.res"
				check(ResourceSaver.save(mesh, path) == OK, "Bark bake save failed")
				var loaded: Mesh = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)
				check(loaded.surface_get_material(0).normal_texture != null, "Saved mesh lost its generated bark normal map")
				DirAccess.remove_absolute(path)
			rows.append({"preset": preset, "seed": seed_value, "before_triangles": previous_triangles, "triangles": result.triangle_count, "cards": result.leaf_count, "growth": result.growth_stats})
	var atlas: Image = references[0].surface_get_material(1).get_shader_parameter("foliage_atlas").get_image()
	atlas.save_png(OUTPUT + "/foliage_atlas.png")
	for shape in range(8):
		var first := Rect2i((shape * 2 % 4) * 256, (shape * 2 / 4) * 256, 256, 256)
		var second := Rect2i(((shape * 2 + 1) % 4) * 256, ((shape * 2 + 1) / 4) * 256, 256, 256)
		check(atlas.get_region(first).get_data() != atlas.get_region(second).get_data(), "Species %d has duplicated mask variants" % shape)
	FileAccess.open(OUTPUT + "/validation.json", FileAccess.WRITE).store_string(JSON.stringify({"failures": failures, "cases": rows}, "\t"))
	print("TREEGEN_HOUDINI_VALIDATE ", "PASS" if failures.is_empty() else "FAIL", " ", failures)
	quit(0 if failures.is_empty() else 1)
