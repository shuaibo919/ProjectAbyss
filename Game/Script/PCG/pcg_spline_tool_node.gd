@tool
extends Node3D
class_name PcgSplineToolNode

## One-node, Inspector-driven spline PCG tool — the "select it, swap the mesh in
## the detail panel" workflow.
##
## Drop this on a scene, pick a [member tool_type] (Dock / Fence / Road), drag a
## [Path3D] into [member target_spline], and it builds the matching structure
## along that curve. Every mesh is a native [Mesh] @export slot, so replacing a
## piece is just dragging your model onto the slot in the Inspector — no Data
## Flow dock, no graph editing. Leave a slot empty to use the built-in greybox
## placeholder from [PcgSplineMeshes].
##
## Any change (mesh, spacing, or the curve) rebuilds the internal Flow graph and
## regenerates — in the editor (via a @tool re-evaluation against the editor
## world) and at runtime. Generated meshes live as MultiMeshInstance3D children
## and are cleared/rebuilt each pass, so nothing is baked into the .tscn beyond
## this node + its @export values.
##
## Relationship to the lower-level API: this is a thin, designer-facing wrapper
## over [PcgSplineTools] (graph builders) + [PcgSplineMeshes] (placeholder pieces).

const PcgSplineTools := preload( "res://Script/PCG/pcg_spline_tools.gd" )
const PcgSplineMeshes := preload( "res://Script/PCG/pcg_spline_meshes.gd" )
const PcgSplineConnectors := preload( "res://Script/PCG/pcg_spline_connectors.gd" )

enum ToolType { DOCK, FENCE, ROAD, CLIFFWALK }


# --- Which tool + which curve -------------------------------------------
@export var tool_type : ToolType = ToolType.DOCK:
	set( v ):
		tool_type = v
		notify_property_list_changed()   # re-show only this type's fields
		_mark_dirty()

## The Path3D whose curve this tool follows. Reshape it and the structure
## rebuilds. If left empty, uses ALL Path3D children of this node — so a dock
## with several Path3D children generates every deck and connects them with
## stairs / junction platforms where they step or cross.
@export var target_spline : Path3D:
	set( v ):
		target_spline = v
		_mark_dirty()

## Turn generation on/off (handy while dragging many meshes in).
@export var enabled : bool = true:
	set( v ):
		enabled = v
		_mark_dirty()


# --- DOCK slots ----------------------------------------------------------
# --- DOCK slots (grouped by structural part) -----------------------------
# Each part is its own named mesh list, so the Inspector shows distinct rows:
# "Deck Planks", "Pilings", "Railing", "Bollards". Leave a list empty to use
# that part's built-in greybox placeholder (Railing empty = no railing at all).
@export_group( "Dock", "dock_" )

@export_subgroup( "Deck Planks", "dock_deck_" )
## Walking-surface plank meshes (weighted random pick per plank).
@export var dock_deck_meshes : Array[Mesh] = []:
	set( v ):
		dock_deck_meshes = v
		_mark_dirty()
@export var dock_deck_spacing : float = 1.0:
	set( v ):
		dock_deck_spacing = maxf( 0.1, v )
		_mark_dirty()

@export_subgroup( "Pilings", "dock_piling_" )
## Support-leg meshes hanging from the deck to the seabed (weighted pick).
@export var dock_piling_meshes : Array[Mesh] = []:
	set( v ):
		dock_piling_meshes = v
		_mark_dirty()
@export var dock_piling_spacing : float = 3.0:
	set( v ):
		dock_piling_spacing = maxf( 0.2, v )
		_mark_dirty()

@export_subgroup( "Railing", "dock_railing_" )
## Handrail unit meshes laid along BOTH deck rims (empty = no railing).
@export var dock_railing_meshes : Array[Mesh] = []:
	set( v ):
		dock_railing_meshes = v
		_mark_dirty()
@export var dock_railing_spacing : float = 1.0:
	set( v ):
		dock_railing_spacing = maxf( 0.2, v )
		_mark_dirty()

