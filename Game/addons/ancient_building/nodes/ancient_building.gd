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
#
# Point overrides: when the input carries `ab_*` streams (written by the Ancient Town node),
# points are grouped by their quantised parameter combination and one variant is baked per
# combination (capped by variant_count). Points sharing a combination share one mesh, so a
# whole city of ~250 buildings costs a few dozen baked meshes.

const AncientBuildingNodeSettings = preload(
	"res://addons/ancient_building/nodes/ancient_building_settings.gd")

const ROOF_TYPE_COUNT := 9

# Streams the town generator may carry, read as per-point parameter overrides.
const OVERRIDE_FLOATS := ["ab_width", "ab_depth", "ab_tile_coverage",
	"ab_tile_course_width", "ab_corner_rise_scale"]
const OVERRIDE_INTS := ["ab_roof_type", "ab_bays_x", "ab_bays_z",
	"ab_material_style", "ab_rafter_courses", "ab_fence_lambda"]
const OVERRIDE_BOOLS := ["ab_fence", "ab_walls", "ab_steps"]

const ROOF_NAMES := ["硬山", "歇山", "庑殿", "悬山", "卷棚", "盝顶", "攒尖", "圆攒尖", "盔顶"]


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
	var label: String = "Mixed" if settings.randomize_roof_type \
		else ROOF_NAMES[clampi(settings.roof_type, 0, ROOF_TYPE_COUNT - 1)]

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

	var overrides = _collect_overrides(in_data, point_count)
	if overrides == null or not settings.use_point_overrides:
		_execute_legacy(in_data, point_count)
	else:
		_execute_overrides(in_data, point_count, overrides)


# --- legacy path: random variants, no per-point parameters -------------------

func _execute_legacy(in_data: FlowData.Data, point_count: int) -> void:
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


# --- override path: one baked variant per quantised parameter combination -----

func _execute_overrides(in_data: FlowData.Data, point_count: int, overrides: Array) -> void:
	# Quantise the continuous identity fields. If the combination count exceeds
	# the cap, coarsen the width/depth quantum until it fits — roof / bays /
	# material stay exact, those are what define a building's character.
	var quantum := 0.5
	var keys := {}
	var key_per_point: Array[String] = []
	key_per_point.resize(point_count)
	while true:
		keys.clear()
		for i in point_count:
			var key := _combo_key(overrides[i], quantum)
			keys[key] = true
			key_per_point[i] = key
		if keys.size() <= maxi(settings.variant_count, 1) or quantum >= 4.0:
			break
		quantum *= 2.0

	# Bake one variant per combination, seeded by the key so the same layout
	# always produces the same buildings.
	var mesh_by_key := {}
	var first_by_key := {}
	for i in point_count:
		var key: String = key_per_point[i]
		if not first_by_key.has(key):
			first_by_key[key] = overrides[i]
	for key in keys:
		var sample: Dictionary = first_by_key[key]
		var rng := RandomNumberGenerator.new()
		rng.seed = hash([settings.seed, key])
		var mesh := _bake_from_params(sample, rng)
		if mesh != null:
			mesh_by_key[key] = mesh
	if mesh_by_key.is_empty():
		setError("Failed to generate any building mesh.")
		return
	if mesh_by_key.size() != keys.size():
		setError("Some building variants failed to bake (%d/%d)." % [
			mesh_by_key.size(), keys.size()])
		return

	var out_data: FlowData.Data = in_data.duplicate()
	var container = out_data.newContainerOfType(FlowData.DataType.Resource)
	if container == null:
		setError("Failed to create a Resource container.")
		return
	container.resize(point_count)
	for i in point_count:
		container[i] = mesh_by_key[key_per_point[i]]

	var err = out_data.registerStream(
		settings.mesh_attribute, container, FlowData.DataType.Resource)
	if err:
		setError(err)
		return

	set_output(0, out_data)


func _combo_key(ov: Dictionary, quantum: float) -> String:
	# Only footprint + roof + material define the shared mesh. Bays, tile tweaks,
	# fences etc. stay in the per-point override but the baked mesh follows the
	# first sample of the combo — those details are invisible at town scale.
	var parts := []
	parts.append(str(int(round(ov.ab_width / quantum))))
	parts.append(str(int(round(ov.ab_depth / quantum))))
	parts.append(str(int(ov.ab_roof_type)))
	parts.append(str(int(ov.ab_material_style)))
	return ",".join(parts)


