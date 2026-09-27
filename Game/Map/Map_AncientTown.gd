extends Node3D

# 古代城镇生成关卡: Ancient Town flow 节点(聚落/村镇/市集/城市) + AncientBuilding 变体烘焙
# + 水墨三件套材质 + 高俯角相机 rig。
#
#   - 城镇布局由 town_lots 节点一步生成(里坊网格/市集/街巷 + 城墙/道路/摊位/树)
#   - 建筑走 ancient_building 节点的 per-point 参数覆盖: 每栋按 level 分屋顶(硬山→庑殿)、
#     开间、材质, 相同参数组合共享同一网格 → MultiMesh 实例化
#   - 路面/城墙/摊位/树用顶点色网格, 全部套 InkPainting 三件套
#
# 运行: Engine/bin/godot.windows.editor.x86_64.console.exe --path Game/ res://Map/Map_AncientTown.tscn
#   SHOTS=1             只出图然后退出
#   TOWN_PRESET=city    聚落 hamlet / 村镇 village / 市集 market / 城市 city / 全览 all (默认 city)
#   TOWN_SEED=42        布局种子
#
# 操作: WASD 移动, 右键拖拽环绕, 滚轮缩放, Q/E 偏摆, R/F 俯仰

const FlowGraphBuilder := preload("res://Script/PCG/flow_graph_builder.gd")
const ShotOutput := preload("res://Develop/Tools/shot_output.gd")
const TownMeshes := preload("res://addons/ancient_town/town_meshes.gd")
const PropMeshes := preload("res://Script/PCG/pcg_prop_meshes.gd")

const AB_NODE_DIR := "res://addons/ancient_building/nodes"
const TOWN_NODE_DIR := "res://addons/ancient_town/nodes"
const SHADER_DIR := "res://Assets/Shaders/InkPainting"
const TEX_DIR := "res://Assets/Shaders/InkPainting/Textures"

const PAPER := Color(0.898, 0.859, 0.824)

# ---------------------------------------------------------------------------
# 「民居样板」形制 —— **缺省档**（2026-09-27 起不再是主路径）
#
# 形制的主路径已经**固化进 PCG 数据流**：`town_lots` 按地块等级写 `ab_platform` /
# `ab_dado_height_ratio` / … 等 10 条 `ab_*` 流，Ancient Building 节点逐点读取并把它们
# **并入组合键**（见 `addons/ancient_building/nodes/ancient_building.gd` 的 `_combo_key`）。
# 因此每栋建筑可以有**自己的**台基/台面/下碱砖带，而不是全城一律。
#
# 本字典现在只作**流缺席时的节点级缺省**（例如别的图接了同一节点但没写这些流）。
# 城镇里它已被流覆盖；改它不会影响本图的观感 —— 要调城镇的形制，改 `town_lots.gd`
# 里那段按等级分档的逻辑。
#
# 值取自手册 `20_墙体 R15` 与 `60_台基地面 R6·R9·R12`。**除 `column_base_height_scale`
# 外全部是 新增参数，默认关/0。**
# 作用域 `dwelling_style_scope`：仅在**没有流**时生效；1 = 只给民居屋顶，2 = 全部，0 = 关。
# ---------------------------------------------------------------------------
const MINGJU_STYLE := {
	"dwelling_style_scope": 1,
	"dado_height_ratio": 0.25,      # 下碱高 ÷ 墙高（0 = 旧行为）
	"dado_top_trim": 1.0,           # 分界带高，单位 = 模数 D（0 = 关）
	"platform_top_joints": true,    # 台基顶面接缝网格
	"platform_edge_lip": true,      # 台基边缘凸出沿口
	"paving": true,                 # 方砖铺地
	"paving_joint_geometry": true,  # 铺地几何板缝（关 = 顶点色缝）
	"step_side_cheek": true,        # 踏步两侧侧挡
	"column_base_square": true,     # 方形石础（关 = 旧车削圆础盘）
	"column_base_height_scale": 0.35,
	"ridge_detail": 1,              # 分层圆脊 + 山尖连续接头（2026-09-27 用户裁定为默认）
}

