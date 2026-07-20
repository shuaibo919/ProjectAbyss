@tool
extends RefCounted
class_name PcgSplineConnectors

## Post-process geometry for the dock tool: things that need GLOBAL analysis of
## the spline set, which the per-point Flow graph can't express.
##
##  1. STAIRS at steep breakpoints — walk each curve; where the vertical rise
##     over a short horizontal run exceeds a slope threshold, drop a staircase
##     bridging that climb (gentle slopes stay as ramped deck planks).
##  2. PLATFORMS at XZ intersections — where two deck curves cross in plan view,
##     drop a junction platform at the crossing (at the mean deck height).
##
## Both spawn individual MeshInstance3D (exact-size meshes, not MultiMesh
## variants) into a caller-provided container that is cleared each rebuild.

const PcgSplineMeshes := preload( "res://Script/PCG/pcg_spline_meshes.gd" )


## Rebuild all connectors under [container] for the given [splines].
## @param p  parameter dict (see PcgSplineToolNode for keys / defaults).
static func rebuild( container: Node3D, splines: Array, p: Dictionary ) -> int:
	# Clear previous connectors.
	for child in container.get_children():
		child.queue_free()

	var owner_node: Node = p.get( "owner", null )
	var count := 0

	if p.get( "stairs_enabled", true ):
		for path in splines:
			for s in _stairs_for_curve( path, p ):
				var mi := MeshInstance3D.new()
				mi.mesh = PcgSplineMeshes.stairs( s.width, s.rise, s.run, s.steps )
				mi.transform = s.xform
				container.add_child( mi )
				if owner_node != null:
					mi.owner = owner_node
				count += 1

	if p.get( "platforms_enabled", true ) and splines.size() >= 1:
		# _platform_hits are in WORLD space; the platform MeshInstance3D is a child
		# of [container], so its position is read in the container's LOCAL space.
		# Convert so a non-identity tool-node transform still lands platforms at the
		# true crossing rather than at tool_transform * hit.
		var to_local := container.global_transform.affine_inverse()
		for hit in _platform_hits( splines, p ):
			var mi := MeshInstance3D.new()
			var sz: float = p.get( "platform_size", 5.0 )
			mi.mesh = PcgSplineMeshes.junction_platform( sz, sz, p.get( "platform_thickness", 0.22 ) )
			mi.position = to_local * hit
			container.add_child( mi )
			if owner_node != null:
				mi.owner = owner_node
			count += 1

	return count


# --- exclusion footprints (published BEFORE the Flow graph runs) ---------

