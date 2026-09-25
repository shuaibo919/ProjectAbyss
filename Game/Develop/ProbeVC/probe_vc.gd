extends Node3D

# 顶点色渲染矩阵探针: 每种变体一块 4x4 板, 俯拍相机逐块读回中心像素, 打印 RGB。
# 目的: 精确定位"道路顶点色丢失"发生在哪一层 (网格 COLOR / MMI use_colors /
# 实例色 / 材质类型), 不依赖截图与屏幕映射。
#
# 运行: godot --path Game res://Develop/ProbeVC/probe_vc.tscn

const TownMeshes := preload("res://addons/ancient_town/town_meshes.gd")
const PropMeshes := preload("res://Script/PCG/pcg_prop_meshes.gd")
const SHADER_DIR := "res://Assets/Shaders/InkPainting"
const TEX_DIR := "res://Assets/Shaders/InkPainting/Textures"

var _camera: Camera3D
var _plates: Array[Dictionary] = []


func _make_ink() -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = load(SHADER_DIR + "/ink_surface.gdshader")
	m.set_shader_parameter("base_color", Color(1, 1, 1))
	m.set_shader_parameter("use_vertex_color", true)
	m.set_shader_parameter("color_remap_tex", load(TEX_DIR + "/ColorRemap.png"))
	m.set_shader_parameter("noise_tex", load(TEX_DIR + "/Noise.png"))
	m.set_shader_parameter("stroke_tex", load(TEX_DIR + "/Stroke.png"))
	return m


func _add_plate(tag: String, x: float, mesh: Mesh, mat: Material, mmi_opts: Dictionary) -> void:
	var node: Node3D
	if mmi_opts.is_empty():
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		mi.material_override = mat
		node = mi
	else:
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = mesh
		mm.use_colors = mmi_opts.get("use_colors", false)
		mm.instance_count = 1
		var scale: Vector3 = mmi_opts.get("scale", Vector3(4, 0.2, 4))
		mm.set_instance_transform(0, Transform3D(Basis().scaled(scale), Vector3(x, 0.5, 0)))
		if mm.use_colors:
			mm.set_instance_color(0, mmi_opts.get("inst_color", Color.WHITE))
		var pm := MultiMeshInstance3D.new()
		pm.multimesh = mm
		pm.material_override = mat
		node = pm
	node.name = tag
	add_child(node)
	_plates.append({ "tag": tag, "x": x })


func _ready() -> void:
	# 环境: 暗背景 + 无阴影太阳, 排除阴影/雾干扰。
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.2, 0.2, 0.25)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(1, 1, 1)
	e.ambient_light_energy = 0.15
	env.environment = e
	add_child(env)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, -30, 0)
	sun.light_energy = 1.0
	sun.shadow_enabled = false
	add_child(sun)

	# 暗色地面盘 (接收板放上面)。
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(120, 20)
	var gm := StandardMaterial3D.new()
	gm.albedo_color = Color(0.35, 0.35, 0.35)
	gm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ground.mesh = plane
	ground.material_override = gm
	add_child(ground)

	var road_mesh := TownMeshes.road()      # 顶点色 = 品红 (当前 ROAD_COL)
	var tree_mesh: Mesh = PropMeshes.conifer()
	var ink := _make_ink()

	var std_vc := StandardMaterial3D.new()
	std_vc.vertex_color_use_as_albedo = true

	# 板: 4x4 米, y=0.5, x 依次排开。
	# V1 普通 MeshInstance3D + ink          -> 网格 COLOR 是否直达 ink?
	_add_plate("V1_mi_ink", -16.0, road_mesh, ink, {})
	# V2 MMI uc=true 白实例色 + ink          -> MMI+ink 全链路 (生产道路等价配置)
	_add_plate("V2_mmi_uc_ink", -12.0, road_mesh, ink, { "use_colors": true, "inst_color": Color.WHITE })
	# V3 MMI uc=true 品红实例色 + ink        -> 实例色是否参与 (乘 or 替)
	_add_plate("V3_mmi_uc_mag", -8.0, road_mesh, ink, { "use_colors": true, "inst_color": Color(1, 0, 1) })
	# V4 MMI uc=false + ink                 -> 无实例色时网格 COLOR 是否保留 (probe2 P2 为白)
	_add_plate("V4_mmi_nouc_ink", -4.0, road_mesh, ink, { "use_colors": false })
	# V5 MMI uc=true 白实例色 + StdMat VC    -> 不用 ink, 标准材质下实例色行为
	_add_plate("V5_mmi_uc_std", 0.0, road_mesh, std_vc, { "use_colors": true, "inst_color": Color.WHITE })
	# V6 MMI uc=false + StdMat VC           -> 标准材质纯网格色基线
	_add_plate("V6_mmi_nouc_std", 4.0, road_mesh, std_vc, { "use_colors": false })
	# V7 大非均匀缩放 MMI uc=true 白实例色 + ink (生产道路尺寸比例: 20x0.2x4)
	_add_plate("V7_mmi_big_ink", 8.0, road_mesh, ink, { "use_colors": true, "inst_color": Color.WHITE, "scale": Vector3(20, 0.2, 4) })
	# V8 树网格 MMI uc=true 白实例色 + ink    -> 对照: 树在生产里是绿的
	_add_plate("V8_mmi_tree_ink", 14.0, tree_mesh, ink, { "use_colors": true, "inst_color": Color.WHITE })

	# V9-V11: 单 MMI 多实例不同 yaw (生产道路 MMI n=12 混 yaw 的场景)。
	# 若 yaw=90 实例白而 yaw=0 实例品红 -> fork MMI 按实例 yaw 丢网格顶点色。
	var v9 := MultiMesh.new()
	v9.transform_format = MultiMesh.TRANSFORM_3D
	v9.mesh = road_mesh
	v9.use_colors = true
	v9.instance_count = 3
	var vy := [
		{ "x": -40.0, "yaw": 0.0 },
		{ "x": -36.0, "yaw": 90.0 },
		{ "x": -32.0, "yaw": 180.0 },
	]
	for i in 3:
		v9.set_instance_transform(i, Transform3D(
			Basis.from_euler(Vector3(0, deg_to_rad(vy[i]["yaw"]), 0)).scaled(Vector3(4, 0.2, 4)),
			Vector3(vy[i]["x"], 0.5, 0)))
		v9.set_instance_color(i, Color.WHITE)
	var p9 := MultiMeshInstance3D.new()
	p9.name = "V9_mixyaw"
	p9.multimesh = v9
	p9.material_override = ink
	add_child(p9)
	for i in 3:
		_plates.append({ "tag": "V9_yaw%d" % int(vy[i]["yaw"]), "x": vy[i]["x"] })

	_camera = Camera3D.new()
	_camera.current = true
	_camera.fov = 90.0
	_camera.position = Vector3(0, 60, 0)
	_camera.rotation_degrees = Vector3(-90, 0, 0)
	add_child(_camera)

	await RenderingServer.frame_post_draw
	await get_tree().process_frame

	var img: Image = get_viewport().get_texture().get_image()
	for p in _plates:
		var px: Vector2 = _camera.unproject_position(Vector3(p.x, 0.6, 0))
		var c: Color = img.get_pixelv(px)
		print("PROBE %-16s px=%s rgb=(%d,%d,%d)" % [
			p.tag, px.round(), int(c.r * 255), int(c.g * 255), int(c.b * 255)])
	get_tree().quit()