var _ink_material: ShaderMaterial
var _towns: Array[Dictionary] = []
var _env: Environment
var _dof_attrs: CameraAttributesPractical


func _ready() -> void:
	_build_environment()
	_ink_material = _make_ink_material()

	var preset := _preset_from_env()
	var seed: int = _seed_from_env()

	_build_ground(preset)
	_build_towns(preset, seed)

	var focus: Vector3 = _towns[0]["center"] if _towns.size() > 0 else Vector3.ZERO
	_build_camera_rig(focus)

	if OS.has_environment("SHOTS"):
		await _shoot()
		get_tree().quit()


func _preset_from_env() -> String:
	var p: String = OS.get_environment("TOWN_PRESET") if OS.has_environment("TOWN_PRESET") else ""
	match p:
		"hamlet", "village", "market", "all":
			return p
		_:
			return "city"


func _seed_from_env() -> int:
	var s: String = OS.get_environment("TOWN_SEED") if OS.has_environment("TOWN_SEED") else ""
	if s.is_valid_int():
		return s.to_int()
	return 42


# ---------------------------------------------------------------- 环境

func _build_environment() -> void:
	var light := DirectionalLight3D.new()
	light.name = "SunLight"
	light.rotation_degrees = Vector3(-42, -38, 0)
	light.light_energy = 0.95
	light.shadow_enabled = true
	add_child(light)

	# 南侧补光 (无阴影): 墨水 shader 是光照驱动的, 单盏太阳下所有背光面
	# NdotL≈0 → 近黑 (~44)。补光通过 fill_light_dir 旁路不走墨色闸门,
	# 直接给平面漫反射, 把阴影地面/暗面抬到可读的深灰 (~85-95), 同时
	# 不抹掉太阳光画的墨 (2026-09-26 round-5: 旁路修复低角度补光被
	# light_ink 判成阴面涂成全墨的问题)。
	# 能量配比: 光照面总叠加强度 = sun(NdotL×0.95) + ambient + fill(NdotL×0.5)。
	# shader 修 NdotL 后 (见 ink_surface light() 注释), 地面 0.82 base ×1.2 ≈ 0.99
	# 恰好不 clamp, 纸面纹理保留; 道路 0.62 base → 日照 ~190 暖灰、阴影 ~90 深灰。
	var fill := DirectionalLight3D.new()
	fill.name = "FillLight"
	fill.rotation_degrees = Vector3(-45, 130, 0)
	fill.light_energy = 0.5
	fill.shadow_enabled = false
	add_child(fill)

	# 水平低角度南侧补光 (无阴影): 只打亮朝南竖直面 (地面/路面 NdotL=0 不受
	# 影响), 把被建筑阴影盖住的摊位立面等从近黑抬到可读深灰 (2026-09-26
	# round-4 评审: 市场广场中央摊位黑色块)。
	var fill2 := DirectionalLight3D.new()
	fill2.name = "FillLight2"
	fill2.rotation_degrees = Vector3(0, 180, 0)
	fill2.light_energy = 0.3
	fill2.shadow_enabled = false
	add_child(fill2)

	# 东西向水平补光 (无阴影, 0.3+0.3): 只打亮朝东/朝西竖直面, 让市场东西两侧
	# 摊位面向广场的前脸不再只剩 ambient 的近黑。南侧 FillLight2 对东西朝向立面
	# NdotL=0 无能为力 (2026-09-26 round-4 评审续, shader 侧按 |dir.y|<0.1 旁路)。
	var fill3 := DirectionalLight3D.new()
	fill3.name = "FillLightW"  # surface->light dir = (-1,0,0): 打亮朝西面
	fill3.rotation_degrees = Vector3(0, -90, 0)
	fill3.light_energy = 0.3
	fill3.shadow_enabled = false
	add_child(fill3)
	var fill4 := DirectionalLight3D.new()
	fill4.name = "FillLightE"  # surface->light dir = (1,0,0): 打亮朝东面
	fill4.rotation_degrees = Vector3(0, 90, 0)
	fill4.light_energy = 0.3
	fill4.shadow_enabled = false
	add_child(fill4)

	var env := WorldEnvironment.new()
	env.name = "InkEnvironment"
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = PAPER
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.86, 0.84, 0.82)
	e.ambient_light_energy = 0.25
	e.fog_enabled = true
	e.fog_mode = Environment.FOG_MODE_DEPTH
	e.fog_light_color = PAPER
	e.fog_depth_begin = 300.0
	e.fog_depth_end = 1500.0
	env.environment = e
	_env = e
	add_child(env)


