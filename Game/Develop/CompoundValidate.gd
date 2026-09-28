extends Node3D

# 连体 visual check: the five AncientBuildingCompound presets — 抱厦, 勾连搭, 十字脊, 工字, 曲尺.
# Numeric checks: tests/ancient_building_compound_test.gd.
# Run: godot --path Game/ res://Develop/CompoundValidate.tscn   (renders, so not --headless)
# Shots land in Reference/Shots/Compound/. `-- --cull` adds a CULL_DISABLED pass per shot.

const ShotOutput := preload("res://Develop/Tools/shot_output.gd")

const NAMES := ["baosha", "goulianda", "shiziji", "gongzi", "quchi"]
const SPACING := 42.0


func _ready() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X
	var cull := "--cull" in OS.get_cmdline_user_args()

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-38, -35, 0)
	sun.light_energy = 0.85
	sun.shadow_enabled = true
	add_child(sun)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.86, 0.85, 0.82)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.9, 0.88, 0.85)
	env.environment.ambient_light_energy = 0.45
	add_child(env)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(600, 600)
	ground.mesh = plane
	add_child(ground)

	var compounds := []
	for i in NAMES.size():
		var c = ClassDB.instantiate("AncientBuildingCompound")
		c.name = NAMES[i]
		c.auto_regenerate = false
		c.apply_preset(i)
		c.position = Vector3((i - (NAMES.size() - 1) * 0.5) * SPACING, 0, 0)
		add_child(c)
		c.generate()
		print("%s: %s" % [NAMES[i], c.get_report()])
		compounds.append(c)

	var cam := Camera3D.new()
	cam.fov = 40.0
	cam.far = 1500.0
	add_child(cam)
	cam.current = true
	var shots := [["overview", Vector3(0, 80, 170), Vector3(0, 6, 0)]]
	for c in compounds:
		var o: Vector3 = c.position
		shots.append([c.name + "_aerial", o + Vector3(20, 26, 30), o + Vector3(0, 6, 0)])
		shots.append([c.name + "_back", o + Vector3(-22, 18, -28), o + Vector3(0, 6, 0)])
		shots.append([c.name + "_eye", o + Vector3(14, 3.5, 22), o + Vector3(0, 7, 0)])

	for pass_cull in ([false, true] if cull else [false]):
		if pass_cull:
			var m := StandardMaterial3D.new()
			m.vertex_color_use_as_albedo = true
			m.cull_mode = BaseMaterial3D.CULL_DISABLED
			for c in compounds:
				c.material_override = m
		for s in shots:
			cam.position = s[1]
			cam.look_at(s[2])
			for f in 8:
				await get_tree().process_frame
			await RenderingServer.frame_post_draw
			var file: String = s[0] + ("_nocull" if pass_cull else "") + ".png"
			get_viewport().get_texture().get_image().save_png(ShotOutput.file("Compound", file))
	print("shots done")
	get_tree().quit()
