@tool
extends Node3D
class_name PcgTerraceToolNode

## Terraced-field PCG tool (梯田) — drapes stepped agricultural terraces over a
## hillside. Place the node over a slope (e.g. a PcgTerrainToolNode island),
## size the region, and it:
##
##   1. raycasts a grid of cells straight down onto whatever ground colliders
##      exist below,
##   2. quantizes each cell's hit height UP to a multiple of [member step_height]
##      (cut-and-fill reads: the platform rides just above the natural grade,
##      its skirt walls hide the gap),
##   3. builds ONE ArrayMesh: a flat paddy platform per cell, plus a stone
##      retaining-wall face on every edge where the neighbouring cell steps
##      down (or is outside the terrace) — the classic contour-terrace look.
##
## Cells outside [member height_band], on ground steeper than
## [member slope_limit], or where the ray misses, are skipped — so the terraces
## naturally hug the mid-slope band and stop at cliffs and the beach.
##
## Direct GDScript generation (no Flow graph): the whole terrace is one mesh, so
## per-point instancing buys nothing here. Regenerates on any Inspector change,
## waiting for physics so the terrain collider below is live.

# --- palette ---------------------------------------------------------------
const PADDY_A := Color( 0.45, 0.54, 0.30 )   # young green field
const PADDY_B := Color( 0.52, 0.58, 0.33 )   # drier level (alternates by tier)
const PADDY_WET := Color( 0.40, 0.50, 0.42 ) # flooded paddy sheen
const WALL_COL := Color( 0.48, 0.45, 0.40 )  # dry-stone retaining wall
const RIM_COL := Color( 0.38, 0.34, 0.27 )   # earthen bund on each platform lip

@export var enabled : bool = true:
	set( v ):
		enabled = v
		_mark_dirty()

## Region footprint (m) centred on this node that the terraces may cover.
@export var region_size : Vector2 = Vector2( 36, 26 ):
	set( v ):
		region_size = Vector2( maxf( 4.0, v.x ), maxf( 4.0, v.y ) )
		_mark_dirty()

## Cell size (m) of one terrace patch — smaller = finer-grained steps.
@export var cell_size : float = 2.2:
	set( v ):
		cell_size = maxf( 0.5, v )
		_mark_dirty()

## Vertical rise between terrace levels.
@export var step_height : float = 1.6:
	set( v ):
		step_height = maxf( 0.3, v )
		_mark_dirty()

## Only ground whose height (global y) falls in this band is terraced.
@export var height_band : Vector2 = Vector2( 4.0, 15.0 ):
	set( v ):
		height_band = v
		_mark_dirty()

## Max ground steepness accepted, as the normal's minimum y (1 = flat only,
## lower accepts steeper). ~0.45 keeps terraces off true cliff faces.
@export_range( 0.2, 1.0 ) var slope_limit : float = 0.45:
	set( v ):
		slope_limit = clampf( v, 0.2, 1.0 )
		_mark_dirty()

## Noise-thins the coverage so the field patchwork has organic gaps (0 = solid
## coverage, higher = more gaps).
@export_range( 0.0, 0.9 ) var patchiness : float = 0.25:
	set( v ):
		patchiness = clampf( v, 0.0, 0.9 )
		_mark_dirty()

## Fraction of levels rendered as flooded paddies (water-sheen tint).
@export_range( 0.0, 1.0 ) var wet_fraction : float = 0.35:
	set( v ):
		wet_fraction = clampf( v, 0.0, 1.0 )
		_mark_dirty()

@export var seed : int = 5:
	set( v ):
		seed = v
		_mark_dirty()

## Physics frames to wait before regenerating (terrain collider must be live).
@export var settle_frames : int = 8

## Inspector button: tick to force a rebuild now.
@export var regenerate_now : bool = false:
	set( v ):
		regenerate_now = false
		if v:
			regenerate()


var _dirty : bool = false
var _regenerating : bool = false


func _ready() -> void:
	_mark_dirty()


func _mark_dirty() -> void:
	if not is_inside_tree():
		return
	_dirty = true
	if not _regenerating:
		_deferred_regenerate.call_deferred()


func _deferred_regenerate() -> void:
	if _regenerating:
		return
	_regenerating = true
	while _dirty:
		_dirty = false
		for i in range( maxi( 1, settle_frames ) ):
			await get_tree().physics_frame
		regenerate()
	_regenerating = false