func _make_ink_material() -> ShaderMaterial:
	var remap: Texture2D = load(TEX_DIR + "/ColorRemap.png")
	var noise: Texture2D = load(TEX_DIR + "/Noise.png")
	var stroke: Texture2D = load(TEX_DIR + "/Stroke.png")
	var matcap: Texture2D = load(TEX_DIR + "/MatCap.png")

	var surface := ShaderMaterial.new()
	surface.shader = load(SHADER_DIR + "/ink_surface.gdshader")
	surface.set_shader_parameter("base_color", Color(1, 1, 1))
	surface.set_shader_parameter("use_vertex_color", true)
	surface.set_shader_parameter("color_remap_tex", remap)
	surface.set_shader_parameter("noise_tex", noise)
	surface.set_shader_parameter("stroke_tex", stroke)
	# 补光旁路: 与该方向同向的平行光跳过墨色闸门 (见 shader 注释)。
	var fill := get_node_or_null("FillLight")
	if fill != null:
		surface.set_shader_parameter("fill_light_dir", fill.global_transform.basis.z.normalized())
	var fill2 := get_node_or_null("FillLight2")
	if fill2 != null:
		surface.set_shader_parameter("fill_light_dir2", fill2.global_transform.basis.z.normalized())

	var outline0 := ShaderMaterial.new()
	outline0.shader = load(SHADER_DIR + "/ink_outline_0.gdshader")
	outline0.set_shader_parameter("color_remap_tex", remap)
	outline0.set_shader_parameter("matcap_tex", matcap)
	outline0.set_shader_parameter("noise_tex", noise)
	outline0.set_shader_parameter("outline_width", 2.0)

	var outline1 := ShaderMaterial.new()
	outline1.shader = load(SHADER_DIR + "/ink_outline_1.gdshader")
	outline1.set_shader_parameter("color_remap_tex", remap)
	outline1.set_shader_parameter("noise_tex", noise)
	outline1.set_shader_parameter("outline_width", 3.5)

	outline0.next_pass = outline1
	surface.next_pass = outline0
	return surface


func _apply_ink(node: Node) -> int:
	var count := 0
	if node is GeometryInstance3D:
		node.material_override = _ink_material
		count += 1
	for child in node.get_children():
		count += _apply_ink(child)
	return count


# ---------------------------------------------------------------- 地面

func _build_ground(preset: String) -> void:
	var ground := MeshInstance3D.new()
	ground.name = "Ground"
	var plane := PlaneMesh.new()
	plane.size = Vector2(2400, 2400) if preset == "all" else Vector2(760, 760)
	plane.subdivide_width = 8
	plane.subdivide_depth = 8
	ground.mesh = plane
	var ground_mat := ShaderMaterial.new()
	ground_mat.shader = load(SHADER_DIR + "/ink_surface.gdshader")
	ground_mat.set_shader_parameter("base_color", Color(0.82, 0.81, 0.78))
	ground_mat.set_shader_parameter("use_vertex_color", false)
	ground_mat.set_shader_parameter("color_remap_tex", load(TEX_DIR + "/ColorRemap.png"))
	ground_mat.set_shader_parameter("noise_tex", load(TEX_DIR + "/Noise.png"))
	ground_mat.set_shader_parameter("stroke_tex", load(TEX_DIR + "/Stroke.png"))
	ground.material_override = ground_mat
	add_child(ground)


