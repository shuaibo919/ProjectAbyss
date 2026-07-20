@tool
extends RefCounted
class_name PcgSplineTools

## Spline-driven PCG tools — the "draw a Path3D, get a structure" workflow.
##
## Each builder returns a FlowGraphResource that is entirely spline-driven: it
## starts from a [b]Scan Splines[/b] node filtered by a scene group, so the user
## only has to drop a Path3D into the scene, tag it, and the graph rebuilds the
## dock / fence / road along whatever curve they draw. No point count, no manual
## placement — reshape the curve and re-run.
##
## Variation ("变体") is exposed through spawn_meshes.mesh_variants + weights +
## randomize_mesh_variants, plus a handful of numeric knobs (spacing, width,
## depth) passed to each builder. Same builder + different args = a different
## structure variant with zero graph edits.
##
## Orientation contract (see [PcgSplineMeshes]): sample_spline / split_splines
## emit points whose +Z faces along travel and +X is lateral, with rotation baked
## into the point stream — so pieces authored to that contract tile correctly on
## curves. ray_cast is used only to drape onto terrain (position), never to
## reorient (out_rotation_attribute is left empty so the path yaw survives).
##
## Builds on [FlowGraphBuilder] and mirrors the pipeline style of [PcgScatter].

const FlowGraphBuilder := preload( "res://Script/PCG/flow_graph_builder.gd" )
const PcgSplineMeshes := preload( "res://Script/PCG/pcg_spline_meshes.gd" )

## Physics layer the terrain/seabed colliders live on (matches the veg pipeline).
const GROUND_MASK := 1

# Scene group names the tools scan for. Tag your Path3D with one of these.
const GROUP_DOCK := "pcg_spline_dock"
const GROUP_FENCE := "pcg_spline_fence"
const GROUP_ROAD := "pcg_spline_road"


# =========================================================================
# DOCK  — deck follows the drawn curve at its height; pilings drop to seabed;
#         bollards line the deck edges.
# =========================================================================

