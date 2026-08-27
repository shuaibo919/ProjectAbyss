extends Node3D

# Renders every .vtree in a directory, one shot each, framed from its own AABB.
#
# Used to study externally-authored graphs: the upstream sample project packs several plants into
# one file, so split_vtree.py separates them and this shoots them for comparison. Any .vtree from
# anywhere works — the format is plain text and needs no importer.
#
# Run: godot --path Game/ res://Develop/VtreeSampleTest.tscn
#      DIR=<abs dir of .vtree files>  to point it elsewhere

const DEFAULT_DIR := "E:/ProjectAbyss/Reference/SlowTree/samples/split"
const OUT_DIR := "res://Develop/VtreeShots"


func _ready() -> void:
	get_viewport().msaa_3d = Viewport.MSAA_8X
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, -35, 0)
	light.light_energy = 1.5
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.55, 0.68, 0.85)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.5, 0.55, 0.6)
	e.ambient_light_energy = 0.6
	env.environment = e
	add_child(env)

	# Roots dive below y=0 by design; without ground they float and read as spikes.
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(200, 200)
	ground.mesh = plane
	var gm := StandardMaterial3D.new()
	gm.albedo_color = Color(0.34, 0.36, 0.28)
	gm.roughness = 0.95
	ground.material_override = gm
	add_child(ground)

	var camera := Camera3D.new()
	camera.current = true
	camera.fov = 40.0
	add_child(camera)

	var dir_path: String = OS.get_environment("DIR") if OS.has_environment("DIR") else DEFAULT_DIR
	var d := DirAccess.open(dir_path)
	if d == null:
		print("cannot open %s" % dir_path)
		get_tree().quit()
		return

	var names: Array[String] = []
	for f in d.get_files():
		if f.ends_with(".vtree"):
			names.append(f)
	names.sort()

	for f in names:
		await _shoot(dir_path.path_join(f), f.get_basename(), camera)

	get_tree().quit()


func _shoot(path: String, name: String, camera: Camera3D) -> void:
	var res: Dictionary = SlowTreeGenerator.generate_from_file(path, 0, false)
	if res.get("error", "") != "":
		print("%-20s ERROR %s" % [name, res["error"]])
		return

	var mi := MeshInstance3D.new()
	mi.mesh = res["mesh"]
	add_child(mi)

	var aabb: AABB = mi.get_aabb()
	var centre := aabb.get_center()
	# Fit the tallest/widest extent to the frame rather than guessing from the diagonal — plants
	# here range from a 1.2 m rosette to a 12 m tree and a fixed factor crops the big ones.
	var extent: float = maxf(aabb.size.y, maxf(aabb.size.x, aabb.size.z))
	var radius: float = extent / (2.0 * tan(deg_to_rad(camera.fov * 0.5))) * 1.25
	camera.position = centre + Vector3(0.35, 0.15, 1.0).normalized() * radius
	camera.look_at(centre)

	for i in 6:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(
		ProjectSettings.globalize_path(OUT_DIR + "/" + name + ".png"))

	print("%-20s tris=%7d surfaces=%d  h=%.1fm  w=%.1fm" % [
		name, res["triangle_count"], res["surface_count"], aabb.size.y,
		maxf(aabb.size.x, aabb.size.z)])

	mi.queue_free()
	await get_tree().process_frame