# ---------------------------------------------------------------- 城镇 (PCG)

func _build_towns(preset: String, seed: int) -> void:
	FlowNodeRegistry.register_node_directory(AB_NODE_DIR)
	FlowNodeRegistry.register_node_directory(TOWN_NODE_DIR)

	var plan: Array[Dictionary]
	match preset:
		"hamlet":
			plan = [{ "type": 0, "seed": seed, "origin": Vector3.ZERO, "ex": 90.0, "ez": 90.0 }]
		"village":
			plan = [{ "type": 1, "seed": seed, "origin": Vector3.ZERO, "ex": 110.0, "ez": 110.0 }]
		"market":
			plan = [{ "type": 2, "seed": seed, "origin": Vector3.ZERO, "ex": 95.0, "ez": 95.0 }]
		"all":
			plan = [
				{ "type": 3, "seed": seed,     "origin": Vector3(0, 0, 0), "ex": 260.0, "ez": 260.0 },
				{ "type": 2, "seed": seed + 1, "origin": Vector3(-335, 0, 335), "ex": 95.0, "ez": 95.0 },
				{ "type": 1, "seed": seed + 2, "origin": Vector3(335, 0, 345), "ex": 110.0, "ez": 110.0 },
				{ "type": 0, "seed": seed + 3, "origin": Vector3(-340, 0, -340), "ex": 90.0, "ez": 90.0 },
			]
		_:
			plan = [{ "type": 3, "seed": seed, "origin": Vector3.ZERO, "ex": 260.0, "ez": 260.0 }]

	for entry in plan:
		var flow := FlowGraphNode3D.new()
		flow.name = "TownPCG_%d" % (_towns.size())
		flow.graph = _town_graph(entry)
		add_child(flow)  # FlowGraphNode3D._ready() executes the graph in game runs

		var applied := _apply_ink(flow)

		var mmi_count := 0
		var instance_count := 0
		for child in flow.get_children():
			var mmi := child as MultiMeshInstance3D
			if mmi != null and mmi.multimesh != null:
				mmi_count += 1
				instance_count += mmi.multimesh.instance_count

		print("town[%d] preset=%d inked=%d mmis=%d instances=%d" % [
			_towns.size(), entry.type, applied, mmi_count, instance_count])
		_towns.append({ "center": entry.origin, "type": entry.type })


