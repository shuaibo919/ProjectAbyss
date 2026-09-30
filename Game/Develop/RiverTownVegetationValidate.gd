extends SceneTree

# Run with --script res://Develop/RiverTownVegetationValidate.gd; append -- --terrain
# for the Terrain3D placement checks using the normal renderer.
const Settings := preload("res://addons/ancient_town/nodes/river_town_lots_settings.gd")
const Vegetation := preload("res://Script/PCG/river_town_vegetation.gd")
const Styles := preload("res://Script/NPR/npr_style_kit.gd")
var lots_script: Script
var failures: Array[String] = []


func _initialize() -> void:
	call_deferred("run")


func check(condition: bool, message: String) -> void:
	if not condition and not failures.has(message):
		failures.append(message)
		push_error(message)


func layout(script: Script, seed_value: int, density: float) -> Node:
	var node: Node = script.new()
	node.settings = Settings.new()
	node.settings.random_seed = seed_value
	node.settings.density = density
	node.settings.terrain_ground = true
	node.rng.seed = seed_value
	node.execute(load("res://addons/flow_nodes_editor/flow_data.gd").EvaluationContext.new())
	return node


func inspect_layout(node: Node, density: float) -> Dictionary:
	var counts := {"peach": 0, "bamboo": 0}
	var groves := {}
	var spans := {}
	var data = node.get_bulk_output(0, 4)
	var variants: PackedInt32Array = data.findStream(Vegetation.VARIANT_ATTRIBUTE).container
	check(variants.size() == node._trees.size(), "Tree selector stream lost points")
	for index in node._trees.size():
		var tree: Dictionary = node._trees[index]
		var species: int = tree.get("species", Vegetation.Species.PEACH)
		counts["peach" if species == Vegetation.Species.PEACH else "bamboo"] += 1
		check(variants[index] / Vegetation.VARIANTS_PER_SPECIES == species, "Wrong species selected")
		if species == Vegetation.Species.BAMBOO:
			groves[tree.grove] = int(groves.get(tree.grove, 0)) + 1
			if not spans.has(tree.grove):
				spans[tree.grove] = Rect2(tree.bank_pos, Vector2.ZERO)
			spans[tree.grove] = spans[tree.grove].expand(tree.bank_pos)
			check(tree.bank_pos.y > tree.town_edge, "Bamboo entered a town block")
			for lot in node._lots:
				var local: Vector3 = Basis(Vector3.UP, deg_to_rad(lot.yaw)).transposed() * (tree.pos - lot.pos)
				check(absf(local.x) >= float(lot.params.width) * 0.5 + 0.4 or
					absf(local.z) >= float(lot.params.depth) * 0.5 + 0.4, "Bamboo trunk intersects a building")
	check(groves.size() == 2, "Expected continuous woodland on both banks")
	for count in groves.values():
		check(count >= roundi(300.0 * density), "A bamboo forest is too sparse")
	for span in spans.values():
		check(span.size.x > 240.0 and span.size.y > 24.0, "Bamboo forest lost its continuous extent")
	check(counts.peach > 0 and counts.bamboo > 0, "Lost a plant species")
	print("RIVER_VEGETATION_LAYOUT ", node.settings.random_seed, " ", counts, " groves=", groves)
	return counts


func inspect_instances(scene: Node3D, expected: Dictionary) -> void:
	var counts := {"peach": 0, "bamboo": 0}
	var batches := 0
	var shared_meshes := {}
	var maximum_ground_error := 0.0
	for child in scene._flow.get_children():
		if not child is MultiMeshInstance3D or child.is_queued_for_deletion():
			continue
		var mm: MultiMesh = child.multimesh
		var mesh: Mesh = mm.mesh
		if not mesh.get_meta("town_tree", false):
			continue
		batches += 1
		shared_meshes[mesh] = true
		check(mesh.get_meta("town_tree_species", "") in counts, "Placeholder vegetation survived")
		check(mesh.get_meta("treegen_masked_foliage", false), "Plant lost its alpha mask")
		check(child.material_override == null, "Spawner or style pass replaced masked foliage")
		counts[mesh.get_meta("town_tree_species")] += mm.instance_count
		check(child.has_meta("flow_instance_cell"), "Vegetation was not split into spatial batches")
		var cell: Vector2i = child.get_meta("flow_instance_cell")
		# The dummy renderer returns identity for every instance transform.
		for index in (mm.instance_count if DisplayServer.get_name() != "headless" else 0):
			var at: Vector3 = mm.get_instance_transform(index).origin
			check(Vector2i(floori(at.x / Vegetation.INSTANCE_CELL_SIZE), floori(at.z / Vegetation.INSTANCE_CELL_SIZE)) == cell,
				"A batch contains a tree from another cell")
		if scene._terrain != null:
			for index in mm.instance_count:
				var at: Vector3 = (child.global_transform * mm.get_instance_transform(index)).origin
				var height: float = scene._terrain.data.get_height(at)
				check(not is_nan(height), "Plant fell outside terrain data")
				check(height > float(lots_script.last_terrain.water_y), "Plant is under water")
				maximum_ground_error = maxf(maximum_ground_error, absf(at.y - height))
	check(batches > 20 and shared_meshes.size() == 6, "Spatial batches do not share six meshes")
	check(counts == expected, "Spawned vegetation does not match the layout")
	check(maximum_ground_error < 0.001, "Plant origin is floating above the terrain")
	print("RIVER_VEGETATION_INSTANCES ", counts, " batches=", batches, " ground_error=", maximum_ground_error)


