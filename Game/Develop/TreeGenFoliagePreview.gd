extends Node3D
## Interactive single-tree preview: run this scene, drag the controls, orbit with right mouse.

@export_enum("HelloTree", "Willow", "Pine", "Ginkgo", "Bamboo", "Metasequoia", "Peach") var initial_preset := 1

var tree: MeshInstance3D
var camera: Camera3D
var stats: Label
var yaw := 0.6
var pitch := 0.20
var focus := Vector3(0, 3.8, 0)
var distance := 17.0
var frame_requested := true
var bamboo_controls: FoldableContainer
var peach_controls: FoldableContainer
var season_slider: HSlider

func _ready() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_4X
	var world := WorldEnvironment.new()
	world.environment = Environment.new()
	world.environment.background_mode = Environment.BG_COLOR
	world.environment.background_color = Color("b7c4ce")
	world.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	world.environment.ambient_light_color = Color("c9d4e0")
	world.environment.ambient_light_energy = 0.40
	add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-48, -30, 0)
	light.light_energy = 0.95
	light.shadow_enabled = true
	add_child(light)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(200, 200)
	ground.mesh = plane
	var ground_material := StandardMaterial3D.new()
	ground_material.albedo_color = Color("7c8774")
	ground.material_override = ground_material
	add_child(ground)
	tree = ClassDB.instantiate("ProceduralTree")
	tree.auto_regenerate = false
	tree.backend = 1
	tree.slowtree_preset = 6 if "--peach" in OS.get_cmdline_user_args() else (4 if "--bamboo" in OS.get_cmdline_user_args() else initial_preset)
	tree.season = 1.0 if tree.slowtree_preset == 6 else 2.0
	tree.growth_parameters = ProceduralTreeGrowthParameters.new()
	tree.generation_completed.connect(frame_generated_tree)
	tree.name = "PreviewTree"
	add_child(tree)
	tree.auto_regenerate = true
	camera = Camera3D.new()
	camera.fov = 42
	add_child(camera)
	camera.current = true
	update_camera()
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var panel := PanelContainer.new()
	panel.position = Vector2(16, 16)
	panel.custom_minimum_size.x = 285
	canvas.add_child(panel)
	var resize_panel := func(): panel.size.y = get_viewport().get_visible_rect().size.y - 32
	get_viewport().size_changed.connect(resize_panel)
	resize_panel.call()
	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 12)
	panel.add_child(margin)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	margin.add_child(scroll)
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_theme_constant_override("separation", 8)
	scroll.add_child(box)
	var title := Label.new()
	title.text = "TreeGen · 实时植被预览"
	box.add_child(title)
	var species := OptionButton.new()
	for preset in range(7):
		species.add_item(SlowTreeGenerator.get_preset_name(preset), preset)
	species.select(tree.slowtree_preset)
	species.item_selected.connect(func(index):
		frame_requested = true
		tree.slowtree_preset = species.get_item_id(index)
		bamboo_controls.visible = tree.slowtree_preset == 4
		peach_controls.visible = tree.slowtree_preset == 6
		if tree.slowtree_preset == 6:
			season_slider.value = 1.0)
	box.add_child(species)
	add_slider(box, "枝条密度", "branch_density", 0.3, 2.0, 1.0)
	add_slider(box, "叶簇密度", "leaf_density", 0.02, 1.0, 1.0)
	season_slider = add_slider(box, "季节 · 0 冬 / 1 春 / 2 夏 / 3 秋", "season", 0.0, 4.0, tree.season)
	add_slider(box, "叶簇风动", "wind_strength", 0.0, 5.0, 0.0)
	var new_seed := Button.new()
	new_seed.text = "下一随机种子"
	new_seed.pressed.connect(func(): tree.seed += 1)
	box.add_child(new_seed)
	var frame_view := Button.new()
	frame_view.text = "适配整株视图"
	frame_view.pressed.connect(func():
		frame_requested = true
		frame_generated_tree())
	box.add_child(frame_view)
	var foliage := CheckBox.new()
	foliage.text = "十字叶簇 + Alpha Mask"
	foliage.button_pressed = true
	foliage.toggled.connect(func(value): tree.foliage_mode = 1 if value else 0)
	box.add_child(foliage)
	var rules := CheckBox.new()
	rules.text = "树种枝杈规则"
	rules.button_pressed = true
	rules.toggled.connect(func(value): tree.species_rules = value)
	box.add_child(rules)
	var bare := CheckBox.new()
	bare.text = "无叶枝架"
	bare.toggled.connect(func(value): tree.generate_leaves = not value)
	box.add_child(bare)
	var structure := CheckBox.new()
	structure.text = "连续枝架 · 全部 SlowTree 预设"
	structure.button_pressed = true
	structure.toggled.connect(func(value): tree.structural_branches = value)
	box.add_child(structure)
	bamboo_controls = FoldableContainer.new()
	bamboo_controls.title = "竹子精修参数"
	bamboo_controls.folded = true
	bamboo_controls.visible = tree.slowtree_preset == 4
	box.add_child(bamboo_controls)
	var bamboo_box := VBoxContainer.new()
	bamboo_controls.add_child(bamboo_box)
	add_slider(bamboo_box, "平均节间长度（米）", "bamboo_internode_length", 0.15, 0.60, 0.28, tree.growth_parameters)
	add_slider(bamboo_box, "竹节起伏", "bamboo_node_definition", 0.0, 2.0, 1.0, tree.growth_parameters)
	add_slider(bamboo_box, "竹叶尺寸", "bamboo_leaf_scale", 0.5, 2.0, 1.0, tree.growth_parameters)
	peach_controls = FoldableContainer.new()
	peach_controls.title = "桃树 · 花枝与花量"
	peach_controls.folded = true
	peach_controls.visible = tree.slowtree_preset == 6
	box.add_child(peach_controls)
	var peach_box := VBoxContainer.new()
	peach_controls.add_child(peach_box)
	add_slider(peach_box, "花枝密度", "peach_twig_density", 0.25, 2.0, 1.0, tree.growth_parameters)
	add_slider(peach_box, "花量", "peach_blossom_density", 0.0, 3.0, 1.0, tree.growth_parameters)
	add_slider(peach_box, "花簇大小", "peach_blossom_scale", 0.5, 1.8, 1.0, tree.growth_parameters)
	stats = Label.new()
	box.add_child(stats)
	var help := Label.new()
	help.text = "鼠标右键环绕 · 滚轮缩放\n拖动时保留上一版，后台完成后更新"
	box.add_child(help)
	frame_generated_tree()