func _town_graph(entry: Dictionary) -> FlowGraphResource:
	var builder := FlowGraphBuilder.new()

	var lots := builder.AddNode("town_lots", {
		"settlement_type": entry.type,
		"origin": entry.origin,
		"extent_x": entry.ex,
		"extent_z": entry.ez,
		"random_seed": entry.seed,
	}, Vector2(0, 0))

	# Buildings: per-point ab_* overrides bake one mesh per parameter combo.
	var ab_settings := {
		"mesh_attribute": "mesh",
		"variant_count": 24,
		"seed": entry.seed + 7,
		"size_jitter": 0.06,
	}
	ab_settings.merge(MINGJU_STYLE, true)   # 民居形制，见文件上方 MINGJU_STYLE
	var buildings := builder.AddNode("ancient_building", ab_settings, Vector2(300, 0), { "Points": 0 })
	var spawn_b := builder.AddNode("spawn_meshes", {
		"clear_previous_instances": true,
		"mesh_attribute": "mesh",
		"color_attribute": "color",
		"use_vertex_colors": true,
		"random_seed": entry.seed + 8,
	}, Vector2(600, 0), { "In": 0 })

	# Roads: unit box stretched by the per-point size stream.
	var spawn_road := builder.AddNode("spawn_meshes", {
		"clear_previous_instances": true,
		"mesh": TownMeshes.road(),
		"color_attribute": "color",
		"use_vertex_colors": true,
		"random_seed": entry.seed + 9,
	}, Vector2(600, 200), { "In": 0 })

	# Walls: city ring + palace wall.
	var spawn_wall := builder.AddNode("spawn_meshes", {
		"clear_previous_instances": true,
		"mesh": TownMeshes.wall(),
		"color_attribute": "color",
		"use_vertex_colors": true,
		"random_seed": entry.seed + 10,
	}, Vector2(600, 400), { "In": 0 })

	# Props: prop_type stream selects stall / well / 牌坊.
	var spawn_prop := builder.AddNode("spawn_meshes", {
		"clear_previous_instances": true,
		"mesh_variants": [TownMeshes.stall(), TownMeshes.well(), TownMeshes.archway()],
		"mesh_variant_weights": [1.0, 1.0, 1.0],
		"mesh_selector_attribute": "prop_type",
		"color_attribute": "color",
		"use_vertex_colors": true,
		"random_seed": entry.seed + 11,
	}, Vector2(600, 600), { "In": 0 })

	# Trees: conifer / broadleaf weighted variants.
	var spawn_tree := builder.AddNode("spawn_meshes", {
		"clear_previous_instances": true,
		"mesh_variants": [PropMeshes.conifer(), PropMeshes.broadleaf()],
		"mesh_variant_weights": [1.0, 2.0],
		"randomize_mesh_variants": true,
		"color_attribute": "color",
		"use_vertex_colors": true,
		"random_seed": entry.seed + 12,
	}, Vector2(600, 800), { "In": 0 })

	builder.Connect(lots, 0, buildings, 0)
	builder.Connect(buildings, 0, spawn_b, 0)
	builder.Connect(lots, 1, spawn_road, 0)
	builder.Connect(lots, 2, spawn_wall, 0)
	builder.Connect(lots, 3, spawn_prop, 0)
	builder.Connect(lots, 4, spawn_tree, 0)

	return builder.Build()


# ---------------------------------------------------------------- 相机

func _build_camera_rig(focus: Vector3) -> void:
	var player := Node3D.new()
	player.name = "CameraFocus"
	player.position = focus
	add_child(player)

	var yaw := Node3D.new()
	yaw.name = "YawPivot"
	player.add_child(yaw)

	var pitch := Node3D.new()
	pitch.name = "PitchPivot"
	yaw.add_child(pitch)

	var arm := SpringArm3D.new()
	arm.name = "SpringArm"
	arm.spring_length = 46.0
	arm.collision_mask = 0
	pitch.add_child(arm)

	var camera := Camera3D.new()
	camera.name = "Camera3D"
	camera.current = true
	camera.fov = 38.0
	camera.far = 6000.0
	var attrs := CameraAttributesPractical.new()
	attrs.dof_blur_far_enabled = true
	attrs.dof_blur_far_distance = 220.0
	attrs.dof_blur_far_transition = 90.0
	attrs.dof_blur_near_enabled = true
	attrs.dof_blur_near_distance = 22.0
	attrs.dof_blur_near_transition = 14.0
	attrs.dof_blur_amount = 0.06
	camera.attributes = attrs
	_dof_attrs = attrs
	arm.add_child(camera)

	var rig = ClassDB.instantiate("CameraRigController")
	rig.name = "CameraRig"
	rig.player_path = NodePath("../CameraFocus")
	rig.yaw_pivot_path = NodePath("../CameraFocus/YawPivot")
	rig.pitch_pivot_path = NodePath("../CameraFocus/YawPivot/PitchPivot")
	rig.spring_arm_path = NodePath("../CameraFocus/YawPivot/PitchPivot/SpringArm")
	rig.default_pitch = -38.0
	rig.zoom_default = 62.0
	rig.zoom_min = 18.0
	rig.zoom_max = 460.0
	add_child(rig)


# ---------------------------------------------------------------- 出图

