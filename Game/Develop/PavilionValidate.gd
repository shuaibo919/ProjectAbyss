extends Node3D

# 亭台楼阁榭廊 (Hu & Qin Fig 2): one AncientBuilding per archetype preset, from
# AncientBuildingParameters.apply_archetype(). Plus two 亭 variants: a 重檐八角亭 (storey stack on a
# polygon) and a square 坐凳栏杆 亭.
# Run: godot --path Game/ res://Develop/PavilionValidate.tscn   (renders, so not --headless)
# Shots land in Reference/Shots/Pavilion/. Headless numeric checks: tests/ancient_building_pavilion_test.gd

const ShotOutput := preload("res://Develop/Tools/shot_output.gd")

const NAMES := ["ting", "tai", "lou", "ge", "xie", "lang"]
const SPACING := 30.0


func _ready() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X

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
	plane.size = Vector2(500, 500)
	ground.mesh = plane
	add_child(ground)
	# A water sheet under the 榭 so its piles have something to stand in.
	var water := MeshInstance3D.new()
	var wplane := PlaneMesh.new()
	wplane.size = Vector2(22, 18)
	water.mesh = wplane
	var wmat := StandardMaterial3D.new()
	wmat.albedo_color = Color(0.5, 0.58, 0.62)
	wmat.roughness = 0.3
	water.material_override = wmat
	water.position = Vector3(_x(4), 0.02, 0)
	add_child(water)

	var centres := {}
	for i in NAMES.size():
		var p = ClassDB.instantiate("AncientBuildingParameters")
		p.apply_archetype(i)
		p.ridge_detail = 1
		_add(NAMES[i], p, Vector3(_x(i), 0, 0))
		centres[NAMES[i]] = Vector3(_x(i), 0, 0)

	var octagon = ClassDB.instantiate("AncientBuildingParameters")
	octagon.apply_archetype(0)
	octagon.sides = 8
	octagon.width = 7.0
	octagon.depth = 7.0
	octagon.storey_count = 2
	octagon.storey_setback_bays = 1
	octagon.upper_column_height_scale = 0.4
	octagon.railing_kind = 1
	_add("double_eave_octagon", octagon, Vector3(_x(1), 0, 34))
	centres["double_eave_octagon"] = Vector3(_x(1), 0, 34)

	var square = ClassDB.instantiate("AncientBuildingParameters")
	square.apply_archetype(0)
	square.sides = 4
	square.bays_x = 1
	square.bays_z = 1
	square.width = 5.0
	square.depth = 5.0
	square.railing_kind = 1
	square.roof_type = 6
	_add("square_ting", square, Vector3(_x(3), 0, 34))
	centres["square_ting"] = Vector3(_x(3), 0, 34)

	var cam := Camera3D.new()
	cam.fov = 40.0
	cam.far = 1000.0
	add_child(cam)
	cam.current = true
	var shots := [["overview", Vector3(0, 70, 150), Vector3(0, 5, 10)]]
	for key in centres:
		var c: Vector3 = centres[key]
		shots.append([key, c + Vector3(15, 9, 19), c + Vector3(0, 3.5, 0)])
	shots.append(["ting_closeup", centres["ting"] + Vector3(5, 2.6, 6.5), centres["ting"] + Vector3(0, 1.4, 0)])
	shots.append(["lang_closeup", centres["lang"] + Vector3(4, 2.2, 5), centres["lang"] + Vector3(-4, 1.4, 0)])
	for s in shots:
		cam.position = s[1]
		cam.look_at(s[2])
		for f in 8:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(ShotOutput.file("Pavilion", s[0] + ".png"))
		print("shot " + s[0])
	get_tree().quit()


func _x(i: int) -> float:
	return (i - (NAMES.size() - 1) * 0.5) * SPACING


func _add(label: String, params: Resource, at: Vector3) -> void:
	var b = ClassDB.instantiate("AncientBuilding")
	b.name = label
	b.parameters = params
	b.position = at
	add_child(b)
