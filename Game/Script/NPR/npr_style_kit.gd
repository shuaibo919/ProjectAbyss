extends RefCounted

# Style passes for the StyleShowcase scenes. A showcase builds its content once
# with plain vertex-colour materials; `apply()` then restyles every
# GeometryInstance3D under a root, so the same town can be compared under each
# NPR treatment by switching one argument.
#
#   plain        vertex colour, matte — the reference the NPR looks are judged against
#   ink          InkPainting three-pass stack (ink_surface → ink_outline_0 → _1)
#   outline      plain + baked mesh-edge outline (CartoonOutlineBuilder)
#   ink_outline  both
#
# Meshes tagged with `town_water` meta keep a water material in every style.
#
# Usage: const NprStyleKit := preload("res://Script/NPR/npr_style_kit.gd")
#        NprStyleKit.apply(town_root, NprStyleKit.parse("ink"), { "fill_dirs": [...] })

enum EStyle { PLAIN, INK, OUTLINE, INK_OUTLINE }

const STYLE_NAMES := ["plain", "ink", "outline", "ink_outline"]
const SHADER_DIR := "res://Assets/Shaders/InkPainting"
const TEX_DIR := "res://Assets/Shaders/InkPainting/Textures"
const InkBrush := preload("res://Develop/Tools/ink_brush.gd")


static func parse(style_name: String) -> int:
	var i := STYLE_NAMES.find(style_name.strip_edges().to_lower())
	return maxi(i, 0)


static func has_ink(style: int) -> bool:
	return style == EStyle.INK or style == EStyle.INK_OUTLINE


static func has_outline(style: int) -> bool:
	return style == EStyle.OUTLINE or style == EStyle.INK_OUTLINE


## Restyles every GeometryInstance3D under `root`. Options:
##   fill_dirs        Array[Vector3] — fill-light directions the ink shader bypasses
##   line_width_px    mesh-edge outline width (default 1.4)
##   lod_world_width  outline line LOD, 0 = off (see ink_mesh_edge_outline.gdshader)
## Returns { "styled": n, "outlined": n }.
static func apply(root: Node, style: int, opts: Dictionary = {}) -> Dictionary:
	var surface: Material
	if has_ink(style):
		surface = make_ink_material(opts.get("fill_dirs", []))
	else:
		surface = make_plain_material()
	var water := make_water_material(style)
	var outline_mat: ShaderMaterial = null
	if has_outline(style):
		outline_mat = make_outline_material(opts.get("line_width_px", 1.4), opts.get("lod_world_width", 0.0))

	var targets: Array[GeometryInstance3D] = []
	_collect(root, targets)
	var outline_cache := {}
	var outlined := 0
	for g in targets:
		var mesh := _mesh_of(g)
		var is_water := mesh != null and mesh.has_meta("town_water")
		g.material_override = water if is_water else surface
		if outline_mat != null and mesh != null and not is_water:
			if _attach_outline(g, mesh, outline_mat, outline_cache):
				outlined += 1
	return { "styled": targets.size(), "outlined": outlined }


static func make_plain_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.vertex_color_use_as_albedo = true
	m.roughness = 0.9
	return m


static func make_water_material(style: int) -> Material:
	if has_ink(style):
		# Ink water is left as paper with a faint wash; the banks carry the drawing.
		var ink := ShaderMaterial.new()
		ink.shader = load(SHADER_DIR + "/ink_surface.gdshader")
		ink.set_shader_parameter("base_color", Color(0.80, 0.82, 0.81))
		ink.set_shader_parameter("use_vertex_color", false)
		ink.set_shader_parameter("color_remap_tex", load(TEX_DIR + "/ColorRemap.png"))
		ink.set_shader_parameter("noise_tex", load(TEX_DIR + "/Noise.png"))
		ink.set_shader_parameter("stroke_tex", load(TEX_DIR + "/Stroke.png"))
		return ink
	var m := StandardMaterial3D.new()
	m.vertex_color_use_as_albedo = true
	m.roughness = 0.32
	m.metallic_specular = 0.2
	return m


