@tool
extends Node3D
class_name PcgTerrainToolNode

## Procedural terrain generator — the "岛屿/地形" PCG tool. One node = one
## terrain feature, driven entirely by Inspector parameters:
##
##   ISLAND — a karst island: noise-wobbled coastline, flat sandy beach ring at
##            the waterline, ridged rocky mass rising to a peak (the Penglai
##            silhouette), plus an optional ring of karst sea-stack pillars.
##   SEABED — a gently undulating noise plane (put it below the water).
##   HILLS  — rolling ground for inland areas.
##   WATER  — a translucent sea surface disc (visual only, NO collider, so the
##            scatter tools' down-rays pass through it onto the seabed/island).
##
## Every type (except WATER) also builds a trimesh collider, so the scatter
## tools (houses/trees/rocks) and the spline tools (fence/road drape) can
## raycast onto the generated ground — chaining PCG on top of PCG.
##
## Vertex-colored like all the greybox factories; sand/grass/rock tinting is
## computed per-vertex from height + slope. Everything regenerates on any
## Inspector change (and at load), nothing is baked into the scene.

const PcgVillageMeshes := preload( "res://Script/PCG/pcg_village_meshes.gd" )

enum TerrainType { ISLAND, SEABED, HILLS, WATER }

# --- palette (matches the 彩墨-leaning greybox look) -----------------------
const SAND_COL := Color( 0.76, 0.70, 0.55 )
const GRASS_COL := Color( 0.42, 0.50, 0.33 )
const ROCK_COL := Color( 0.55, 0.53, 0.48 )
const ROCK_DARK := Color( 0.42, 0.41, 0.38 )
const SEABED_COL := Color( 0.35, 0.40, 0.36 )
const WATER_COL := Color( 0.25, 0.46, 0.55, 0.55 )

@export var terrain_type : TerrainType = TerrainType.ISLAND:
	set( v ):
		terrain_type = v
		notify_property_list_changed()
		_mark_dirty()

## Random seed driving every noise layer (same seed = same island).
@export var seed : int = 7:
	set( v ):
		seed = v
		_mark_dirty()

## Side length of the generated patch (m). The island fits inside this square.
@export var size : float = 120.0:
	set( v ):
		size = maxf( 8.0, v )
		_mark_dirty()

## Grid resolution per side (quads). 64-96 is plenty for greybox.
@export_range( 8, 160 ) var resolution : int = 72:
	set( v ):
		resolution = clampi( v, 8, 160 )
		_mark_dirty()


@export_group( "Island", "island_" )
## Peak height of the island mass above the waterline (node local y=0).
@export var island_peak : float = 26.0:
	set( v ):
		island_peak = maxf( 1.0, v )
		_mark_dirty()
## Radius of the island footprint (before coastline wobble).
@export var island_radius : float = 42.0:
	set( v ):
		island_radius = maxf( 4.0, v )
		_mark_dirty()
## How far the beach ring extends above the waterline before ground steepens.
@export var island_beach_height : float = 1.4:
	set( v ):
		island_beach_height = maxf( 0.2, v )
		_mark_dirty()
## Depth of the underwater skirt (should reach your seabed).
@export var island_skirt_depth : float = 7.0:
	set( v ):
		island_skirt_depth = maxf( 1.0, v )
		_mark_dirty()
## Ridged-noise detail strength on the rocky mass (karst cragginess).
@export_range( 0.0, 1.0 ) var island_ruggedness : float = 0.45:
	set( v ):
		island_ruggedness = clampf( v, 0.0, 1.0 )
		_mark_dirty()
## Number of karst sea-stack pillars ringed in the water around the island.
@export_range( 0, 24 ) var island_stack_count : int = 7:
	set( v ):
		island_stack_count = clampi( v, 0, 24 )
		_mark_dirty()
## Distance band (min/max factor of island_radius) where the stacks stand.
@export var island_stack_ring : Vector2 = Vector2( 1.15, 1.6 ):
	set( v ):
		island_stack_ring = v
		_mark_dirty()


@export_group( "Ground", "ground_" )
## Vertical amplitude of the SEABED / HILLS noise.
@export var ground_amplitude : float = 1.6:
	set( v ):
		ground_amplitude = maxf( 0.0, v )
		_mark_dirty()
## Noise feature scale for SEABED / HILLS (bigger = broader swells).
@export var ground_feature_size : float = 22.0:
	set( v ):
		ground_feature_size = maxf( 2.0, v )
		_mark_dirty()