## Publish a Path3D loop for every exclusion footprint into [container], tagged
## into [group_name], so the dock's clutter/drape/swag branches can clip out props
## that would land on a crossing landing OR overhang a deck END. This MUST run
## before the Flow graph is built/evaluated (the clip node scans these at eval
## time), whereas [rebuild] spawns the visible platform meshes AFTER. Two kinds:
##   1. JUNCTION footprints — a square around every XZ crossing (needs 2+ curves),
##      kept in lockstep with [_platform_hits] so a footprint matches each platform.
##   2. END-CAP footprints — a small oriented rectangle straddling each curve's two
##      terminal samples. Rim props (rope swags span ±half their span, draped nets
##      hang ±~0.7m) anchored on the LAST sample would otherwise jut out over open
##      water past the deck head; clipping the terminal anchor removes that spike
##      while leaving the next-inboard prop (a full sample spacing in) untouched.
## All loop points are stored in the container's LOCAL space (container.global_
## transform.affine_inverse() * world), so a non-identity tool-node transform still
## clips at the true world footprint. Returns the number of footprints published.
## @param p  same param dict as [rebuild]; reads platform_size, endcaps_enabled,
##           endcap_half_width, endcap_inward, endcap_outward.
static func publish_exclusions( container: Node3D, splines: Array, p: Dictionary, group_name: String ) -> int:
	# Clear previous footprints IMMEDIATELY (not deferred) — the graph runs in the
	# same synchronous pass, so a queued-but-not-freed stale loop would still be in
	# the group and clip against last frame's crossing.
	for child in container.get_children():
		container.remove_child( child )
		child.queue_free()

	var owner_node: Node = p.get( "owner", null )
	var to_local: Transform3D = container.global_transform.affine_inverse()
	var count := 0

	# --- 1. Junction crossings -------------------------------------------------
	if p.get( "platforms_enabled", true ) and splines.size() >= 2:
		# A touch larger than the platform so props straddling the rim at the
		# crossing are fully cleared, leaving an unambiguous landing.
		var half: float = maxf( 0.5, p.get( "platform_size", 5.0 ) ) * 0.5 * 1.08
		for hit in _platform_hits( splines, p ):
			var corners: Array = [
				hit + Vector3( -half, 0, -half ),
				hit + Vector3( half, 0, -half ),
				hit + Vector3( half, 0, half ),
				hit + Vector3( -half, 0, half ),
			]
			_add_loop( container, to_local, corners, group_name, owner_node )
			count += 1

	# --- 2. End caps at every terminal sample ----------------------------------
	if p.get( "endcaps_enabled", true ):
		var lat_half: float = maxf( 0.3, p.get( "endcap_half_width", 3.0 ) )
		var inward: float = maxf( 0.1, p.get( "endcap_inward", 0.5 ) )
		var outward: float = maxf( 0.1, p.get( "endcap_outward", 1.0 ) )
		for path in splines:
			for corners in _end_cap_loops( path, lat_half, inward, outward ):
				_add_loop( container, to_local, corners, group_name, owner_node )
				count += 1

	# --- 3. Stair runs — clip the deck planks that a staircase covers -----------
	# The staircase is a solid stepped wedge dropped ON the curve at a steep
	# breakpoint; without this the deck branch also tiles slanted planks through
	# that span, doubling geometry and z-fighting the treads. Emit a footprint
	# rectangle over each stair run so the deck clip removes those planks. (Only
	# the deck branch clips against stairs meaningfully; rim props rarely land in
	# the run, and clipping them too is harmless.)
	if p.get( "stairs_enabled", true ):
		var stair_lat: float = maxf( 0.3, p.get( "stair_width", 3.0 ) ) * 0.5 + 0.2
		for path in splines:
			for s in _stairs_for_curve( path, p ):
				# s.xform: origin at the low end, local +Z = climb dir, +X = lateral.
				var b: Basis = s.xform.basis
				var o: Vector3 = s.xform.origin
				var fwd: Vector3 = b.z
				var side: Vector3 = b.x
				# Pad ±0.3 along the run so the top/bottom transition planks go too.
				var z0: float = -0.3
				var z1: float = s.run + 0.3
				var corners: Array = [
					o + fwd * z0 - side * stair_lat,
					o + fwd * z1 - side * stair_lat,
					o + fwd * z1 + side * stair_lat,
					o + fwd * z0 + side * stair_lat,
				]
				_add_loop( container, to_local, corners, group_name, owner_node )
				count += 1
	return count


## Build a closed Path3D from world-space [corners] (converted to container local),
## parent it under [container], and tag it into [group_name].
static func _add_loop( container: Node3D, to_local: Transform3D, corners: Array, group_name: String, owner_node: Node ) -> void:
	var path := Path3D.new()
	var curve := Curve3D.new()
	# clip_points_by_polygon treats the open curve as closed.
	for c in corners:
		curve.add_point( to_local * ( c as Vector3 ) )
	path.curve = curve
	container.add_child( path )
	path.add_to_group( group_name, true )
	if owner_node != null:
		path.owner = owner_node


