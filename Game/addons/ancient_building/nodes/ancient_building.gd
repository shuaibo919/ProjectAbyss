@tool
extends FlowNodeBase

# Ancient Building — generates ancient Chinese architecture meshes and assigns one to every
# incoming point.
#
# The heavy lifting is the `abyss` GDExtension (Source/AncientBuilding/, a port of Hu & Qin
# 2020 — see Docs/AncientBuilding_Spec.md). This node only drives it and writes the results
# into a Resource stream, so the existing `spawn_meshes` node can instance them into a
# MultiMeshInstance3D exactly as it does for any other mesh attribute.
#
# Meshes are generated once per *variant*, not per point. A village of forty houses therefore
# costs four meshes, which is the difference between this being usable in a graph and not.

const AncientBuildingNodeSettings = preload(
	"res://addons/ancient_building/nodes/ancient_building_settings.gd")

const ROOF_TYPE_COUNT := 3


func _init() -> void:
	meta_node = {
		"title": "Ancient Building",
		"settings": AncientBuildingNodeSettings,
		"ins": [{"label": "Points"}],
		"outs": [{"label": "Points"}],
		"aliases": ["Chinese Building", "Hall", "Pavilion", "Temple"],
		"category": "Sampler",
		"tooltip": "Generates ancient Chinese building meshes and writes one per point into a\n"
			+ "Resource attribute. Feed the output to Spawn Meshes with a matching\n"
			+ "mesh attribute name.",
	}


func getTitle() -> String:
	var names := ["Flush Gable", "Gable and Hip", "Hip"]
	var label: String = "Mixed" if settings.randomize_roof_type else names[clampi(settings.roof_type, 0, 2)]

	return "Ancient Building - %s x%d" % [label, settings.variant_count]


func execute(_ctx: FlowData.EvaluationContext) -> void:
	if not ClassDB.class_exists("AncientBuilding"):
		setError("The `abyss` GDExtension is not loaded, so AncientBuilding is unavailable.")
		return
	if settings.mesh_attribute.strip_edges() == "":
		setError("Mesh attribute name can't be empty.")
		return

	var in_data: FlowData.Data = get_optional_input(0)
	if in_data == null:
		setError("Ancient Building needs an input point set.")
		return

	var point_count: int = in_data.size()
	if point_count <= 0:
		set_output(0, in_data)
		return

	var variants := _build_variants()
	if variants.is_empty():
		setError("Failed to generate any building mesh.")
		return

	var out_data: FlowData.Data = in_data.duplicate()

	var container = out_data.newContainerOfType(FlowData.DataType.Resource)
	if container == null:
		setError("Failed to create a Resource container.")
		return
	container.resize(point_count)

	# Deterministic per-point pick, so re-running the graph gives the same village.
	var rng := RandomNumberGenerator.new()
	for index in point_count:
		rng.seed = hash(settings.seed) + index * 2654435761
		container[index] = variants[rng.randi() % variants.size()]

	var err = out_data.registerStream(
		settings.mesh_attribute, container, FlowData.DataType.Resource)
	if err:
		setError(err)
		return

	set_output(0, out_data)


## One mesh per variant. Uses bake_mesh() on a throwaway node so nothing enters the scene.
func _build_variants() -> Array[Mesh]:
	var result: Array[Mesh] = []
	var count: int = maxi(settings.variant_count, 1)

	for index in count:
		var rng := RandomNumberGenerator.new()
		rng.seed = hash(settings.seed) + index

		var params := ClassDB.instantiate("AncientBuildingParameters")

		var jitter: float = settings.size_jitter
		params.width = settings.width * (1.0 + rng.randf_range(-jitter, jitter))
		params.depth = settings.depth * (1.0 + rng.randf_range(-jitter, jitter))
		params.bays_x = settings.bays_x
		params.bays_z = settings.bays_z

		var roof: int = settings.roof_type
		if settings.randomize_roof_type:
			roof = rng.randi() % ROOF_TYPE_COUNT
		# Eq 8: a hip roof on a square plan collapses the ridge to a point. That is a valid
		# 攒尖 pyramid, so it is allowed rather than corrected.
		params.roof_type = roof

		params.rafter_courses = settings.rafter_courses
		params.tile_coverage = settings.tile_coverage
		params.corner_rise_scale = settings.corner_rise_scale
		params.tile_course_width = settings.tile_course_width

		params.generate_fence = settings.generate_fence
		params.generate_steps = settings.generate_steps
		params.generate_walls = settings.generate_walls
		params.fence_lambda = settings.fence_lambda

		params.stone_color = settings.stone_color
		params.timber_color = settings.timber_color
		params.plaster_color = settings.plaster_color
		params.tile_color = settings.tile_color

		var building = ClassDB.instantiate("AncientBuilding")
		building.auto_regenerate = false
		building.parameters = params
		var mesh: Mesh = building.bake_mesh()
		# bake_mesh() returns the node's own mesh, which outlives the node.
		building.free()

		if mesh != null:
			result.append(mesh)

	return result
