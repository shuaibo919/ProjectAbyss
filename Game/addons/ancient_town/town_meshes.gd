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
const TIMBER_DARK := Color( 0.40, 0.33, 0.26 )   # 提亮: 阴影中只剩 ambient 时不至于近黑 (2026-09-26 round-4)
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


# --- river town pieces (River Town Lots node) -----------------------------

const MASONRY_COL := Color( 0.60, 0.58, 0.54 )   # dressed stone: embankment, piers, bridge
const MASONRY_DARK := Color( 0.47, 0.46, 0.43 )  # waterline band / plinth
const CITY_WALL_COL := Color( 0.56, 0.53, 0.48 ) # grey brick city wall
const WATER_COL := Color( 0.50, 0.57, 0.60 )
const LAND_COL := Color( 0.57, 0.57, 0.48 )    # packed earth with a green cast
const PAVING_COL := Color( 0.66, 0.63, 0.58 )
const HULL_COL := Color( 0.42, 0.33, 0.24 )
const CANOPY_COL := Color( 0.20, 0.20, 0.19 )    # 乌篷: black bamboo matting
const RIDGE_COL := Color( 0.58, 0.61, 0.60 )     # distant hills, fog does the rest

static var _water_mat: StandardMaterial3D = null
# Set only for the duration of a builder that wants smooth normals (terrain-like
# surfaces); every other piece is faceted masonry and timber.
static var _smooth := false

static func _water_material() -> StandardMaterial3D:
	if _water_mat == null:
		_water_mat = StandardMaterial3D.new()
		_water_mat.vertex_color_use_as_albedo = true
		_water_mat.roughness = 0.3
		_water_mat.metallic_specular = 0.2
	return _water_mat