@export_subgroup( "Bollards", "dock_bollard_" )
## Mooring-post meshes placed along the deck edges (weighted pick).
@export var dock_bollard_meshes : Array[Mesh] = []:
	set( v ):
		dock_bollard_meshes = v
		_mark_dirty()

@export_subgroup( "Clutter", "dock_clutter_" )
## Deck-prop meshes (crates / barrels / nets / fish) clumped along the deck
## edges via noise — empty = no clutter. Weighted random pick per prop.
@export var dock_clutter_meshes : Array[Mesh] = []:
	set( v ):
		dock_clutter_meshes = v
		_mark_dirty()
## Per-variant weights matching dock_clutter_meshes (empty = even). Lets crates
## dominate over rarer fish piles, etc.
@export var dock_clutter_weights : Array[float] = []:
	set( v ):
		dock_clutter_weights = v
		_mark_dirty()
## Sampling step for clutter candidates (smaller = denser piles).
@export var dock_clutter_spacing : float = 1.2:
	set( v ):
		dock_clutter_spacing = maxf( 0.2, v )
		_mark_dirty()
## Clump threshold 0..1 — higher leaves more bare deck between piles.
@export_range( 0.0, 1.0 ) var dock_clutter_density : float = 0.62:
	set( v ):
		dock_clutter_density = clampf( v, 0.0, 1.0 )
		_mark_dirty()
## Fishing-net meshes DRAPED over the railing (empty = none). Needs a railing to
## drape over — the net straddles the rim and hangs down the outboard side.
@export var dock_clutter_drape_meshes : Array[Mesh] = []:
	set( v ):
		dock_clutter_drape_meshes = v
		_mark_dirty()
## Sampling step for draped nets along each rim (larger = rarer nets).
@export var dock_clutter_drape_spacing : float = 3.0:
	set( v ):
		dock_clutter_drape_spacing = maxf( 0.3, v )
		_mark_dirty()
## Clump threshold 0..1 for draped nets — higher = fewer nets.
@export_range( 0.0, 1.0 ) var dock_clutter_drape_density : float = 0.6:
	set( v ):
		dock_clutter_drape_density = clampf( v, 0.0, 1.0 )
		_mark_dirty()

@export_subgroup( "Deck Shape", "dock_" )
## Lateral distance from the spline centre to each deck edge (half the width).
@export var dock_half_width : float = 2.0:
	set( v ):
		dock_half_width = maxf( 0.1, v )
		_mark_dirty()

@export_subgroup( "Cross Braces", "dock_brace_" )
## Diagonal under-deck brace meshes tying the two piling lines together (empty =
## none). Sampled on the centreline; each mesh spans laterally to straddle both
## legs. Author the width to match 2 * (piling X-offset).
@export var dock_brace_meshes : Array[Mesh] = []:
	set( v ):
		dock_brace_meshes = v
		_mark_dirty()
## Sampling step for cross-braces (usually the piling spacing so a brace sits
## under each cross-section of legs).
@export var dock_brace_spacing : float = 3.0:
	set( v ):
		dock_brace_spacing = maxf( 0.2, v )
		_mark_dirty()

@export_subgroup( "Lamp Posts", "dock_lamp_" )
## Quayside lamp-post meshes spaced along BOTH rims (empty = none). Anchored on
## the deck edge like bollards; keep the spacing sparse for a lighting cadence.
@export var dock_lamp_meshes : Array[Mesh] = []:
	set( v ):
		dock_lamp_meshes = v
		_mark_dirty()
## Sampling step for lamp posts along each rim (larger = fewer lamps).
@export var dock_lamp_spacing : float = 14.0:
	set( v ):
		dock_lamp_spacing = maxf( 0.5, v )
		_mark_dirty()

@export_subgroup( "Rope Swags", "dock_swag_" )
## Hanging rope-swag (festoon) meshes strung along BOTH rims (empty = none). Each
## mesh spans one [member dock_swag_spacing] gap and dips in the middle — match
## the spacing to the swag mesh span so consecutive festoons meet at their posts.
@export var dock_swag_meshes : Array[Mesh] = []:
	set( v ):
		dock_swag_meshes = v
		_mark_dirty()