## 帧位参数: yaw = 相机绕焦点水平角 (0 = 相机在焦点北侧朝南看), pitch = 俯角(负),
## zoom = SpringArm 臂长; focus 用 Vector3.INF 表示 "auto" —— 按生成内容包围盒自动取景。
func _shots_for(preset: String) -> Array[Dictionary]:
	match preset:
		"hamlet":
			return [
				# yaw 180: 从南侧看背光面, 小聚落墨色更足 (2026-09-26 round-4)。
				{ "name": "overview", "focus": Vector3.INF, "pitch": -60.0, "yaw": 180.0, "zoom": -1.0 },
				{ "name": "lane", "focus": Vector3(18, 4, 12), "pitch": -38.0, "yaw": 0.0, "zoom": 30.0 },
			]
		"village":
			return [
				{ "name": "overview", "focus": Vector3.INF, "pitch": -60.0, "yaw": 180.0, "zoom": -1.0 },
				{ "name": "street", "focus": Vector3(35, 3, 0), "pitch": -12.0, "yaw": -90.0, "zoom": 40.0 },
				{ "name": "temple", "focus": Vector3(82.7, 8, 0), "pitch": -26.0, "yaw": -90.0, "zoom": 30.0 },
			]
		"market":
			return [
				{ "name": "overview", "focus": Vector3.INF, "pitch": -60.0, "yaw": 0.0, "zoom": -1.0 },
				{ "name": "square", "focus": Vector3(0, 2, 0), "pitch": -35.0, "yaw": 180.0, "zoom": 46.0 },
				{ "name": "stalls", "focus": Vector3(0, 1.5, 10), "pitch": -35.0, "yaw": 180.0, "zoom": 22.0 },
			]
		"all":
			return [
				{ "name": "overview", "focus": Vector3.INF, "pitch": -60.0, "yaw": 0.0, "zoom": -1.0 },
				{ "name": "city", "focus": Vector3(0, 8, 0), "pitch": -55.0, "yaw": 0.0, "zoom": 950.0 },
			]
		_:
			return [
				{ "name": "overview", "focus": Vector3.INF, "pitch": -60.0, "yaw": 0.0, "zoom": -1.0 },
				{ "name": "palace", "focus": Vector3(0, 10, 17), "pitch": -30.0, "yaw": 180.0, "zoom": 44.0 },
				{ "name": "avenue", "focus": Vector3(0, 4, -40), "pitch": -12.0, "yaw": 180.0, "zoom": 48.0 },
				{ "name": "gate", "focus": Vector3(0, 10, -258), "pitch": -28.0, "yaw": 180.0, "zoom": 42.0 },
				{ "name": "street", "focus": Vector3(60, 5, 0), "pitch": -16.0, "yaw": 90.0, "zoom": 40.0 },
				{ "name": "corner", "focus": Vector3(263, 12, 263), "pitch": -35.0, "yaw": -135.0, "zoom": 62.0 },
			]


## 所有已生成 MultiMesh 实例的世界包围盒。用于 auto 取景。
func _town_bounds(flows: Array) -> Dictionary:
	var mn := Vector3(1e9, 1e9, 1e9)
	var mx := Vector3(-1e9, -1e9, -1e9)
	for flow in flows:
		for child in flow.get_children():
			var mmi := child as MultiMeshInstance3D
			if mmi == null or mmi.multimesh == null:
				continue
			for i in mmi.multimesh.instance_count:
				var t := mmi.multimesh.get_instance_transform(i)
				mn = mn.min(t.origin - Vector3(4, 0, 4))
				mx = mx.max(t.origin + Vector3(4, 8, 4))
	return { "center": (mn + mx) * 0.5, "size": mx - mn }


