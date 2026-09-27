@tool
extends Node3D

# =============================================================================
# AncientBuilding 「民居样板」验证场景
#
# **打开 `AncientBuildingDwelling.tscn` 即可**：
#   - 编辑器里：直接看到三栋建筑（本脚本是 @tool，视口内即渲染），可选节点、改参数
#   - F6 运行：**右键拖动转视角 + WASD 平移 + QE 升降 + Shift 加速**，Esc 松开鼠标
#
# 与本目录 `AncientBuildingDwelling.gd` 的关系：那个是 `SceneTree` 脚本，只能命令行
# `--script` 跑、用来批量出图；本文件是**普通节点脚本 + .tscn**，供人工查看。
# 两者用同一套光照与相机数值（05 契约 §4.4）。
# -----------------------------------------------------------------------------

const ENV_BG := Color(0.12, 0.15, 0.19)
const ENV_AMBIENT := Color(0.85, 0.87, 0.92)
const ENV_AMBIENT_ENERGY := 0.4
const SUN_ROT := Vector3(-25, 15, 0)
const SUN_ENERGY := 1.2
const FILL_ROT := Vector3(38, -95, 0)
const FILL_ENERGY := 0.55
const FLOOR_SIZE := Vector2(120, 120)
const FLOOR_ALBEDO := Color(0.32, 0.34, 0.36)
const FOV := 42.0

## M4 民居整栋机位（05 §4.4 新增；视角 45° 同时看到山面与正立面，仰角 ≈12.5°）。
const CAM_M4 := {"pos": Vector3(21, 12, 21), "look": Vector3(0, 5.4, 0)}

# --- 飞行相机（仅运行模式生效；编辑器里用编辑器自己的视角）---
const MOVE_SPEED := 12.0        ## m/s
const MOVE_SPEED_FAST := 45.0   ## 按住 Shift
const LOOK_SENSITIVITY := 0.15  ## 度 / 像素

# ---------------------------------------------------------------------------
# 「民居样板」形制 —— **本场景的唯一开关**
#
# 值取自手册 `20_墙体 R15` / `60_台基地面 R6·R9·R12`。除 `column_base_height_scale`
# 外全部是 新增参数，默认关/0；本块显式打开。
# **把整块清空（改成 `{}`）就是"旧行为"对照。**
# ---------------------------------------------------------------------------
const DWELLING_STYLE := {
	"ridge_detail": 1,              # 分层圆脊与连续山尖接头
	"dado_height_ratio": 0.25,      # 下碱高 ÷ 墙高（0 = 旧行为）
	"dado_top_trim": 1.0,           # 分界带高，单位 = 模数 D（0 = 关）
	"platform_top_joints": true,    # 台基顶面接缝网格
	"platform_edge_lip": true,      # 台基边缘凸出沿口
	"paving": true,                 # 方砖铺地
	"paving_joint_geometry": true,  # 铺地几何板缝（关 = 顶点色缝）
	"step_side_cheek": true,        # 踏步两侧侧挡
	"column_base_square": true,     # 方形石础（关 = 旧车削圆础盘）
	"column_base_height_scale": 0.35,
}

## 三栋一排，间距 18 m：硬山（民居主对象）/ 悬山（看山面）/ 卷棚（滚脊）。
const SUBJECTS := [
	{"key": "flush", "x": -18.0, "roof_type": 0, "note": "硬山 民居"},
	{"key": "overhang", "x": 0.0, "roof_type": 3, "note": "悬山"},
	{"key": "rolled", "x": 18.0, "roof_type": 4, "note": "卷棚"},
]

var _generated: Node3D = null
var _yaw := 0.0
var _pitch := 0.0
var _looking := false


func _ready() -> void:
	# 延迟一帧再建：编辑器加载场景时自身正在装配子节点，此时直接 add_child 会触发
	# "Parent node is busy setting up children"。运行模式下同样安全。
	call_deferred("rebuild")


## 重新生成全部内容。编辑器里改完 `DWELLING_STYLE` / `SUBJECTS` 后，
## 在检查器右上角菜单里选「重新加载脚本」或直接关掉再打开场景即可。
func rebuild() -> void:
	if _generated != null and is_instance_valid(_generated):
		remove_child(_generated)
		_generated.free()
	_generated = Node3D.new()
	_generated.name = "Generated"
	add_child(_generated)

	_build_environment()
	_build_buildings()
	_build_camera()