## Sampling step for rope swags along each rim (match to the swag mesh span).
@export var dock_swag_spacing : float = 6.0:
	set( v ):
		dock_swag_spacing = maxf( 0.5, v )
		_mark_dirty()

@export_subgroup( "Stairs", "dock_stairs_" )
## Insert a staircase where a deck curve climbs steeper than this angle (deg).
## Gentle grades stay as ramped planks; only steep breakpoints get stairs.
@export var dock_stairs_enabled : bool = true:
	set( v ):
		dock_stairs_enabled = v
		_mark_dirty()
@export_range( 5.0, 80.0 ) var dock_stairs_min_angle : float = 22.0:
	set( v ):
		dock_stairs_min_angle = clampf( v, 5.0, 80.0 )
		_mark_dirty()
## Minimum total rise for a staircase to be placed (skips tiny bumps).
@export var dock_stairs_min_rise : float = 0.6:
	set( v ):
		dock_stairs_min_rise = maxf( 0.1, v )
		_mark_dirty()

@export_subgroup( "Junction Platforms", "dock_platform_" )
## Drop a platform where two deck curves cross in plan view (needs 2+ Path3D
## children on this node).
@export var dock_platform_enabled : bool = true:
	set( v ):
		dock_platform_enabled = v
		_mark_dirty()
## Side length of the square junction platform.
@export var dock_platform_size : float = 5.0:
	set( v ):
		dock_platform_size = maxf( 0.5, v )
		_mark_dirty()
## Clip rim props (rope swags / draped nets / edge clutter) off the deck TERMINALS
## so nothing juts out over open water past the deck head. Leave on unless you
## want props deliberately overhanging the end.
@export var dock_endcaps_enabled : bool = true:
	set( v ):
		dock_endcaps_enabled = v
		_mark_dirty()


# --- FENCE slots ---------------------------------------------------------
@export_group( "Fence", "fence_" )
## Fence-unit meshes: each is one post + rail span (weighted pick).
@export var fence_unit_meshes : Array[Mesh] = []:
	set( v ):
		fence_unit_meshes = v
		_mark_dirty()
@export var fence_unit_weights : Array[float] = []:
	set( v ):
		fence_unit_weights = v
		_mark_dirty()
@export var fence_post_spacing : float = 2.0:
	set( v ):
		fence_post_spacing = maxf( 0.2, v )
		_mark_dirty()


# --- ROAD slots ----------------------------------------------------------
@export_group( "Road", "road_" )
## Road-slab meshes laid along the curve (weighted pick).
@export var road_slab_meshes : Array[Mesh] = []:
	set( v ):
		road_slab_meshes = v
		_mark_dirty()
@export var road_slab_spacing : float = 1.4:
	set( v ):
		road_slab_spacing = maxf( 0.2, v )
		_mark_dirty()


# --- CLIFF WALK slots ------------------------------------------------------
@export_group( "Cliff Walk", "cliffwalk_" )
## Which side of the travel direction the rock face is on — braces anchor
## there; the railing guards the opposite (void) edge.
enum CliffSide { RIGHT, LEFT }
@export var cliffwalk_cliff_side : CliffSide = CliffSide.RIGHT:
	set( v ):
		cliffwalk_cliff_side = v
		_mark_dirty()
## Walking-plank meshes (weighted pick per plank).
@export var cliffwalk_plank_meshes : Array[Mesh] = []:
	set( v ):
		cliffwalk_plank_meshes = v
		_mark_dirty()
@export var cliffwalk_plank_spacing : float = 0.9:
	set( v ):
		cliffwalk_plank_spacing = maxf( 0.2, v )
		_mark_dirty()
## Knee-brace meshes anchored into the cliff (weighted pick).
@export var cliffwalk_brace_meshes : Array[Mesh] = []:
	set( v ):
		cliffwalk_brace_meshes = v
		_mark_dirty()