## Two oriented rectangles (world space), one straddling each terminal sample of
## [path]. Each rectangle reaches [outward] past the end (over the water) and
## [inward] back onto the deck, spanning ±[lat_half] laterally so it covers BOTH
## rim anchors at the end. [inward] must stay below the prop sample spacing so only
## the terminal-most prop is caught, not the next one in.
static func _end_cap_loops( path: Path3D, lat_half: float, inward: float, outward: float ) -> Array:
	var out: Array = []
	var curve: Curve3D = path.curve
	if curve == null:
		return out
	var length := curve.get_baked_length()
	if length <= 0.0:
		return out
	var xform: Transform3D = path.global_transform
	# A short probe distance to read the end tangent (clamped to the curve length).
	var probe := minf( 0.5, length * 0.5 )

	# START end: tangent points INTO the deck (toward increasing offset).
	var s0 := xform * curve.sample_baked( 0.0 )
	var s1 := xform * curve.sample_baked( probe )
	out.append( _cap_rect( s0, _flat_dir( s1 - s0 ), lat_half, inward, outward ) )

	# END end: tangent points INTO the deck (toward decreasing offset).
	var e0 := xform * curve.sample_baked( length )
	var e1 := xform * curve.sample_baked( length - probe )
	out.append( _cap_rect( e0, _flat_dir( e1 - e0 ), lat_half, inward, outward ) )
	return out


## Corner list for one end-cap rectangle at world point [p], with [fwd_in] the
## flattened unit direction pointing inboard along the deck.
static func _cap_rect( p: Vector3, fwd_in: Vector3, lat_half: float, inward: float, outward: float ) -> Array:
	var lat := Vector3( fwd_in.z, 0.0, -fwd_in.x )   # perpendicular in XZ
	var out_dir := -fwd_in                            # toward open water past the end
	return [
		p + out_dir * outward - lat * lat_half,
		p + out_dir * outward + lat * lat_half,
		p + fwd_in * inward + lat * lat_half,
		p + fwd_in * inward - lat * lat_half,
	]


## Flatten a vector into the XZ plane and normalize; fall back to +Z if degenerate.
static func _flat_dir( v: Vector3 ) -> Vector3:
	var f := Vector3( v.x, 0.0, v.z )
	if f.length_squared() < 0.000001:
		return Vector3( 0, 0, 1 )
	return f.normalized()


# --- stairs --------------------------------------------------------------

## Detect steep runs on one curve and return staircase placements. Each entry:
## { xform:Transform3D, width, rise, run, steps }.
static func _stairs_for_curve( path: Path3D, p: Dictionary ) -> Array:
	var out: Array = []
	var curve: Curve3D = path.curve
	if curve == null or curve.get_point_count() < 2:
		return out

	var xform: Transform3D = path.global_transform
	var length := curve.get_baked_length()
	if length <= 0.0:
		return out

	var scan: float = maxf( 0.2, p.get( "stair_scan", 0.6 ) )
	var min_angle_deg: float = p.get( "stair_min_angle", 22.0 )
	var min_rise: float = p.get( "stair_min_rise", 0.5 )
	var width: float = p.get( "stair_width", 3.0 )
	var step_height: float = maxf( 0.1, p.get( "stair_step_height", 0.3 ) )
	var tan_thresh := tan( deg_to_rad( clampf( min_angle_deg, 1.0, 80.0 ) ) )

	# Sample global points along the curve at the scan interval.
	var pts: PackedVector3Array = PackedVector3Array()
	var n := int( length / scan ) + 1
	for i in range( n + 1 ):
		var offset := minf( length, float( i ) * scan )
		pts.append( xform * curve.sample_baked( offset ) )

	# Merge contiguous steep spans into single staircase runs.
	var run_start := -1
	var i := 0
	while i < pts.size() - 1:
		var a := pts[i]
		var b := pts[i + 1]
		var horiz := Vector2( b.x - a.x, b.z - a.z ).length()
		var dy := b.y - a.y
		var steep := absf( dy ) >= step_height and ( horiz < 0.001 or absf( dy ) / horiz >= tan_thresh )
		if steep:
			if run_start < 0:
				run_start = i
		else:
			if run_start >= 0:
				_emit_stair( out, pts[run_start], pts[i], width, min_rise, step_height )
				run_start = -1
		i += 1
	if run_start >= 0:
		_emit_stair( out, pts[run_start], pts[pts.size() - 1], width, min_rise, step_height )
	return out