## Rebuild the terrace mesh from the current parameters.
func regenerate() -> void:
	if not is_inside_tree():
		return
	_clear_generated()
	if not enabled:
		return
	var world := get_world_3d()
	if world == null:
		return
	var space := world.direct_space_state
	if space == null:
		return

	var fnl := FastNoiseLite.new()
	fnl.seed = seed
	fnl.noise_type = FastNoiseLite.TYPE_SIMPLEX
	fnl.frequency = 0.06

	var nx := maxi( 2, int( region_size.x / cell_size ) )
	var nz := maxi( 2, int( region_size.y / cell_size ) )
	var origin: Vector3 = global_transform.origin
	var x0 := origin.x - 0.5 * nx * cell_size
	var z0 := origin.z - 0.5 * nz * cell_size

	# --- 1+2. Sample the ground per cell and quantize to terrace levels ------
	# levels[j][i] = terrace platform height, or NAN when the cell is skipped.
	var levels: Array = []
	var query := PhysicsRayQueryParameters3D.create( Vector3.ZERO, Vector3.ZERO )
	query.collision_mask = 1
	for j in range( nz ):
		var row := PackedFloat32Array()
		row.resize( nx )
		for i in range( nx ):
			row[i] = NAN
			var cx := x0 + ( i + 0.5 ) * cell_size
			var cz := z0 + ( j + 0.5 ) * cell_size
			query.from = Vector3( cx, origin.y + 120.0, cz )
			query.to = query.from + Vector3( 0, -260.0, 0 )
			var hit: Dictionary = space.intersect_ray( query )
			if hit.is_empty():
				continue
			var h: float = hit.position.y
			var n: Vector3 = hit.normal
			if h < height_band.x or h > height_band.y:
				continue
			if n.y < slope_limit:
				continue
			if patchiness > 0.0 and fnl.get_noise_2d( cx, cz ) < lerpf( -1.0, 0.5, patchiness ):
				continue
			row[i] = ceilf( h / step_height ) * step_height
		levels.append( row )

	# --- 3. One mesh: platforms + walls where the neighbour steps down -------
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var quads := 0
	var wet_rng := RandomNumberGenerator.new()
	wet_rng.seed = seed * 17 + 3
	for j in range( nz ):
		for i in range( nx ):
			var h: float = levels[j][i]
			if is_nan( h ):
				continue
			var tier := int( roundf( h / step_height ) )
			var wet := _tier_is_wet( tier )
			var col := PADDY_WET if wet else ( PADDY_A if tier % 2 == 0 else PADDY_B )
			var cx0 := x0 + i * cell_size
			var cz0 := z0 + j * cell_size
			var y := h + 0.06   # ride just above the quantized grade
			# Platform quad (local to this node so the packed scene stays clean).
			var p00 := to_local( Vector3( cx0, y, cz0 ) )
			var p10 := to_local( Vector3( cx0 + cell_size, y, cz0 ) )
			var p01 := to_local( Vector3( cx0, y, cz0 + cell_size ) )
			var p11 := to_local( Vector3( cx0 + cell_size, y, cz0 + cell_size ) )
			_quad( st, p00, p10, p11, p01, col )
			quads += 1
			# Retaining wall on any edge where the neighbour is lower / missing.
			# Wall drops far enough to bury its base in the natural grade.
			var drop := step_height * 1.5
			_edge_wall( st, i, j, -1, 0, levels, nx, nz, h, drop, p00, p01, RIM_COL )
			_edge_wall( st, i, j, 1, 0, levels, nx, nz, h, drop, p11, p10, RIM_COL )
			_edge_wall( st, i, j, 0, -1, levels, nx, nz, h, drop, p10, p00, RIM_COL )
			_edge_wall( st, i, j, 0, 1, levels, nx, nz, h, drop, p01, p11, RIM_COL )
	if quads == 0:
		return
	st.generate_normals()
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 0.95
	st.set_material( mat )

	var mi := MeshInstance3D.new()
	mi.name = "TerraceMesh"
	mi.mesh = st.commit()
	add_child( mi )
	mi.set_meta( "pcg_terrace", true )


## Deterministic wet/dry per tier so all cells of one level agree (a paddy is
## flooded as a whole, not per-cell).
func _tier_is_wet( tier: int ) -> bool:
	if wet_fraction <= 0.0:
		return false
	return fposmod( float( hash( [ tier, seed ] ) % 1000 ) / 1000.0, 1.0 ) < wet_fraction


## Emit a retaining wall along one platform edge if the neighbour cell sits
## lower (or out of the terrace). [a]/[b] are the platform-top corners of that
## edge, already in local space, ordered so the wall faces outward.
func _edge_wall( st: SurfaceTool, i: int, j: int, di: int, dj: int, levels: Array, nx: int, nz: int, h: float, drop: float, a: Vector3, b: Vector3, rim_col: Color ) -> void:
	var ni := i + di
	var nj := j + dj
	var nh := NAN
	if ni >= 0 and ni < nx and nj >= 0 and nj < nz:
		nh = levels[nj][ni]
	if not is_nan( nh ) and nh >= h - 0.01:
		return   # neighbour is level or higher — no exposed face
	# Wall from the platform lip down. If the neighbour is a known lower level,
	# reach just below its top; otherwise use the full drop into the grade.
	var bottom := h - drop
	if not is_nan( nh ):
		bottom = nh - 0.3
	# Platform verts are local and the node is unrotated/unscaled in practice —
	# the wall is the same edge shifted straight down by the drop.
	var dy := ( h + 0.06 ) - bottom
	var a_lo := a - Vector3( 0, dy, 0 )
	var b_lo := b - Vector3( 0, dy, 0 )
	_quad( st, a, b, b_lo, a_lo, WALL_COL )
	# A thin earthen bund strip along the lip so the edge reads from above.
	var lip := 0.12
	_quad( st, a + Vector3( 0, lip, 0 ), b + Vector3( 0, lip, 0 ), b, a, rim_col )


func _clear_generated() -> void:
	for child in get_children():
		if child.has_meta( "pcg_terrace" ):
			remove_child( child )
			child.queue_free()


func _quad( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, d: Vector3, col: Color ) -> void:
	st.set_color( col ); st.add_vertex( a )
	st.set_color( col ); st.add_vertex( b )
	st.set_color( col ); st.add_vertex( c )
	st.set_color( col ); st.add_vertex( a )
	st.set_color( col ); st.add_vertex( c )
	st.set_color( col ); st.add_vertex( d )