@export var cliffwalk_brace_spacing : float = 2.4:
	set( v ):
		cliffwalk_brace_spacing = maxf( 0.4, v )
		_mark_dirty()
## Railing units on the outer/void edge (empty = none).
@export var cliffwalk_railing_meshes : Array[Mesh] = []:
	set( v ):
		cliffwalk_railing_meshes = v
		_mark_dirty()
@export var cliffwalk_railing_spacing : float = 1.0:
	set( v ):
		cliffwalk_railing_spacing = maxf( 0.2, v )
		_mark_dirty()
## Half the walkway width (narrow: a plank road, not a pier).
@export var cliffwalk_half_width : float = 1.2:
	set( v ):
		cliffwalk_half_width = maxf( 0.3, v )
		_mark_dirty()
## true = boardwalk mode:每个采样点向下贴地并抬升 clearance(适合起伏山坡);
## false = 严格按手绘曲线高度(适合真正垂直的崖壁栈道)。
@export var cliffwalk_drape : bool = true:
	set( v ):
		cliffwalk_drape = v
		_mark_dirty()
## Deck height above the ground when draping.
@export var cliffwalk_ground_clearance : float = 1.1:
	set( v ):
		cliffwalk_ground_clearance = maxf( 0.1, v )
		_mark_dirty()


# --- Shared -------------------------------------------------------------
@export_group( "" )
@export var variant_seed : int = 11:
	set( v ):
		variant_seed = v
		_mark_dirty()

## Physics frames to wait before regenerating (colliders must be live for the
## fence/road ground raycasts).
@export var settle_frames : int = 6

## Inspector button: tick to force an immediate rebuild. Useful in the editor
## when auto-regeneration ran before the viewport physics was ready (raycast
## tools need live colliders) — reshape the curve, then tick this.
@export var regenerate_now : bool = false:
	set( v ):
		regenerate_now = false   # momentary: never stays checked
		if v:
			regenerate()


var _flow : FlowGraphNode3D = null
var _dirty : bool = false
var _regenerating : bool = false
# Scene group holding the junction-platform footprint loops published each rebuild
# (empty when no crossing exists) — read by _build_graph to wire the clutter/drape
# exclusion clip so props never bury a crossing landing.
var _exclusion_group : String = ""
# Editor-side change detection: dragging a Path3D's control points mutates the
# Curve3D resource (and moving the node changes its transform) WITHOUT touching
# any @export setter — so those edits never reach _mark_dirty on their own. We
# poll a cheap fingerprint of the spline set each editor frame and rebuild when
# it changes, giving live "reshape the spline → structure updates" behaviour.
var _spline_fp : int = 0


func _ready() -> void:
	# Both in editor (@tool) and at runtime we regenerate once colliders exist.
	_mark_dirty()
	# Only the editor needs the live spline-change poll; at runtime splines are
	# static after load, so leave _process disabled there.
	set_process( Engine.is_editor_hint() )


func _process( _delta : float ) -> void:
	if not Engine.is_editor_hint():
		return
	var fp := _spline_fingerprint()
	if fp != _spline_fp:
		_spline_fp = fp
		_mark_dirty()


## Cheap hash of every resolved spline's shape + placement. Changes when a
## control point moves, a point is added/removed, or the Path3D is transformed.
func _spline_fingerprint() -> int:
	var parts : Array = [ tool_type ]
	for path in _resolve_splines():
		var curve : Curve3D = path.curve
		if curve == null:
			continue
		parts.append( curve.get_point_count() )
		# Quantize so sub-millimetre float churn doesn't spuriously retrigger.
		parts.append( int( round( curve.get_baked_length() * 100.0 ) ) )
		var origin : Vector3 = path.global_transform.origin
		parts.append( int( round( origin.x * 100.0 ) ) )
		parts.append( int( round( origin.y * 100.0 ) ) )
		parts.append( int( round( origin.z * 100.0 ) ) )
		# Include each control point so reshaping (which can leave baked_length
		# nearly unchanged) still registers.
		for i in range( curve.get_point_count() ):
			var p : Vector3 = curve.get_point_position( i )
			parts.append( int( round( p.x * 100.0 ) ) )
			parts.append( int( round( p.y * 100.0 ) ) )
			parts.append( int( round( p.z * 100.0 ) ) )
	return hash( parts )


