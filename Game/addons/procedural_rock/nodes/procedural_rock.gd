@tool
extends FlowNodeBase

# Procedural Rock — generates marching-cubes rock meshes and assigns one to every
# incoming point.
#
# The heavy lifting is the `abyss` GDExtension (Source/RockGen/, a CPU port of
# "Unity Procedural Rock Generation" — see Reference/Unity-Procedural-Rock-Generation/).
# This node only drives it and writes the results into a Resource stream, so the
# existing `spawn_meshes` node can instance them into a MultiMeshInstance3D exactly as
# it does for any other mesh attribute.
#
# Meshes are generated once per *variant*, not per point — the same economy that the
# ancient_building node gets from its variant cap.

const ProceduralRockNodeSettings = preload(
	"res://addons/procedural_rock/nodes/procedural_rock_settings.gd")

const FORM_NAMES := ["Boulder", "Pebble", "Slab"]


func _init() -> void:
	meta_node = {
		"title": "Procedural Rock",
		"settings": ProceduralRockNodeSettings,
		"ins": [{"label": "Points"}],
		"outs": [{"label": "Points"}],
		"aliases": ["Rock", "Boulder", "Stone", "Pebble"],
		"category": "Sampler",
		"tooltip": "Generates marching-cubes rock meshes and writes one per point into a\n"
			+ "Resource attribute. Feed the output to Spawn Meshes with a matching\n"
			+ "mesh attribute name.",
	}


func getTitle() -> String:
	return "Procedural Rock - %s x%d" % [FORM_NAMES[clampi(settings.form, 0, 2)], settings.variant_count]


func execute(_ctx: FlowData.EvaluationContext) -> void:
	if not ClassDB.class_exists("ProceduralRock"):
		setError("The `abyss` GDExtension is not loaded, so ProceduralRock is unavailable.")
		return
	if settings.mesh_attribute.strip_edges() == "":
		setError("Mesh attribute name can't be empty.")
		return

	var in_data: FlowData.Data = get_optional_input(0)
	if in_data == null:
		setError("Procedural Rock needs an input point set.")
		return

	var point_count: int = in_data.size()
	if point_count <= 0:
		set_output(0, in_data)
		return

	var variants := _build_variants()
	if variants.is_empty():
		setError("Failed to generate any rock mesh.")
		return

	var out_data: FlowData.Data = in_data.duplicate()

	var container = out_data.newContainerOfType(FlowData.DataType.Resource)
	if container == null:
		setError("Failed to create a Resource container.")
		return
	container.resize(point_count)

	# Deterministic per-point pick, so re-running the graph gives the same field.
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
		var params := ClassDB.instantiate("ProceduralRockParameters")

		params.form = clampi(settings.form, 0, 2)
		params.resolution = settings.resolution
		params.scale = settings.scale
		params.steps = settings.steps
		params.smoothness = settings.smoothness
		params.seed = settings.seed + index

		params.displacement_scale = settings.displacement_scale
		params.displacement_spread = settings.displacement_spread
		params.flatness = settings.flatness
		params.roundness = settings.roundness

		params.cut_ground = settings.cut_ground
		params.ground_cut = settings.ground_cut

		params.base_color = settings.base_color
		params.crevice_color = settings.crevice_color

		var rock = ClassDB.instantiate("ProceduralRock")
		rock.auto_regenerate = false
		rock.parameters = params
		var mesh: Mesh = rock.bake_mesh()
		# bake_mesh() returns the node's own mesh, which outlives the node.
		rock.free()

		if mesh != null:
			result.append(mesh)

	return result
