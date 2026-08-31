extends Node3D

# 视觉验证关卡: AncientBuilding PCG + 程序化树 + 水墨着色 + 现有高俯角相机 rig。
#
# 目的是把三条独立管线第一次放到同一个画面里看：
#   - 建筑走 PCG 图（ancient_building Flow 节点 → spawn_meshes）
#   - 树直接摆 ProceduralTree 节点（Tree 的 Flow 节点尚未做，见任务 #37）
#   - 两者统一套 InkPainting 三件套材质
#   - 相机用 Source/CameraManager 的 CameraRigController（HD-2D 高俯角）
#
# 运行: Engine/bin/godot.windows.editor.x86_64.console.exe --path Game/ res://Map/Map_InkValidation.tscn
#   SHOTS=1  只出图然后退出（供无人验证用）
#
# 操作: WASD 移动, 右键拖拽环绕, 滚轮缩放, Q/E 偏摆, R/F 俯仰
#   —— 动作由 InputActions 自动加载注册（res://Script/input_actions.gd）。

const FlowGraphBuilder := preload("res://Script/PCG/flow_graph_builder.gd")
const ShotOutput := preload("res://Develop/Tools/shot_output.gd")

const NODE_DIR := "res://addons/ancient_building/nodes"
const SHADER_DIR := "res://Assets/Shaders/InkPainting"
const TEX_DIR := "res://Assets/Shaders/InkPainting/Textures"

# 纸色。水墨的“白”不是纯白，压一点暖灰才不刺眼。
const PAPER := Color(0.898, 0.859, 0.824)

var _ink_material: ShaderMaterial


func _ready() -> void:
	_build_environment()
	_ink_material = _make_ink_material()

	_build_ground()
	_build_village()
	_build_trees()

	var focus := Vector3(0.0, 0.0, 0.0)
	_build_camera_rig(focus)

	if OS.has_environment("SHOTS"):
		await _shoot()
		get_tree().quit()


# ---------------------------------------------------------------- 环境

func _build_environment() -> void:
	# 水墨要平光: 主光压低、环境光提高, 免得暗部被 PBR 阴影吃掉。皴/擦/染的层次由
	# ink_surface 自己按 NdotL 生成, 不靠光照对比度。
	var light := DirectionalLight3D.new()
	light.name = "SunLight"
	light.rotation_degrees = Vector3(-42, -38, 0)
	light.light_energy = 1.1
	light.shadow_enabled = true
	add_child(light)

	var env := WorldEnvironment.new()
	env.name = "InkEnvironment"
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = PAPER
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.86, 0.84, 0.82)
	e.ambient_light_energy = 0.9
	# 远处淡出到纸色 = 留白, 这是水墨里“远”的表达方式。
	e.fog_enabled = true
	e.fog_mode = Environment.FOG_MODE_DEPTH
	e.fog_light_color = PAPER
	e.fog_depth_begin = 95.0
	e.fog_depth_end = 320.0
	e.fog_density = 0.5
	env.environment = e
	add_child(env)


# ---------------------------------------------------------------- 材质

func _make_ink_material() -> ShaderMaterial:
	# 三件套是**反向壳描边**（两个 outline shader 都是 cull_front + unshaded），所以必须靠
	# next_pass 串成一条链: 表面 → 主描边 → 副描边。不是后处理，材质本身要挂三遍。
	#
	# 四张贴图**必须显式赋值**。这几个 shader 的笔触全靠采样贴图: 不给的话
	# color_remap_tex 默认黑 → remap 恒为黑, stroke_tex 默认白 → alpha 恒 1 → 皴/擦因子恒为 0,
	# 于是整个 light() 退化成平淡的 lambert, 一点墨都出不来。项目里没有任何 .tres 用过这三个
	# shader, 所以没有现成配置可抄, 默认值是不能用的。
	var remap: Texture2D = load(TEX_DIR + "/ColorRemap.png")
	var noise: Texture2D = load(TEX_DIR + "/Noise.png")
	var stroke: Texture2D = load(TEX_DIR + "/Stroke.png")
	var matcap: Texture2D = load(TEX_DIR + "/MatCap.png")

	var surface := ShaderMaterial.new()
	surface.shader = load(SHADER_DIR + "/ink_surface.gdshader")
	surface.set_shader_parameter("base_color", Color(1, 1, 1))   # 颜色全部来自顶点色
	surface.set_shader_parameter("use_vertex_color", true)
	surface.set_shader_parameter("color_remap_tex", remap)
	surface.set_shader_parameter("noise_tex", noise)
	surface.set_shader_parameter("stroke_tex", stroke)

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
	# GeometryInstance3D.material_override 会盖掉网格自带材质。我们的几何色全在顶点色里,
	# 所以丢掉原 StandardMaterial3D 没有信息损失 —— 这正是单 surface 顶点色改造的回报。
	var count := 0
	if node is GeometryInstance3D:
		node.material_override = _ink_material
		count += 1
	for child in node.get_children():
		count += _apply_ink(child)

	return count


