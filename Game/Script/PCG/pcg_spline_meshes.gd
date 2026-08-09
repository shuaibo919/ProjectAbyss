@tool
extends RefCounted
class_name PcgSplineMeshes

## Factory for greybox modular pieces used by the spline-driven PCG tools
## (dock / fence / road). Same philosophy as [PcgPropMeshes]: neutral, textureless
## ArrayMeshes that carry their own per-vertex colors so one MultiMeshInstance3D
## can render each variant with no material setup.
##
## Every piece is authored in LOCAL space with an orientation contract that
## matches how sample_spline / split_splines lay points down:
##   - +Z is the travel direction along the spline (segment forward),
##   - +X is the lateral (cross-section) direction,
##   - origin is at the mesh's ground anchor (y = 0 sits on the sampled point),
## so a chain of instances tiles seamlessly along a path.
##
## Pieces come in variants so the spawn_meshes node's mesh_variants + weights can
## pick between them per point — that is the "变体 / variation" knob the tools expose.

# --- Palettes ------------------------------------------------------------
const WOOD_WARM := Color( 0.42, 0.30, 0.19 )
const WOOD_COOL := Color( 0.36, 0.27, 0.20 )
const WOOD_GREY := Color( 0.30, 0.28, 0.26 )   # weathered / driftwood
const PILING_COL := Color( 0.28, 0.22, 0.16 )
const PILING_WET := Color( 0.17, 0.16, 0.14 )   # water-darkened lower timber
const ALGAE_COL := Color( 0.24, 0.30, 0.18 )    # slimy green tide-line growth
const METAL_COL := Color( 0.24, 0.24, 0.27 )   # bollard / iron
const STONE_LIGHT := Color( 0.62, 0.60, 0.55 )
const STONE_DARK := Color( 0.44, 0.43, 0.41 )
const DIRT_COL := Color( 0.34, 0.24, 0.15 )
const COBBLE_COL := Color( 0.56, 0.55, 0.53 )
const PAVED_COL := Color( 0.30, 0.30, 0.33 )

static var _vc_mat: StandardMaterial3D = null
static var _vc_mat_two_sided: StandardMaterial3D = null

static func _material() -> StandardMaterial3D:
	if _vc_mat == null:
		_vc_mat = StandardMaterial3D.new()
		_vc_mat.vertex_color_use_as_albedo = true
		_vc_mat.roughness = 0.92
	return _vc_mat


## Two-sided variant for thin sheet geometry (draped nets) — lets a single-layer
## quad strip be seen from both rims without doubling the triangle count.
static func _material_two_sided() -> StandardMaterial3D:
	if _vc_mat_two_sided == null:
		_vc_mat_two_sided = StandardMaterial3D.new()
		_vc_mat_two_sided.vertex_color_use_as_albedo = true
		_vc_mat_two_sided.roughness = 0.92
		_vc_mat_two_sided.cull_mode = BaseMaterial3D.CULL_DISABLED
	return _vc_mat_two_sided


# =========================================================================
# DOCK pieces
# =========================================================================