func _shoot() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X

	var focus: Node3D = get_node("CameraFocus")
	var yaw_pivot: Node3D = get_node("CameraFocus/YawPivot")
	var pitch_pivot: Node3D = get_node("CameraFocus/YawPivot/PitchPivot")
	var arm: SpringArm3D = get_node("CameraFocus/YawPivot/PitchPivot/SpringArm")
	var preset := _preset_from_env()

	var flows: Array = []
	for child in get_children():
		if child.name.begins_with("TownPCG_"):
			flows.append(child)

	for shot in _shots_for(preset):
		# auto 取景: 焦点=包围盒中心, 臂长=按画幅把内容框进来。相机由 SpringArm
		# 看向焦点本身, 画面中心即焦点; 焦点在包围盒中心上方 center.y 处, 视线穿过
		# 焦点后的落地点比中心偏 center.y·cot(pitch), 把焦点沿视线方向前移该量
		# 即可让画面中心正好落在地面主体中心 (2026-09-26 视觉评审发现旧公式把主体
		# 推到画面底部 1/3)。
		var focus_pos: Vector3 = shot["focus"]
		var zoom: float = shot["zoom"]
		if focus_pos == Vector3.INF:
			var b := _town_bounds(flows)
			var yaw_rad := deg_to_rad(shot["yaw"])
			var pitch_rad := deg_to_rad(shot["pitch"])
			var fov_half := deg_to_rad(get_viewport().get_camera_3d().fov * 0.5)
			# 相机在主体一侧俯视, 画面近侧覆盖的地面长度远小于远侧 (透视非对称),
			# 框幅必须按近侧算: 近侧地面 = H·(cot(p) - cot(p+fov/2)), 否则主体
			# 靠近相机的一半会在下边缘被切掉 (旧 1.25× 因子低估了 ~25%)。
			var pa: float = absf(pitch_rad)
			var near_gnd: float = sin(pa) * (1.0 / tan(pa) - 1.0 / tan(pa + fov_half))
			var far_gnd: float = sin(pa) * (1.0 / tan(maxf(pa - fov_half, 0.02)) - 1.0 / tan(pa))
			zoom = maxf(b["size"].x, b["size"].z) * 0.5 / minf(near_gnd, far_gnd) * 1.05
			# 村镇/聚落的实例散布范围 (道路+田块) 远大于建筑组团: 按包围盒取景会把
			# 村落拍成画面中央的小块 (2026-09-26 复拍: village_overview 97.5% 留白,
			# 建筑组团只占 ~17% 画宽)。收紧臂长让组团成为画面主体, 四周留白仍足。
			if preset in [ "village", "hamlet" ]:
				zoom *= 0.55
			var lead: float = b["center"].y / tan(pa)
			focus_pos = b["center"] + Vector3(
				sin(yaw_rad) * lead, 0.0, cos(yaw_rad) * lead)
			print("bounds %s center=%s size=%s nflows=%d" % [shot["name"], b["center"], b["size"], flows.size()])

		focus.position = focus_pos
		# CameraRigController (C++) 只在输入事件时重设枢轴/臂长, 直接改节点即可。
		yaw_pivot.rotation = Vector3(0, deg_to_rad(shot["yaw"]), 0)
		pitch_pivot.rotation = Vector3(deg_to_rad(shot["pitch"]), 0, 0)
		arm.spring_length = zoom
		# 远景俯拍 (臂长 >200) 拉远雾与景深, 否则鸟瞰图被雾洗白、城镇糊成一片
		# (2026-09-26 视觉评审: all_city 暗部 0%、overview 边缘全雾); 近景保持
		# 默认 300/1500、DOF 220+90 保留空气感。
		_env.fog_depth_begin = maxf(300.0, zoom * 1.4)
		_env.fog_depth_end = maxf(1500.0, zoom * 4.0)
		_dof_attrs.dof_blur_far_distance = maxf(220.0, zoom * 1.5)
		for i in 12:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		if not OS.has_environment("NOSAVE"):
			get_viewport().get_texture().get_image().save_png(
				ShotOutput.file("AncientTown", "%s_%s.png" % [preset, shot["name"]]))
		var cam := get_viewport().get_camera_3d()
		print("shot %s_%s  cam=%.1f,%.1f,%.1f  yaw=%.0f pitch=%.0f zoom=%.0f" % [
			preset, shot["name"], cam.global_position.x, cam.global_position.y, cam.global_position.z,
			shot["yaw"], shot["pitch"], zoom])