# Hide the @export slots that don't belong to the selected tool_type, so the
# Inspector only shows the relevant mesh slots.
func _validate_property( property : Dictionary ) -> void:
	var n : String = property.name
	# NOTE: check "cliffwalk_" BEFORE the other prefixes — none collide today,
	# but the guard order documents that longest-prefix wins.
	var is_cliffwalk : bool = n.begins_with( "cliffwalk_" )
	var is_dock : bool = n.begins_with( "dock_" ) and not is_cliffwalk
	var is_fence : bool = n.begins_with( "fence_" )
	var is_road : bool = n.begins_with( "road_" )
	if not ( is_dock or is_fence or is_road or is_cliffwalk ):
		return
	var show := ( is_dock and tool_type == ToolType.DOCK ) \
		or ( is_fence and tool_type == ToolType.FENCE ) \
		or ( is_road and tool_type == ToolType.ROAD ) \
		or ( is_cliffwalk and tool_type == ToolType.CLIFFWALK )
	if not show:
		property.usage &= ~PROPERTY_USAGE_EDITOR


func _mark_dirty() -> void:
	if not is_inside_tree():
		return
	_dirty = true
	# A change arriving mid-rebuild is not lost: the running loop re-checks
	# _dirty. Only kick off a fresh deferred pass when none is in flight.
	if not _regenerating:
		_deferred_regenerate.call_deferred()


func _deferred_regenerate() -> void:
	if _regenerating:
		return
	_regenerating = true
	# Drain: keep rebuilding while edits keep coming in (e.g. a live spline drag).
	while _dirty:
		_dirty = false
		# Wait for physics so the ground colliders answer raycasts (fence/road).
		for i in range( maxi( 1, settle_frames ) ):
			await get_tree().physics_frame
		regenerate()
	_regenerating = false


## Rebuild the graph from the current @export values and run it. Public so it can
## be called from a script or the editor after external changes.
func regenerate() -> void:
	if not is_inside_tree():
		return
	_ensure_flow_child()
	if not enabled:
		_clear_output()
		_clear_connectors()
		return

	var splines := _resolve_splines()
	if splines.is_empty():
		_clear_output()
		_clear_connectors()
		return

	# Tag every resolved spline into a group unique to THIS node so scan_splines
	# under our graph sees all our curves (and nobody else's).
	var group := _scan_group()
	for s in splines:
		_retag_spline( s, group )

	# Publish junction-platform exclusion footprints BEFORE building the graph
	# (dock only) — the clutter/drape clip nodes scan these at eval time, so the
	# loops must already be in the tree + group. Sets _exclusion_group to a
	# non-empty name only when at least one crossing footprint exists, so the
	# graph wires the clip only when there is something to clip against.
	_exclusion_group = ""
	if tool_type == ToolType.DOCK:
		_publish_exclusions( splines )

	_flow.graph = _build_graph( group )

	# Editor: evaluate directly against the editor world (the flow node's own
	# _ready() only auto-runs at runtime). Runtime: execute() drives it.
	if Engine.is_editor_hint():
		_evaluate_in_editor()
	else:
		_flow.execute()

	# Structural connectors (stairs at steep breakpoints, platforms at crossings)
	# are a GLOBAL post-process over the curve set — only for the dock tool.
	if tool_type == ToolType.DOCK:
		_rebuild_connectors( splines )
	else:
		_clear_connectors()

	# Sync the change-poll baseline to what we just built so the editor _process
	# watcher doesn't see this very rebuild as a fresh edit and loop forever.
	_spline_fp = _spline_fingerprint()


