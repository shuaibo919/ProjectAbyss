@tool
extends RefCounted
class_name PcgPropMeshes

## Factory for slightly-refined greybox placeholder meshes used by the PCG
## scatter graphs — still neutral/natural toned (no textures), but a step up
## from raw cubes: trunked conifers, layered broadleaf canopies, irregular
## faceted rocks. All return a single ArrayMesh so one spawn_meshes node can
## instance the whole prop per point.

const TRUNK_COL := Color( 0.34, 0.26, 0.18 )
const FOLIAGE_COL := Color( 0.24, 0.36, 0.20 )
const FOLIAGE_HI := Color( 0.30, 0.42, 0.24 )
const ROCK_COL := Color( 0.46, 0.46, 0.46 )
const ROCK_DARK := Color( 0.38, 0.38, 0.40 )

# Shared material that renders the mesh's own per-vertex colors as albedo, so a
# single ArrayMesh can carry trunk/foliage/rock tints with no textures.
static var _vc_mat: StandardMaterial3D = null

static func _vertex_color_material() -> StandardMaterial3D:
	if _vc_mat == null:
		_vc_mat = StandardMaterial3D.new()
		_vc_mat.vertex_color_use_as_albedo = true
		_vc_mat.roughness = 0.9
	return _vc_mat


## A conifer: short trunk + 2–3 stacked tapered cones. Origin at base (y=0).
static func conifer( height: float = 4.0, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )

	var trunk_h := height * 0.22
	var trunk_r := height * 0.045
	_add_cylinder( st, Vector3( 0, 0, 0 ), trunk_r, trunk_r, trunk_h, 6, TRUNK_COL )

	# Stacked canopy cones, each smaller and higher.
	var tiers := 3
	var canopy_base := trunk_h * 0.7
	var canopy_h := height - canopy_base
	for i in range( tiers ):
		var t := float( i ) / float( tiers )
		var y0 := canopy_base + canopy_h * ( t * 0.62 )
		var seg_h := canopy_h * 0.5
		var r := ( height * 0.26 ) * ( 1.0 - t * 0.55 )
		var col := FOLIAGE_COL if ( i % 2 == 0 ) else FOLIAGE_HI
		_add_cylinder( st, Vector3( 0, y0, 0 ), 0.0, r, seg_h, 7, col )

	st.generate_normals()
	st.set_material( _vertex_color_material() )
	return st.commit()


## A broadleaf: trunk + a lumpy rounded canopy approximated by stacked discs.
static func broadleaf( height: float = 3.5, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )

	var trunk_h := height * 0.4
	var trunk_r := height * 0.05
	_add_cylinder( st, Vector3( 0, 0, 0 ), trunk_r * 0.8, trunk_r, trunk_h, 6, TRUNK_COL )

	# Canopy: an oblate blob from 3 offset spit cylinders/cones.
	var cr := height * 0.34
	_add_cylinder( st, Vector3( 0, trunk_h, 0 ), cr * 0.6, cr, height * 0.28, 8, FOLIAGE_COL )
	_add_cylinder( st, Vector3( 0, trunk_h + height * 0.24, 0 ), 0.0, cr * 0.92, height * 0.34, 8, FOLIAGE_HI )

	st.generate_normals()
	st.set_material( _vertex_color_material() )
	return st.commit()


## An irregular faceted rock: a low icosphere-ish blob, jittered by seed.
static func rock( radius: float = 1.0, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )

	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	# Two stacked rings + apex/base => a faceted boulder, squashed in Y.
	var rings := 2
	var radial := 6
	var verts: Array[Vector3] = []
	verts.append( Vector3( 0, -radius * 0.5, 0 ) )      # base
	for ring in range( rings ):
		var ry := lerpf( -0.25, 0.55, float( ring ) / float( rings - 1 ) ) * radius
		var rr := radius * ( 1.0 - absf( ry / radius ) * 0.4 )
		for a in range( radial ):
			var ang := TAU * float( a ) / float( radial )
			var jitter := 1.0 + rng.randf_range( -0.22, 0.22 )
			verts.append( Vector3( cos( ang ) * rr * jitter, ry, sin( ang ) * rr * jitter ) )
	verts.append( Vector3( 0, radius * 0.6, 0 ) )       # apex

	var apex := verts.size() - 1
	# base fan
	for a in range( radial ):
		var v0 := 1 + a
		var v1 := 1 + ( a + 1 ) % radial
		_tri( st, verts[0], verts[v1], verts[v0], ROCK_DARK )
	# side band
	var r0 := 1
	var r1 := 1 + radial
	for a in range( radial ):
		var a0 := a
		var a1 := ( a + 1 ) % radial
		_quad( st, verts[r0 + a0], verts[r0 + a1], verts[r1 + a1], verts[r1 + a0], ROCK_COL )
	# top fan
	for a in range( radial ):
		var v0 := r1 + a
		var v1 := r1 + ( a + 1 ) % radial
		_tri( st, verts[v0], verts[v1], verts[apex], ROCK_COL )

	st.generate_normals()
	st.set_material( _vertex_color_material() )
	return st.commit()


# --- helpers -------------------------------------------------------------

static func _add_cylinder( st: SurfaceTool, base: Vector3, top_r: float, bot_r: float, h: float, seg: int, col: Color ) -> void:
	var y0 := base.y
	var y1 := base.y + h
	for a in range( seg ):
		var a0 := TAU * float( a ) / float( seg )
		var a1 := TAU * float( a + 1 ) / float( seg )
		var b0 := base + Vector3( cos( a0 ) * bot_r, 0, sin( a0 ) * bot_r )
		var b1 := base + Vector3( cos( a1 ) * bot_r, 0, sin( a1 ) * bot_r )
		var t0 := Vector3( base.x + cos( a0 ) * top_r, y1, base.z + sin( a0 ) * top_r )
		var t1 := Vector3( base.x + cos( a1 ) * top_r, y1, base.z + sin( a1 ) * top_r )
		b0.y = y0
		b1.y = y0
		if top_r <= 0.0001:
			# cone
			var apex := Vector3( base.x, y1, base.z )
			_tri( st, b0, b1, apex, col )
		else:
			_quad( st, b0, b1, t1, t0, col )


static func _tri( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, col: Color ) -> void:
	st.set_color( col )
	st.add_vertex( a )
	st.set_color( col )
	st.add_vertex( b )
	st.set_color( col )
	st.add_vertex( c )


static func _quad( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, d: Vector3, col: Color ) -> void:
	_tri( st, a, b, c, col )
	_tri( st, a, c, d, col )