var _dirty : bool = false
var _regenerating : bool = false


func _ready() -> void:
	_mark_dirty()


func _validate_property( property : Dictionary ) -> void:
	var n : String = property.name
	if n.begins_with( "island_" ) and terrain_type != TerrainType.ISLAND:
		property.usage &= ~PROPERTY_USAGE_EDITOR
	if n.begins_with( "ground_" ) and terrain_type != TerrainType.SEABED and terrain_type != TerrainType.HILLS:
		property.usage &= ~PROPERTY_USAGE_EDITOR


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
		regenerate()
		# Yield once so a burst of setter calls coalesces into few rebuilds.
		await get_tree().process_frame
	_regenerating = false


## Rebuild the terrain mesh/collider/extras from the current parameters.
func regenerate() -> void:
	if not is_inside_tree():
		return
	_clear_generated()
	match terrain_type:
		TerrainType.WATER:
			_build_water()
		TerrainType.ISLAND:
			_build_heightfield( true )
			_build_sea_stacks()
		_:
			_build_heightfield( false )


func _clear_generated() -> void:
	for child in get_children():
		if child.has_meta( "pcg_terrain" ):
			remove_child( child )
			child.queue_free()


func _mark_generated( node: Node ) -> void:
	node.set_meta( "pcg_terrain", true )


# --- water -----------------------------------------------------------------

func _build_water() -> void:
	var mi := MeshInstance3D.new()
	mi.name = "WaterSurface"
	var pm := PlaneMesh.new()
	pm.size = Vector2( size, size )
	var mat := StandardMaterial3D.new()
	mat.albedo_color = WATER_COL
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	# Matte-ish water: a tight specular lobe reads as a white glare blob from any
	# raised camera, so keep the highlight broad and dim for the greybox look.
	mat.roughness = 0.32
	mat.metallic_specular = 0.2
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	pm.material = mat
	mi.mesh = pm
	add_child( mi )
	_mark_generated( mi )


# --- heightfield (island / seabed / hills) ----------------------------------

func _height_at( x: float, z: float, fnl: FastNoiseLite, coast: FastNoiseLite ) -> float:
	match terrain_type:
		TerrainType.ISLAND:
			var r := Vector2( x, z ).length()
			# Noise-wobbled coastline so the footprint isn't a perfect circle.
			var ang := atan2( z, x )
			var wobble := 1.0 + 0.22 * coast.get_noise_2d( cos( ang ) * 3.0, sin( ang ) * 3.0 )
			var rr := island_radius * wobble
			var t := clampf( 1.0 - pow( r / maxf( rr, 0.01 ), 1.7 ), 0.0, 1.0 )
			var h := island_peak * t
			# Ridged karst detail, stronger toward the top so cliffs get craggy.
			var n := absf( fnl.get_noise_2d( x, z ) )
			h += ( 1.0 - n ) * island_ruggedness * island_peak * 0.28 * t
			# Beach flattening: compress the band just above the waterline into a
			# gentle sandy apron instead of a steep shoreline.
			if h < island_beach_height * 2.2:
				h *= 0.4
			# Underwater skirt down to the seabed so the coast has no open edge.
			if r > rr:
				var over := ( r - rr ) / maxf( size * 0.5 - rr, 0.01 )
				h = -island_skirt_depth * clampf( over * 1.6, 0.05, 1.0 )
			return h
		_:
			return ground_amplitude * fnl.get_noise_2d( x / ground_feature_size * 10.0, z / ground_feature_size * 10.0 )
	return 0.0


