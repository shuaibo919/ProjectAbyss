@tool
extends RefCounted
class_name PcgTownMeshes

## Factory for the ancient-town greybox pieces used by the town PCG graphs:
## road slabs, city walls, market stalls, wells and 牌坊 archways. Same
## philosophy as [PcgVillageMeshes] / [PcgPropMeshes]: textureless ArrayMeshes
## with per-vertex colors, so one MultiMeshInstance3D renders each kind.
##
## Orientation contract:
##   - roads / walls are unit boxes (1x1x1) centered at the origin; the scatter
##     node scales them through the per-point `size` stream,
##   - stalls / wells / archways are authored at real-world scale, origin at
##     the ground anchor (y = 0), +Z is the "front".

const ROAD_COL := Color( 0.62, 0.58, 0.52 )    # rammed-earth road, warm grey
const WALL_COL := Color( 0.55, 0.50, 0.42 )    # rammed-earth wall
const WALL_DARK := Color( 0.42, 0.38, 0.32 )
const TIMBER_DARK := Color( 0.30, 0.24, 0.19 )
const AWNING_COL := Color( 0.76, 0.74, 0.70 )  # pale cloth
const STONE_COL := Color( 0.52, 0.50, 0.47 )

static var _vc_mat: StandardMaterial3D = null

static func _material() -> StandardMaterial3D:
	if _vc_mat == null:
		_vc_mat = StandardMaterial3D.new()
		_vc_mat.vertex_color_use_as_albedo = true
		_vc_mat.roughness = 0.92
	return _vc_mat


## A 1x1x1 box centered at the origin, tinted. Roads and walls instance this
## and stretch it with the per-point size stream.
static func unit_box( col: Color ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_box( st, Vector3( -0.5, -0.5, -0.5 ), Vector3( 0.5, 0.5, 0.5 ), col )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


static func road() -> ArrayMesh:
	return unit_box( ROAD_COL )


static func wall() -> ArrayMesh:
	return unit_box( WALL_COL )


## A market stall: four timber posts, a sales counter and a slanted cloth
## awning. Footprint ~1.7 x 1.3, awning peak ~2.0.
static func stall() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )

	# Posts.
	var px := 0.62
	var pz := 0.42
	var post := 0.05
	var post_h := 1.62
	for sx in [ -1.0, 1.0 ]:
		for sz in [ -1.0, 1.0 ]:
			var cx: float = sx * px
			var cz: float = sz * pz
			_box( st, Vector3( cx - post, 0, cz - post ), Vector3( cx + post, post_h, cz + post ), TIMBER_DARK )
	# Counter.
	_box( st, Vector3( -px, 0.55, -0.25 ), Vector3( px, 0.82, 0.25 ), TIMBER_DARK )
	_box( st, Vector3( -px, 0.82, -0.25 ), Vector3( px, 0.9, 0.25 ), AWNING_COL )
	# Awning: slanted slab front (+Z) and back (-Z), plus a short back wall.
	var front_hi := 1.95
	var back_hi := 1.5
	var e0 := Vector3( -px - 0.18, front_hi, pz + 0.22 )
	var e1 := Vector3( px + 0.18, front_hi, pz + 0.22 )
	var r0 := Vector3( -px - 0.05, back_hi, -pz - 0.18 )
	var r1 := Vector3( px + 0.05, back_hi, -pz - 0.18 )
	# 顶面绕序向上受光, 底面绕序向下 (旧绕序法线朝下, 俯拍时棚顶近黑)。
	_quad( st, r0, e0, e1, r1, AWNING_COL )
	_quad( st, r0, r1, e1, e0, AWNING_COL )
	_box( st, Vector3( -px - 0.05, back_hi, -pz - 0.18 ), Vector3( px + 0.05, front_hi, -pz - 0.1 ), AWNING_COL )

	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A stone well: low ring, two posts and a tiny roof cap.
static func well() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )

	var r := 0.42
	var seg := 8
	var h := 0.55
	for a in range( seg ):
		var a0 := TAU * float( a ) / float( seg )
		var a1 := TAU * float( a + 1 ) / float( seg )
		var b0 := Vector3( cos( a0 ) * r, 0, sin( a0 ) * r )
		var b1 := Vector3( cos( a1 ) * r, 0, sin( a1 ) * r )
		var t0 := Vector3( cos( a0 ) * r, h, sin( a0 ) * r )
		var t1 := Vector3( cos( a1 ) * r, h, sin( a1 ) * r )
		_quad( st, b0, b1, t1, t0, STONE_COL )

	# Posts + cap.
	for sx in [ -1.0, 1.0 ]:
		var cx: float = sx * r * 0.8
		_box( st, Vector3( cx - 0.04, h, -0.04 ), Vector3( cx + 0.04, h + 1.0, 0.04 ), TIMBER_DARK )
	_box( st, Vector3( -0.62, h + 1.0, -0.62 ), Vector3( 0.62, h + 1.1, 0.62 ), AWNING_COL )

	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A 牌坊 memorial archway: two pillars, two lintel beams, a small roof slab.
## ~3.6 wide, ~4.2 tall. +Z is the through direction.
static func archway() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )

	var half := 1.45
	var p := 0.22
	var h := 3.4
	for sx in [ -1.0, 1.0 ]:
		var cx: float = sx * half
		_box( st, Vector3( cx - p, 0, -p ), Vector3( cx + p, h, p ), TIMBER_DARK )
	# Lintels.
	_box( st, Vector3( -half - 0.25, 2.1, -p ), Vector3( half + 0.25, 2.42, p ), TIMBER_DARK )
	_box( st, Vector3( -half - 0.35, 2.6, -p ), Vector3( half + 0.35, 2.82, p ), TIMBER_DARK )
	# Roof slab.
	var rw := half + 0.75
	_box( st, Vector3( -rw, 2.82, -0.6 ), Vector3( rw, 3.05, 0.6 ), AWNING_COL )

	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


# --- helpers -------------------------------------------------------------

static func _tri( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, col: Color ) -> void:
	st.set_color( col ); st.add_vertex( a )
	st.set_color( col ); st.add_vertex( b )
	st.set_color( col ); st.add_vertex( c )


static func _quad( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, d: Vector3, col: Color ) -> void:
	_tri( st, a, b, c, col )
	_tri( st, a, c, d, col )


static func _box( st: SurfaceTool, mn: Vector3, mx: Vector3, col: Color ) -> void:
	var c000 := Vector3( mn.x, mn.y, mn.z )
	var c100 := Vector3( mx.x, mn.y, mn.z )
	var c101 := Vector3( mx.x, mn.y, mx.z )
	var c001 := Vector3( mn.x, mn.y, mx.z )
	var c010 := Vector3( mn.x, mx.y, mn.z )
	var c110 := Vector3( mx.x, mx.y, mn.z )
	var c111 := Vector3( mx.x, mx.y, mx.z )
	var c011 := Vector3( mn.x, mx.y, mx.z )
	_quad( st, c001, c101, c111, c011, col )
	_quad( st, c100, c000, c010, c110, col )
	_quad( st, c101, c100, c110, c111, col )
	_quad( st, c000, c001, c011, c010, col )
	_quad( st, c011, c111, c110, c010, col )
	_quad( st, c000, c100, c101, c001, col )
