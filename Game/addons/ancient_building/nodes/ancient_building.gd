@tool
extends FlowNodeBase

# Ancient Building — generates ancient Chinese architecture meshes and assigns one to every
# incoming point.
#
# The heavy lifting is the `abyss` GDExtension (Source/AncientBuilding/, a port of Hu & Qin
# 2020 — see ProjectAbyssWiki/documentation/systems/AncientBuilding_Spec.md). This node only drives it and writes the results
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
	"ab_tile_course_width", "ab_corner_rise_scale",
	# 民居形制 (2026-09-27). Absent ⇒ the node-level `settings.*` value is used instead,
	# so graphs that don't write these streams keep the old behaviour.
	"ab_dado_height_ratio", "ab_dado_top_trim", "ab_column_base_height_scale",
	# 瓦作逐点 (2026-09-27 复审 §3.2 同款缺陷): 泥背厚 changes the 瓦面 height, which is what every
	# 脊's 高度链 is measured from, so two points with different bedding must not share a mesh.
	"ab_tile_bedding_thickness"]
const OVERRIDE_INTS := ["ab_roof_type", "ab_bays_x", "ab_bays_z",
	"ab_material_style", "ab_rafter_courses", "ab_fence_lambda",
	# 瓦作 detail 与距离档: both change the baked mesh (叠压 / 檐口件 / 泥背; and the far tier's
	# coarser section), so they are part of a variant's identity, not node-level garnish.
	"ab_tile_detail", "ab_lod_level", "ab_ridge_detail"]
const OVERRIDE_BOOLS := ["ab_fence", "ab_walls", "ab_steps",
	# 民居形制. 地基 (ab_platform) is the "does this building stand on a 台基 at all" switch.
	"ab_platform", "ab_platform_top_joints", "ab_platform_edge_lip",
	"ab_paving", "ab_paving_joint_geometry", "ab_step_side_cheek", "ab_column_base_square"]

# 形制字段入 `_combo_key` 的口径（见该函数）。连续量按步长量化后再入键。
const FORM_BOOLS := ["ab_platform", "ab_platform_top_joints", "ab_platform_edge_lip",
	"ab_paving", "ab_paving_joint_geometry", "ab_step_side_cheek", "ab_column_base_square"]
const FORM_FLOATS := [
	["ab_dado_height_ratio", 0.05],
	["ab_dado_top_trim", 0.25],
	["ab_column_base_height_scale", 0.05],
	# 泥背厚 is a metre-scale thickness; 5 cm is finer than any value a layout would author.
	["ab_tile_bedding_thickness", 0.05],
]
# 整型形制字段入 `_combo_key` 的口径同 FORM_BOOLS：只按原值入键（档位是离散的）。
const FORM_INTS := ["ab_tile_detail", "ab_lod_level", "ab_ridge_detail"]

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
	# Footprint + roof + material have always defined the shared mesh. Bays, tile tweaks and
	# fences stay per-point because they are invisible at town scale.
	#
	# 民居形制 (2026-09-27) is NOT that kind of detail: 地基 / 台面 / 下碱砖带 change the
	# silhouette and the wall surface, so they must split the combo — otherwise two houses with
	# different 形制 would share the first sample's mesh (review R2 §3.2, the exact defect the
	# rollback came from).
	#
	# Rule: a field contributes **only when the graph actually wrote its stream**. Graphs that
	# don't carry the form streams therefore produce exactly the old keys and keep the old
	# variant grouping; when a town does write them, the form is part of the mesh identity.
	var parts := []
	parts.append(str(int(round(ov.ab_width / quantum))))
	parts.append(str(int(round(ov.ab_depth / quantum))))
	parts.append(str(int(ov.ab_roof_type)))
	parts.append(str(int(ov.ab_material_style)))
	for stream in FORM_BOOLS:
		if ov.has(stream):
			parts.append("b:%s=%d" % [stream, 1 if ov[stream] else 0])
	for stream in FORM_INTS:
		if ov.has(stream):
			parts.append("i:%s=%d" % [stream, int(ov[stream])])
	for entry in FORM_FLOATS:
		if ov.has(entry[0]):
			# 连续量先量化再入键：同一格内的差异不分裂变体。
			parts.append("f:%s=%d" % [entry[0], int(round(float(ov[entry[0]]) / float(entry[1])))])
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

		# 无覆盖路径没有 `ov`：传空字典 ⇒ 走节点级 settings 缺省。
		_apply_dwelling_style(params, roof, {})
		_apply_tile_detail(params, {})

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
## 属性存在才赋值。新增的 AncientBuildingParameters 属性在老 DLL 上不存在，
## 直接赋值会让烘焙报错并中断整张图。
func _apply_if_present(params: Object, prop: String, value) -> void:
	if prop in params:
		params.set(prop, value)