func _rebuild_connectors( splines : Array ) -> void:
	var container := _ensure_connectors_child()
	var params := {
		"owner": _connector_owner(),
		"stairs_enabled": dock_stairs_enabled,
		"stair_min_angle": dock_stairs_min_angle,
		"stair_min_rise": dock_stairs_min_rise,
		"stair_width": dock_half_width * 2.0,
		"platforms_enabled": dock_platform_enabled,
		"platform_size": dock_platform_size,
	}
	PcgSplineConnectors.rebuild( container, splines, params )


## Publish junction-platform footprint loops (Path3D) into a private group so the
## Flow graph's clip nodes can carve clutter/drape out of each crossing landing.
## Runs BEFORE the graph is built; sets _exclusion_group when >=1 footprint exists.
func _publish_exclusions( splines : Array ) -> void:
	var container := _ensure_exclusions_child()
	var group := "%s_excl" % _scan_group()
	# End-cap inward reach must stay below the TIGHTEST rim-prop spacing so only the
	# terminal-most prop (the one that would jut over open water) is clipped, never
	# the next one inboard. Swags/drapes/clutter all route through this clip.
	var min_prop_spacing : float = min( dock_swag_spacing, dock_clutter_drape_spacing, dock_clutter_spacing )
	var params := {
		"owner": _connector_owner(),
		"platforms_enabled": dock_platform_enabled,
		"platform_size": dock_platform_size,
		# Stair footprints clip the deck planks under each staircase (so the solid
		# stairs don't z-fight slanted planks). Must use the SAME detection params
		# as _rebuild_connectors so footprint and drawn staircase agree.
		"stairs_enabled": dock_stairs_enabled,
		"stair_min_angle": dock_stairs_min_angle,
		"stair_min_rise": dock_stairs_min_rise,
		"stair_width": dock_half_width * 2.0,
		# End caps guard the deck terminals for every dock (single or multi-curve).
		"endcaps_enabled": dock_endcaps_enabled,
		"endcap_half_width": dock_half_width + 0.6,      # covers both rim lines + margin
		"endcap_inward": maxf( 0.25, min_prop_spacing * 0.45 ),
		"endcap_outward": 1.0,
	}
	var n := PcgSplineConnectors.publish_exclusions( container, splines, params, group )
	# Only expose the group to the graph when it actually holds footprints — the
	# clip node errors on an empty polygon set.
	_exclusion_group = group if n > 0 else ""


# --- graph construction --------------------------------------------------