func _build_environment() -> void:
	var env := WorldEnvironment.new()
	env.name = "WorldEnvironment"
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = ENV_BG
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = ENV_AMBIENT
	env.environment.ambient_light_energy = ENV_AMBIENT_ENERGY
	_generated.add_child(env)

	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	sun.rotation_degrees = SUN_ROT
	sun.light_energy = SUN_ENERGY
	sun.shadow_enabled = true
	_generated.add_child(sun)

	var fill := DirectionalLight3D.new()
	fill.name = "Fill"
	fill.rotation_degrees = FILL_ROT
	fill.light_energy = FILL_ENERGY
	fill.shadow_enabled = false
	_generated.add_child(fill)

	var floor_node := MeshInstance3D.new()
	floor_node.name = "Ground"
	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = FLOOR_SIZE
	var floor_mat := StandardMaterial3D.new()
	floor_mat.albedo_color = FLOOR_ALBEDO
	floor_mesh.material = floor_mat
	floor_node.mesh = floor_mesh
	_generated.add_child(floor_node)


func _build_buildings() -> void:
	if not ClassDB.class_exists("AncientBuilding"):
		push_error("AncientBuilding 不可用：`abyss` GDExtension 没加载。")
		return

	for subject in SUBJECTS:
		var p = ClassDB.instantiate("AncientBuildingParameters")
		p.width = 9.0
		p.depth = 6.0
		p.bays_x = 3
		p.bays_z = 2
		p.roof_type = subject["roof_type"]
		p.generate_walls = true
		p.generate_fence = false
		p.generate_steps = true
		p.rafter_courses = 5
		p.eave_rafter_style = 2      # 檐椽头 + 飞椽头 + 连檐
		p.material_style = 0         # 官式配色
		for prop in DWELLING_STYLE:
			# 逐项守卫：这些属性由 新增，老 DLL 上不存在 —— 缺失就跳过而不是崩。
			if prop in p:
				p.set(prop, DWELLING_STYLE[prop])

		var building = ClassDB.instantiate("AncientBuilding")
		building.name = "Dwelling_%s" % subject["key"]
		building.parameters = p
		building.position = Vector3(subject["x"], 0.0, 0.0)
		_generated.add_child(building)
		if not Engine.is_editor_hint():
			print("%-9s %-10s roof_type=%d vertices=%d triangles=%d" % [
				subject["key"], subject["note"], subject["roof_type"],
				building.get_vertex_count(), building.get_triangle_count()])


func _build_camera() -> void:
	var cam := Camera3D.new()
	cam.name = "Camera3D"
	cam.fov = FOV
	cam.position = CAM_M4["pos"]
	_generated.add_child(cam)
	cam.look_at(CAM_M4["look"])
	cam.current = true
	# 从相机朝向初始化 yaw/pitch，飞行相机的第一次拖动不会跳。
	var rot := cam.rotation
	_yaw = rad_to_deg(rot.y)
	_pitch = rad_to_deg(rot.x)


# --- 飞行相机（仅运行时）------------------------------------------------------

func _unhandled_input(event: InputEvent) -> void:
	if Engine.is_editor_hint():
		return
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT:
		_looking = event.pressed
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if _looking else Input.MOUSE_MODE_VISIBLE
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and _looking:
		_yaw -= event.relative.x * LOOK_SENSITIVITY
		_pitch = clampf(_pitch - event.relative.y * LOOK_SENSITIVITY, -89.0, 89.0)
	elif event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		_looking = false
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _process(delta: float) -> void:
	if Engine.is_editor_hint():
		return
	var cam := get_viewport().get_camera_3d()
	if cam == null:
		return
	cam.rotation_degrees = Vector3(_pitch, _yaw, 0.0)

	var dir := Vector3.ZERO
	if Input.is_key_pressed(KEY_W): dir -= cam.global_transform.basis.z
	if Input.is_key_pressed(KEY_S): dir += cam.global_transform.basis.z
	if Input.is_key_pressed(KEY_A): dir -= cam.global_transform.basis.x
	if Input.is_key_pressed(KEY_D): dir += cam.global_transform.basis.x
	if Input.is_key_pressed(KEY_E): dir += Vector3.UP
	if Input.is_key_pressed(KEY_Q): dir -= Vector3.UP
	if dir != Vector3.ZERO:
		var speed := MOVE_SPEED_FAST if Input.is_key_pressed(KEY_SHIFT) else MOVE_SPEED
		cam.position += dir.normalized() * speed * delta