static func _emit_stair( out: Array, p0: Vector3, p1: Vector3, width: float, min_rise: float, step_height: float ) -> void:
	# Low → high ordering.
	var low := p0 if p0.y <= p1.y else p1
	var high := p1 if p0.y <= p1.y else p0
	var rise := high.y - low.y
	if rise < min_rise:
		return
	var horiz_vec := Vector3( high.x - low.x, 0.0, high.z - low.z )
	var run := horiz_vec.length()
	if run < 0.05:
		return
	var fwd := horiz_vec / run
	# Basis with local +Z = climb direction (the stairs mesh climbs along +Z).
	var right := Vector3.UP.cross( fwd ).normalized()
	var basis := Basis( right, Vector3.UP, fwd )
	# Origin at the low point (mesh front-bottom sits there).
	var xform := Transform3D( basis, Vector3( low.x, low.y, low.z ) )
	var steps := maxi( 2, int( ceil( rise / step_height ) ) )
	out.append( { "xform": xform, "width": width, "rise": rise, "run": run, "steps": steps } )


# --- platforms (XZ intersections) ---------------------------------------

## Find XZ crossings between distinct curves; return world positions (Y = mean
## deck height at the crossing), deduplicated by platform footprint.
static func _platform_hits( splines: Array, p: Dictionary ) -> Array:
	var scan: float = maxf( 0.3, p.get( "platform_scan", 0.8 ) )
	var dedup: float = maxf( 0.5, p.get( "platform_size", 5.0 ) ) * 0.75

	# Resample every curve into a global polyline once.
	var lines: Array = []
	for path in splines:
		lines.append( _sample_polyline( path, scan ) )

	var hits: Array = []
	for a in range( lines.size() ):
		for b in range( a + 1, lines.size() ):
			_cross_polylines( lines[a], lines[b], hits )

	# Deduplicate hits that fall within one platform footprint of each other.
	var merged: Array = []
	for h in hits:
		var dup := false
		for m in merged:
			if Vector2( h.x - m.x, h.z - m.z ).length() < dedup:
				dup = true
				break
		if not dup:
			merged.append( h )
	return merged


static func _sample_polyline( path: Path3D, scan: float ) -> PackedVector3Array:
	var out := PackedVector3Array()
	var curve: Curve3D = path.curve
	if curve == null:
		return out
	var xform: Transform3D = path.global_transform
	var length := curve.get_baked_length()
	if length <= 0.0:
		return out
	var n := int( length / scan ) + 1
	for i in range( n + 1 ):
		var offset := minf( length, float( i ) * scan )
		out.append( xform * curve.sample_baked( offset ) )
	return out


static func _cross_polylines( line_a: PackedVector3Array, line_b: PackedVector3Array, hits: Array ) -> void:
	for ia in range( line_a.size() - 1 ):
		var a0 := line_a[ia]
		var a1 := line_a[ia + 1]
		var a0xz := Vector2( a0.x, a0.z )
		var a1xz := Vector2( a1.x, a1.z )
		for ib in range( line_b.size() - 1 ):
			var b0 := line_b[ib]
			var b1 := line_b[ib + 1]
			var hit = Geometry2D.segment_intersects_segment(
				a0xz, a1xz, Vector2( b0.x, b0.z ), Vector2( b1.x, b1.z ) )
			if hit == null:
				continue
			# Interpolate Y on both segments at the hit and average.
			var ta := _seg_t( a0xz, a1xz, hit )
			var tb := _seg_t( Vector2( b0.x, b0.z ), Vector2( b1.x, b1.z ), hit )
			var ya := lerpf( a0.y, a1.y, ta )
			var yb := lerpf( b0.y, b1.y, tb )
			hits.append( Vector3( hit.x, ( ya + yb ) * 0.5, hit.y ) )


static func _seg_t( s0: Vector2, s1: Vector2, pt: Vector2 ) -> float:
	var d := s1 - s0
	var len2 := d.length_squared()
	if len2 < 0.000001:
		return 0.0
	return clampf( ( pt - s0 ).dot( d ) / len2, 0.0, 1.0 )
