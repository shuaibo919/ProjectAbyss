@tool
extends RefCounted
class_name PcgVillageMeshes

## Factory for greybox Penglai fishing-village pieces used by the scatter PCG
## tools: houses (several variants), landmark structures, and karst sea-stack
## pillars. Same philosophy as [PcgSplineMeshes] / [PcgPropMeshes]: neutral,
## textureless ArrayMeshes with per-vertex colors so one MultiMeshInstance3D
## renders each variant with no material setup.
##
## Orientation contract (scatter tools drop these on raycast ground hits):
##   - origin is the ground anchor (y = 0 sits on the hit point),
##   - +Z is the house "front" (scatter adds random yaw anyway),
##   - authored at real-world-ish scale (a small hut ~3m wide).
##
## Visual language from the concept sheets (ProjectAbyssWiki penglai_concepts):
## dark timber walls, pale plaster, grey-green tiled roofs with deep eaves,
## stilted shore houses, layered pagoda, pale karst rock streaked with green.

const TIMBER_DARK := Color( 0.23, 0.18, 0.14 )
const TIMBER_WARM := Color( 0.35, 0.26, 0.18 )
const PLASTER := Color( 0.72, 0.68, 0.60 )
const ROOF_GREY := Color( 0.27, 0.30, 0.32 )   # grey-green tile
const ROOF_DARK := Color( 0.20, 0.23, 0.25 )
const ROOF_RED := Color( 0.42, 0.22, 0.16 )    # faded vermilion (pagoda)
const STILT_COL := Color( 0.25, 0.20, 0.15 )
const KARST_PALE := Color( 0.62, 0.60, 0.54 )
const KARST_DARK := Color( 0.46, 0.45, 0.42 )
const KARST_GREEN := Color( 0.30, 0.38, 0.24 ) # clinging vegetation

static var _vc_mat: StandardMaterial3D = null

static func _material() -> StandardMaterial3D:
	if _vc_mat == null:
		_vc_mat = StandardMaterial3D.new()
		_vc_mat.vertex_color_use_as_albedo = true
		_vc_mat.roughness = 0.94
	return _vc_mat


# =========================================================================
# HOUSES
# =========================================================================

