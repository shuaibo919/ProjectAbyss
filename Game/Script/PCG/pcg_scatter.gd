@tool
extends RefCounted
class_name PcgScatter

## Standalone, reusable PCG scatter-graph builders for the ChapterZero island.
## GDScript-maintained (not baked into the Python generator). Each builder
## returns a FlowGraphResource ready to assign to a FlowGraphNode3D.
##
## Algorithmic basis (classic PCG):
##  - Surface sampling  : a high regular grid + downward ray_cast onto colliders
##                        (the "surface sampler" pattern; grid+project).
##  - Biome density     : fractal (FBM) noise -> per-point density in [0,1].
##  - Slope mask        : normal_to_density combines ground flatness into density.
##  - Thresholding      : density_filter keeps clumps, leaves bare patches.
##  - Spacing           : relax (Lloyd relaxation) evens intra-clump spacing.
##  - Variation         : transform applies per-point yaw + scale jitter.
##
## ray_cast queries the physics world, so graphs must run on an instantiated
## scene with colliders. Water (a layer-1 body) is excluded via a node group.

const FlowGraphBuilder := preload( "res://Script/PCG/flow_graph_builder.gd" )

## Node group name that ray_cast excludes (tag the Water body with this).
const NO_SCATTER_GROUP := "pcg_no_scatter"


## Vegetation grove: clumped trees on flat-ish island ground, off the water.
## @param mesh   The tree ArrayMesh to instance.
## @param region_origin / region_cells / spacing  define the grid footprint.
static func build_vegetation( mesh: Mesh, region_origin: Vector3, region_cells: int, spacing: float, density_seed: int = 7 ) -> FlowGraphResource:
	var b := FlowGraphBuilder.new()

	# Surface-sampler source: regular grid lifted high so the down-ray clears
	# rooftops and lands on terrain/decks.
	var src := b.AddNode( "grid", {
		"x": region_cells, "y": 1, "z": region_cells,
		"step": Vector3( spacing, 0.0, spacing ),
		"origin": region_origin,
		"size": 1.0,
	} )

	# FBM noise -> density (node remaps output to [0,1] internally).
	var dens := b.AddNode( "noise", {
		"out_name": "density",
		"in_scale": 0.03,
		"noise_type": 4,            # Simplex
		"fractal_type": 1,          # FBM
		"fractal_octaves": 4,
		"random_seed": density_seed,
		"mode": 0,                  # Override
		"sample_space": 1,          # XZ2D
	}, Vector2( 200, 0 ), { "In": 0 } )

	# Project onto island colliders, excluding the sea surface.
	var ray := b.AddNode( "ray_cast", {
		"dir": Vector3( 0, -1, 0 ),
		"max_distance": 220.0,
		"collision_mask": 1,
		"out_result_attribute": "hit",
		"out_normal_attribute": "normal",
		"out_position_attribute": "position",
		"exclude_nodes_group": NO_SCATTER_GROUP,
	}, Vector2( 400, 0 ), { "In": 0 } )

	var hit_filter := b.AddNode( "filter", {
		"in_nameA": "hit", "in_nameB": "True", "condition": 0,   # Equal
	}, Vector2( 600, 0 ), { "In A": 0 } )

	# Slope mask: only reasonably flat ground keeps full density (Minimum-combine).
	var slope := b.AddNode( "normal_to_density", {
		"normal_to_compare": Vector3( 0, 1, 0 ),
		"offset": 0.15,             # tolerance so gentle slopes still qualify
		"strength": 1.0,
		"density_mode": 1,          # Minimum
	}, Vector2( 800, 0 ), { "In": 0 } )

	# Threshold -> grove with clearings (not a solid hedge).
	var cull := b.AddNode( "density_filter", {
		"lower_bound": 0.55,
		"upper_bound": 1.0,
	}, Vector2( 1000, 0 ), { "In": 0 } )

	# Even out spacing within clumps.
	var relax := b.AddNode( "relax", {
		"num_iterations": 10,
		"strength": 0.6,
		"padding": 2.2,
	}, Vector2( 1200, 0 ), { "In": 0 } )

	# Per-tree yaw + scale variation (uniform scale so trunks stay vertical).
	var xform := b.AddNode( "transform", {
		"rotation_min": Vector3( 0, 0, 0 ),
		"rotation_max": Vector3( 0, 360, 0 ),
		"scale_min": Vector3( 0.75, 0.7, 0.75 ),
		"scale_max": Vector3( 1.25, 1.4, 1.25 ),
		"uniform_scale": false,
		"random_seed": 21,
	}, Vector2( 1400, 0 ), { "In": 0 } )

	var spawn := b.AddNode( "spawn_meshes", {
		"mesh": mesh,
		"use_vertex_colors": false,     # mesh carries its own vertex-color material
		"clear_previous_instances": true,
	}, Vector2( 1600, 0 ), { "In": 0 } )

	b.Connect( src, 0, dens, 0 )
	b.Connect( dens, 0, ray, 0 )
	b.Connect( ray, 0, hit_filter, 0 )
	b.Connect( hit_filter, 0, slope, 0 )
	b.Connect( slope, 0, cull, 0 )
	b.Connect( cull, 0, relax, 0 )
	b.Connect( relax, 0, xform, 0 )
	b.Connect( xform, 0, spawn, 0 )
	return b.Build()