## A deck plank slab: spans [width] in X (across the dock), [length] in Z (along
## travel), [thickness] tall. Top sits at y=0 (anchor is the deck surface), so a
## row of planks lays a continuous walking surface at the sampled height.
static func deck_plank( width: float = 4.0, length: float = 1.0, thickness: float = 0.18, tint: Color = WOOD_WARM ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	# Slight overlap in Z so consecutive planks never gap on curves.
	_box( st, Vector3( -width * 0.5, -thickness, -length * 0.52 ), Vector3( width * 0.5, 0.0, length * 0.52 ), tint )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## Two deck-edge rails (low curbs) running along Z on both X sides of the deck.
## Purely a variant flavor for the deck (a "with-railing" plank).
static func deck_plank_railed( width: float = 4.0, length: float = 1.0, thickness: float = 0.18 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_box( st, Vector3( -width * 0.5, -thickness, -length * 0.52 ), Vector3( width * 0.5, 0.0, length * 0.52 ), WOOD_COOL )
	var rail_h := 0.9
	var rail_w := 0.12
	for sx in [ -1.0, 1.0 ]:
		var cx: float = sx * ( width * 0.5 - rail_w * 0.5 )
		_box( st, Vector3( cx - rail_w * 0.5, 0.0, -length * 0.52 ), Vector3( cx + rail_w * 0.5, rail_h, length * 0.52 ), WOOD_GREY )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A vertical piling/leg: a tapered square post that starts at y=0 (deck bottom)
## and drops [depth] downward. spawn_meshes stretches Z-bounds, not this, so the
## piling keeps its own height; [depth] should exceed deck-to-seabed distance.
## [waterline] is how far below the deck anchor the water surface sits — below it
## the timber is water-darkened and a slimy algae ring marks the tide line, so the
## leg reads as grounded in water rather than a uniform dry stick. Pass
## waterline <= 0 to skip the wet band (a fully dry inland leg).
static func piling( depth: float = 8.0, radius: float = 0.28, waterline: float = 1.4 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var top := 0.2
	var bot := -depth - 0.2
	if waterline > 0.0 and -waterline > bot:
		var wl := -waterline
		# Leg tapers from `radius` at the deck top down to `radius*0.8` at the
		# seabed; find the radius at the waterline so the dry/wet prisms meet flush.
		# _prism puts bot_r at base.y and top_r at base.y+h, so we anchor each
		# segment at its LOWER y with positive height and pass (top_r, bot_r) as
		# (upper radius, lower radius) — earlier code anchored at the top with a
		# negative height, which silently inverted the taper and left a step at wl.
		var frac := ( top - wl ) / ( top - bot )
		var r_wl : float = lerpf( radius, radius * 0.8, frac )
		_prism( st, Vector3( 0, wl, 0 ), radius, r_wl, top - wl, 8, PILING_COL )        # dry upper: r_wl@wl → radius@top
		_prism( st, Vector3( 0, bot, 0 ), r_wl, radius * 0.8, wl - bot, 8, PILING_WET )  # wet lower: 0.8r@bot → r_wl@wl
		# Algae collar just above the waterline where growth clings to the tide zone.
		_torus( st, Vector3( 0, wl + radius * 0.15, 0 ), r_wl * 1.02, radius * 0.16, 8, 4, ALGAE_COL )
	else:
		_prism( st, Vector3( 0, bot, 0 ), radius, radius * 0.8, top - bot, 8, PILING_COL )  # radius@top → 0.8r@bot
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A mooring bollard: short stout metal/wood post with a cap. Anchor at y=0.
static func bollard( height: float = 0.9, radius: float = 0.22, tint: Color = METAL_COL ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_prism( st, Vector3( 0, 0, 0 ), radius, radius, height, 8, tint )
	_prism( st, Vector3( 0, height, 0 ), radius * 1.35, radius * 1.35, radius * 0.6, 8, tint )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A dock railing unit: a post + top rail spanning [span] along +Z, meant to be
## laid along a deck edge (like the fence unit, but a lighter handrail styled to
## match the deck). Anchor at the post base (y=0), so it drops onto the deck rim.
static func dock_railing( span: float = 1.0, post_h: float = 1.0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var pw := 0.08
	_box( st, Vector3( -pw, 0.0, -pw ), Vector3( pw, post_h, pw ), WOOD_WARM )
	# Top handrail + a mid rail running the full [span] to meet the next unit, so
	# consecutive units form ONE continuous rail (nets/swags can crest it). The
	# rails start at z=0 (this post) and run +span to the next post's base.
	var rail_t := 0.06
	for ry in [ post_h - rail_t, post_h * 0.5 ]:
		_box( st, Vector3( -rail_t, ry - rail_t, 0.0 ), Vector3( rail_t, ry + rail_t, span ), WOOD_COOL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A cliff-walk support unit, anchored at the CLIFF-side deck edge (y=0 is the
## deck surface, +X points INTO the rock face). One bearer beam runs back under
## the full deck width (-[deck_w]..0) and a diagonal knee strut drops from the
## outer bearer end down-inward to a rock pad at (+[reach], -[drop]) — the
## classic plank-road bracket (栈道). Symmetric in Z so a 180° yaw mirrors it
## for a left-hand cliff.
static func cliff_brace( deck_w: float = 2.4, drop: float = 1.8, reach: float = 1.1 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var t := 0.11
	# Bearer beam carrying the planks, tucked under the deck.
	_box( st, Vector3( -deck_w, -0.24, -t ), Vector3( 0.15, -0.02, t ), BRACE_COL )
	# Diagonal strut: outer bearer end → rock anchor pad. Built as a sheared box.
	var strut_top := Vector3( -deck_w * 0.92, -0.24, 0 )
	var strut_bot := Vector3( reach, -drop, 0 )
	var dir := strut_bot - strut_top
	var steps := 4
	for k in range( steps ):
		var f0 := float( k ) / float( steps )
		var f1 := float( k + 1 ) / float( steps )
		var p0 := strut_top + dir * f0
		var p1 := strut_top + dir * f1
		_box( st, Vector3( minf( p0.x, p1.x ) - t, minf( p0.y, p1.y ) - t, -t ), Vector3( maxf( p0.x, p1.x ) + t, maxf( p0.y, p1.y ) + t, t ), BRACE_COL )
	# Rock anchor pad.
	_box( st, Vector3( reach - 0.16, -drop - 0.16, -0.18 ), Vector3( reach + 0.2, -drop + 0.2, 0.18 ), METAL_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


# --- structural richness under / around the deck -------------------------

const LAMP_POST_COL := Color( 0.20, 0.19, 0.18 )   # dark iron lamp column
const LAMP_GLASS_COL := Color( 0.95, 0.86, 0.55 )  # warm lantern glow (bright)
const BRACE_COL := Color( 0.30, 0.24, 0.17 )       # tarred timber strut

## A diagonal cross-brace tying the two piling lines together UNDER the deck: an
## X of two struts in the X-Y plane plus a horizontal tie, spanning [width] in X
## (match this to the piling lateral separation, i.e. 2 * piling X-offset). The
## X reaches from [top_depth] below the deck down to [bottom_depth]. Authored to
## the spline contract (origin at deck anchor y=0, +X lateral, +Z travel) so it
## drops onto centreline samples and straddles the port/starboard pilings.
static func piling_cross_brace( width: float = 3.4, top_depth: float = 1.2, bottom_depth: float = 4.5, r: float = 0.09 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var hw := width * 0.5
	var yt := -top_depth
	var yb := -bottom_depth
	# The X: each strut runs from one piling's upper station to the other's lower.
	_beam( st, Vector3( -hw, yt, 0 ), Vector3( hw, yb, 0 ), r, BRACE_COL )
	_beam( st, Vector3( hw, yt, 0 ), Vector3( -hw, yb, 0 ), r, BRACE_COL )
	# Horizontal tie at the crossing depth so the brace reads as a truss, not a
	# loose scissor.
	var ymid := ( yt + yb ) * 0.5
	_beam( st, Vector3( -hw, ymid, 0 ), Vector3( hw, ymid, 0 ), r * 0.85, BRACE_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A quayside lamp post: a slim iron column rising to [height] with a boxy
## lantern head (bright warm glass) and a small cap, anchored at y=0 so it stands
## on the deck rim. +Z is travel; the lantern is centred so it reads from either
## rim. Meant to be spaced sparsely along the railing like real dock lighting.
static func lamp_post( height: float = 2.4, tint: Color = LAMP_POST_COL ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	# Column: an octagonal iron post with a slightly wider footing.
	_prism( st, Vector3( 0, 0, 0 ), 0.06, 0.11, height * 0.06, 8, tint )         # footing
	_prism( st, Vector3( 0, height * 0.06, 0 ), 0.05, 0.06, height * 0.82, 8, tint )   # shaft
	# A little bracket ring where the lantern mounts.
	_torus( st, Vector3( 0, height * 0.88, 0 ), 0.1, 0.03, 8, 4, tint )
	# Lantern cage: a bright glass box held in a dark frame.
	var lh := height * 0.16
	var lw := 0.14
	var ly := height * 0.9
	_box( st, Vector3( -lw, ly, -lw ), Vector3( lw, ly + lh, lw ), LAMP_GLASS_COL )
	# Dark corner frame posts so the head reads as a lantern, not a glowing cube.
	var fw := 0.03
	for sx in [ -1.0, 1.0 ]:
		for sz in [ -1.0, 1.0 ]:
			var cx: float = sx * lw
			var cz: float = sz * lw
			_box( st, Vector3( cx - fw, ly, cz - fw ), Vector3( cx + fw, ly + lh, cz + fw ), tint )
	# Peaked cap on top.
	_prism( st, Vector3( 0, ly + lh, 0 ), 0.0, lw * 1.15, lh * 0.7, 4, tint )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A hanging rope swag (mooring festoon) strung across one gap between mooring
## posts along +Z. The mesh is anchored CENTRED on a post (the placement samples
## sit on the bollard stations), so the rope must attach HIGH at the centre (on
## the post it sits on) and dip toward its two ends, where it meets the low ends
## of the neighbouring swags in the middle of each gap: centre u=0 → y=hang_h,
## ends u=±1 → y=hang_h - sag. Two consecutive festoons therefore touch at their
## low points mid-gap and rise to the shared post between them — a real festoon.
## Spans [span] along Z, authored on the rim line (X=0). Built from [segs] short
## beam links so the droop reads smooth. Place at bollard stations with
## span = bollard spacing so each swag bridges one gap between mooring posts.
static func rope_swag( span: float = 6.0, hang_h: float = 0.85, sag: float = 0.5, segs: int = 8, r: float = 0.045 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	segs = maxi( 2, segs )
	var half := span * 0.5
	var prev := Vector3.ZERO
	for i in range( segs + 1 ):
		var t := float( i ) / float( segs )         # 0..1
		var z := lerpf( -half, half, t )
		# Parabola peaking (attached, HIGH) at the centre where it sits on a post,
		# dipping to its lowest at the two ends where it meets the neighbouring
		# festoons mid-gap.
		var u := ( 2.0 * t - 1.0 )                  # -1..1 across the span
		var y := hang_h - sag * ( u * u )
		var cur := Vector3( 0, y, z )
		if i > 0:
			_beam( st, prev, cur, r, ROPE_COL )
		prev = cur
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


# =========================================================================
# DOCK CLUTTER  (props scattered on the deck surface — crates, barrels, nets,
# fish. All anchored at y=0 so they sit on the planks; small enough to cluster.)
# =========================================================================

const CRATE_COL := Color( 0.46, 0.33, 0.19 )
const CRATE_COL2 := Color( 0.38, 0.28, 0.17 )
const BARREL_COL := Color( 0.34, 0.26, 0.17 )
const NET_COL := Color( 0.30, 0.34, 0.26 )
const FISH_COL := Color( 0.55, 0.58, 0.62 )
const ROPE_COL := Color( 0.52, 0.45, 0.30 )    # tarred hemp / manila coil
const BUOY_COL := Color( 0.62, 0.24, 0.20 )     # painted float
const BUOY_BAND := Color( 0.78, 0.76, 0.70 )    # weathered stripe
const WICKER_COL := Color( 0.50, 0.40, 0.24 )   # woven fish basket
const PALLET_COL := Color( 0.40, 0.33, 0.22 )   # pallet slats

## A cargo crate: a wooden box with a lid-rim, optionally stacked. Anchor y=0.
static func cargo_crate( size: float = 0.7, stacked: bool = false ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var h := size
	_box( st, Vector3( -size * 0.5, 0.0, -size * 0.5 ), Vector3( size * 0.5, h, size * 0.5 ), CRATE_COL )
	# A thin rim near the top edge to read as a lid.
	_box( st, Vector3( -size * 0.52, h * 0.82, -size * 0.52 ), Vector3( size * 0.52, h * 0.9, size * 0.52 ), CRATE_COL2 )
	if stacked:
		# A smaller crate perched on top, offset for a haphazard look.
		var s2 := size * 0.66
		var ox := size * 0.12
		_box( st, Vector3( -s2 * 0.5 + ox, h, -s2 * 0.5 - ox ), Vector3( s2 * 0.5 + ox, h + s2, s2 * 0.5 - ox ), CRATE_COL2 )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A barrel: a squat octagonal prism with a lighter mid band. Anchor y=0.
static func barrel( height: float = 0.8, radius: float = 0.3 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	# Slight bulge: wider in the middle by using a mid ring is overkill for
	# greybox; a straight prism reads fine, with a band for the hoop.
	_prism( st, Vector3( 0, 0, 0 ), radius * 0.92, radius * 0.92, height, 8, BARREL_COL )
	_prism( st, Vector3( 0, height * 0.45, 0 ), radius, radius, height * 0.1, 8, CRATE_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A heaped fishing net: a low lumpy mound approximated by a squashed faceted
## dome. Anchor y=0. [seed] jitters the silhouette so instances differ.
static func fishing_net( radius: float = 0.6, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	var radial := 7
	var ring_y := radius * 0.05
	var top := Vector3( 0, radius * 0.5, 0 )
	var ring: Array[Vector3] = []
	for a in range( radial ):
		var ang := TAU * float( a ) / float( radial )
		var jitter := 1.0 + rng.randf_range( -0.18, 0.18 )
		ring.append( Vector3( cos( ang ) * radius * jitter, ring_y, sin( ang ) * radius * jitter ) )
	for a in range( radial ):
		var v0 := ring[a]
		var v1 := ring[( a + 1 ) % radial]
		# side skirt down to ground + top fan to the apex
		_tri( st, Vector3( v0.x, 0, v0.z ), Vector3( v1.x, 0, v1.z ), v1, NET_COL )
		_tri( st, Vector3( v0.x, 0, v0.z ), v1, v0, NET_COL )
		_tri( st, v0, v1, top, NET_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A small pile of fish catch: a few overlapping flattened ellipsoid-ish blobs.
## Anchor y=0. Kept tiny — meant to sit beside crates/nets.
static func fish_catch( scale: float = 0.5, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	var count := 3
	for i in range( count ):
		var ox := rng.randf_range( -0.5, 0.5 ) * scale
		var oz := rng.randf_range( -0.5, 0.5 ) * scale
		var r := scale * rng.randf_range( 0.18, 0.28 )
		# A tiny 4-sided pyramid pair = a crude fish body.
		var base := Vector3( ox, 0.02, oz )
		_prism( st, base, 0.0, r, r * 1.4, 5, FISH_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A pallet stacked into a small cargo tower: a slatted pallet base with
## [levels] crates piled on top, each smaller and jittered for a leaning,
## hand-stacked read. Anchor y=0 (pallet sits flat on the deck). [seed] varies
## the lean/offset so instances differ. This is the "vertical stacking" prop —
## reads much taller than a single crate so a clump gains real silhouette.
static func crate_stack( size: float = 0.7, levels: int = 3, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	levels = clampi( levels, 1, 4 )
	# Slatted pallet base: a thin wide slab slightly larger than the crates so
	# the tower reads as sitting ON a pallet, not floating.
	var pallet_h := size * 0.16
	var pw := size * 0.62
	_box( st, Vector3( -pw, 0.0, -pw ), Vector3( pw, pallet_h, pw ), PALLET_COL )
	# Stack crates upward, each shrinking a touch and nudged laterally so the
	# tower leans like real hand-piled cargo (never past the pallet footprint).
	var y := pallet_h
	var cur := size
	for i in range( levels ):
		var half := cur * 0.5
		var jx := rng.randf_range( -0.12, 0.12 ) * size * float( i )
		var jz := rng.randf_range( -0.12, 0.12 ) * size * float( i )
		var col := CRATE_COL if ( i % 2 == 0 ) else CRATE_COL2
		_box( st, Vector3( jx - half, y, jz - half ), Vector3( jx + half, y + cur, jz + half ), col )
		# Lid rim so each crate reads as a discrete box in the stack.
		_box( st, Vector3( jx - half * 1.04, y + cur * 0.84, jz - half * 1.04 ),
			Vector3( jx + half * 1.04, y + cur * 0.92, jz + half * 1.04 ), CRATE_COL2 )
		y += cur
		cur *= 0.82
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A coiled mooring rope: two stacked flattened rings (tori) tapering inward, so
## it reads as a neatly flaked coil left on the deck. Anchor y=0. Low profile —
## meant to sit beside bollards/cleats. [radius] is the outer coil radius.
static func coiled_rope( radius: float = 0.45 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var minor := radius * 0.16    # rope thickness
	# Two loops: the lower wider ring and a slightly smaller ring resting on it.
	_torus( st, Vector3( 0, minor, 0 ), radius - minor, minor, 10, 5, ROPE_COL )
	_torus( st, Vector3( 0, minor * 2.6, 0 ), radius - minor * 2.2, minor, 10, 5, WOOD_GREY )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A mooring buoy: a faceted ball (two cones base-to-base) on a tiny nub, with a
## painted mid band. Anchor y=0, so it rests on the deck. [radius] is the ball
## half-height (ball spans y=0..2*radius).
static func buoy( radius: float = 0.34 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var seg := 8
	# A short cylindrical skirt at the base gives the float a small flat footprint
	# (bot_r ~0.5*radius) so it sits believably on the deck rather than balancing
	# on a point. Above the skirt it bulges to the equator, then narrows to a cap.
	var base_r := radius * 0.5
	var skirt_h := radius * 0.22
	_prism( st, Vector3( 0, 0, 0 ), base_r, base_r, skirt_h, seg, BUOY_COL )
	_prism( st, Vector3( 0, skirt_h, 0 ), radius, base_r, radius - skirt_h, seg, BUOY_COL )
	_prism( st, Vector3( 0, radius, 0 ), radius * 0.28, radius, radius, seg, BUOY_COL )
	# Painted stripe band around the equator.
	_torus( st, Vector3( 0, radius, 0 ), radius * 0.92, radius * 0.14, seg, 4, BUOY_BAND )
	# Little top nub / lifting eye.
	_prism( st, Vector3( 0, radius * 2.0, 0 ), radius * 0.1, radius * 0.14, radius * 0.35, 6, METAL_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A woven fish basket: a tapered open-topped cylinder (wider at the rim) with a
## rope-toned lip and a couple of fish poking out. Anchor y=0. [height] tall.
static func fish_basket( radius: float = 0.34, height: float = 0.5, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	# Body: narrower base, wider mouth (classic creel silhouette).
	_prism( st, Vector3( 0, 0, 0 ), radius, radius * 0.72, height, 8, WICKER_COL )
	# Rim lip.
	_torus( st, Vector3( 0, height, 0 ), radius * 0.94, radius * 0.1, 8, 4, ROPE_COL )
	# A couple of fish spilling over the rim.
	for i in range( 2 ):
		var ox := rng.randf_range( -0.4, 0.4 ) * radius
		var oz := rng.randf_range( -0.4, 0.4 ) * radius
		_prism( st, Vector3( ox, height * 0.85, oz ), 0.0, radius * 0.22, radius * 0.6, 5, FISH_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A cluster of [count] barrels grouped shoulder-to-shoulder with jittered
## heights, for a fuller footprint than a lone barrel. Anchor y=0. [seed] varies
## the arrangement.
static func barrel_cluster( count: int = 3, radius: float = 0.28, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	count = clampi( count, 2, 4 )
	# Place barrels around a tight ring so they touch but don't interpenetrate.
	var ring_r := radius * 1.05
	for i in range( count ):
		var ang := TAU * float( i ) / float( count ) + rng.randf_range( -0.2, 0.2 )
		var cx := cos( ang ) * ring_r
		var cz := sin( ang ) * ring_r
		var h := rng.randf_range( 0.72, 0.9 )
		_prism( st, Vector3( cx, 0, cz ), radius * 0.92, radius * 0.92, h, 8, BARREL_COL )
		_prism( st, Vector3( cx, h * 0.45, cz ), radius, radius, h * 0.1, 8, CRATE_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A metal deck cleat: a low pedestal with a horn bar crossing it (the classic
## anvil/T mooring fitting). Anchor y=0, kept low so it hugs the planks. The horn
## crosses +X so it reads regardless of which rim it lands on.
static func mooring_cleat( length: float = 0.55 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var base_h := length * 0.28
	var bw := length * 0.16
	# Central pedestal.
	_box( st, Vector3( -bw, 0.0, -bw ), Vector3( bw, base_h, bw ), METAL_COL )
	# Horn bar across +X, sitting on the pedestal top; slight overhang each side.
	var horn_r := length * 0.11
	_box( st, Vector3( -length * 0.5, base_h - horn_r, -horn_r ),
		Vector3( length * 0.5, base_h + horn_r, horn_r ), METAL_COL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A fishing net DRAPED over a deck railing: a sagging sheet that rises from the
## deck on the inner side, up over the rail top, and hangs down the outer side.
## Authored so X=0 sits on the rail line (+X = outboard), +Z runs along the rail;
## place it via a rim offset so it straddles the handrail rather than lying flat.
## [span] along Z, [rail_h] where the rail top is, [drop] how far it hangs.
static func fishing_net_draped( span: float = 1.4, rail_h: float = 0.9, drop: float = 0.7, seed: int = 0 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	# Cross-section profile over the rail (inner deck → over rail → outboard), as
	# (x, y) pairs. The net clears the rail top then sags down each side.
	var inner := 0.55       # how far inboard the net reaches
	var outer := 0.6        # how far the outer skirt hangs out
	var profile: Array[Vector2] = [
		Vector2( -inner, rail_h * 0.15 ),          # tucked on the deck inboard
		Vector2( -0.1, rail_h + 0.06 ),            # cresting the rail
		Vector2( 0.1, rail_h + 0.06 ),
		Vector2( outer, rail_h - drop ),           # sagging skirt outboard
	]
	var nz := 4
	var half_span := span * 0.5
	# Per-profile-index vertical jitter, computed ONCE per profile vertex so the
	# shared edge between adjacent profile bands (profile[pi+1] is both segment
	# pi's 'pb' and segment pi+1's 'pa') gets the SAME jitter — the 3 bands stay
	# welded instead of cracking apart in Y. The sin() term below gives the real
	# per-z sag (the fabric bellies between support points); the jitter only
	# breaks the straight profile silhouette.
	var pj: Array[float] = []
	for pi in range( profile.size() ):
		pj.append( rng.randf_range( -0.03, 0.03 ) )
	# Build the drape as a single strip of quads. The net material is two-sided
	# (cull disabled), so one layer reads from both rims — no doubled geometry.
	for zi in range( nz ):
		var z0 := lerpf( -half_span, half_span, float( zi ) / float( nz ) )
		var z1 := lerpf( -half_span, half_span, float( zi + 1 ) / float( nz ) )
		var sag0 := sin( PI * float( zi ) / float( nz ) ) * drop * 0.12
		var sag1 := sin( PI * float( zi + 1 ) / float( nz ) ) * drop * 0.12
		for pi in range( profile.size() - 1 ):
			var pa := profile[pi]
			var pb := profile[pi + 1]
			var ja := pj[pi]
			var jb := pj[pi + 1]
			var a0 := Vector3( pa.x, pa.y - sag0 + ja, z0 )
			var a1 := Vector3( pb.x, pb.y - sag0 + jb, z0 )
			var b1 := Vector3( pb.x, pb.y - sag1 + jb, z1 )
			var b0 := Vector3( pa.x, pa.y - sag1 + ja, z1 )
			_quad( st, a0, a1, b1, b0, NET_COL )
	st.generate_normals()
	st.set_material( _material_two_sided() )
	return st.commit()


# =========================================================================
# STRUCTURAL CONNECTORS  (built to exact dimensions per placement, so these are
# spawned as individual MeshInstance3D by the post-process — NOT MultiMesh
# variants. Each bridges a specific geometric gap: a rise between deck levels,
# or an intersection footprint.)
# =========================================================================

## A staircase climbing from y=0 at the near end (+Z near = low) up by [rise]
## over a horizontal run of [run], spanning [width] in X. Built as [steps]
## discrete boxes so it reads as stairs, not a ramp. Origin at the bottom-front
## edge centre; +Z is the climb direction. Deck-toned.
static func stairs( width: float = 3.0, rise: float = 1.5, run: float = 2.5, steps: int = 5 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	steps = maxi( 1, steps )
	var step_rise := rise / float( steps )
	var step_run := run / float( steps )
	var hw := width * 0.5
	for i in range( steps ):
		# Each tread is a box from the ground up to its step height, so the
		# stack forms a solid stepped wedge (closed sides, no gaps under treads).
		var y_top := step_rise * float( i + 1 )
		var z0 := step_run * float( i )
		var z1 := run
		var col := WOOD_WARM if ( i % 2 == 0 ) else WOOD_COOL
		_box( st, Vector3( -hw, 0.0, z0 ), Vector3( hw, y_top, z1 ), col )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A junction platform: a flat deck slab of [size_x] × [size_z] whose TOP sits
## slightly PROUD of the deck plane (at +[proud], default +0.12 like road_slab),
## [thickness] thick. Centred on origin. The proud top defeats the coplanar
## z-fight where two decks (plank tops at y=0) cross and the platform lands on
## the same plane — the landing reads as a built-up deck plate over the crossing.
## For the cross/T shape variants, pass a bigger square — the greybox reads fine
## as a widened landing. Deck-toned, optional rim for a finished edge.
static func junction_platform( size_x: float = 5.0, size_z: float = 5.0, thickness: float = 0.22, proud: float = 0.12 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_box( st, Vector3( -size_x * 0.5, proud - thickness, -size_z * 0.5 ), Vector3( size_x * 0.5, proud, size_z * 0.5 ), WOOD_COOL )
	# A low rim lip around the top edge so the platform reads as a built landing.
	var lip := 0.12
	var lh := proud + 0.14
	_box( st, Vector3( -size_x * 0.5, proud, -size_z * 0.5 ), Vector3( size_x * 0.5, lh, -size_z * 0.5 + lip ), WOOD_WARM )
	_box( st, Vector3( -size_x * 0.5, proud, size_z * 0.5 - lip ), Vector3( size_x * 0.5, lh, size_z * 0.5 ), WOOD_WARM )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()



# =========================================================================
# FENCE pieces  (one self-contained post+rail unit per spline segment)
# =========================================================================

## Wooden fence unit: a post at the segment start plus two horizontal rails
## spanning [span] along +Z to meet the next unit. Anchor at the post base (y=0).
static func fence_wood( span: float = 2.0, post_h: float = 1.5 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var pw := 0.14
	_box( st, Vector3( -pw, 0.0, -pw ), Vector3( pw, post_h, pw ), WOOD_WARM )
	var rail_t := 0.08
	for ry in [ post_h * 0.4, post_h * 0.82 ]:
		_box( st, Vector3( -rail_t, ry - rail_t, 0.0 ), Vector3( rail_t, ry + rail_t, span ), WOOD_COOL )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## Stone pillar + low wall unit — a heavier variant.
static func fence_stone( span: float = 2.0, post_h: float = 1.3 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var pw := 0.24
	_box( st, Vector3( -pw, 0.0, -pw ), Vector3( pw, post_h, pw ), STONE_LIGHT )
	# Low connecting wall to the next pillar.
	_box( st, Vector3( -0.14, 0.0, 0.0 ), Vector3( 0.14, post_h * 0.6, span ), STONE_DARK )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A broken/collapsed fence unit: a leaning short post, no rail — adds decay.
static func fence_broken( post_h: float = 0.8 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	var pw := 0.13
	# Leaned by shearing the top of the post in X.
	var lean := 0.35
	_box_sheared( st, Vector3( -pw, 0.0, -pw ), Vector3( pw, post_h, pw ), Vector3( lean, 0, 0 ), WOOD_GREY )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


# =========================================================================
# ROAD pieces  (flat slabs laid along the path, draped on terrain)
# =========================================================================

## A road slab: spans [width] in X, [length] in Z, laid just above the ground.
## Sits slightly proud (top at +0.12) so it reads against the terrain instead of
## z-fighting / blending into it.
static func road_slab( width: float = 5.0, length: float = 1.2, tint: Color = PAVED_COL ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_box( st, Vector3( -width * 0.5, -0.1, -length * 0.55 ), Vector3( width * 0.5, 0.12, length * 0.55 ), tint )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


## A cobbled road slab: paved base with two shoulder curbs (variant flavor).
static func road_cobble( width: float = 5.0, length: float = 1.2 ) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin( Mesh.PRIMITIVE_TRIANGLES )
	_box( st, Vector3( -width * 0.5, -0.1, -length * 0.55 ), Vector3( width * 0.5, 0.03, length * 0.55 ), COBBLE_COL )
	for sx in [ -1.0, 1.0 ]:
		var cx: float = sx * ( width * 0.5 - 0.18 )
		_box( st, Vector3( cx - 0.16, 0.0, -length * 0.55 ), Vector3( cx + 0.16, 0.14, length * 0.55 ), STONE_DARK )
	st.generate_normals()
	st.set_material( _material() )
	return st.commit()


# --- geometry helpers ----------------------------------------------------

## Axis-aligned box from min corner to max corner, all 6 faces, flat color.
static func _box( st: SurfaceTool, mn: Vector3, mx: Vector3, col: Color ) -> void:
	_box_sheared( st, mn, mx, Vector3.ZERO, col )


## Box whose top face is displaced by [top_shift] (for leaning pieces).
static func _box_sheared( st: SurfaceTool, mn: Vector3, mx: Vector3, top_shift: Vector3, col: Color ) -> void:
	# 8 corners; top 4 (max Y) get shifted.
	var s := top_shift
	var c000 := Vector3( mn.x, mn.y, mn.z )
	var c100 := Vector3( mx.x, mn.y, mn.z )
	var c101 := Vector3( mx.x, mn.y, mx.z )
	var c001 := Vector3( mn.x, mn.y, mx.z )
	var c010 := Vector3( mn.x, mx.y, mn.z ) + s
	var c110 := Vector3( mx.x, mx.y, mn.z ) + s
	var c111 := Vector3( mx.x, mx.y, mx.z ) + s
	var c011 := Vector3( mn.x, mx.y, mx.z ) + s
	_quad( st, c001, c101, c111, c011, col )   # +Z
	_quad( st, c100, c000, c010, c110, col )   # -Z
	_quad( st, c101, c100, c110, c111, col )   # +X
	_quad( st, c000, c001, c011, c010, col )   # -X
	_quad( st, c011, c111, c110, c010, col )   # +Y (top)
	_quad( st, c000, c100, c101, c001, col )   # -Y (bottom)


## Vertical prism (regular n-gon cross-section) from base upward by [h].
## [h] may be negative to build downward (pilings).
static func _prism( st: SurfaceTool, base: Vector3, top_r: float, bot_r: float, h: float, seg: int, col: Color ) -> void:
	var y0 := base.y
	var y1 := base.y + h
	for a in range( seg ):
		var a0 := TAU * float( a ) / float( seg )
		var a1 := TAU * float( a + 1 ) / float( seg )
		var b0 := Vector3( base.x + cos( a0 ) * bot_r, y0, base.z + sin( a0 ) * bot_r )
		var b1 := Vector3( base.x + cos( a1 ) * bot_r, y0, base.z + sin( a1 ) * bot_r )
		var t0 := Vector3( base.x + cos( a0 ) * top_r, y1, base.z + sin( a0 ) * top_r )
		var t1 := Vector3( base.x + cos( a1 ) * top_r, y1, base.z + sin( a1 ) * top_r )
		if top_r <= 0.0001:
			var apex := Vector3( base.x, y1, base.z )
			_tri( st, b0, b1, apex, col )
		else:
			_quad( st, b0, b1, t1, t0, col )


## Ring torus centred on [center] in the XZ plane: [major_r] is the ring radius,
## [minor_r] the tube radius. [seg] segments around the ring, [tube_seg] around
## the tube. Flat-shaded, flat color — used for rope coils / buoy bands / rims.
static func _torus( st: SurfaceTool, center: Vector3, major_r: float, minor_r: float, seg: int, tube_seg: int, col: Color ) -> void:
	for a in range( seg ):
		var a0 := TAU * float( a ) / float( seg )
		var a1 := TAU * float( a + 1 ) / float( seg )
		for t in range( tube_seg ):
			var t0 := TAU * float( t ) / float( tube_seg )
			var t1 := TAU * float( t + 1 ) / float( tube_seg )
			var p00 := _torus_pt( center, major_r, minor_r, a0, t0 )
			var p01 := _torus_pt( center, major_r, minor_r, a0, t1 )
			var p10 := _torus_pt( center, major_r, minor_r, a1, t0 )
			var p11 := _torus_pt( center, major_r, minor_r, a1, t1 )
			_quad( st, p00, p10, p11, p01, col )


static func _torus_pt( center: Vector3, major_r: float, minor_r: float, ring_a: float, tube_a: float ) -> Vector3:
	var out_dir := Vector3( cos( ring_a ), 0.0, sin( ring_a ) )
	var ring_c := center + out_dir * major_r
	return ring_c + out_dir * ( cos( tube_a ) * minor_r ) + Vector3( 0, sin( tube_a ) * minor_r, 0 )


static func _tri( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, col: Color ) -> void:
	st.set_color( col ); st.add_vertex( a )
	st.set_color( col ); st.add_vertex( b )
	st.set_color( col ); st.add_vertex( c )


## A square-section beam between two arbitrary points [a] and [b], half-thickness
## [r]. Builds an oriented box (4 side quads + 2 end caps) so diagonal struts,
## catenary rope segments, and lamp arms can be authored from endpoint to
## endpoint without axis-aligned constraints.
static func _beam( st: SurfaceTool, a: Vector3, b: Vector3, r: float, col: Color ) -> void:
	var dir := b - a
	var length := dir.length()
	if length < 0.0001:
		return
	dir /= length
	# Pick a stable 'up' reference; swap when the beam runs near-vertical so the
	# cross-frame never degenerates.
	var up_ref := Vector3.UP if absf( dir.dot( Vector3.UP ) ) < 0.95 else Vector3.FORWARD
	var right := up_ref.cross( dir ).normalized() * r
	var up := dir.cross( right ).normalized() * r
	var a0 := a - right - up
	var a1 := a + right - up
	var a2 := a + right + up
	var a3 := a - right + up
	var b0 := b - right - up
	var b1 := b + right - up
	var b2 := b + right + up
	var b3 := b - right + up
	_quad( st, a0, a1, b1, b0, col )
	_quad( st, a1, a2, b2, b1, col )
	_quad( st, a2, a3, b3, b2, col )
	_quad( st, a3, a0, b0, b3, col )
	_quad( st, a3, a2, a1, a0, col )   # cap at a
	_quad( st, b0, b1, b2, b3, col )   # cap at b


static func _quad( st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, d: Vector3, col: Color ) -> void:
	_tri( st, a, b, c, col )
	_tri( st, a, c, d, col )
