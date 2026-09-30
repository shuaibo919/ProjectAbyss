@tool
extends "res://addons/ancient_town/nodes/town_layout_base.gd"

# Town Layout — a flat town with no river: a 朱雀大街 axis with a cross street, lanes grown by
# the SE L-System about the axis (Qin 2023, SYM from the settings), blocks read off the network,
# parcels cut frontage-first, and each parcel planned as a courtyard (TownCourtyards). The 市
# sits on the axis near the centre; a 牌坊 marks each end of the axis.
#
# This is the reusable counterpart of River Town Lots (which is the same pipeline in a curved
# river frame). Everything the two share lives in town_layout_base.gd; this node only defines
# the skeleton, the reserved plots and the use table.
#
#   0 "Buildings"  one point per building — `ab_*` streams for the Ancient Building node.
#   1 "Roads"      unused (the street network is one Structures mesh).
#   2 "Walls"      unused.
#   3 "Props"      prop_type: 0 stall, 1 well, 2 牌坊, 6/7 figures, 9 院门.
#   4 "Trees"      tree points, `size` carries per-tree scale.
#   5 "Structures" the street surface.
#   6 "Yard Walls" 院墙 segments (unit segment, size = len / height / thickness).

const TownLayoutSettings = preload("res://addons/ancient_town/nodes/town_layout_settings.gd")

## The last layout's street graph, for tests and plan-view debugging.
var _last_graph: TownStreetGraph


func _init() -> void:
	meta_node = {
		"title": "Town Layout",
		"settings": TownLayoutSettings,
		"ins": [],
		"outs": [
			{"label": "Buildings"},
			{"label": "Roads"},
			{"label": "Walls"},
			{"label": "Props"},
			{"label": "Trees"},
			{"label": "Structures"},
			{"label": "Yard Walls"},
		],
		"aliases": ["Town", "街巷", "坊", "Settlement Layout", "Courtyard Town"],
		"category": "Generator",
		"tooltip": "Lays out a flat courtyard town: a main axis, lanes grown by the SE L-System\n"
			+ "(symmetry from the settings), blocks → parcels → courtyards.\n"
			+ "Feed Buildings into Ancient Building → Spawn Meshes; spawn Structures\n"
			+ "with a mesh attribute; Yard Walls with a unit yard-wall mesh.",
	}


func getTitle() -> String:
	return "Town Layout - %d x %d m" % [int(settings.extent_x * 2.0), int(settings.extent_z * 2.0)]


func execute(_ctx: FlowData.EvaluationContext) -> void:
	_reset_layout()
	_layout()
	set_output(0, _pack_layout_buildings())
	set_output(1, _pack_strips([]))
	set_output(2, _pack_strips([]))
	set_output(3, _pack_props())
	set_output(4, _pack_trees())
	set_output(5, _pack_structures())
	set_output(6, _pack_strips(_yard_walls))


func _layout() -> void:
	var ex: float = settings.extent_x
	var ez: float = settings.extent_z
	var g := TownStreetGraph.new()
	# Layout frame is centred: (0,0) is the town centre, v the main axis (north).
	var x0 := -ex + 4.0
	var x1 := ex - 4.0
	var z0 := -ez + 4.0
	var z1 := ez - 4.0
	var z_cross := rng.randf_range(-ez * 0.25, ez * 0.25)

	# Skeleton: the main axis, one cross street, a ring of streets inside the boundary.
	g.insert_segment(Vector2(0.0, z0), Vector2(0.0, z1), MAIN, 9.0)
	g.insert_segment(Vector2(x0, z_cross), Vector2(x1, z_cross), STREET, 6.0)
	for x in [x0, x1]:
		g.insert_segment(Vector2(x, z0), Vector2(x, z1), STREET, 5.0)
	for z in [z0, z1]:
		g.insert_segment(Vector2(x0, z), Vector2(x1, z), STREET, 5.0)

	# Lanes about the axis, then about the cross street.
	var growth := TownStreetGrowth.new(g, rng)
	growth.zone = _rect(x0, z0, x1, z1)
	growth.axis = 0.0
	growth.sym = settings.symmetry
	growth.grid = settings.grid
	growth.min_spacing = 19.0
	growth.min_length = 9.0
	growth.max_level = LANE
	growth.level_rules[LANE].length = Vector2(18.0, 30.0)
	growth.level_rules[LANE].branch = 0.55
	for u in [-0.45, -0.2, 0.2, 0.45]:
		growth.add_seed(Vector2(u * ex, z0), Vector2(0.0, 1.0), LANE)
		growth.add_seed(Vector2(u * ex, z1), Vector2(0.0, -1.0), LANE)
	growth.add_seed(Vector2(0.0, (z0 + z_cross) * 0.5), Vector2(-1.0, 0.0), LANE)
	growth.add_seed(Vector2(0.0, (z_cross + z1) * 0.5), Vector2(-1.0, 0.0), LANE)
	growth.grow()

	var levels := TownLevelField.new(settings.random_seed)
	levels.extent = maxf(ex, ez) * 2.0
	levels.foci = [Vector2(0.0, z_cross)]

	# 市 on the axis near the centre (parcels arrive largest first).
	var market := {"taken": false}
	var use_of := func(_parcel: Dictionary, rect: Dictionary, centre: Vector2) -> int:
		match int(rect.level):
			MAIN:
				if not market.taken and absf(centre.y - z_cross) < ez * 0.4:
					market.taken = true
					return USE.MARKET
				return USE.SHOP
			STREET:
				return USE.SHOP if rng.randf() < 0.4 else USE.RESIDENCE
			-1:
				return USE.GARDEN
		return USE.RESIDENCE
	_fill_blocks(0.0, g, [], TownParcels.rules({"width_min": 10.8, "width_max": 21.6, "depth_max": 32.0}),
		levels, use_of, Callable())
	_emit_streets(0.0, g)
	_last_graph = g

	# 牌坊 at both ends of the axis; people on the axis and the cross street.
	for z in [z0 + 5.0, z1 - 5.0]:
		_add_prop(_world(0.0, Vector2(0.0, z)), _yaw_to(Vector3(0.0, 0.0, signf(z))), PROP_ARCH)
	for i in roundi(30.0 * settings.density):
		var q := Vector2(rng.randf_range(-3.5, 3.5), rng.randf_range(z0 + 6.0, z1 - 6.0))
		if i % 2 == 1:
			q = Vector2(rng.randf_range(x0 + 6.0, x1 - 6.0), z_cross + rng.randf_range(-2.0, 2.0))
		_add_prop(_world(0.0, q), rng.randf() * 360.0, PROP_FIGURE_A + (i % 2))
	# A green belt outside the street ring.
	for i in roundi(30.0 * settings.density):
		var a := rng.randf() * TAU
		var r := maxf(ex, ez) + rng.randf_range(8.0, 30.0)
		var q := Vector2(cos(a) * r * 0.8, sin(a) * r * 0.8)
		var tp := _world(0.0, q)
		if _claim(tp, 2.0):
			_add_tree(tp, rng.randf_range(0.8, 1.6))