func add_slider(box: VBoxContainer, text: String, property: String, low: float, high: float, value: float, target: Object = null) -> HSlider:
	var label := Label.new()
	label.text = "%s · %.2f" % [text, value]
	box.add_child(label)
	var slider := HSlider.new()
	slider.min_value = low
	slider.max_value = high
	slider.step = 0.01
	slider.value = value
	var controlled: Object = target if target else tree
	slider.value_changed.connect(func(new_value):
		controlled.set(property, new_value)
		label.text = "%s · %.2f" % [text, new_value])
	box.add_child(slider)
	return slider

func _process(_delta: float) -> void:
	if stats:
		stats.text = "%d 三角形 · %d 叶簇\n生成 %.1f ms · 主线程提交 %.1f ms\n%s" % [
			tree.get_triangle_count(), tree.get_leaf_count(), tree.get_generation_ms(), tree.get_commit_ms(),
			"正在更新…" if tree.is_preview_pending() else ("触及枝段预算" if tree.was_truncated() else "预览就绪")]

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		yaw -= event.relative.x * 0.006
		pitch = clampf(pitch + event.relative.y * 0.004, -0.15, 1.25)
		update_camera()
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			distance = maxf(2.0, distance * 0.9)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			distance = minf(50.0, distance * 1.1)
		update_camera()

func update_camera() -> void:
	camera.position = focus + Vector3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch)) * distance
	camera.look_at(focus)

func frame_generated_tree() -> void:
	if not frame_requested or camera == null or tree.mesh == null:
		return
	var bounds := tree.mesh.get_aabb()
	focus = bounds.get_center()
	distance = maxf(2.0, bounds.size.length() * 0.56 / sin(deg_to_rad(camera.fov * 0.5)))
	frame_requested = false
	update_camera()