# ---------------------------------------------------------------- 地面

func _build_ground() -> void:
	var ground := MeshInstance3D.new()
	ground.name = "Ground"
	var plane := PlaneMesh.new()
	plane.size = Vector2(400, 400)
	plane.subdivide_width = 8
	plane.subdivide_depth = 8
	ground.mesh = plane
	# 地面单独一份材质: 它没有顶点色, 靠 base_color 给纸面一点冷灰, 和建筑区分开。
	var ground_mat := ShaderMaterial.new()
	ground_mat.shader = load(SHADER_DIR + "/ink_surface.gdshader")
	ground_mat.set_shader_parameter("base_color", Color(0.82, 0.81, 0.78))
	ground_mat.set_shader_parameter("use_vertex_color", false)
	ground_mat.set_shader_parameter("color_remap_tex", load(TEX_DIR + "/ColorRemap.png"))
	ground_mat.set_shader_parameter("noise_tex", load(TEX_DIR + "/Noise.png"))
	ground_mat.set_shader_parameter("stroke_tex", load(TEX_DIR + "/Stroke.png"))
	ground.material_override = ground_mat
	add_child(ground)


# ---------------------------------------------------------------- 建筑 (PCG)

func _build_village() -> void:
	# ancient_building 插件未在 project.godot 里启用, 但它的 class_name 脚本仍然可用 ——
	# 只需要显式注册节点目录。这是 FlowNodeRegistry.register_node_directory 的第一个消费者。
	FlowNodeRegistry.register_node_directory(NODE_DIR)

	var flow := FlowGraphNode3D.new()
	flow.name = "VillagePCG"
	flow.graph = _village_graph()
	add_child(flow)
	flow.execute()

	var applied := _apply_ink(flow)
	print("village: %d geometry instances inked" % applied)


func _village_graph() -> FlowGraphResource:
	var builder := FlowGraphBuilder.new()

	# 2x3 的宅院, 间距拉开到 30 免得屋檐互相穿插; 随机偏摆让它不像棋盘。
	var grid := builder.AddNode("grid", {
		"x": 3, "y": 1, "z": 2,
		"step": Vector3(30, 0, 34),
	})
	var jitter := builder.AddNode("transform", {
		"rotation_min": Vector3(0, -18, 0),
		"rotation_max": Vector3(0, 18, 0),
	})
	var halls := builder.AddNode("ancient_building", {
		"mesh_attribute": "mesh",
		"variant_count": 4,
		"randomize_roof_type": true,
		"width": 11.0,
		"depth": 7.0,
		"seed": 11,
	})
	var spawn := builder.AddNode("spawn_meshes", {
		"mesh_attribute": "mesh",
	})

	builder.Connect(grid, 0, jitter, 0)
	builder.Connect(jitter, 0, halls, 0)
	builder.Connect(halls, 0, spawn, 0)

	return builder.Build()


# ---------------------------------------------------------------- 树 (直接摆)

func _build_trees() -> void:
	# Tree 还没有 Flow 节点（任务 #37）, 所以这里直接实例 ProceduralTree。
	# 桃放主位: 花卡是大而正对镜头的面, 最能看出描边和顶点色是否生效。
	var plan := [
		{ "preset": "Peach", "pos": Vector3(-6, 0, 16), "season": 1.0 },
		{ "preset": "Peach", "pos": Vector3(9, 0, 21), "season": 1.2 },
		{ "preset": "Willow", "pos": Vector3(-28, 0, 8), "season": 2.0 },
		{ "preset": "Ginkgo", "pos": Vector3(26, 0, -4), "season": 3.4 },
		{ "preset": "Pine", "pos": Vector3(-34, 0, -26), "season": 2.0 },
		{ "preset": "Pine", "pos": Vector3(-22, 0, -34), "season": 2.0 },
		{ "preset": "Bamboo", "pos": Vector3(34, 0, 24), "season": 2.0 },
		{ "preset": "Bamboo", "pos": Vector3(37, 0, 28), "season": 2.0 },
		{ "preset": "Bamboo", "pos": Vector3(31, 0, 29), "season": 2.0 },
		{ "preset": "Metasequoia", "pos": Vector3(16, 0, -30), "season": 2.0 },
	]

	var group := Node3D.new()
	group.name = "Trees"
	add_child(group)

	var total_tris := 0
	for i in plan.size():
		var entry: Dictionary = plan[i]
		var idx := _preset_index(entry["preset"])
		if idx < 0:
			push_warning("unknown SlowTree preset: %s" % entry["preset"])
			continue

		var tree = ClassDB.instantiate("ProceduralTree")
		tree.name = "%s_%d" % [entry["preset"], i]
		tree.auto_regenerate = false
		tree.backend = 1               # BACKEND_SLOWTREE
		tree.slowtree_preset = idx
		tree.seed = 100 + i * 7
		tree.season = entry["season"]
		group.add_child(tree)
		tree.position = entry["pos"]
		tree.generate()
		total_tris += tree.get_triangle_count()

	var applied := _apply_ink(group)
	print("trees: %d placed, %d inked, %d tris total" % [plan.size(), applied, total_tris])