func _build_graph( group : String ) -> FlowGraphResource:
	match tool_type:
		ToolType.DOCK:
			# Multiple tinted/sized default variants per part so a run of greybox
			# pieces reads as weathered individual timbers, not one cloned mesh
			# (spawn_meshes picks per-point from these by the per-point seed).
			var deck := _meshes_or( dock_deck_meshes, [
				PcgSplineMeshes.deck_plank( dock_half_width * 2.0, dock_deck_spacing, 0.18, PcgSplineMeshes.WOOD_WARM ),
				PcgSplineMeshes.deck_plank( dock_half_width * 2.0, dock_deck_spacing, 0.18, PcgSplineMeshes.WOOD_COOL ),
				PcgSplineMeshes.deck_plank( dock_half_width * 2.0, dock_deck_spacing, 0.20, PcgSplineMeshes.WOOD_GREY ),
				PcgSplineMeshes.deck_plank_railed( dock_half_width * 2.0, dock_deck_spacing ),
			] )
			var pilings := _meshes_or( dock_piling_meshes, [
				PcgSplineMeshes.piling( 8.0, 0.28 ),
				PcgSplineMeshes.piling( 8.0, 0.24 ),
				PcgSplineMeshes.piling( 8.5, 0.30 ),
			] )
			var bollards := _meshes_or( dock_bollard_meshes, [
				PcgSplineMeshes.bollard( 0.9, 0.22, PcgSplineMeshes.METAL_COL ),
				PcgSplineMeshes.bollard( 0.78, 0.2, PcgSplineMeshes.WOOD_WARM ),
				PcgSplineMeshes.bollard( 1.0, 0.24, PcgSplineMeshes.METAL_COL ),
			] )
			# Railing is opt-in: only the user's meshes (empty = no railing). We
			# do NOT substitute a placeholder here, so an empty slot means "none".
			var railings := _clean( dock_railing_meshes )
			# Clutter is likewise opt-in (empty = bare deck).
			var clutter := _clean( dock_clutter_meshes )
			return PcgSplineTools.build_dock(
				deck, pilings, bollards, railings, clutter,
				dock_deck_spacing, dock_piling_spacing, dock_railing_spacing,
				dock_clutter_spacing, dock_clutter_density,
				dock_half_width, variant_seed, group, dock_clutter_weights,
				_clean( dock_clutter_drape_meshes ), dock_clutter_drape_spacing, dock_clutter_drape_density,
				_clean( dock_brace_meshes ), dock_brace_spacing,
				_clean( dock_lamp_meshes ), dock_lamp_spacing,
				_clean( dock_swag_meshes ), dock_swag_spacing,
				_exclusion_group )
		ToolType.FENCE:
			var units := _meshes_or( fence_unit_meshes, [
				PcgSplineMeshes.fence_wood( fence_post_spacing ),
				PcgSplineMeshes.fence_stone( fence_post_spacing ),
				PcgSplineMeshes.fence_broken(),
			] )
			return PcgSplineTools.build_fence(
				units, fence_post_spacing, fence_unit_weights, variant_seed, group )
		ToolType.CLIFFWALK:
			var planks := _meshes_or( cliffwalk_plank_meshes, [
				PcgSplineMeshes.deck_plank( cliffwalk_half_width * 2.0, cliffwalk_plank_spacing, 0.14, PcgSplineMeshes.WOOD_GREY ),
				PcgSplineMeshes.deck_plank( cliffwalk_half_width * 2.0, cliffwalk_plank_spacing, 0.14, PcgSplineMeshes.WOOD_COOL ),
			] )
			var braces := _meshes_or( cliffwalk_brace_meshes, [
				PcgSplineMeshes.cliff_brace( cliffwalk_half_width * 2.0, 1.8, 1.1 ),
			] )
			var rails := _meshes_or( cliffwalk_railing_meshes, [
				PcgSplineMeshes.dock_railing( cliffwalk_railing_spacing, 0.95 ),
			] )
			return PcgSplineTools.build_cliffwalk(
				planks, braces, rails,
				cliffwalk_plank_spacing, cliffwalk_brace_spacing, cliffwalk_railing_spacing,
				cliffwalk_half_width, cliffwalk_cliff_side == CliffSide.RIGHT,
				variant_seed, group, cliffwalk_drape, cliffwalk_ground_clearance )
		_:  # ROAD
			var slabs := _meshes_or( road_slab_meshes, [
				PcgSplineMeshes.road_slab( 5.0, road_slab_spacing, PcgSplineMeshes.PAVED_COL ),
				PcgSplineMeshes.road_cobble( 5.0, road_slab_spacing ),
				PcgSplineMeshes.road_slab( 5.0, road_slab_spacing, PcgSplineMeshes.DIRT_COL ),
			] )
			return PcgSplineTools.build_road(
				slabs, road_slab_spacing, variant_seed, group )


## Return the user-provided mesh list if it has any non-null entry, else the
## built-in placeholder list. Keeps empty Inspector slots meaning "use default".
func _meshes_or( provided : Array[Mesh], fallback : Array[Mesh] ) -> Array[Mesh]:
	var cleaned := _clean( provided )
	return cleaned if not cleaned.is_empty() else fallback


## Drop null entries from a mesh list (no placeholder substitution). An empty
## result means "the caller supplied nothing" — used for opt-in parts.
func _clean( provided : Array[Mesh] ) -> Array[Mesh]:
	var cleaned : Array[Mesh] = []
	for m in provided:
		if m != null:
			cleaned.append( m )
	return cleaned


