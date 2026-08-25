extends Node3D

# Validates the Hu & Qin 2020 section 3.1 sweep against the failure case in their Fig 5:
# a spline with an acute angle, which the prior "re-orient the contour at every knot"
# approach distorts and the paper's miter propagation does not.
#
# Run: godot --path Game/ res://Develop/SweepValidate.tscn

const OUT_DIR := "res://Develop/SweepShots"

# Fig 5's case: a ridge that doubles back on itself at a sharp angle.
const ACUTE := [
	Vector3(-4.0, 0.0, -2.0),
	Vector3(0.0, 0.0, -2.0),
	Vector3(3.2, 0.0, 2.4),   # ~40 degree interior angle
	Vector3(-1.0, 0.0, 3.0),
]


func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-52, -38, 0)
	light.light_energy = 1.4
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.62, 0.70, 0.80)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.55, 0.58, 0.62)
	e.ambient_light_energy = 0.75
	env.environment = e
	add_child(env)

	var camera := Camera3D.new()
	camera.current = true
	add_child(camera)

	# 1. The headline comparison: same curve, same profile, two modes.
	await _shoot("acute_frame_priorart", ACUTE, camera, {"mode": 1})
	await _shoot("acute_miter_paper", ACUTE, camera, {"mode": 0})

	# 2. Same, with a round profile, where the pinch is easiest to see.
	await _shoot("acute_round_frame", ACUTE, camera, {"mode": 1, "preset": 2})
	await _shoot("acute_round_miter", ACUTE, camera, {"mode": 0, "preset": 2})

	# 3. Displacement curve (Eq 2): a ridge that swells towards its middle.
	var swell := Curve.new()
	swell.add_point(Vector2(0.0, 0.0))
	swell.add_point(Vector2(0.5, 1.0))
	swell.add_point(Vector2(1.0, 0.0))
	await _shoot("displacement_radial", ACUTE, camera,
		{"mode": 0, "curve": swell, "phi": 0.45})

	# 4. Direction constraints (Eq 3-5) at delta = 60: only the up-facing vertices move,
	#    and along the bitangent — the mechanism behind steps and ridge tails.
	var ramp := Curve.new()
	ramp.add_point(Vector2(0.0, 0.0))
	ramp.add_point(Vector2(1.0, 1.0))
	await _shoot("displacement_constrained", ACUTE, camera,
		{"mode": 0, "curve": ramp, "phi": 0.9, "delta": 60.0})

	_verify_direction_constraints()

	get_tree().quit()


## Eq 3-5 make a claim that is checkable rather than eyeballable: at delta > 90 the
## displacement is radial (so the section grows in every direction), and at delta <= 90 only
## the vertices facing the bitangent move, and they move *along* it — so with up = +Y the
## section may only grow upward.
func _verify_direction_constraints() -> void:
	var straight := [Vector3(-3, 0, 0), Vector3(0, 0, 0), Vector3(3, 0, 0)]

	var ramp := Curve.new()
	ramp.add_point(Vector2(0.0, 1.0))
	ramp.add_point(Vector2(1.0, 1.0))

	var base := _measure(straight, {})
	var radial := _measure(straight, {"curve": ramp, "phi": 0.5, "delta": 180.0})
	var constrained := _measure(straight, {"curve": ramp, "phi": 0.5, "delta": 60.0})

	var eps := 0.002

	# Radial: grows below as well as above, and sideways.
	var radial_ok := (radial.position.y < base.position.y - eps) \
		and (radial.end.y > base.end.y + eps) \
		and (radial.size.z > base.size.z + eps)
	print("VERIFY radial (delta=180) grows in Y-, Y+ and Z: %s" % ("PASS" if radial_ok else "FAIL"))

	# Constrained along +Y: top rises, bottom must not move, width must not change.
	var constrained_ok := (constrained.end.y > base.end.y + eps) \
		and (absf(constrained.position.y - base.position.y) < eps) \
		and (absf(constrained.size.z - base.size.z) < eps)
	print("VERIFY constrained (delta=60) grows only in Y+: %s" % ("PASS" if constrained_ok else "FAIL"))
	print("  base=%s radial=%s constrained=%s" % [base, radial, constrained])


func _measure(points: Array, opts: Dictionary) -> AABB:
	var path := Path3D.new()
	var curve := Curve3D.new()
	for p in points:
		curve.add_point(p)
	path.curve = curve
	add_child(path)

	var sweep := AncientSplineSweep.new()
	sweep.auto_regenerate = false
	sweep.contour_preset = 1          # Square, so the extents are unambiguous
	sweep.contour_scale = 1.0
	sweep.up_reference = Vector3(0, 1, 0)
	if opts.has("curve"):
		sweep.displacement_curve = opts["curve"]
		sweep.displacement_scale = opts.get("phi", 0.0)
	if opts.has("delta"):
		sweep.constraint_angle = opts["delta"]
	add_child(sweep)
	sweep.target_path = sweep.get_path_to(path)
	sweep.generate()

	var aabb: AABB = sweep.get_aabb()
	sweep.free()
	path.free()

	return aabb


func _shoot(name: String, points: Array, camera: Camera3D, opts: Dictionary) -> void:
	var path := Path3D.new()
	var curve := Curve3D.new()
	for p in points:
		curve.add_point(p)
	path.curve = curve
	add_child(path)

	var sweep := AncientSplineSweep.new()
	sweep.auto_regenerate = false
	sweep.contour_preset = opts.get("preset", 3)   # 3 = Ridge Tile
	sweep.contour_scale = 0.8
	sweep.mode = opts.get("mode", 0)
	if opts.has("curve"):
		sweep.displacement_curve = opts["curve"]
		sweep.displacement_scale = opts.get("phi", 0.0)
	if opts.has("delta"):
		sweep.constraint_angle = opts["delta"]
	add_child(sweep)
	# NodePath is resolved relative to the sweep node, so it can only be set once both
	# nodes are in the tree.
	sweep.target_path = sweep.get_path_to(path)
	sweep.generate()

	var vertex_count := 0
	var triangle_count := 0
	if sweep.mesh != null and sweep.mesh.get_surface_count() > 0:
		var arrays: Array = sweep.mesh.surface_get_arrays(0)
		vertex_count = arrays[Mesh.ARRAY_VERTEX].size()
		triangle_count = arrays[Mesh.ARRAY_INDEX].size() / 3

	var aabb: AABB = sweep.get_aabb()
	var centre := aabb.get_center()
	var radius := maxf(aabb.size.length() * 0.6, 1.0)
	camera.position = centre + Vector3(0.25, 0.85, 0.5).normalized() * radius * 1.35
	camera.look_at(centre)

	for i in 6:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	var image := get_viewport().get_texture().get_image()
	image.save_png(ProjectSettings.globalize_path(OUT_DIR + "/" + name + ".png"))

	print("%s: knots=%d verts=%d tris=%d degenerate_joints=%d max_miter_stretch=%.3f" % [
		name,
		sweep.get_knot_count(),
		vertex_count,
		triangle_count,
		sweep.get_degenerate_joint_count(),
		sweep.get_max_miter_stretch(),
	])

	sweep.queue_free()
	path.queue_free()
	await get_tree().process_frame