## A masonry block with arched openings punched through it along local Z.
## Covers both the gate pier (墩台 + 门洞) and a multi-span arch bridge: the
## block spans x in [-width/2, width/2], y in [y0, y1], z in [-depth/2, depth/2].
## Each opening is Vector2(center_x, opening_width); its sides rise straight to
## `spring_y`, then a half-ellipse of height `rise` closes it.
static func arch_block( width: float, y0: float, y1: float, depth: float, openings: Array, spring_y: float, rise: float, col: Color = MASONRY_COL ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var hz := depth * 0.5
	var sorted := openings.duplicate()
	sorted.sort_custom( func( a: Vector2, b: Vector2 ) -> bool: return a.x < b.x )

	# Solid piers between the openings.
	var x_cursor := -width * 0.5
	for o in sorted:
		var xa: float = o.x - o.y * 0.5
		if xa > x_cursor + 0.01:
			_box( st, Vector3( x_cursor, y0, -hz ), Vector3( xa, y1, hz ), col )
		x_cursor = o.x + o.y * 0.5
	if x_cursor < width * 0.5 - 0.01:
		_box( st, Vector3( x_cursor, y0, -hz ), Vector3( width * 0.5, y1, hz ), col )

	# Spandrel over each opening: front/back faces, intrados, top.
	var arc_steps := 14
	for o in sorted:
		var cx: float = o.x
		var hw: float = o.y * 0.5
		var arc: Array[Vector2] = []
		for k in arc_steps + 1:
			var phi := PI * ( 1.0 - float( k ) / float( arc_steps ) )
			arc.append( Vector2( cx + hw * cos( phi ), spring_y + rise * sin( phi ) ) )
		for k in arc_steps:
			var a: Vector2 = arc[ k ]
			var b: Vector2 = arc[ k + 1 ]
			for zs in [ hz, -hz ]:
				_quad_facing( st, Vector3( a.x, a.y, zs ), Vector3( b.x, b.y, zs ),
					Vector3( b.x, y1, zs ), Vector3( a.x, y1, zs ), Vector3( 0, 0, signf( zs ) ), col )
			var mid := ( a + b ) * 0.5
			_quad_facing( st, Vector3( a.x, a.y, hz ), Vector3( b.x, b.y, hz ),
				Vector3( b.x, b.y, -hz ), Vector3( a.x, a.y, -hz ),
				Vector3( cx - mid.x, spring_y - mid.y, 0.0 ), col )
		# The jambs below the spring line are the neighbouring piers' own side faces.
		_quad_facing( st, Vector3( cx - hw, y1, hz ), Vector3( cx + hw, y1, hz ),
			Vector3( cx + hw, y1, -hz ), Vector3( cx - hw, y1, -hz ), Vector3.UP, col )

	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## 乌篷船: a small canal boat with a black arched canopy amidships. ~7 m long,
## +X is the bow, y = 0 is the waterline.
static func boat_wupeng() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_hull( st, 7.0, 1.7, 0.45, 0.3 )

	# Canopy: a half-cylinder shell over the middle third, faces both ways so
	# the open ends don't show a culled interior.
	var r := 0.78
	var x0 := -1.5
	var x1 := 1.2
	var base_y := 0.42
	var seg := 7
	for k in seg:
		var a0 := PI * float( k ) / float( seg )
		var a1 := PI * float( k + 1 ) / float( seg )
		var p0 := Vector2( cos( a0 ) * r, sin( a0 ) * r * 0.85 )
		var p1 := Vector2( cos( a1 ) * r, sin( a1 ) * r * 0.85 )
		var q0 := Vector3( x0, base_y + p0.y, p0.x )
		var q1 := Vector3( x0, base_y + p1.y, p1.x )
		var q2 := Vector3( x1, base_y + p1.y, p1.x )
		var q3 := Vector3( x1, base_y + p0.y, p0.x )
		var out := Vector3( 0, ( p0.y + p1.y ) * 0.5, ( p0.x + p1.x ) * 0.5 )
		_quad_facing( st, q0, q1, q2, q3, out, CANOPY_COL )
		_quad_facing( st, q0, q1, q2, q3, -out, CANOPY_COL )
	# Stern oar.
	_box( st, Vector3( -3.9, 0.3, -0.04 ), Vector3( -2.6, 0.38, 0.04 ), TIMBER_DARK )

	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A larger cargo junk: wide hull, deck cabin and a mast with a furled sail.
## ~11 m long, +X is the bow, y = 0 is the waterline.
static func boat_cargo() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_hull( st, 11.0, 3.0, 0.7, 0.45 )
	_box( st, Vector3( -4.2, 0.65, -1.0 ), Vector3( -1.6, 2.0, 1.0 ), TIMBER_DARK )
	_box( st, Vector3( -4.4, 2.0, -1.15 ), Vector3( -1.4, 2.14, 1.15 ), CANOPY_COL )
	_box( st, Vector3( 1.0, 0.65, -0.09 ), Vector3( 1.18, 8.0, 0.09 ), TIMBER_DARK )
	_box( st, Vector3( 0.7, 2.6, -0.2 ), Vector3( 1.48, 7.4, 0.2 ), AWNING_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A pedestrian for scale: a tapered robe, a head, a wide hat. 1.7 m tall.
static func figure( robe: Color ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var seg := 6
	for k in seg:
		var a0 := TAU * float( k ) / float( seg )
		var a1 := TAU * float( k + 1 ) / float( seg )
		var b0 := Vector3( cos( a0 ) * 0.3, 0.0, sin( a0 ) * 0.3 )
		var b1 := Vector3( cos( a1 ) * 0.3, 0.0, sin( a1 ) * 0.3 )
		var t0 := Vector3( cos( a0 ) * 0.17, 1.3, sin( a0 ) * 0.17 )
		var t1 := Vector3( cos( a1 ) * 0.17, 1.3, sin( a1 ) * 0.17 )
		_quad_facing( st, b0, b1, t1, t0, ( b0 + b1 ) * 0.5, robe )
		# Hat: a flat cone.
		var h0 := Vector3( cos( a0 ) * 0.36, 1.58, sin( a0 ) * 0.36 )
		var h1 := Vector3( cos( a1 ) * 0.36, 1.58, sin( a1 ) * 0.36 )
		var apex := Vector3( 0, 1.75, 0 )
		if ( h1 - h0 ).cross( apex - h0 ).dot( ( h0 + h1 ) * 0.5 ) <= 0.0:
			_tri( st, h0, h1, apex, AWNING_COL )
		else:
			_tri( st, h1, h0, apex, AWNING_COL )
	_box( st, Vector3( -0.11, 1.3, -0.11 ), Vector3( 0.11, 1.56, 0.11 ), Color( 0.78, 0.66, 0.54 ) )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## One crenel merlon (垛口) of a city-wall parapet; +X runs along the wall.
static func merlon() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_box( st, Vector3( -0.6, 0.0, -0.28 ), Vector3( 0.6, 1.1, 0.28 ), CITY_WALL_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## 码头 landing stairs: descend along +Z from the quay (y = 0) to `drop` below
## it over `run` metres. Each tread is a solid block down to the riverbed, so
## the flight reads as masonry rather than a floating ladder.
static func river_steps( width: float, drop: float, run: float ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var count := maxi( ceili( drop / 0.32 ), 2 )
	var tread := run / float( count )
	var bottom := -drop - 1.5
	for k in count:
		var top := -drop * float( k ) / float( count )
		_box( st, Vector3( -width * 0.5, bottom, tread * k ), Vector3( width * 0.5, top, tread * ( k + 1 ) ), MASONRY_COL )
	# Landing cheek walls.
	for sx in [ -1.0, 1.0 ]:
		var x0: float = sx * width * 0.5
		_box( st, Vector3( minf( x0, x0 + sx * 0.5 ), bottom, 0.0 ), Vector3( maxf( x0, x0 + sx * 0.5 ), 0.25, run ), MASONRY_DARK )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## An upward-facing sheet lofted across matching polylines (land terraces,
## the water surface). Every row must have the same point count; consecutive
## rows are joined quad by quad. Tag water meshes so a style pass can give
## them their own material.
static func sheet( rows: Array, col: Color, water: bool = false ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	for r in rows.size() - 1:
		var a: PackedVector3Array = rows[ r ]
		var b: PackedVector3Array = rows[ r + 1 ]
		for i in mini( a.size(), b.size() ) - 1:
			_quad_facing( st, a[ i ], a[ i + 1 ], b[ i + 1 ], b[ i ], Vector3.UP, col )
	st.generate_normals()
	st.set_material( _water_material() if water else _material() )
	var mesh := st.commit()
	if water:
		mesh.set_meta( "town_water", true )
	return mesh


## A vertical masonry face along a polyline — the river revetment (驳岸).
## `outward` holds one horizontal normal per point, pointing at the water. A
## darker band at the bottom reads as the wet waterline.
static func revetment( pts: PackedVector3Array, outward: PackedVector3Array, top_y: float, band_y: float, bottom_y: float ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	for i in pts.size() - 1:
		var p0 := pts[ i ]
		var p1 := pts[ i + 1 ]
		var n := ( outward[ i ] + outward[ i + 1 ] ).normalized()
		for band in [ [ band_y, top_y, MASONRY_COL ], [ bottom_y, band_y, MASONRY_DARK ] ]:
			var y_lo: float = band[ 0 ]
			var y_hi: float = band[ 1 ]
			_quad_facing( st, Vector3( p0.x, y_lo, p0.z ), Vector3( p1.x, y_lo, p1.z ),
				Vector3( p1.x, y_hi, p1.z ), Vector3( p0.x, y_hi, p0.z ), n, band[ 2 ] )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A far mountain range: a strip of peaks along local X, `length` long, facing
## +Z. Sum-of-sines profile, so it is deterministic from `seed` alone.
static func distant_ridge( length: float, height: float, seed: int ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var steps := 96
	var r := RandomNumberGenerator.new()
	r.seed = seed
	var f := [ r.randf_range( 1.5, 2.5 ), r.randf_range( 4.0, 6.0 ), r.randf_range( 9.0, 13.0 ) ]
	var ph := [ r.randf() * TAU, r.randf() * TAU, r.randf() * TAU ]
	var prev_top := Vector3.ZERO
	var prev_back := Vector3.ZERO
	var prev_base := Vector3.ZERO
	_smooth = true
	for k in steps + 1:
		var t := float( k ) / float( steps )
		var x := ( t - 0.5 ) * length
		var h := 0.55 + 0.25 * sin( f[ 0 ] * TAU * t + ph[ 0 ] ) + 0.14 * sin( f[ 1 ] * TAU * t + ph[ 1 ] ) \
			+ 0.06 * sin( f[ 2 ] * TAU * t + ph[ 2 ] )
		# Taper the ends into the plain so the range has no cut-off edge.
		h *= smoothstep( 0.0, 0.15, t ) * smoothstep( 1.0, 0.85, t )
		var top := Vector3( x, height * h, -height * 0.4 )
		var base := Vector3( x, -2.0, height * 0.9 )
		var back := Vector3( x, -2.0, -height * 1.6 )
		if k > 0:
			_quad_facing( st, prev_base, base, top, prev_top, Vector3( 0, 0.5, 1 ), RIDGE_COL )
			_quad_facing( st, prev_top, top, back, prev_back, Vector3( 0, 0.5, -1 ), RIDGE_COL )
		prev_top = top
		prev_base = base
		prev_back = back
	_smooth = false
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## Boat hull lofted from cross sections along X. y = 0 is the waterline;
## `freeboard` is deck height amidships, the ends sweep up by `sheer`.
static func _hull( st: SurfaceTool, length: float, beam: float, freeboard: float, sheer: float ) -> void:
	var sections := 10
	var prev: Array = []
	for k in sections + 1:
		var t := float( k ) / float( sections )
		var x := ( t - 0.5 ) * length
		var u := absf( t - 0.5 ) * 2.0
		var hw := maxf( beam * 0.5 * sqrt( maxf( 1.0 - u * u, 0.0 ) ), 0.08 )
		var top := freeboard + sheer * u * u
		var cur := [
			Vector3( x, top, -hw ),
			Vector3( x, -0.35, -hw * 0.45 ),
			Vector3( x, -0.35, hw * 0.45 ),
			Vector3( x, top, hw ),
		]
		if k > 0:
			_quad_facing( st, prev[ 0 ], cur[ 0 ], cur[ 1 ], prev[ 1 ], Vector3( 0, 0, -1 ), HULL_COL )
			_quad_facing( st, prev[ 1 ], cur[ 1 ], cur[ 2 ], prev[ 2 ], Vector3.DOWN, HULL_COL )
			_quad_facing( st, prev[ 2 ], cur[ 2 ], cur[ 3 ], prev[ 3 ], Vector3( 0, 0, 1 ), HULL_COL )
			_quad_facing( st, prev[ 3 ], cur[ 3 ], cur[ 0 ], prev[ 0 ], Vector3.UP, TIMBER_DARK )
		prev = cur


# --- helpers -------------------------------------------------------------

## A quad whose winding is chosen so its face normal agrees with `facing`.
## Godot's front face (and `generate_normals()`) follows clockwise winding, so
## the right order is the one whose right-handed cross product points *away*
## from `facing`. Lets generated shells be written without hand-checking every
## vertex order.
static func _quad_facing( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, d: Vector3, facing: Vector3, col: Color ) -> void:
	if ( b - a ).cross( c - a ).dot( facing ) <= 0.0:
		_quad( st, a, b, c, d, col )
	else:
		_quad( st, d, c, b, a, col )


static func _tri( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, col: Color ) -> void:
	# Flat shading unless a builder opts into smoothing: generate_normals()
	# otherwise smooths across every shared position, which turned stretched
	# road boxes into diagonal gradients.
	st.set_smooth_group( 0 if _smooth else -1 )
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
	# Every face through _quad_facing: the hand-wound order this used to have was
	# counter-clockwise, i.e. inside-out under Godot's culling — roads showed
	# their unlit bottom face from above.
	_quad_facing( st, c001, c101, c111, c011, Vector3( 0, 0, 1 ), col )
	_quad_facing( st, c100, c000, c010, c110, Vector3( 0, 0, -1 ), col )
	_quad_facing( st, c101, c100, c110, c111, Vector3( 1, 0, 0 ), col )
	_quad_facing( st, c000, c001, c011, c010, Vector3( -1, 0, 0 ), col )
	_quad_facing( st, c011, c111, c110, c010, Vector3.UP, col )
	_quad_facing( st, c000, c100, c101, c001, Vector3.DOWN, col )