# --- spline / group plumbing --------------------------------------------

func _resolve_splines() -> Array:
	# Explicit target wins (single-curve tools); otherwise use ALL Path3D
	# children so a dock can span several decks and connect them.
	if target_spline != null:
		return [ target_spline ]
	var out: Array = []
	for child in get_children():
		if child is Path3D:
			out.append( child )
	return out


func _scan_group() -> String:
	return "pcg_spline_%d" % get_instance_id()


func _retag_spline( spline : Path3D, group : String ) -> void:
	# Ensure the spline is ONLY in our group (drop stale tool groups so it isn't
	# double-generated if the user reassigned it between tools).
	for g in [ PcgSplineTools.GROUP_DOCK, PcgSplineTools.GROUP_FENCE, PcgSplineTools.GROUP_ROAD ]:
		if spline.is_in_group( g ):
			spline.remove_from_group( g )
	if not spline.is_in_group( group ):
		spline.add_to_group( group, true )


# --- internal flow node --------------------------------------------------

func _ensure_flow_child() -> void:
	if _flow != null and is_instance_valid( _flow ):
		return
	# Reuse an existing child if the scene was reloaded.
	for child in get_children():
		if child is FlowGraphNode3D:
			_flow = child
			return
	_flow = FlowGraphNode3D.new()
	_flow.name = "FlowGraph"
	add_child( _flow )
	# Keep the helper node out of the saved scene tree ownership so it isn't
	# persisted with a stale graph — it's rebuilt from our @exports each load.
	if not Engine.is_editor_hint():
		_flow.owner = null


func _clear_output() -> void:
	if _flow == null or not is_instance_valid( _flow ):
		return
	for child in _flow.get_children():
		if child is MultiMeshInstance3D:
			child.queue_free()


# --- structural connectors child ----------------------------------------

var _connectors : Node3D = null

func _ensure_connectors_child() -> Node3D:
	if _connectors != null and is_instance_valid( _connectors ):
		return _connectors
	for child in get_children():
		if child.name == "Connectors" and child is Node3D:
			_connectors = child
			return _connectors
	_connectors = Node3D.new()
	_connectors.name = "Connectors"
	add_child( _connectors )
	# Not owned at runtime (rebuilt from @exports each load); in the editor we
	# leave owner unset too so the individual connector meshes aren't persisted.
	return _connectors


func _clear_connectors() -> void:
	if _connectors == null or not is_instance_valid( _connectors ):
		return
	for child in _connectors.get_children():
		child.queue_free()


# --- exclusion footprints child (junction landings) ----------------------

var _exclusions : Node3D = null

func _ensure_exclusions_child() -> Node3D:
	if _exclusions != null and is_instance_valid( _exclusions ):
		return _exclusions
	for child in get_children():
		if child.name == "Exclusions" and child is Node3D:
			_exclusions = child
			return _exclusions
	_exclusions = Node3D.new()
	_exclusions.name = "Exclusions"
	add_child( _exclusions )
	# Footprint loops are invisible helpers rebuilt each pass — never persisted.
	return _exclusions


## Owner to assign to spawned connector meshes. In the editor, owning them to the
## edited scene root would bake them into the .tscn (we regenerate instead), so
## return null there; at runtime owner is irrelevant for rendering.
func _connector_owner() -> Node:
	return null


func _evaluate_in_editor() -> void:
	# Mirror the verified in-editor evaluation path: run the graph with ctx.owner
	# set to our flow node so scan_splines/ray_cast use the editor viewport world.
	var io = load( "res://addons/flow_nodes_editor/flow_nodes_io.gd" )
	var ctx = load( "res://addons/flow_nodes_editor/flow_data.gd" ).EvaluationContext.new()
	ctx.owner = _flow
	ctx.eval_id = 0
	ctx.gedit_nodes_by_name = {}
	ctx.runtime_params = {}
	io.evaluate_graph( _flow.graph, {}, ctx, {}, 0 )