## A small fisher's hut: plaster box + gabled tile roof with eave overhang.
static func house_hut( w: float = 3.0, d: float = 2.6, wall_h: float = 1.9 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_walls_gable( st, w, d, wall_h, PLASTER, TIMBER_DARK )
	_roof_gable( st, w * 1.18, d * 1.15, wall_h, wall_h * 0.55, ROOF_GREY )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A timber house: dark wood walls, longer plan, slightly taller roof.
static func house_timber( w: float = 3.6, d: float = 3.0, wall_h: float = 2.2 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_walls_gable( st, w, d, wall_h, TIMBER_WARM, TIMBER_DARK )
	_roof_gable( st, w * 1.2, d * 1.18, wall_h, wall_h * 0.5, ROOF_DARK )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A two-story house: plaster lower, timber upper, tile roof — village core.
static func house_two_story( w: float = 3.4, d: float = 3.0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var h1 := 2.0
	var h2 := 1.7
	_box( st, Vector3( -w * 0.5, 0, -d * 0.5 ), Vector3( w * 0.5, h1, d * 0.5 ), PLASTER )
	var w2 := w * 1.06   # upper floor juts slightly (jetty)
	var d2 := d * 1.06
	_box( st, Vector3( -w2 * 0.5, h1, -d2 * 0.5 ), Vector3( w2 * 0.5, h1 + h2, d2 * 0.5 ), TIMBER_WARM )
	_roof_gable( st, w2 * 1.2, d2 * 1.18, h1 + h2, 1.0, ROOF_GREY )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A stilt house for the shoreline: living box raised on 4 legs, ladder-side
## open. Anchor y=0 at the STILT BASE so it reads right on sloped shore ground.
static func house_stilt( w: float = 3.0, d: float = 2.6, stilt_h: float = 1.6 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var leg := 0.14
	for sx in [ -1.0, 1.0 ]:
		for sz in [ -1.0, 1.0 ]:
			var cx: float = sx * ( w * 0.5 - leg )
			var cz: float = sz * ( d * 0.5 - leg )
			_box( st, Vector3( cx - leg, 0, cz - leg ), Vector3( cx + leg, stilt_h, cz + leg ), STILT_COL )
	# Deck slab + house on top.
	_box( st, Vector3( -w * 0.55, stilt_h, -d * 0.55 ), Vector3( w * 0.55, stilt_h + 0.12, d * 0.55 ), TIMBER_DARK )
	var wall_h := 1.8
	_walls_gable( st, w * 0.94, d * 0.94, wall_h, TIMBER_WARM, TIMBER_DARK, stilt_h + 0.12 )
	_roof_gable( st, w * 1.12, d * 1.1, stilt_h + 0.12 + wall_h, 0.8, ROOF_DARK )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


# =========================================================================
# LANDMARKS
# =========================================================================

## A layered pagoda: [tiers] stacked, shrinking floors, each with a flared tile
## roof; topped with a finial. The island-summit landmark from the concepts.
static func pagoda( tiers: int = 4, base_w: float = 5.0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	tiers = clampi( tiers, 2, 7 )
	var y := 0.0
	var w := base_w
	for t in range( tiers ):
		var floor_h := 1.7 * ( 1.0 - float( t ) * 0.06 )
		_box( st, Vector3( -w * 0.5, y, -w * 0.5 ), Vector3( w * 0.5, y + floor_h, w * 0.5 ), PLASTER if t % 2 == 0 else TIMBER_WARM )
		# Flared roof plate wider than the floor, thin, dark red.
		var rw := w * 1.45
		_pyramid_roof( st, rw, y + floor_h, 0.55, ROOF_RED if t < tiers - 1 else ROOF_DARK )
		y += floor_h + 0.35
		w *= 0.82
	# Finial spike.
	_box( st, Vector3( -0.08, y, -0.08 ), Vector3( 0.08, y + 1.2, 0.08 ), TIMBER_DARK )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


# =========================================================================
# KARST ROCK
# =========================================================================

## A karst sea-stack pillar: tall tapering faceted column, wider cap than waist
## (the signature undercut silhouette), green vegetation tuft on top. Anchor at
## y=0 (waterline/seabed); [height] above the anchor. Seeded jitter per variant.
static func karst_stack( height: float = 10.0, base_r: float = 2.2, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	var radial := 7
	# Ring radii from base up: base wide, waist narrow, shoulder wide (undercut), cap.
	var profile := [
		Vector2( base_r * 1.0, 0.0 ),
		Vector2( base_r * 0.72, height * 0.3 ),
		Vector2( base_r * 0.62, height * 0.55 ),
		Vector2( base_r * 0.85, height * 0.8 ),   # shoulder bulge
		Vector2( base_r * 0.55, height * 1.0 ),
	]
	var rings: Array = []
	for p in profile:
		var ring: Array[Vector3] = []
		for a in range( radial ):
			var ang := TAU * float( a ) / float( radial )
			var jitter := 1.0 + rng.randf_range( -0.16, 0.16 )
			ring.append( Vector3( cos( ang ) * p.x * jitter, p.y, sin( ang ) * p.x * jitter ) )
		rings.append( ring )
	for i in range( rings.size() - 1 ):
		var col := KARST_PALE if i % 2 == 0 else KARST_DARK
		for a in range( radial ):
			var a1 := ( a + 1 ) % radial
			_quad( st, rings[i][a], rings[i][a1], rings[i + 1][a1], rings[i + 1][a], col )
	# Cap fan + vegetation tuft.
	var top_ring: Array = rings[rings.size() - 1]
	var apex := Vector3( 0, height * 1.04, 0 )
	for a in range( radial ):
		_tri( st, top_ring[a], top_ring[( a + 1 ) % radial], apex, KARST_GREEN )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


# --- shared shape helpers --------------------------------------------------

## Gabled wall block: rectangular walls + triangular gable ends along X faces.
static func _walls_gable( st: SurfaceTool, w: float, d: float, wall_h: float, wall_col: Color, trim_col: Color, y0: float = 0.0 ) -> void:
	_box( st, Vector3( -w * 0.5, y0, -d * 0.5 ), Vector3( w * 0.5, y0 + wall_h, d * 0.5 ), wall_col )
	# Gable triangles rise from wall top to the ridge on both Z ends.
	var ridge_h := wall_h * 0.45
	var y1 := y0 + wall_h
	for sz in [ -1.0, 1.0 ]:
		var z: float = sz * d * 0.5
		var a := Vector3( -w * 0.5, y1, z )
		var b := Vector3( w * 0.5, y1, z )
		var c := Vector3( 0, y1 + ridge_h, z )
		if sz > 0:
			_tri( st, a, b, c, wall_col )
		else:
			_tri( st, b, a, c, wall_col )
	# Corner trim posts.
	var t := 0.09
	for sx in [ -1.0, 1.0 ]:
		for sz in [ -1.0, 1.0 ]:
			var cx: float = sx * ( w * 0.5 - t * 0.5 )
			var cz: float = sz * ( d * 0.5 - t * 0.5 )
			_box( st, Vector3( cx - t, y0, cz - t ), Vector3( cx + t, y1, cz + t ), trim_col )


## Gabled roof: two slabs meeting at a ridge along Z, overhanging eaves.
static func _roof_gable( st: SurfaceTool, w: float, d: float, y_eave: float, rise: float, col: Color ) -> void:
	var hw := w * 0.5
	var hd := d * 0.5
	var ridge := y_eave + rise
	var t := 0.12   # roof slab thickness (visual)
	for sx in [ -1.0, 1.0 ]:
		var eave_out := Vector3( sx * hw, y_eave, 0 )
		# Slab as a thin quad box from ridge line down to the eave, full depth.
		var r0 := Vector3( 0, ridge, -hd )
		var r1 := Vector3( 0, ridge, hd )
		var e0 := Vector3( eave_out.x, y_eave, -hd )
		var e1 := Vector3( eave_out.x, y_eave, hd )
		if sx > 0:
			_quad( st, r0, r1, e1, e0, col )
			_quad( st, r0 + Vector3( 0, t, 0 ), e0 + Vector3( 0, t, 0 ), e1 + Vector3( 0, t, 0 ), r1 + Vector3( 0, t, 0 ), col )
		else:
			_quad( st, r1, r0, e0, e1, col )
			_quad( st, r1 + Vector3( 0, t, 0 ), e1 + Vector3( 0, t, 0 ), e0 + Vector3( 0, t, 0 ), r0 + Vector3( 0, t, 0 ), col )


## Square pyramid roof plate (pagoda tier): flat square slab with raised centre.
static func _pyramid_roof( st: SurfaceTool, w: float, y: float, rise: float, col: Color ) -> void:
	var hw := w * 0.5
	var c := Vector3( 0, y + rise, 0 )
	var p0 := Vector3( -hw, y, -hw )
	var p1 := Vector3( hw, y, -hw )
	var p2 := Vector3( hw, y, hw )
	var p3 := Vector3( -hw, y, hw )
	_tri( st, p0, p1, c, col )
	_tri( st, p1, p2, c, col )
	_tri( st, p2, p3, c, col )
	_tri( st, p3, p0, c, col )
	# Underside so eaves read from below.
	_tri( st, p1, p0, c, col )
	_tri( st, p2, p1, c, col )
	_tri( st, p3, p2, c, col )
	_tri( st, p0, p3, c, col )


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


static func _tri( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, col: Color ) -> void:
	st.set_color( col ); st.add_vertex( a )
	st.set_color( col ); st.add_vertex( b )
	st.set_color( col ); st.add_vertex( c )


static func _quad( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, d: Vector3, col: Color ) -> void:
	_tri( st, a, b, c, col )
	_tri( st, a, c, d, col )