func _build_heightfield( is_island: bool ) -> void:
	var fnl := FastNoiseLite.new()
	fnl.seed = seed
	fnl.noise_type = FastNoiseLite.TYPE_SIMPLEX
	fnl.fractal_type = FastNoiseLite.FRACTAL_FBM
	fnl.fractal_octaves = 4
	fnl.frequency = 0.035
	var coast := FastNoiseLite.new()
	coast.seed = seed * 7 + 13
	coast.noise_type = FastNoiseLite.TYPE_SIMPLEX
	coast.frequency = 0.9

	var n := resolution
	var step := size / float( n )
	var half := size * 0.5

	# Height grid (n+1 x n+1).
	var hs := PackedFloat32Array()
	hs.resize( ( n + 1 ) * ( n + 1 ) )
	for j in range( n + 1 ):
		for i in range( n + 1 ):
			hs[j * ( n + 1 ) + i] = _height_at( -half + i * step, -half + j * step, fnl, coast )

	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	for j in range( n ):
		for i in range( n ):
			var x0 := -half + i * step
			var z0 := -half + j * step
			var p00 := Vector3( x0, hs[j * ( n + 1 ) + i], z0 )
			var p10 := Vector3( x0 + step, hs[j * ( n + 1 ) + i + 1], z0 )
			var p01 := Vector3( x0, hs[( j + 1 ) * ( n + 1 ) + i], z0 + step )
			var p11 := Vector3( x0 + step, hs[( j + 1 ) * ( n + 1 ) + i + 1], z0 + step )
			var c00 := _vertex_color( p00, p10, p01, is_island )
			var c10 := _vertex_color( p10, p11, p00, is_island )
			var c01 := _vertex_color( p01, p00, p11, is_island )
			var c11 := _vertex_color( p11, p01, p10, is_island )
			st.set_color( c00 ); st.add_vertex( p00 )
			st.set_color( c10 ); st.add_vertex( p10 )
			st.set_color( c11 ); st.add_vertex( p11 )
			st.set_color( c00 ); st.add_vertex( p00 )
			st.set_color( c11 ); st.add_vertex( p11 )
			st.set_color( c01 ); st.add_vertex( p01 )
	st.generate_normals()
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 0.96
	st.set_material( mat )
	var mesh := st.commit()

	var mi := MeshInstance3D.new()
	mi.name = "TerrainMesh"
	mi.mesh = mesh
	add_child( mi )
	_mark_generated( mi )

	# Trimesh collider so scatter/spline tools can raycast onto this ground.
	var body := StaticBody3D.new()
	body.name = "TerrainBody"
	var shape := CollisionShape3D.new()
	shape.shape = mesh.create_trimesh_shape()
	body.add_child( shape )
	add_child( body )
	_mark_generated( body )


## Sand low + flat, grass mid, rock steep/high — judged per-vertex from height
## and an approximate local slope (from the triangle neighbours).
func _vertex_color( p: Vector3, q: Vector3, r: Vector3, is_island: bool ) -> Color:
	if not is_island:
		return SEABED_COL if terrain_type == TerrainType.SEABED else GRASS_COL
	if p.y < -0.2:
		return SEABED_COL
	var normal := ( q - p ).cross( r - p ).normalized()
	var steep := absf( normal.y ) < 0.62
	if p.y < island_beach_height:
		return SAND_COL
	if steep or p.y > island_peak * 0.55:
		return ROCK_COL if int( p.x * 3.1 + p.z * 7.7 ) % 2 == 0 else ROCK_DARK
	return GRASS_COL


# --- karst sea stacks --------------------------------------------------------

func _build_sea_stacks() -> void:
	if island_stack_count <= 0:
		return
	# A few stack variants shared by one MultiMesh each (deterministic per seed).
	var rng := RandomNumberGenerator.new()
	rng.seed = seed * 31 + 5
	var variants: Array[Mesh] = [
		PcgVillageMeshes.karst_stack( 9.0, 2.0, seed + 1 ),
		PcgVillageMeshes.karst_stack( 13.0, 2.6, seed + 2 ),
		PcgVillageMeshes.karst_stack( 6.5, 1.5, seed + 3 ),
	]
	var per_variant: Array = [ [], [], [] ]
	for k in range( island_stack_count ):
		var ang := rng.randf() * TAU
		var dist := island_radius * rng.randf_range( island_stack_ring.x, island_stack_ring.y )
		var pos := Vector3( cos( ang ) * dist, -island_skirt_depth * 0.55, sin( ang ) * dist )
		var basis := Basis( Vector3.UP, rng.randf() * TAU ).scaled( Vector3.ONE * rng.randf_range( 0.75, 1.35 ) )
		per_variant[k % variants.size()].append( Transform3D( basis, pos ) )
	for v in range( variants.size() ):
		var list: Array = per_variant[v]
		if list.is_empty():
			continue
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = variants[v]
		mm.instance_count = list.size()
		for idx in range( list.size() ):
			mm.set_instance_transform( idx, list[idx] )
		var mmi := MultiMeshInstance3D.new()
		mmi.name = "SeaStacks%d" % v
		mmi.multimesh = mm
		add_child( mmi )
		_mark_generated( mmi )