## @param deck_variants    plank meshes for the walking surface (weighted pick)
## @param piling_variants  leg meshes, dropped from the deck to the seabed (weighted)
## @param bollard_variants edge-post meshes placed laterally along the deck
## @param railing_variants handrail units laid along both deck rims (empty = none)
## @param clutter_variants deck-prop meshes clustered on the planks (empty = none)
## @param drape_variants   fishing-net meshes DRAPED over the rails (empty = none)
## @param plank_spacing    distance between deck samples (deck resolution)
## @param piling_spacing   distance between leg samples (sparser than planks)
## @param railing_spacing  distance between railing units along each rim
## @param clutter_spacing  sampling step for clutter candidates along the deck
## @param clutter_density  density_filter lower bound (higher = fewer, tighter clumps)
## @param clutter_weights  per-variant weights for clutter (empty = even)
## @param deck_half_width  lateral distance from spline centre to a deck edge
## @param drape_spacing    sampling step for draped-net candidates along each rim
## @param drape_density    density_filter lower bound for draped nets (higher = rarer)
## @param brace_variants   diagonal under-deck cross-brace meshes (empty = none),
##                         dropped on centreline samples straddling the piling lines
## @param brace_spacing    sampling step for cross-braces (match to piling_spacing)
## @param lamp_variants    lamp-post meshes spaced along BOTH rims (empty = none)
## @param lamp_spacing     sampling step for lamps (sparse — dock lighting cadence)
## @param swag_variants    hanging rope-swag meshes strung along BOTH rims (empty = none)
## @param swag_spacing     sampling step for swags (match to bollard spacing so each
##                         festoon bridges one gap between mooring posts)
## @param exclusion_group  scene group of Path3D footprint loops (junction platforms)
##                         whose interiors clutter/drape are clipped out of, so a
##                         crossing landing stays clear ("" = no exclusion)
static func build_dock(
		deck_variants: Array[Mesh],
		piling_variants: Array[Mesh],
		bollard_variants: Array[Mesh],
		railing_variants: Array[Mesh],
		clutter_variants: Array[Mesh],
		plank_spacing: float = 1.0,
		piling_spacing: float = 3.0,
		railing_spacing: float = 1.0,
		clutter_spacing: float = 1.2,
		clutter_density: float = 0.55,
		deck_half_width: float = 2.0,
		variant_seed: int = 11,
		scan_group: String = GROUP_DOCK,
		clutter_weights: Array[float] = [],
		drape_variants: Array[Mesh] = [],
		drape_spacing: float = 3.0,
		drape_density: float = 0.6,
		brace_variants: Array[Mesh] = [],
		brace_spacing: float = 3.0,
		lamp_variants: Array[Mesh] = [],
		lamp_spacing: float = 6.0,
		swag_variants: Array[Mesh] = [],
		swag_spacing: float = 6.0,
		exclusion_group: String = "" ) -> FlowGraphResource:
	var b := FlowGraphBuilder.new()

	var scan := b.AddNode( "scan_splines", {
		"group_name": scan_group,
		"recursive": true,
	}, Vector2( 0, 0 ) )

	# --- Deck: dense samples along the curve, one plank per sample -----------
	var deck_samples := b.AddNode( "sample_spline", {
		"sampling_mode": 0,             # Uniform
		"uniform_interval": plank_spacing,
		"fill_curve": false,
		"adjust_to_borders": true,
		"sample_segments_centers": false,
	}, Vector2( 260, -120 ), { "Splines": 0 } )

	var deck_spawn := b.AddNode( "spawn_meshes", {
		"mesh": deck_variants[0],
		"mesh_variants": deck_variants,
		"mesh_variant_weights": _even_weights( deck_variants.size() ),
		"randomize_mesh_variants": true,
		"random_seed": variant_seed,
		"use_vertex_colors": false,
		"clear_previous_instances": true,
	}, Vector2( 520, -120 ), { "In": 0 } )

	# --- Pilings: sparser samples, ray straight down to the seabed ----------
	var leg_samples := b.AddNode( "sample_spline", {
		"sampling_mode": 0,
		"uniform_interval": piling_spacing,
		"fill_curve": false,
		"adjust_to_borders": true,
	}, Vector2( 260, 40 ), { "Splines": 0 } )

	# Two legs per cross-section (port/starboard), offset laterally in local X.
	# Legs spawn at deck height and hang down by their own fixed mesh length —
	# no raycast reposition, so a deep-enough piling always bridges deck→seabed
	# (the seabed collider hides any overshoot). Adapting leg length to a bumpy
	# bottom would need a down-ray + distance scale; unnecessary for a flat bed.
	var leg_pairs := b.AddNode( "point_offsets", {
		"offsets": [ Vector3( -deck_half_width * 0.85, 0, 0 ), Vector3( deck_half_width * 0.85, 0, 0 ) ],
		"rotations": [ Vector3.ZERO ],
		"sizes": [ Vector3.ONE ],
		"local_space": true,
		"combine_rotation": true,
	}, Vector2( 520, 40 ), { "Anchors": 0 } )

	var leg_spawn := b.AddNode( "spawn_meshes", {
		"mesh": piling_variants[0],
		"mesh_variants": piling_variants,
		"mesh_variant_weights": _even_weights( piling_variants.size() ),
		"randomize_mesh_variants": true,
		"random_seed": variant_seed + 1,
		"use_vertex_colors": false,
		"clear_previous_instances": true,
	}, Vector2( 1040, 40 ), { "In": 0 } )

	# --- Bollards: sparse edge posts, offset to both deck rims --------------
	var bollard_samples := b.AddNode( "sample_spline", {
		"sampling_mode": 0,
		"uniform_interval": piling_spacing * 2.0,
		"fill_curve": false,
		"adjust_to_borders": true,
	}, Vector2( 260, 200 ), { "Splines": 0 } )

	var bollard_edges := b.AddNode( "point_offsets", {
		"offsets": [ Vector3( -deck_half_width, 0, 0 ), Vector3( deck_half_width, 0, 0 ) ],
		"rotations": [ Vector3.ZERO ],
		"sizes": [ Vector3.ONE ],
		"local_space": true,
	}, Vector2( 520, 200 ), { "Anchors": 0 } )

	var bollard_spawn := b.AddNode( "spawn_meshes", {
		"mesh": bollard_variants[0],
		"mesh_variants": bollard_variants,
		"mesh_variant_weights": _even_weights( bollard_variants.size() ),
		"randomize_mesh_variants": true,
		"random_seed": variant_seed + 3,
		"use_vertex_colors": false,
		"clear_previous_instances": true,
	}, Vector2( 780, 200 ), { "In": 0 } )

	b.Connect( scan, 0, deck_samples, 0 )

	b.Connect( scan, 0, leg_samples, 0 )
	b.Connect( leg_samples, 0, leg_pairs, 0 )
	b.Connect( leg_pairs, 0, leg_spawn, 0 )

	b.Connect( scan, 0, bollard_samples, 0 )
	b.Connect( bollard_samples, 0, bollard_edges, 0 )
	b.Connect( bollard_edges, 0, bollard_spawn, 0 )

	# --- Exclusion mask: scan the junction-platform + stair footprint loops
	# (published by PcgSplineConnectors into their own group) once, so the deck,
	# clutter and drape branches can clip out anything that lands inside a crossing
	# landing OR a staircase run. Built only when the caller passes an exclusion
	# group AND footprints exist there; clip_points_by_polygon errors on an empty
	# polygon set, so the tool node only forwards a non-empty group name.
	var excl_scan := StringName()
	var has_exclusion := exclusion_group != ""
	if has_exclusion:
		excl_scan = b.AddNode( "scan_splines", {
			"group_name": exclusion_group,
			"recursive": true,
		}, Vector2( 0, 760 ) )

	# Deck planks: clip out the spans a staircase covers, so the solid stairs don't
	# double up on slanted planks (z-fighting at the step). Junction footprints are
	# in the same group; clipping the deck there too keeps the crossing landing flush
	# with the platform slab rather than planks poking through it.
	if has_exclusion:
		var deck_clip := b.AddNode( "clip_points_by_polygon", {
			"plane": 0,                 # XZ
			"keep_inside": false,       # keep points OUTSIDE the footprints
			"spline_stream_attribute": "node",
		}, Vector2( 780, -120 ), { "Points": 0, "Polygon": 1 } )
		b.Connect( deck_samples, 0, deck_clip, 0 )
		b.Connect( excl_scan, 0, deck_clip, 1 )
		b.Connect( deck_clip, 0, deck_spawn, 0 )
	else:
		b.Connect( deck_samples, 0, deck_spawn, 0 )

	# --- Railings: handrail units laid dense along BOTH deck rims ----------
	# Optional — only wired when the caller supplies railing meshes.
	if not railing_variants.is_empty():
		var rail_samples := b.AddNode( "sample_spline", {
			"sampling_mode": 0,
			"uniform_interval": railing_spacing,
			"fill_curve": false,
			"adjust_to_borders": true,
		}, Vector2( 260, 340 ), { "Splines": 0 } )

		var rail_edges := b.AddNode( "point_offsets", {
			"offsets": [ Vector3( -deck_half_width, 0, 0 ), Vector3( deck_half_width, 0, 0 ) ],
			"rotations": [ Vector3.ZERO ],
			"sizes": [ Vector3.ONE ],
			"local_space": true,
			"combine_rotation": true,
		}, Vector2( 520, 340 ), { "Anchors": 0 } )

		var rail_spawn := b.AddNode( "spawn_meshes", {
			"mesh": railing_variants[0],
			"mesh_variants": railing_variants,
			"mesh_variant_weights": _even_weights( railing_variants.size() ),
			"randomize_mesh_variants": true,
			"random_seed": variant_seed + 5,
			"use_vertex_colors": false,
			"clear_previous_instances": true,
		}, Vector2( 780, 340 ), { "In": 0 } )

		b.Connect( scan, 0, rail_samples, 0 )
		b.Connect( rail_samples, 0, rail_edges, 0 )
		b.Connect( rail_edges, 0, rail_spawn, 0 )

	# --- Clutter: crates / barrels / nets / fish piled along the deck EDGES -
	# Optional — only wired when the caller supplies clutter meshes. Cargo on a
	# real dock hugs the rails and leaves a walking lane, so we: sample the deck →
	# thin by FBM noise into clumps → offset each clump to BOTH rims (clearing the
	# centre) → small per-prop jitter + yaw → weighted-variant spawn.
	if not clutter_variants.is_empty():
		var clutter_samples := b.AddNode( "sample_spline", {
			"sampling_mode": 0,
			"uniform_interval": clutter_spacing,
			"fill_curve": false,
			"adjust_to_borders": true,
		}, Vector2( 260, 480 ), { "Splines": 0 } )

		# FBM noise → density (node remaps to [0,1] internally).
		var clutter_noise := b.AddNode( "noise", {
			"out_name": "density",
			"in_scale": 0.35,
			"noise_type": 4,            # Simplex
			"fractal_type": 1,          # FBM
			"fractal_octaves": 3,
			"random_seed": variant_seed + 7,
			"mode": 0,                  # Override
			"sample_space": 1,          # XZ2D
		}, Vector2( 520, 480 ), { "In": 0 } )

		# Threshold → clumps with bare stretches of deck between them.
		var clutter_cull := b.AddNode( "density_filter", {
			"lower_bound": clutter_density,
			"upper_bound": 1.0,
		}, Vector2( 780, 480 ), { "In": 0 } )

		# Move each surviving clump anchor to both deck rims (inset slightly so
		# props sit just inside the railing, not hanging off the edge). This is
		# what clears the central walkway.
		var clutter_edge : float = deck_half_width * 0.72
		var clutter_edges := b.AddNode( "point_offsets", {
			"offsets": [ Vector3( -clutter_edge, 0, 0 ), Vector3( clutter_edge, 0, 0 ) ],
			"rotations": [ Vector3.ZERO ],
			"sizes": [ Vector3.ONE ],
			"local_space": true,
			"combine_rotation": true,
		}, Vector2( 1040, 480 ), { "Anchors": 0 } )

		# Small jitter around each edge anchor + full yaw + per-prop scale, so a
		# stack reads as haphazardly piled cargo rather than a ruled line.
		var clutter_xform := b.AddNode( "transform", {
			"offset_min": Vector3( -deck_half_width * 0.16, 0, -clutter_spacing * 0.45 ),
			"offset_max": Vector3( deck_half_width * 0.16, 0, clutter_spacing * 0.45 ),
			"rotation_min": Vector3( 0, -180, 0 ),
			"rotation_max": Vector3( 0, 180, 0 ),
			"scale_min": Vector3( 0.8, 0.8, 0.8 ),
			"scale_max": Vector3( 1.25, 1.25, 1.25 ),
			"uniform_scale": true,
			"rotation_local_space": true,
			"random_seed": variant_seed + 9,
		}, Vector2( 1300, 480 ), { "In": 0 } )

		var clutter_w : Array[float] = clutter_weights if not clutter_weights.is_empty() else _even_weights( clutter_variants.size() )
		var clutter_spawn := b.AddNode( "spawn_meshes", {
			"mesh": clutter_variants[0],
			"mesh_variants": clutter_variants,
			"mesh_variant_weights": clutter_w,
			"randomize_mesh_variants": true,
			"random_seed": variant_seed + 11,
			"use_vertex_colors": false,
			"clear_previous_instances": true,
		}, Vector2( 1560, 480 ), { "In": 0 } )

		b.Connect( scan, 0, clutter_samples, 0 )
		b.Connect( clutter_samples, 0, clutter_noise, 0 )
		b.Connect( clutter_noise, 0, clutter_cull, 0 )
		b.Connect( clutter_cull, 0, clutter_edges, 0 )
		b.Connect( clutter_edges, 0, clutter_xform, 0 )
		# Clip props inside any junction footprint so the crossing landing stays a
		# clean walking surface (keep_inside=false → keep only points OUTSIDE).
		if has_exclusion:
			var clutter_clip := b.AddNode( "clip_points_by_polygon", {
				"plane": 0,                 # XZ
				"keep_inside": false,
				"spline_stream_attribute": "node",
			}, Vector2( 1400, 480 ), { "Points": 0, "Polygon": 1 } )
			b.Connect( clutter_xform, 0, clutter_clip, 0 )
			b.Connect( excl_scan, 0, clutter_clip, 1 )
			b.Connect( clutter_clip, 0, clutter_spawn, 0 )
		else:
			b.Connect( clutter_xform, 0, clutter_spawn, 0 )

	# --- Draped nets: fishing nets slung OVER the railing at sparse points -----
	# Optional — only wired when the caller supplies draped-net meshes AND a
	# railing exists to drape over (a net over nothing floats). The drape mesh is
	# authored with X=0 on the rail line and +X = outboard, so we place anchors
	# exactly on each rim (no inset) and rotate the LEFT copy 180° so its outboard
	# side faces -X. Sparse + noise-thinned so nets read as occasional, not a wall.
	if not drape_variants.is_empty() and not railing_variants.is_empty():
		var drape_samples := b.AddNode( "sample_spline", {
			"sampling_mode": 0,
			"uniform_interval": drape_spacing,
			"fill_curve": false,
			"adjust_to_borders": true,
		}, Vector2( 260, 620 ), { "Splines": 0 } )

		var drape_noise := b.AddNode( "noise", {
			"out_name": "density",
			"in_scale": 0.5,
			"noise_type": 4,            # Simplex
			"fractal_type": 1,          # FBM
			"fractal_octaves": 2,
			"random_seed": variant_seed + 13,
			"mode": 0,
			"sample_space": 1,          # XZ2D
		}, Vector2( 520, 620 ), { "In": 0 } )

		var drape_cull := b.AddNode( "density_filter", {
			"lower_bound": drape_density,
			"upper_bound": 1.0,
		}, Vector2( 780, 620 ), { "In": 0 } )

		# Anchors sit ON each rim; the mesh straddles the rail from there. The left
		# copy is yawed 180° so its authored outboard (+X) hangs off the port side.
		var drape_edges := b.AddNode( "point_offsets", {
			"offsets": [ Vector3( -deck_half_width, 0, 0 ), Vector3( deck_half_width, 0, 0 ) ],
			"rotations": [ Vector3( 0, 180, 0 ), Vector3( 0, 0, 0 ) ],
			"sizes": [ Vector3.ONE ],
			"local_space": true,
			"combine_rotation": true,
		}, Vector2( 1040, 620 ), { "Anchors": 0 } )

		var drape_spawn := b.AddNode( "spawn_meshes", {
			"mesh": drape_variants[0],
			"mesh_variants": drape_variants,
			"mesh_variant_weights": _even_weights( drape_variants.size() ),
			"randomize_mesh_variants": true,
			"random_seed": variant_seed + 15,
			"use_vertex_colors": false,
			"clear_previous_instances": true,
		}, Vector2( 1300, 620 ), { "In": 0 } )

		b.Connect( scan, 0, drape_samples, 0 )
		b.Connect( drape_samples, 0, drape_noise, 0 )
		b.Connect( drape_noise, 0, drape_cull, 0 )
		b.Connect( drape_cull, 0, drape_edges, 0 )
		# Same junction exclusion as clutter: no nets slung over the crossing.
		if has_exclusion:
			var drape_clip := b.AddNode( "clip_points_by_polygon", {
				"plane": 0,                 # XZ
				"keep_inside": false,
				"spline_stream_attribute": "node",
			}, Vector2( 1160, 620 ), { "Points": 0, "Polygon": 1 } )
			b.Connect( drape_edges, 0, drape_clip, 0 )
			b.Connect( excl_scan, 0, drape_clip, 1 )
			b.Connect( drape_clip, 0, drape_spawn, 0 )
		else:
			b.Connect( drape_edges, 0, drape_spawn, 0 )

	# --- Cross-braces: diagonal struts tying the port/starboard piling lines
	# together UNDER the deck. Optional. Sampled on the CENTRELINE (no rim offset)
	# at the piling cadence; each brace mesh spans laterally to straddle both legs,
	# so a single centreline instance bridges the pair. No raycast — the brace
	# hangs from the deck anchor by its own fixed depth, matching the pilings.
	if not brace_variants.is_empty():
		var brace_samples := b.AddNode( "sample_spline", {
			"sampling_mode": 0,
			"uniform_interval": brace_spacing,
			"fill_curve": false,
			"adjust_to_borders": true,
		}, Vector2( 260, 760 ), { "Splines": 0 } )

		var brace_spawn := b.AddNode( "spawn_meshes", {
			"mesh": brace_variants[0],
			"mesh_variants": brace_variants,
			"mesh_variant_weights": _even_weights( brace_variants.size() ),
			"randomize_mesh_variants": true,
			"random_seed": variant_seed + 17,
			"use_vertex_colors": false,
			"clear_previous_instances": true,
		}, Vector2( 520, 760 ), { "In": 0 } )

		b.Connect( scan, 0, brace_samples, 0 )
		b.Connect( brace_samples, 0, brace_spawn, 0 )

	# --- Lamp posts: sparse quayside lighting spaced along BOTH rims. Optional.
	# Anchored exactly on each rim (like bollards) so they stand on the deck edge.
	if not lamp_variants.is_empty():
		var lamp_samples := b.AddNode( "sample_spline", {
			"sampling_mode": 0,
			"uniform_interval": lamp_spacing,
			"fill_curve": false,
			"adjust_to_borders": true,
		}, Vector2( 260, 900 ), { "Splines": 0 } )

		var lamp_edges := b.AddNode( "point_offsets", {
			"offsets": [ Vector3( -deck_half_width, 0, 0 ), Vector3( deck_half_width, 0, 0 ) ],
			"rotations": [ Vector3.ZERO ],
			"sizes": [ Vector3.ONE ],
			"local_space": true,
			"combine_rotation": true,
		}, Vector2( 520, 900 ), { "Anchors": 0 } )

		var lamp_spawn := b.AddNode( "spawn_meshes", {
			"mesh": lamp_variants[0],
			"mesh_variants": lamp_variants,
			"mesh_variant_weights": _even_weights( lamp_variants.size() ),
			"randomize_mesh_variants": true,
			"random_seed": variant_seed + 19,
			"use_vertex_colors": false,
			"clear_previous_instances": true,
		}, Vector2( 780, 900 ), { "In": 0 } )

		b.Connect( scan, 0, lamp_samples, 0 )
		b.Connect( lamp_samples, 0, lamp_edges, 0 )
		b.Connect( lamp_edges, 0, lamp_spawn, 0 )

	# --- Rope swags: hanging mooring festoons strung between rim stations along
	# BOTH rims. Optional. Each swag mesh spans one [swag_spacing] gap along +Z and
	# dips in the middle; placed on the rim so it drapes just inside the edge.
	# Match swag_spacing to the bollard cadence so each festoon links two posts.
	if not swag_variants.is_empty():
		var swag_samples := b.AddNode( "sample_spline", {
			"sampling_mode": 0,
			"uniform_interval": swag_spacing,
			"fill_curve": false,
			"adjust_to_borders": true,
		}, Vector2( 260, 1040 ), { "Splines": 0 } )

		var swag_edges := b.AddNode( "point_offsets", {
			"offsets": [ Vector3( -deck_half_width, 0, 0 ), Vector3( deck_half_width, 0, 0 ) ],
			"rotations": [ Vector3.ZERO ],
			"sizes": [ Vector3.ONE ],
			"local_space": true,
			"combine_rotation": true,
		}, Vector2( 520, 1040 ), { "Anchors": 0 } )

		var swag_spawn := b.AddNode( "spawn_meshes", {
			"mesh": swag_variants[0],
			"mesh_variants": swag_variants,
			"mesh_variant_weights": _even_weights( swag_variants.size() ),
			"randomize_mesh_variants": true,
			"random_seed": variant_seed + 21,
			"use_vertex_colors": false,
			"clear_previous_instances": true,
		}, Vector2( 780, 1040 ), { "In": 0 } )

		b.Connect( scan, 0, swag_samples, 0 )
		b.Connect( swag_samples, 0, swag_edges, 0 )
		# Clip swags whose anchor lands on a terminal sample (end cap) or inside a
		# crossing landing, so no festoon juts out over open water past the deck end.
		if has_exclusion:
			var swag_clip := b.AddNode( "clip_points_by_polygon", {
				"plane": 0,                 # XZ
				"keep_inside": false,
				"spline_stream_attribute": "node",
			}, Vector2( 660, 1040 ), { "Points": 0, "Polygon": 1 } )
			b.Connect( swag_edges, 0, swag_clip, 0 )
			b.Connect( excl_scan, 0, swag_clip, 1 )
			b.Connect( swag_clip, 0, swag_spawn, 0 )
		else:
			b.Connect( swag_edges, 0, swag_spawn, 0 )

	return b.Build()