## The InkPainting stack as tuned on Map_AncientTown (surface → inverted-hull
## outline → second outline pass).
static func make_ink_material(fill_dirs: Array) -> ShaderMaterial:
	var remap: Texture2D = load(TEX_DIR + "/ColorRemap.png")
	var noise: Texture2D = load(TEX_DIR + "/Noise.png")

	var surface := ShaderMaterial.new()
	surface.shader = load(SHADER_DIR + "/ink_surface.gdshader")
	surface.set_shader_parameter("base_color", Color(1, 1, 1))
	surface.set_shader_parameter("use_vertex_color", true)
	surface.set_shader_parameter("color_remap_tex", remap)
	surface.set_shader_parameter("noise_tex", noise)
	surface.set_shader_parameter("stroke_tex", load(TEX_DIR + "/Stroke.png"))
	if fill_dirs.size() > 0:
		surface.set_shader_parameter("fill_light_dir", fill_dirs[0])
	if fill_dirs.size() > 1:
		surface.set_shader_parameter("fill_light_dir2", fill_dirs[1])

	var outline0 := ShaderMaterial.new()
	outline0.shader = load(SHADER_DIR + "/ink_outline_0.gdshader")
	outline0.set_shader_parameter("color_remap_tex", remap)
	outline0.set_shader_parameter("matcap_tex", load(TEX_DIR + "/MatCap.png"))
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


## Mesh-edge outline with the brush settings from StyleShowcase.gd.
static func make_outline_material(line_width_px: float, lod_world_width: float) -> ShaderMaterial:
	var mat := ShaderMaterial.new()
	mat.shader = load(SHADER_DIR + "/ink_mesh_edge_outline.gdshader")
	mat.set_shader_parameter("line_width_px", line_width_px)
	# AncientBuilding is a stack of open shells: boundary edges would ink everything.
	mat.set_shader_parameter("draw_boundary_edges", false)
	mat.set_shader_parameter("crease_cos_threshold", 0.25)
	mat.set_shader_parameter("end_taper", 0.12)
	mat.set_shader_parameter("width_jitter", 0.35)
	mat.set_shader_parameter("brush_tex", InkBrush.make_brush_texture())
	mat.set_shader_parameter("brush_amount", 0.65)
	mat.set_shader_parameter("brush_tile_size", 0.8)
	mat.set_shader_parameter("alpha_cut", 0.45)
	if lod_world_width > 0.0:
		mat.set_shader_parameter("max_world_width", lod_world_width)
	return mat


static func _collect(node: Node, out: Array[GeometryInstance3D]) -> void:
	if node is GeometryInstance3D and not node.has_meta("npr_outline"):
		out.append(node)
	for child in node.get_children():
		_collect(child, out)


static func _mesh_of(g: GeometryInstance3D) -> Mesh:
	if g is MeshInstance3D:
		return g.mesh
	if g is MultiMeshInstance3D and g.multimesh != null:
		return g.multimesh.mesh
	return null


## Bakes the outline once per source mesh and mirrors the instance's transforms.
## Works for MultiMeshInstance3D, which is how every PCG spawn arrives.
static func _attach_outline(g: GeometryInstance3D, mesh: Mesh, mat: ShaderMaterial, cache: Dictionary) -> bool:
	if not ClassDB.class_exists("CartoonOutlineBuilder"):
		return false
	var line_mesh: ArrayMesh = cache.get(mesh)
	if line_mesh == null:
		line_mesh = CartoonOutlineBuilder.build_outline_mesh(mesh, 90.0)
		cache[mesh] = line_mesh
	if line_mesh == null or line_mesh.get_surface_count() == 0:
		return false

	var line: GeometryInstance3D
	if g is MultiMeshInstance3D:
		var src: MultiMesh = g.multimesh
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = line_mesh
		mm.instance_count = src.instance_count
		for i in src.instance_count:
			mm.set_instance_transform(i, src.get_instance_transform(i))
		var mmi := MultiMeshInstance3D.new()
		mmi.multimesh = mm
		line = mmi
	else:
		var mi := MeshInstance3D.new()
		mi.mesh = line_mesh
		line = mi
	line.name = "%s_Outline" % g.name
	line.set_meta("npr_outline", true)
	line.material_override = mat
	line.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	g.add_child(line)
	return true
