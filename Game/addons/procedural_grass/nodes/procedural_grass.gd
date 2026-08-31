@tool
extends FlowNodeBase

# Procedural Grass — generates grass-clump meshes and assigns one to every incoming
# point.
#
# The heavy lifting is the `abyss` GDExtension (Source/GrassGen/, an original
# clump/blade generator — see Docs/ProceduralGrass_Spec.md). This node only drives it
# and writes the results into a Resource stream, so the existing `spawn_meshes` node
# can instance them into a MultiMeshInstance3D exactly as it does for any other mesh
# attribute.
#
# Meshes are generated once per *variant*, not per point — the same economy that the
# procedural_rock and ancient_building nodes get from their variant caps.

const ProceduralGrassNodeSettings = preload(
	"res://addons/procedural_grass/nodes/procedural_grass_settings.gd")

const SPECIES_NAMES := ["Thatch", "Foxtail", "Short", "Weed"]


func _init() -> void:
	meta_node = {
		"title": "Procedural Grass",
		"settings": ProceduralGrassNodeSettings,
		"ins": [{"label": "Points"}],
		"outs": [{"label": "Points"}],
		"aliases": ["Grass", "Weed", "Lawn", "Turf"],
		"category": "Sampler",
		"tooltip": "Generates grass-clump meshes and writes one per point into a\n"
			+ "Resource attribute. Feed the output to Spawn Meshes with a matching\n"
			+ "mesh attribute name.",
	}


func getTitle() -> String:
	return "Procedural Grass - %s x%d" % [SPECIES_NAMES[clampi(settings.species, 0, 3)], settings.variant_count]


func execute(_ctx: FlowData.EvaluationContext) -> void:
	if not ClassDB.class_exists("ProceduralGrass"):
		setError("The `abyss` GDExtension is not loaded, so ProceduralGrass is unavailable.")
		return
	if settings.mesh_attribute.strip_edges() == "":
		setError("Mesh attribute name can't be empty.")
		return

	var in_data: FlowData.Data = get_optional_input(0)
	if in_data == null:
		setError("Procedural Grass needs an input point set.")
		return

	var point_count: int = in_data.size()
	if point_count <= 0:
		set_output(0, in_data)
		return

	var variants := _build_variants()
	if variants.is_empty():
		setError("Failed to generate any grass mesh.")
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
		var params := ClassDB.instantiate("ProceduralGrassParameters")

		params.species = clampi(settings.species, 0, 3)
		params.seed = settings.seed + index

		params.scale = settings.scale
		params.clump_radius = settings.clump_radius
		params.blade_count = settings.blade_count
		params.curvature = settings.curvature
		params.lean_angle = settings.lean_angle
		params.lean_azimuth = settings.lean_azimuth

		params.color_variance = settings.color_variance
		params.use_species_colors = settings.use_species_colors
		params.base_color = settings.base_color
		params.tip_color = settings.tip_color

		var grass = ClassDB.instantiate("ProceduralGrass")
		grass.auto_regenerate = false
		grass.parameters = params
		var mesh: Mesh = grass.bake_mesh()
		# bake_mesh() returns the node's own mesh, which outlives the node.
		grass.free()

		if mesh != null:
			result.append(mesh)

	return result