func _preset_index(preset_name: String) -> int:
	for i in SlowTreeGenerator.get_preset_count():
		if SlowTreeGenerator.get_preset_name(i) == preset_name:
			return i

	return -1


# ---------------------------------------------------------------- 相机

func _build_camera_rig(focus: Vector3) -> void:
	# CameraRigController 要的层级是 Player → YawPivot → PitchPivot → SpringArm3D → Camera3D,
	# 四条 NodePath 都要显式给。它不依赖 CameraManager 单例（那个没进 autoload）。
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
	# 地面是 400x400 的板, 弹簧臂若做碰撞检测会被地面顶住, 这里只当固定臂用。
	arm.collision_mask = 0
	pitch.add_child(arm)

	var camera := Camera3D.new()
	camera.name = "Camera3D"
	camera.current = true
	camera.fov = 38.0
	camera.far = 400.0
	# 高俯角 + 长焦 + 浅景深 = HD-2D / 伪移轴的观感。景深是这套 rig 的关键一半。
	var attrs := CameraAttributesPractical.new()
	attrs.dof_blur_far_enabled = true
	attrs.dof_blur_far_distance = 70.0
	attrs.dof_blur_far_transition = 40.0
	attrs.dof_blur_near_enabled = true
	attrs.dof_blur_near_distance = 22.0
	attrs.dof_blur_near_transition = 14.0
	attrs.dof_blur_amount = 0.06
	camera.attributes = attrs
	arm.add_child(camera)

	var rig = ClassDB.instantiate("CameraRigController")
	rig.name = "CameraRig"
	# 所有属性必须在 add_child **之前**设好: _ready() 在入树时就跑, 会用当时的
	# default_pitch / zoom_default 初始化 CurrentPitch / CurrentZoom, 之后再改属性不会重新应用。
	# 之前设在后面, 结果 rig 用默认的 zoom 12 / pitch -55 把相机贴到地面上, 整帧全是纸色。
	# NodePath 写成相对路径而不是 get_path_to(), 因为后者要求两个节点都已在树内。
	rig.player_path = NodePath("../CameraFocus")
	rig.yaw_pivot_path = NodePath("../CameraFocus/YawPivot")
	rig.pitch_pivot_path = NodePath("../CameraFocus/YawPivot/PitchPivot")
	rig.spring_arm_path = NodePath("../CameraFocus/YawPivot/PitchPivot/SpringArm")
	rig.default_pitch = -38.0
	rig.zoom_default = 62.0
	rig.zoom_min = 18.0
	rig.zoom_max = 140.0
	add_child(rig)


# ---------------------------------------------------------------- 出图

func _shoot() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X

	var focus: Node3D = get_node("CameraFocus")
	var rig := get_node("CameraRig")

	# 相机由 rig 每帧接管, 直接搬 Camera3D 会被立刻改回去。所以移**焦点节点**并调 rig 的
	# zoom, 让 rig 自己把相机摆过去 —— 这样截图顺带验证了 rig 本身。
	var shots := [
		{ "name": "ink_overview", "focus": Vector3(0, 2, 0), "zoom": 78.0 },
		{ "name": "ink_peach", "focus": Vector3(-6, 3, 16), "zoom": 22.0 },
		{ "name": "ink_hall", "focus": Vector3(-30, 4, -17), "zoom": 30.0 },
	]
	for shot in shots:
		focus.position = shot["focus"]
		rig.zoom_default = shot["zoom"]
		rig.set("current_zoom", shot["zoom"]) if rig.has_method("set") else null
		for i in 12:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(
			ShotOutput.file("Ink", "%s.png" % shot["name"]))
		var cam := get_viewport().get_camera_3d()
		print("shot %s  cam=%.1f,%.1f,%.1f" % [shot["name"],
			cam.global_position.x, cam.global_position.y, cam.global_position.z])