func instance_signature(scene: Node3D) -> Array[int]:
	var signature: Array[int] = []
	for child in scene._flow.get_children():
		if not child is MultiMeshInstance3D or child.is_queued_for_deletion():
			continue
		var mm: MultiMesh = child.multimesh
		if not mm.mesh.get_meta("town_tree", false):
			continue
		for index in mm.instance_count:
			signature.append(hash([mm.mesh.get_meta("town_tree_variant"), mm.get_instance_transform(index),
				mm.get_instance_color(index) if mm.use_colors else Color.WHITE]))
	signature.sort()
	return signature


func run() -> void:
	# Load the map in the same order as a normal scene launch; Flow's global classes
	# reference one another and cannot be eagerly resolved through this test script.
	var packed_scene: PackedScene = load("res://Map/Map_RiverTown.tscn")
	lots_script = load("res://addons/ancient_town/nodes/river_town_lots.gd")
	var baseline: GDScript
	var baseline_path := ProjectSettings.globalize_path("res://../Reference/RiverTownVegetation/river_town_lots.before.gd")
	if FileAccess.file_exists(baseline_path):
		baseline = GDScript.new()
		baseline.source_code = FileAccess.get_file_as_string(baseline_path)
		check(baseline.reload() == OK, "Cannot compile historical layout for comparison")
	var expected := {}
	var default_trees: Array = []
	for sample in [[7, 1.0], [17, 0.5], [42, 1.5]]:
		var current := layout(lots_script, sample[0], sample[1])
		var counts := inspect_layout(current, sample[1])
		if sample[0] == 7:
			expected = counts
			default_trees = current._trees.duplicate(true)
		var repeated := layout(lots_script, sample[0], sample[1])
		check(var_to_bytes(current._trees) == var_to_bytes(repeated._trees), "Vegetation is not deterministic")
		if baseline != null:
			var previous := layout(baseline, sample[0], sample[1])
			for buffer in ["_lots", "_roads", "_walls", "_props", "_yard_walls"]:
				check(var_to_bytes(current.get(buffer)) == var_to_bytes(previous.get(buffer)), "Vegetation changed town layout: " + buffer)
			previous.free()
		repeated.free()
		current.free()
	var meshes := Vegetation.meshes()
	check(meshes.size() == 6, "Missing generated plant variants")
	for index in meshes.size():
		check(meshes[index] == Vegetation.meshes()[index], "Plant variant cache was bypassed")
		check(meshes[index].surface_get_material(1) is ShaderMaterial, "Foliage shader missing")
		var season: float = meshes[index].surface_get_material(1).get_shader_parameter("season")
		check(season == (1.0 if index < 3 else 2.0), "Peach did not keep spring blossoms")
		if index >= 3:
			var imported := ImporterMesh.from_mesh(meshes[index])
			check(imported.get_surface_lod_count(0) >= 3, "Bamboo wood lost its distance LODs")
			check(imported.get_surface_lod_count(1) > 0, "Bamboo foliage lost its distance LOD")
	OS.set_environment("RIVER_STYLE", "textured")
	OS.set_environment("RIVER_SEED", "7")
	OS.set_environment("RIVER_TERRAIN", "1" if "--terrain" in OS.get_cmdline_user_args() else "0")
	OS.unset_environment("SHOTS")
	var scene: Node3D = packed_scene.instantiate()
	root.add_child(scene)
	var deadline := Time.get_ticks_msec() + 60000
	while scene.get_node_or_null("CameraFocus") == null and Time.get_ticks_msec() < deadline:
		await process_frame
	check(scene.get_node_or_null("CameraFocus") != null, "RiverTown did not finish building")
	if scene._terrain != null:
		expected = {"peach": 0, "bamboo": 0}
		for tree in default_trees:
			var height: float = scene._terrain.data.get_height(tree.pos)
			if not is_nan(height) and height > float(lots_script.last_terrain.water_y) + scene.TREE_WATER_CLEARANCE:
				var species: int = tree.get("species", Vegetation.Species.PEACH)
				expected["peach" if species == Vegetation.Species.PEACH else "bamboo"] += 1
	inspect_instances(scene, expected)
	# Re-running the actual PCG graph must replace its batches, not accumulate duplicates.
	scene._flow.execute()
	await process_frame
	await process_frame
	if scene._terrain != null:
		scene._settle_on_terrain()
	Styles.apply(scene._flow, Styles.EStyle.TEXTURED)
	inspect_instances(scene, expected)
	# Disabling spatial batching must preserve every instance transform and tint.
	var chunked_signature := instance_signature(scene)
	for node in scene._flow.graph.data.nodes:
		if node.settings.has("instance_cell_size"):
			node.settings.instance_cell_size = 0.0
	scene._flow.execute()
	await process_frame
	await process_frame
	if scene._terrain != null:
		scene._settle_on_terrain()
	check(instance_signature(scene) == chunked_signature, "Spatial batching changed instance transforms or colours")
	scene.free()
	print("RIVER_TOWN_VEGETATION_VALIDATE ", "PASS" if failures.is_empty() else "FAIL", " ", failures)
	quit(0 if failures.is_empty() else 1)
