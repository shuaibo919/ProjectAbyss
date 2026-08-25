extends Node3D

# Renders the AncientBuilding generator and asserts the Table 1 proportions.
# Run: godot --path Game/ res://Develop/BuildingValidate.tscn

const OUT_DIR := "res://Develop/BuildingShots"


func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-46, -34, 0)
	light.light_energy = 1.5
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.63, 0.71, 0.81)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.56, 0.59, 0.63)
	e.ambient_light_energy = 0.8
	env.environment = e
	add_child(env)

	var camera := Camera3D.new()
	camera.current = true
	add_child(camera)

	_verify_table1()

	await _shoot("hall_3bay", camera, {})
	await _shoot("hall_5bay_wide", camera, {"width": 15.0, "depth": 8.0, "bays_x": 5, "bays_z": 2})
	await _shoot("pavilion_small", camera, {"width": 5.0, "depth": 5.0, "bays_x": 1, "bays_z": 1, "fence_lambda": 2})
	# Cr from the paper: tiles cover only the upper part, exposing the boarding below.
	await _shoot("tile_coverage_40", camera, {"tile_coverage": 0.4})
	# Flatter roof via more rafter courses (equation 10 is inverse in n_R).
	await _shoot("roof_flat_11courses", camera, {"rafter_courses": 11})
	await _shoot("body_only", camera, {"generate_walls": false})
	# 歇山 with the corner upturn, and the same roof with the flip switched off so the
	# contribution of 翼角起翘 is isolated.
	await _shoot("xieshan", camera, {"roof_type": 1})
	await _shoot("xieshan_no_flip", camera, {"roof_type": 1, "corner_rise_scale": 0.0, "corner_extend_scale": 0.0})
	await _shoot("xieshan_strong_flip", camera, {"roof_type": 1, "corner_rise_scale": 3.0, "corner_extend_scale": 1.4})
	await _shoot("xieshan_wide", camera, {"roof_type": 1, "width": 15.0, "depth": 9.0, "bays_x": 5})
	# 庑殿 is the same shell taken to the ridge. A square plan degenerates it to a 攒尖 pyramid.
	await _shoot("wudian", camera, {"roof_type": 2, "width": 13.0, "depth": 8.0, "bays_x": 5})
	await _shoot("wudian_square_pyramid", camera, {"roof_type": 2, "width": 7.0, "depth": 7.0, "bays_x": 1, "bays_z": 1})
	await _shoot("xuanshan", camera, {"roof_type": 3})
	await _shoot("juanpeng", camera, {"roof_type": 4})
	await _shoot("luding", camera, {"roof_type": 5, "width": 11.0, "depth": 8.0, "bays_x": 3})
	# Centralised family. sides != 4 routes to the polygonal generator (Eq 8).
	await _shoot("zanjian_square", camera, {"roof_type": 6, "width": 8.0, "depth": 8.0, "bays_x": 1, "bays_z": 1})
	await _shoot("zanjian_hex", camera, {"roof_type": 6, "width": 8.0, "sides": 6})
	await _shoot("zanjian_oct", camera, {"roof_type": 6, "width": 9.0, "sides": 8})
	await _shoot("yuanzanjian", camera, {"roof_type": 7, "width": 8.0, "sides": 12})
	await _shoot("kuiding", camera, {"roof_type": 8, "width": 8.0, "sides": 8})

	get_tree().quit()


## Table 1 has exact consequences, so assert them rather than trusting the render.
func _verify_table1() -> void:
	var p := AncientBuildingParameters.new()
	p.width = 11.0
	p.depth = 7.0
	p.platform_height_scale = 1.0

	var d: float = p.get_module()
	var eps := 0.0005
	# Derive from the live value rather than the default, so changing n_R does not
	# invalidate the assertion.
	var courses: float = float(p.rafter_courses)

	var checks := [
		["D = width*0.8/11", d, 11.0 * 0.8 / 11.0],
		["eave height = 11D = 0.8*width", p.get_eave_height(), 11.0 * 0.8],
		["platform height = 2D", p.get_platform_height(), 2.0 * d],
		["column height = 9D", p.get_column_height(), 9.0 * d],
		["roof base = 11D + bracket", p.get_roof_base(), p.get_eave_height() + p.get_bracket_height()],
		["roof height = 1.3*depth/((n-1)/2)", p.get_roof_height(), 1.3 * 7.0 / ((courses - 1.0) / 2.0)],
	]
	for c in checks:
		var ok: bool = absf(c[1] - c[2]) < eps
		print("VERIFY %-34s got=%.4f want=%.4f %s" % [c[0], c[1], c[2], "PASS" if ok else "FAIL"])

	# lambda drives both the balustrade gaps and the stair runs.
	p.fence_lambda = 0
	var runs0: int = p.get_step_run_count()
	p.fence_lambda = 2
	var runs2: int = p.get_step_run_count()
	print("VERIFY step runs = 2^lambda            got=%d,%d want=1,4 %s"
		% [runs0, runs2, "PASS" if (runs0 == 1 and runs2 == 4) else "FAIL"])


func _shoot(name: String, camera: Camera3D, opts: Dictionary) -> void:
	var building := AncientBuilding.new()
	building.auto_regenerate = false
	var p := AncientBuildingParameters.new()
	for key in opts:
		p.set(key, opts[key])
	building.parameters = p
	add_child(building)
	building.generate()

	var aabb: AABB = building.get_aabb()
	var centre := aabb.get_center()
	var radius := maxf(aabb.size.length() * 0.6, 1.0)
	camera.position = centre + Vector3(0.75, 0.42, 1.0).normalized() * radius * 1.3
	camera.look_at(centre)

	for i in 6:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	var image := get_viewport().get_texture().get_image()
	image.save_png(ProjectSettings.globalize_path(OUT_DIR + "/" + name + ".png"))

	print("%s: verts=%d tris=%d size=%.1fx%.1fx%.1f total_height=%.2f" % [
		name, building.get_vertex_count(), building.get_triangle_count(),
		aabb.size.x, aabb.size.y, aabb.size.z, p.get_total_height(),
	])

	building.queue_free()
	await get_tree().process_frame