## Reads the `ab_*` override streams. Returns null when none of them exist.
## The override path needs the four identity streams (width / depth / roof /
## material) — a partial `ab_*` input without them falls back to the legacy
## path instead of doing null arithmetic in `_combo_key`.
func _collect_overrides(in_data: FlowData.Data, point_count: int):
	var found := false
	for sname in OVERRIDE_FLOATS + OVERRIDE_INTS + OVERRIDE_BOOLS:
		if in_data.hasStream(sname):
			found = true
			break
	if not found:
		return null
	for sname in ["ab_width", "ab_depth", "ab_roof_type", "ab_material_style"]:
		if not in_data.hasStream(sname):
			return null

	var overrides: Array = []
	overrides.resize(point_count)
	for i in point_count:
		overrides[i] = {}

	for sname in OVERRIDE_FLOATS:
		var stream = in_data.findStream(sname)
		if stream == null:
			continue
		var container: PackedFloat32Array = stream.container
		for i in point_count:
			overrides[i][sname] = container[FlowData.bcast_idx(container.size(), i)]
	for sname in OVERRIDE_INTS:
		var stream = in_data.findStream(sname)
		if stream == null:
			continue
		var container: PackedInt32Array = stream.container
		for i in point_count:
			overrides[i][sname] = container[FlowData.bcast_idx(container.size(), i)]
	for sname in OVERRIDE_BOOLS:
		var stream = in_data.findStream(sname)
		if stream == null:
			continue
		var container: PackedByteArray = stream.container
		for i in point_count:
			overrides[i][sname] = container[FlowData.bcast_idx(container.size(), i)] != 0

	# Optional streams missing from the input fall back to node settings.
	var defaults := {
		"ab_tile_coverage": settings.tile_coverage,
		"ab_tile_course_width": settings.tile_course_width,
		"ab_corner_rise_scale": settings.corner_rise_scale,
		"ab_bays_x": settings.bays_x,
		"ab_bays_z": settings.bays_z,
		"ab_rafter_courses": settings.rafter_courses,
		"ab_fence_lambda": settings.fence_lambda,
		"ab_fence": settings.generate_fence,
		"ab_walls": settings.generate_walls,
		"ab_steps": settings.generate_steps,
	}
	for i in point_count:
		var ov: Dictionary = overrides[i]
		for k in defaults:
			if not ov.has(k):
				ov[k] = defaults[k]
	return overrides


# --- baking ----------------------------------------------------------------

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
		params.material_style = settings.material_style

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


## Bakes one mesh from an explicit override parameter set (`ab_*` values), with
## size jitter drawn from the given deterministic rng.
func _bake_from_params(ov: Dictionary, rng: RandomNumberGenerator) -> Mesh:
	var params := ClassDB.instantiate("AncientBuildingParameters")

	var jitter: float = settings.size_jitter
	params.width = ov.ab_width * (1.0 + rng.randf_range(-jitter, jitter))
	params.depth = ov.ab_depth * (1.0 + rng.randf_range(-jitter, jitter))
	params.bays_x = int(ov.ab_bays_x)
	params.bays_z = int(ov.ab_bays_z)
	params.roof_type = int(ov.ab_roof_type)

	params.rafter_courses = int(ov.ab_rafter_courses)
	params.tile_coverage = ov.ab_tile_coverage
	params.corner_rise_scale = ov.ab_corner_rise_scale
	params.tile_course_width = ov.ab_tile_course_width

	params.generate_fence = ov.ab_fence
	params.generate_steps = ov.ab_steps
	params.generate_walls = ov.ab_walls
	params.fence_lambda = int(ov.ab_fence_lambda)
	params.material_style = int(ov.ab_material_style)

	params.stone_color = settings.stone_color
	params.timber_color = settings.timber_color
	params.plaster_color = settings.plaster_color
	params.tile_color = settings.tile_color

	var building = ClassDB.instantiate("AncientBuilding")
	building.auto_regenerate = false
	building.parameters = params
	var mesh: Mesh = building.bake_mesh()
	building.free()
	return mesh