# =========================================================================
# FENCE — one post+rail unit per segment, draped onto terrain, following curve.
# =========================================================================

## @param unit_variants  fence unit meshes (wood / stone / broken), weighted pick
## @param post_spacing   distance between posts (must match the mesh span)
static func build_fence(
		unit_variants: Array[Mesh],
		post_spacing: float = 2.0,
		variant_weights: Array[float] = [],
		variant_seed: int = 5,
		scan_group: String = GROUP_FENCE ) -> FlowGraphResource:
	var b := FlowGraphBuilder.new()

	var scan := b.AddNode( "scan_splines", {
		"group_name": scan_group,
		"recursive": true,
	}, Vector2( 0, 0 ) )

	var samples := b.AddNode( "sample_spline", {
		"sampling_mode": 0,             # Uniform
		"uniform_interval": post_spacing,
		"fill_curve": false,
		"adjust_to_borders": true,
	}, Vector2( 260, 0 ), { "Splines": 0 } )

	# Drape posts onto the ground (position only) — keep the path yaw so rails
	# still run along the curve rather than tilting to the slope normal.
	var ray := b.AddNode( "ray_cast", {
		"dir": Vector3( 0, -1, 0 ),
		"max_distance": 200.0,
		"collision_mask": GROUND_MASK,
		"from_attribute": "position",
		"out_result_attribute": "hit",
		"out_position_attribute": "position",
		"out_rotation_attribute": "",
		"out_normal_attribute": "",
	}, Vector2( 520, 0 ), { "In": 0 } )

	var hit_filter := b.AddNode( "filter", {
		"in_nameA": "hit", "in_nameB": "True", "condition": 0,   # Equal
	}, Vector2( 780, 0 ), { "In A": 0 } )

	# Tiny yaw jitter for a hand-built look (kept small so rails still meet).
	var jitter := b.AddNode( "transform", {
		"rotation_min": Vector3( 0, -3, 0 ),
		"rotation_max": Vector3( 0, 3, 0 ),
		"scale_min": Vector3.ONE,
		"scale_max": Vector3.ONE,
		"uniform_scale": false,
		"random_seed": variant_seed,
	}, Vector2( 1040, 0 ), { "In": 0 } )

	var weights: Array[float] = variant_weights if not variant_weights.is_empty() else _even_weights( unit_variants.size() )
	var spawn := b.AddNode( "spawn_meshes", {
		"mesh": unit_variants[0],
		"mesh_variants": unit_variants,
		"mesh_variant_weights": weights,
		"randomize_mesh_variants": true,
		"random_seed": variant_seed,
		"use_vertex_colors": false,
		"clear_previous_instances": true,
	}, Vector2( 1300, 0 ), { "In": 0 } )

	b.Connect( scan, 0, samples, 0 )
	b.Connect( samples, 0, ray, 0 )
	b.Connect( ray, 0, hit_filter, 0 )
	b.Connect( hit_filter, 0, jitter, 0 )
	b.Connect( jitter, 0, spawn, 0 )
	return b.Build()