## 「民居样板」形制（2026-09-26）。两条烘焙路径（overrides / legacy）共用，
## 免得同一份设置只在城镇生效、在无 `ab_*` 流的场景里失效。
## 作用域由 `dwelling_style_scope` 控制：默认只给民居屋顶（硬山 0 / 悬山 3 /
## 卷棚 4），免得官式庙宇也长出民居的下碱带与柱础。
func _apply_dwelling_style(params: Object, roof_type: int, ov: Dictionary) -> void:
	# **流存在 ⇒ 以流为准**（"固化进 PCG 数据流"的含义）。地块生成器写了形制，每一栋的
	# 形制就是那一栋自己的事，不再受节点级作用域限制 —— 官式建筑也可以按流拿到台基。
	# 流缺席 ⇒ 退回节点级缺省 + 作用域守卫（保护没写流的图，例如墨线验证场景）。
	var has_streams: bool = ov.has("ab_platform") or ov.has("ab_dado_height_ratio")
	if not has_streams:
		var scope: int = settings.dwelling_style_scope
		if scope == 0:
			return
		if scope == 1 and not (roof_type in [0, 3, 4]):
			return
	# **流优先，settings 缺省**：城镇按地块写 `ab_*` 时以流为准；不写流的图沿用节点级设置，
	# 于是"形制"真正固化在 PCG 数据流里，而不是靠脚本往节点上灌。
	_apply_if_present(params, "generate_platform", _form_value(ov, "ab_platform", settings.generate_platform))
	_apply_if_present(params, "dado_height_ratio", _form_value(ov, "ab_dado_height_ratio", settings.dado_height_ratio))
	_apply_if_present(params, "dado_top_trim", _form_value(ov, "ab_dado_top_trim", settings.dado_top_trim))
	_apply_if_present(params, "platform_top_joints", _form_value(ov, "ab_platform_top_joints", settings.platform_top_joints))
	_apply_if_present(params, "platform_edge_lip", _form_value(ov, "ab_platform_edge_lip", settings.platform_edge_lip))
	_apply_if_present(params, "paving", _form_value(ov, "ab_paving", settings.paving))
	_apply_if_present(params, "paving_joint_geometry", _form_value(ov, "ab_paving_joint_geometry", settings.paving_joint_geometry))
	_apply_if_present(params, "step_side_cheek", _form_value(ov, "ab_step_side_cheek", settings.step_side_cheek))
	_apply_if_present(params, "column_base_square", _form_value(ov, "ab_column_base_square", settings.column_base_square))
	_apply_if_present(params, "column_base_height_scale", _form_value(ov, "ab_column_base_height_scale", settings.column_base_height_scale))


## Stream value if the graph wrote it, otherwise the node-level setting.
func _form_value(ov: Dictionary, stream: String, fallback):
	return ov[stream] if ov.has(stream) else fallback


## 瓦作 detail / 泥背厚 / 距离档（复审 §3.2 同款缺陷的修复）。
##
## 这三项**改变烘焙出来的网格**（叠压与檐口件、瓦面抬升、断面档），却一直只由节点级设置给出、
## 且**不在 `_combo_key` 里**——不同档的点会共用同一份缓存网格。现在：写了 `ab_*` 流就以流为准
## 并入键，没写流的图既用节点缺省、键也不变（分组不变）。
## 没有作用域守卫：瓦作 detail 与屋顶形制无关，官式庙宇也照样是同一档瓦。
func _apply_tile_detail(params: Object, ov: Dictionary) -> void:
	_apply_if_present(params, "ridge_detail",
		int(_form_value(ov, "ab_ridge_detail", settings.ridge_detail)))
	_apply_if_present(params, "tile_detail",
		int(_form_value(ov, "ab_tile_detail", settings.tile_detail)))
	_apply_if_present(params, "tile_bedding_thickness",
		float(_form_value(ov, "ab_tile_bedding_thickness", settings.tile_bedding_thickness)))
	_apply_if_present(params, "lod_level",
		int(_form_value(ov, "ab_lod_level", settings.lod_level)))


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

	_apply_dwelling_style(params, int(ov.ab_roof_type), ov)
	_apply_tile_detail(params, ov)

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
