extends Node3D

# Three AncientBuilding material styles side by side: 官式 / 茅草 / 土木.
# Same 硬山 geometry, different palettes + per-piece colour mottle.
# Run: godot --path Game/ res://Develop/StyleShowcase.tscn [-- --out=Name] [-- --outline]
# --outline additionally bakes the mesh-edge outline (CartoonOutlineBuilder +
# ink_mesh_edge_outline.gdshader) onto every building.

const ShotOutput := preload("res://Develop/Tools/shot_output.gd")
const InkBrush := preload("res://Develop/Tools/ink_brush.gd")
const OUTLINE_SHADER := preload("res://Assets/Shaders/InkPainting/ink_mesh_edge_outline.gdshader")


func _ready() -> void:
	var out := "building_styles"
	var with_outline := false
	var closeup := false
	var lod_world_width := 0.0
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.substr(6)
		elif a == "--outline":
			with_outline = true
		elif a == "--close":
			closeup = true
		elif a.begins_with("--lod="):
			lod_world_width = a.substr(6).to_float()

	# The outline shader's AA is alpha-to-coverage, which needs MSAA on the viewport.
	get_viewport().msaa_3d = Viewport.MSAA_4X

	# Ink environment (paper background + flat light), same as the validation map.
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-42, -38, 0)
	light.light_energy = 1.1
	light.shadow_enabled = true
	add_child(light)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.898, 0.859, 0.824)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.86, 0.84, 0.82)
	e.ambient_light_energy = 0.9
	env.environment = e
	add_child(env)

	var cam := Camera3D.new()
	cam.position = Vector3(7, 7, 13) if closeup else Vector3(0, 22, 34)
	cam.current = true
	add_child(cam)
	# Frame just the middle building when close: tile seams read as intentional ink
	# lines up close, where from the overview camera they merge into black.
	cam.look_at(Vector3(0, 3.5, 0) if closeup else Vector3(0, 3, 0))

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(200, 200)
	ground.mesh = plane
	add_child(ground)

	for style in 3:
		var building = ClassDB.instantiate("AncientBuilding")
		building.name = "Style_%d" % style
		building.position = Vector3(-18.0 + 18.0 * style, 0, 0)
		var params = ClassDB.instantiate("AncientBuildingParameters")
		params.material_style = style
		building.parameters = params
		add_child(building)
		if with_outline:
			_attach_outline(building, lod_world_width)

	for i in 12:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	get_viewport().get_texture().get_image().save_png(
		ShotOutput.file("Ink", "%s.png" % out))
	print("saved %s" % out)
	get_tree().quit()


# The building generates its mesh in _ready, which has already run by the time
# add_child returns, so the outline can bake immediately.
func _attach_outline(building: MeshInstance3D, lod_world_width: float = 0.0) -> void:
	if building.mesh == null:
		push_warning("StyleShowcase: %s has no mesh, outline skipped" % building.name)
		return
	var outline_mesh: ArrayMesh = CartoonOutlineBuilder.build_outline_mesh(building.mesh)
	if outline_mesh == null or outline_mesh.get_surface_count() == 0:
		push_warning("StyleShowcase: outline bake for %s produced no edges" % building.name)
		return
	var outline := MeshInstance3D.new()
	outline.name = "%s_Outline" % building.name
	outline.mesh = outline_mesh
	var mat := ShaderMaterial.new()
	mat.shader = OUTLINE_SHADER
	mat.set_shader_parameter("line_width_px", 1.5)
	# The building is a stack of open shells — open panel rims are everywhere by
	# design, so boundary edges must not draw or the whole model inks black.
	mat.set_shader_parameter("draw_boundary_edges", false)
	# 75-degree minimum dihedral: lattice bars and bracket sets are full of small
	# creases that ink into a dark mass at showcase distance.
	mat.set_shader_parameter("crease_cos_threshold", 0.25)
	# Pencil+4-style stroke brush: tapered stroke ends, per-stroke width jitter,
	# and a bristle texture sampled along the stroke's arc length.
	mat.set_shader_parameter("end_taper", 0.12)
	mat.set_shader_parameter("width_jitter", 0.35)
	mat.set_shader_parameter("brush_tex", InkBrush.make_brush_texture())
	mat.set_shader_parameter("brush_amount", 0.65)
	mat.set_shader_parameter("brush_tile_size", 0.8)
	mat.set_shader_parameter("alpha_cut", 0.45)
	# Line LOD: cap line width in world units so distant dense strokes (tile seams)
	# shrink to 1px and fade instead of moire-ing into a black mass.
	if lod_world_width > 0.0:
		mat.set_shader_parameter("max_world_width", lod_world_width)
	outline.material_override = mat
	building.add_child(outline)