# =========================================================================
# ROAD — segment-centred slabs draped onto terrain, oriented along the curve.
# =========================================================================

## Uses split_splines so each slab is centred on a baked segment and already
## oriented to look along it (its z-bounds carry the segment length). We then
## drape onto the ground and spawn.
## @param slab_variants  road slab meshes (paved / dirt / cobble), weighted pick
## @param slab_spacing   segment length between slab centres
static func build_road(
		slab_variants: Array[Mesh],
		slab_spacing: float = 1.2,
		variant_seed: int = 9,
		scan_group: String = GROUP_ROAD ) -> FlowGraphResource:
	var b := FlowGraphBuilder.new()

	var scan := b.AddNode( "scan_splines", {
		"group_name": scan_group,
		"recursive": true,
	}, Vector2( 0, 0 ) )

	# Segment centres: oriented look-along, evenly spaced.
	var segs := b.AddNode( "sample_spline", {
		"sampling_mode": 0,
		"uniform_interval": slab_spacing,
		"fill_curve": false,
		"adjust_to_borders": true,
		"sample_segments_centers": true,   # centre + look-along per segment
	}, Vector2( 260, 0 ), { "Splines": 0 } )

	var ray := b.AddNode( "ray_cast", {
		"dir": Vector3( 0, -1, 0 ),
		"max_distance": 200.0,
		"collision_mask": GROUND_MASK,
		"from_attribute": "position",
		"out_result_attribute": "hit",
		"out_position_attribute": "position",
		"out_rotation_attribute": "",       # keep the look-along yaw
		"out_normal_attribute": "",
	}, Vector2( 520, 0 ), { "In": 0 } )

	var hit_filter := b.AddNode( "filter", {
		"in_nameA": "hit", "in_nameB": "True", "condition": 0,
	}, Vector2( 780, 0 ), { "In A": 0 } )

	var spawn := b.AddNode( "spawn_meshes", {
		"mesh": slab_variants[0],
		"mesh_variants": slab_variants,
		"mesh_variant_weights": _even_weights( slab_variants.size() ),
		"randomize_mesh_variants": true,
		"random_seed": variant_seed,
		"use_vertex_colors": false,
		"clear_previous_instances": true,
	}, Vector2( 1040, 0 ), { "In": 0 } )

	b.Connect( scan, 0, segs, 0 )
	b.Connect( segs, 0, ray, 0 )
	b.Connect( ray, 0, hit_filter, 0 )
	b.Connect( hit_filter, 0, spawn, 0 )
	return b.Build()


# --- helpers -------------------------------------------------------------

static func _even_weights( count: int ) -> Array[float]:
	var w: Array[float] = []
	w.resize( count )
	w.fill( 1.0 )
	return w
