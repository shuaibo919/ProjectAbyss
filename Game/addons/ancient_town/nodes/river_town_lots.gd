@tool
extends "res://addons/ancient_town/nodes/town_layout_base.gd"

# River Town Lots — a 水城 layout in one deterministic pass: a river through the
# middle, a walled city on the +side bank (wall, gate pier + tiered gate tower,
# water gate, watch towers, a terraced inner city climbing to a 衙署), a
# waterfront of 河房 and 前店后宅 on the −side bank, and an arch bridge carrying a
# covered gallery between them.
#
# Both banks are laid out as 街巷 → 街区 → 地块 → 院落 by the layout library in
# addons/ancient_town/layout/ (TownStreetGraph / TownStreetGrowth / TownBlocks /
# TownParcels / TownCourtyards / TownLevelField), in each bank's river frame
# (u = arc length, v = offset inland), so streets square to the grid are square to
# the river. The city grows its lanes with Qin 2023's SE L-System about the gate
# axis; the waterfront is a comb of lanes off the 河街, each ending at a 河埠头.
#
# Reuses the Ancient Town node's building parametrisation and stream packing
# (this script extends town_lots.gd), so the Buildings output feeds the Ancient
# Building node exactly the same way. Outputs:
#
#   0 "Buildings"  one point per building — `ab_*` streams, plus `ab_tile_color`
#                  and a per-point `ab_platform`.
#   1 "Roads"      paving strips (unit box, size = len / thickness / width); the
#                  street network itself is a Structures mesh.
#   2 "Walls"      city-wall segments (unit box, size = len / height / thickness).
#   3 "Props"      prop_type: 0 stall, 1 well, 2 牌坊, 3 垛口 merlon, 4 乌篷船,
#                  5 cargo junk, 6/7 figures, 8 landing steps, 9 院门.
#   4 "Trees"      peach trees and bamboo groves; `tree_variant` selects the shared mesh.
#   5 "Structures" one-off whitebox meshes (water, land, 驳岸, piers, bridge,
#                  street surfaces, distant ridges) in a Resource stream — spawn with
#                  mesh_attribute = settings.structure_mesh_attribute.
#   6 "Yard Walls" 院墙 segments (unit segment, size = len / height / thickness).
#
# Optional input "River Path": ordered points (e.g. Sample Spline on a Path3D)
# used as the river centreline instead of the built-in meander.
#
# Frame convention: `n` = (t.z, 0, -t.x) is the building-local +X when a lot faces
# along the river tangent t, and points at the city bank. `side` is +1 for the
# city bank, -1 for the waterfront bank; bank offsets are measured from the
# revetment line into the land.

const RiverSettings = preload("res://addons/ancient_town/nodes/river_town_lots_settings.gd")
const Vegetation = preload("res://Script/PCG/river_town_vegetation.gd")


const SIDE_CITY := 1.0
const SIDE_WATERFRONT := -1.0

# 城门楼: the tower's plan and the 城台 margin round it (the terrace reaches into the city).
const GATE_TOWER_W := 18.0
const GATE_TOWER_D := 11.5
const GATE_MARGIN := 3.0

const SAMPLE_STEP := 4.0
const TOWPATH := 2.0
const PLATFORM_MARGIN := 0.7
const LAND_OFFSETS := [0.0, 4.0, 10.0, 16.0, 24.0, 32.0, 40.0, 50.0, 60.0, 70.0,
	80.0, 95.0, 115.0, 140.0, 180.0, 240.0, 320.0]

const ROBE_A := Color(0.30, 0.33, 0.38)
const ROBE_B := Color(0.66, 0.58, 0.44)

# Terrain ground (settings.terrain_ground). The 驳岸 runs this far past the town ends; beyond it the
# banks are natural. A paved quay tops it: the terrain drops from quay height to the river bed
# underneath the paving, which must span a whole terrain cell measured diagonally (2 m spacing,
# up to 2.6 m across a triangle for a river within ~25° of a grid axis) on either side of the step.
const REVETMENT_MARGIN := 24.0
const QUAY_WIDTH := 5.4
const QUAY_RISE := 0.08
const QUAY_STEP := 2.7           # the terrain step, inland of the wall line
const VALLEY_EXTENSION := 1800.0 # the valley runs on past the modelled river to the map edge
const NATURAL_BANK := 8.0        # natural bank: waterline to quay height over this many metres
const VALLEY_FLAT := 60.0        # flat valley floor beyond the natural bank
const VALLEY_FALLOFF := 280.0    # then a long blend into whatever the terrain had (mountains)
const TOWN_REACH := 150.0        # town ground inland from the revetment
const TOWN_FALLOFF := 60.0
const TOWN_END_FALLOFF := 45.0
const BED_CENTRE := 3.2          # river bed depth below the water at the centre
const BED_EDGE := 1.7            # and at the foot of the 驳岸

## The terrain description of the last layout (settings.terrain_ground): `stamps` (stamp_path
## commands, apply in order), `water_y`, the centreline. Empty when the ground is sheets.
static var last_terrain := {}

## The last layout's street graphs, for tests and plan-view debugging.
var _last_city_graph: TownStreetGraph
var _last_waterfront_graph: TownStreetGraph

# River frame, sampled every SAMPLE_STEP metres of arc length.
var _c := PackedVector3Array()
var _t := PackedVector3Array()
var _n := PackedVector3Array()
var _s := PackedFloat32Array()
var _hw := PackedFloat32Array()
var _s_town0 := 0.0
var _s_town1 := 0.0
var _water_y := 0.0


func _init() -> void:
	meta_node = {
		"title": "River Town Lots",
		"settings": RiverSettings,
		"ins": [{"label": "River Path"}],
		"outs": [
			{"label": "Buildings"},
			{"label": "Roads"},
			{"label": "Walls"},
			{"label": "Props"},
			{"label": "Trees"},
			{"label": "Structures"},
			{"label": "Yard Walls"},
		],
		"aliases": ["Water Town", "水城", "Canal Town", "Riverside"],
		"category": "Generator",
		"tooltip": "Generates a deterministic river town: walled city with gate tower on one\n"
			+ "bank, waterfront street on the other, arch bridge with covered gallery.\n"
			+ "Feed Buildings into Ancient Building → Spawn Meshes; spawn Structures\n"
			+ "with a mesh attribute.",
	}


func getTitle() -> String:
	return "River Town - %d m" % int(settings.town_length)


func execute(_ctx: FlowData.EvaluationContext) -> void:
	_reset_layout()

	if not _build_frame():
		setError("River path needs at least two points.")
		return

	var s_bridge: float = lerpf(_s_town0, _s_town1, settings.bridge_position)
	_build_river()
	if not settings.terrain_ground:
		_build_land()
	_build_bridge(s_bridge)
	_build_city_bank(s_bridge)
	_build_waterfront_bank(s_bridge)
	_build_boats(s_bridge)
	_grow_bamboo_patches(s_bridge)
	if settings.terrain_ground:
		last_terrain = _terrain_description()
	else:
		last_terrain = {}
		_build_backdrop()

	set_output(0, _pack_layout_buildings())
	set_output(1, _pack_strips(_roads))
	set_output(2, _pack_strips(_walls))
	set_output(3, _pack_props())
	set_output(4, _pack_trees())
	set_output(5, _pack_structures())
	set_output(6, _pack_strips(_yard_walls))


# =========================================================================
# River frame
# =========================================================================

func _build_frame() -> bool:
	_c.clear()
	_t.clear()
	_n.clear()
	_s.clear()
	_hw.clear()

	var o: Vector3 = settings.origin
	var raw := PackedVector3Array()
	var input = get_optional_input(0)
	if input is FlowData.Data and input.hasStream(FlowData.AttrPosition):
		for p in input.getVector3Container(FlowData.AttrPosition):
			raw.append(Vector3(p.x, o.y, p.z))
		if raw.size() < 2:
			return false
	else:
		var length: float = settings.river_length
		var amp: float = settings.meander_amplitude
		var phase := rng.randf() * TAU
		var count := int(length / SAMPLE_STEP) + 1
		for i in count:
			var u := float(i) / float(count - 1) - 0.5
			var x := amp * sin(u * TAU * 1.1 + phase) + amp * 0.35 * sin(u * TAU * 2.7 + phase * 1.7)
			raw.append(o + Vector3(x, 0.0, u * length))

	_c = _resample(raw, SAMPLE_STEP)
	var width_phase := rng.randf() * TAU
	var arc := 0.0
	for i in _c.size():
		if i > 0:
			arc += _c[i].distance_to(_c[i - 1])
		_s.append(arc)
		var a := _c[maxi(i - 1, 0)]
		var b := _c[mini(i + 1, _c.size() - 1)]
		var t := (b - a).normalized()
		_t.append(t)
		_n.append(Vector3(t.z, 0.0, -t.x))
		_hw.append(settings.river_width * 0.5 * (1.0 + 0.06 * sin(arc / 57.0 + width_phase)))

	var mid := arc * 0.5
	var half: float = minf(settings.town_length * 0.5, arc * 0.5 - 20.0)
	_s_town0 = mid - half
	_s_town1 = mid + half
	_water_y = o.y - settings.water_depth
	return true


static func _resample(pts: PackedVector3Array, step: float) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.append(pts[0])
	var carry := 0.0
	for i in range(1, pts.size()):
		var a := pts[i - 1]
		var b := pts[i]
		var seg := a.distance_to(b)
		var d := step - carry
		while d <= seg:
			out.append(a.lerp(b, d / seg))
			d += step
		carry = seg - (d - step)
	if out[out.size() - 1].distance_to(pts[pts.size() - 1]) > step * 0.25:
		out.append(pts[pts.size() - 1])
	return out


## Frame at arc length s: position `p` (on the centreline), tangent `t`,
## city-side normal `n`, half width `hw`.
func _at(s: float) -> Dictionary:
	var last := _s.size() - 1
	s = clampf(s, 0.0, _s[last])
	var i := clampi(_s.bsearch(s), 1, last)
	var s0 := _s[i - 1]
	var s1 := _s[i]
	var f := 0.0 if s1 <= s0 else (s - s0) / (s1 - s0)
	return {
		"p": _c[i - 1].lerp(_c[i], f),
		"t": _t[i - 1].lerp(_t[i], f).normalized(),
		"n": _n[i - 1].lerp(_n[i], f).normalized(),
		"hw": lerpf(_hw[i - 1], _hw[i], f),
	}


## Point `off` metres inland from the revetment on `side`, at ground height.
func _bank(s: float, side: float, off: float) -> Vector3:
	var f := _at(s)
	var n: Vector3 = f.n
	var p: Vector3 = f.p + n * side * (float(f.hw) + off)
	p.y += _ground(side, off)
	return p


## The river town lays each bank out in its own river frame: u = arc length, v = offset inland
## from the revetment. A street square to that grid is square to the river and follows its bends.
func _world(side: float, q: Vector2) -> Vector3:
	return _bank(q.x, side, q.y)


## Direction from the bank into the land.
func _inland(s: float, side: float) -> Vector3:
	var n: Vector3 = _at(s).n
	return n * side


## The inner city climbs behind the wall so its roofs read over it; the
## waterfront bank stays flat.
func _ground(side: float, off: float) -> float:
	if side < 0.0:
		return 0.0
	var start := _wall_inner() + 8.0
	return settings.terrace_rise * smoothstep(start, start + 70.0, off)


func _wall_centre() -> float:
	return TOWPATH + settings.wall_thickness * 0.5


func _wall_inner() -> float:
	return TOWPATH + settings.wall_thickness


# =========================================================================
# Building parameters
# =========================================================================


## A building standing on its own 城台 (base_kind 1): the terrace is part of the building, so its
## height, passages and parapet come from the same parameters as the hall on top.
func _terrace_params(height: float, margin: float, arches: int) -> Dictionary:
	return {
		"platform": true,
		"base_kind": 1,
		"base_height": height,
		"base_margin": margin,
		"base_arches": arches,
		"base_parapet": 1,
		"fence": false,
		"steps": false,
	}


## Arched masonry from the native AncientMasonry node (券脸, 收分, 垛口), falling back to the
## whitebox arch_block when the extension is not loaded.
func _masonry_mesh(values: Dictionary, fallback: Callable) -> Mesh:
	if not ClassDB.class_exists("AncientMasonry"):
		return fallback.call()
	var m = ClassDB.instantiate("AncientMasonry")
	for key in values:
		m.set(key, values[key])
	var mesh: Mesh = m.bake_mesh()
	m.free()
	mesh.set_meta("town_material", "brick")
	return mesh


# =========================================================================
# River, land, backdrop
# =========================================================================

func _build_river() -> void:
	if settings.terrain_ground:
		_build_town_revetment()
		return
	var a_line := PackedVector3Array()
	var b_line := PackedVector3Array()
	var a_rev := PackedVector3Array()
	var b_rev := PackedVector3Array()
	var a_out := PackedVector3Array()
	var b_out := PackedVector3Array()
	for i in _c.size():
		var edge_a := _c[i] + _n[i] * _hw[i]
		var edge_b := _c[i] - _n[i] * _hw[i]
		a_line.append(Vector3(edge_a.x, _water_y, edge_a.z))
		b_line.append(Vector3(edge_b.x, _water_y, edge_b.z))
		a_rev.append(edge_a)
		b_rev.append(edge_b)
		a_out.append(-_n[i])
		b_out.append(_n[i])
	_add_structure(Vector3.ZERO, 0.0, Meshes.sheet([a_line, b_line], Meshes.WATER_COL, true))

	var y0: float = settings.origin.y
	var band := _water_y + 0.7
	var bottom := _water_y - 1.3
	_add_structure(Vector3.ZERO, 0.0, Meshes.revetment(a_rev, a_out, y0, band, bottom))
	_add_structure(Vector3.ZERO, 0.0, Meshes.revetment(b_rev, b_out, y0, band, bottom))


func _build_land() -> void:
	for side in [SIDE_CITY, SIDE_WATERFRONT]:
		var rows: Array = []
		for off in LAND_OFFSETS:
			var row := PackedVector3Array()
			for i in _c.size():
				var p: Vector3 = _c[i] + _n[i] * side * (_hw[i] + off)
				p.y += _ground(side, off)
				row.append(p)
			rows.append(row)
		_add_structure(Vector3.ZERO, 0.0, Meshes.sheet(rows, Meshes.LAND_COL))


## Terrain ground: the 驳岸 only along the town, with a coping over the terrain step.
func _build_town_revetment() -> void:
	var a_rev := PackedVector3Array()
	var b_rev := PackedVector3Array()
	var a_out := PackedVector3Array()
	var b_out := PackedVector3Array()
	for i in _town_indices():
		a_rev.append(_c[i] + _n[i] * _hw[i])
		b_rev.append(_c[i] - _n[i] * _hw[i])
		a_out.append(-_n[i])
		b_out.append(_n[i])
	var top: float = settings.origin.y + QUAY_RISE
	var band := _water_y + 0.7
	var bottom := _water_y - BED_EDGE - 0.3
	for rev in [[a_rev, a_out], [b_rev, b_out]]:
		_add_structure(Vector3.ZERO, 0.0, Meshes.revetment(rev[0], rev[1], top, band, bottom))
		_add_structure(Vector3.ZERO, 0.0, Meshes.quay(rev[0], rev[1], top, QUAY_WIDTH, 0.5))


## Frame samples of the walled / quayed stretch.
func _town_indices() -> PackedInt32Array:
	var out := PackedInt32Array()
	for i in _c.size():
		if _s[i] >= _s_town0 - REVETMENT_MARGIN and _s[i] <= _s_town1 + REVETMENT_MARGIN:
			out.append(i)
	return out


## Ground shape for a terrain, as stamp_path commands (Terrain3DAgent) over the river centreline:
## the whole valley first (natural banks, a flat floor, then a long blend into the mountains), then
## the town (quays under the coping, the city terrace from `_ground`). Heights are absolute.
func _terrain_description() -> Dictionary:
	var y0: float = settings.origin.y
	var bed_centre := _water_y - BED_CENTRE
	var bed_edge := _water_y - BED_EDGE
	var centre := PackedVector3Array()
	var widths := []
	for i in _c.size():
		centre.append(Vector3(_c[i].x, y0, _c[i].z))
		widths.append(_hw[i])
	# The valley runs straight on from both ends of the modelled river to the edge of the map.
	var valley_points := centre.duplicate()
	var valley_widths := widths.duplicate()
	var last := _c.size() - 1
	var steps := int(VALLEY_EXTENSION / 50.0)
	for k in range(1, steps + 1):
		var head := _c[0] - _t[0] * 50.0 * k
		var tail := _c[last] + _t[last] * 50.0 * k
		valley_points.insert(0, Vector3(head.x, y0, head.z))
		valley_points.append(Vector3(tail.x, y0, tail.z))
		valley_widths.insert(0, _hw[0])
		valley_widths.append(_hw[last])

	var natural := [[-30.0, bed_centre], [-8.0, bed_edge], [0.0, _water_y + 0.25],
		[NATURAL_BANK, y0], [NATURAL_BANK + VALLEY_FLAT, y0]]
	var valley := {
		"op": "stamp_path", "points": valley_points, "half_widths": valley_widths, "profile": natural,
		"falloff": VALLEY_FALLOFF, "auto_regions": false,
	}

	var town_points := PackedVector3Array()
	var town_widths := []
	for i in _town_indices():
		town_points.append(centre[i])
		town_widths.append(_hw[i])
	# The step from river bed to quay height sits under the paved quay.
	var quay := [[-30.0, bed_centre], [-6.0, bed_edge], [QUAY_STEP, bed_edge], [QUAY_STEP + 0.05, y0]]
	var waterfront := quay.duplicate(true)
	waterfront.append([TOWN_REACH, y0])
	var city := quay.duplicate(true)
	var off := QUAY_WIDTH + 2.0
	while off <= TOWN_REACH:
		city.append([off, y0 + _ground(SIDE_CITY, off)])
		off += 4.0
	var town := {
		"op": "stamp_path", "points": town_points, "half_widths": town_widths,
		"profile_left": city, "profile_right": waterfront,
		"falloff": TOWN_FALLOFF, "end_falloff": TOWN_END_FALLOFF, "auto_regions": false,
	}
	return {
		"stamps": [valley, town],
		"water_y": _water_y,
		"bed_centre": bed_centre,
		"centre": centre,
		"half_widths": widths,
		"town_path": town_points,
		"town_half_widths": town_widths,
	}


## Hills up-river and behind both banks. Fog does the atmospheric perspective.
func _build_backdrop() -> void:
	var total := _s[_s.size() - 1]
	var end := _at(total)
	var mid := _at(total * 0.5)
	var t: Vector3 = end.t
	var p: Vector3 = end.p
	var n: Vector3 = end.n
	var mp: Vector3 = mid.p
	var mn: Vector3 = mid.n
	# [position, facing, length, height]
	var ranges := [
		[p + t * 140.0 + n * 60.0, -t, 900.0, 42.0],
		[p + t * 330.0 - n * 120.0, -t, 1400.0, 80.0],
		[mp + mn * 520.0, -mn, 1100.0, 55.0],
		[mp - mn * 540.0, mn, 1100.0, 40.0],
	]
	for k in ranges.size():
		var r: Array = ranges[k]
		var pos: Vector3 = r[0]
		pos.y = settings.origin.y
		_add_structure(pos, _yaw_to(r[1]), Meshes.distant_ridge(r[2], r[3], settings.random_seed + k))


# =========================================================================
# Bridge + gates
# =========================================================================

func _build_bridge(s_bridge: float) -> void:
	if not settings.bridge:
		return
	var f := _at(s_bridge)
	var t: Vector3 = f.t
	var n: Vector3 = f.n
	var hw: float = f.hw
	var p: Vector3 = f.p

	# Arch bridge: local X across the river (n), openings pass the water along t.
	var span := 2.0 * (hw + 1.0)
	var deck: float = settings.origin.y + 0.7
	var arches: int = settings.bridge_arches
	var pitch := span * 0.9 / float(arches)
	var opening := minf(10.0, pitch * 0.72)
	var openings: Array = []
	for k in arches:
		openings.append(Vector2((float(k) - float(arches - 1) * 0.5) * pitch, opening))
	var spring := _water_y + 0.5
	var bridge_base := _water_y - 1.5
	var bridge_height := deck - bridge_base
	var bridge_values := {
		"length": span, "thickness": 7.0, "height": bridge_height,
		"batter_faces": 0.0, "batter_ends": 0.0, "arch_count": arches, "arch_width": opening,
		"arch_pitch": pitch, "arch_height_ratio": clampf((deck - 0.7 - bridge_base) / bridge_height, 0.2, 0.93),
		"parapet": 0, "plinth_height": 0.0, "ring_thickness": 0.45,
	}
	_add_structure(Vector3(p.x, bridge_base, p.z), _yaw_to(t),
		_masonry_mesh(bridge_values, func(): return Meshes.arch_block(span, 0.0, bridge_height, 7.0, openings,
			spring - bridge_base, deck - 0.7 - spring)), "bridge")

	if settings.bridge_gallery:
		# One long gallery would be absurdly tall — Table 1 ties eave height to width —
		# so it is a run of short bays. Alternate depths keep neighbouring 悬山 gables
		# from lying coplanar and z-fighting; the middle bay is a 歇山 pavilion.
		var bays := maxi(3, roundi(2.0 * hw / 7.0))
		if bays % 2 == 0:
			bays += 1
		var seg := snappedf(2.0 * hw / float(bays), 0.5)
		for j in bays:
			var x := (float(j) - float(bays - 1) * 0.5) * seg
			var centre := j == (bays - 1) / 2
			var gp := _house(3, seg, 6.0 if centre else (5.0 if j % 2 == 0 else 5.5),
				ROOF_GABLE_AND_HIP if centre else ROOF_OVERHANGING, MAT_TRADITIONAL)
			gp["platform"] = false
			gp["walls"] = false
			# 廊桥 bays are open, with 美人靠 along both sides and the 楣子 overhead.
			gp["railing"] = 2
			gp["hanging_fascia"] = true
			gp["tile_color"] = TILE_RED if centre else TILE_GREY
			var gpos := p + n * x
			gpos.y = deck
			_add_lot(gpos, _yaw_to(t), 3, LOT_LANDMARK, -1, gp)
		for i in roundi(6.0 * settings.density):
			var fp := p + n * rng.randf_range(-hw, hw) + t * rng.randf_range(-1.8, 1.8)
			fp.y = deck
			_add_prop(fp, rng.randf() * 360.0, PROP_FIGURE_A + (i % 2))

	# Bridgehead on the waterfront bank: a red-roofed hall on its own 城台, the street running
	# through the terrace's passage.
	var bh_depth := 10.0
	var bh := _bank(s_bridge, SIDE_WATERFRONT, 0.3 + bh_depth * 0.5)
	var to_water := n   # the waterfront bank lies at -n, so the river is at +n
	_claim(bh, 8.5)
	var bp := _house(3, 10.5, 7.0, ROOF_GABLE_AND_HIP, MAT_TRADITIONAL)
	bp.merge(_terrace_params(5.5, 1.5, 1), true)
	bp["tile_color"] = TILE_RED
	_add_lot(bh, _yaw_to(to_water), 3, LOT_TOWER, -1, bp)
	_add_marker(bh, "bridgehead")


# =========================================================================
# City bank: wall, gates, terraced inner city
# =========================================================================

func _build_city_bank(s_bridge: float) -> void:
	var len_town := _s_town1 - _s_town0
	var wall_h: float = settings.wall_height
	var thick: float = settings.wall_thickness
	var gaps: Array[Vector2] = []
	var s_wg := _s_town0 + len_town * 0.16
	if not settings.city_wall or absf(s_wg - s_bridge) <= 40.0:
		s_wg = -1.0

	if settings.city_wall:
		# 城门楼 over the bridge landing: one building — its 城台 with the gate passage, and the
		# storeyed tower growing out of the terrace top.
		var tower_w := GATE_TOWER_W
		var tower_d := GATE_TOWER_D
		var margin := GATE_MARGIN
		var pier_w := tower_w + 2.0 * margin
		var pier_d := tower_d + 2.0 * margin
		var pier_h := wall_h + 1.5
		var pier := _bank(s_bridge, SIDE_CITY, 0.3 + pier_d * 0.5)
		var n: Vector3 = _at(s_bridge).n
		_claim(pier, pier_d * 0.5)
		gaps.append(Vector2(s_bridge - pier_w * 0.5, s_bridge + pier_w * 0.5))
		var gate := _terrace_params(pier_h, margin, 1)
		gate.merge({ "bays_x": 7, "bays_z": 5, "storey_setback": 1, "storey_balcony": true,
			"upper_column_scale": 0.7 }, true)
		_add_storeyed(pier, _yaw_to(-n), tower_w, tower_d, ROOF_GABLE_AND_HIP, settings.gate_tower_tiers,
			TILE_GLAZED, 5, LOT_TOWER, gate)
		_add_marker(pier, "gate_pier")

		# Water gate (水门) downstream.
		if s_wg >= 0.0:
			var wg := _bank(s_wg, SIDE_CITY, _wall_centre())
			var wg_values := { "length": 12.0, "thickness": thick + 0.8, "height": wall_h, "batter_faces": 0.08,
				"arch_count": 1, "arch_width": 5.0, "arch_height_ratio": 0.6, "arch_profile": 1, "parapet": 1 }
			_add_structure(wg, _yaw_to(_at(s_wg).n), _masonry_mesh(wg_values, func():
				return Meshes.arch_block(12.0, -0.5, wall_h, thick + 0.8, [Vector2(0.0, 5.0)], 2.6, 2.2,
					Meshes.CITY_WALL_COL)), "water_gate")
			gaps.append(Vector2(s_wg - 6.0, s_wg + 6.0))

		_build_wall_run(gaps)

		# 敌楼 on their own terraces, and 重檐角楼 where the wall turns inland.
		for u in [0.36, 0.84]:
			var s_t: float = _s_town0 + len_town * u
			if absf(s_t - s_bridge) < 30.0:
				continue
			var at := _bank(s_t, SIDE_CITY, _wall_centre() + 0.6)
			_claim(at, 5.0)
			var tp := _house(3, 8.0, 5.5, ROOF_GABLE_AND_HIP, MAT_TRADITIONAL)
			tp.merge(_terrace_params(wall_h + 0.6, 1.0, 0), true)
			_add_lot(at, _yaw_to(-_at(s_t).n), 3, LOT_TOWER, -1, tp)
		for s_c in [_s_town0, _s_town1]:
			var at := _bank(s_c, SIDE_CITY, _wall_centre() + 0.6)
			_claim(at, 4.5)
			var cp := _house(3, 6.5, 6.5, ROOF_PYRAMIDAL, MAT_TRADITIONAL)
			cp.merge(_terrace_params(wall_h + 0.8, 1.25, 0), true)
			cp.merge({ "sides": 8, "walls": false, "railing": 1, "hanging_fascia": true,
				"storey_count": 2, "storey_setback": 1, "upper_column_scale": 0.45 }, true)
			_add_lot(at, 0.0, 3, LOT_TOWER, -1, cp)

	_layout_city(s_bridge, s_wg)


## City wall along the city bank between the town ends, with openings at `gaps`
## (arc-length intervals), a parapet of merlons, and two return walls inland.
func _build_wall_run(gaps: Array[Vector2]) -> void:
	var wall_h: float = settings.wall_height
	var thick: float = settings.wall_thickness
	var off := _wall_centre()
	var step := 6.0
	var s := _s_town0
	while s < _s_town1 - 0.01:
		var s_next := minf(s + step, _s_town1)
		var mid := (s + s_next) * 0.5
		if not _in_gaps(mid, gaps, 0.0):
			var a := _bank(s, SIDE_CITY, off)
			var b := _bank(s_next, SIDE_CITY, off)
			_add_wall((a + b) * 0.5 + Vector3(0.0, wall_h * 0.5, 0.0), _road_yaw(b - a),
				a.distance_to(b) + thick * 0.4, wall_h, thick)
		s = s_next

	# 垛口 on the river face.
	var m := _s_town0 + 1.5
	while m < _s_town1 - 1.5:
		if not _in_gaps(m, gaps, 1.0):
			var mp := _bank(m, SIDE_CITY, TOWPATH + 0.3)
			mp.y = settings.origin.y + wall_h
			_add_prop(mp, _road_yaw(_at(m).t), PROP_MERLON)
		m += 2.6

	# Return walls turn inland at both ends, stepping up the terrace.
	for s_end in [_s_town0, _s_town1]:
		var o := off
		while o < 150.0:
			var a := _bank(s_end, SIDE_CITY, o)
			var b := _bank(s_end, SIDE_CITY, o + step)
			var c := (a + b) * 0.5
			_add_wall(Vector3(c.x, c.y + wall_h * 0.5 - 0.5, c.z), _road_yaw(b - a),
				a.distance_to(b) + thick * 0.4, wall_h + 1.0, thick)
			o += step


static func _in_gaps(s: float, gaps: Array[Vector2], pad: float) -> bool:
	for g in gaps:
		if s > g.x - pad and s < g.y + pad:
			return true
	return false


## Landing steps down to the water at arc length s, with a moored boat and a
## tree. Returns false when the spot is taken.
func _landing(s: float, side: float) -> bool:
	var top := _bank(s, side, 0.0)
	if not _claim(top + _inland(s, side) * 2.0, 2.2):
		return false
	var to_water := -_inland(s, side)
	_add_prop(top, _yaw_to(to_water), PROP_STEPS)
	var f := _at(s)
	var t: Vector3 = f.t
	var boat := top + to_water * 3.2 + t * rng.randf_range(4.0, 6.0)
	boat.y = _water_y
	_add_prop(boat, _road_yaw(t * (1.0 if rng.randf() < 0.5 else -1.0)),
		PROP_BOAT if rng.randf() < 0.8 else PROP_CARGO)
	if rng.randf() < 0.6:
		_add_tree(_bank(s, side, 2.5), rng.randf_range(0.8, 1.2))
	return true


# =========================================================================
# Waterfront bank
# =========================================================================

func _build_waterfront_bank(s_bridge: float) -> void:
	var side := SIDE_WATERFRONT
	var pavilions := PackedFloat32Array()

	# 水榭 out over the water on piles, placed first so the river row leaves room for them.
	# Their stair lands on the quay, so they face the land.
	for u in [0.12, 0.88]:
		var s_x: float = lerpf(_s_town0, _s_town1, u)
		if settings.bridge and absf(s_x - s_bridge) < 30.0:
			continue
		var xie_pos := _bank(s_x, side, -4.8)
		xie_pos.y = settings.origin.y
		if _claim(_bank(s_x, side, 1.0), 6.0):
			var xie := _house(2, 9.0, 6.0, ROOF_ROUND_RIDGE, MAT_TRADITIONAL)
			xie.merge({ "base_kind": 3, "stilt_depth": settings.water_depth + 1.5, "walls": false,
				"fence": false, "steps": true, "fence_lambda": 0, "railing": 2, "hanging_fascia": true }, true)
			_add_lot(xie_pos, _yaw_to(_inland(s_x, side)), 2, LOT_LANDMARK, -1, xie)
			pavilions.append(s_x)

	_layout_waterfront(s_bridge, pavilions)


func _build_boats(s_bridge: float) -> void:
	for i in roundi(9.0 * settings.density):
		var s := rng.randf_range(_s_town0 - 40.0, _s_town1 + 40.0)
		if settings.bridge and absf(s - s_bridge) < 25.0:
			continue
		var f := _at(s)
		var t: Vector3 = f.t
		var n: Vector3 = f.n
		var p: Vector3 = f.p + n * rng.randf_range(-0.55, 0.55) * float(f.hw)
		p.y = _water_y
		_add_prop(p, _road_yaw(t * (1.0 if rng.randf() < 0.5 else -1.0)),
			PROP_BOAT if rng.randf() < 0.7 else PROP_CARGO)


# =========================================================================
# 街巷 → 街区 → 地块 → 院落 (addons/ancient_town/layout/)
# =========================================================================


## Inner city: the street inside the wall, the gate axis (大街) up to a cross street, a 衙署 at the
## axis head, and lanes grown by the SE L-System about the axis between them.
func _layout_city(s_bridge: float, s_wg: float) -> void:
	var side := SIDE_CITY
	var g := TownStreetGraph.new()
	var u0 := _s_town0 + 4.5
	var u1 := _s_town1 - 4.5
	var v_street := _wall_inner() + 3.6
	var v_cross := v_street + 42.0
	var v_back := v_street + 112.0
	var main_w := 9.0

	# Skeleton — the streets an artist would draw first (the "手摆道路" stage of the GDC talk).
	var pier_half := GATE_TOWER_W * 0.5 + GATE_MARGIN
	var pier_in := 0.3 + GATE_TOWER_D + 2.0 * GATE_MARGIN
	var gate_on: bool = settings.city_wall and settings.bridge
	if gate_on:
		# The 城台 reaches across the wall street; the street stops at it and the axis leaves
		# from its inner face, through the gate passage.
		g.insert_segment(Vector2(_s_town0 + 1.5, v_street), Vector2(s_bridge - pier_half, v_street), STREET, 6.0)
		g.insert_segment(Vector2(s_bridge + pier_half, v_street), Vector2(_s_town1 - 1.5, v_street), STREET, 6.0)
		g.insert_segment(Vector2(s_bridge - pier_half, v_street), Vector2(s_bridge - pier_half, pier_in), BOUNDARY, 0.2)
		g.insert_segment(Vector2(s_bridge - pier_half, pier_in), Vector2(s_bridge + pier_half, pier_in), BOUNDARY, 0.2)
		g.insert_segment(Vector2(s_bridge + pier_half, pier_in), Vector2(s_bridge + pier_half, v_street), BOUNDARY, 0.2)
		g.insert_segment(Vector2(s_bridge, pier_in), Vector2(s_bridge, v_cross), MAIN, main_w)
	else:
		g.insert_segment(Vector2(_s_town0 + 1.5, v_street), Vector2(_s_town1 - 1.5, v_street), STREET, 6.0)
		g.insert_segment(Vector2(s_bridge, v_street), Vector2(s_bridge, v_cross), MAIN, main_w)
	for u in [u0, u1]:
		g.insert_segment(Vector2(u, v_street), Vector2(u, v_back), LANE, 3.2)
	g.insert_segment(Vector2(u0, v_cross), Vector2(u1, v_cross), STREET, 6.0)
	g.insert_segment(Vector2(u0, v_back), Vector2(u1, v_back), BOUNDARY, 0.2)
	if s_wg >= 0.0:
		g.insert_segment(Vector2(s_wg, v_street), Vector2(s_wg, v_cross), LANE, 3.2)

	# Reserved ground: the 衙署 on the axis head, the 楼阁 and the 亭 on the hill.
	# 64 m deep: gate hall, a court long enough for 两庑 clear of both hipped eaves, the hall and its
	# stair, a rear hall — TownCourtyards drops the 庑 and the rear hall first when it is shorter.
	var office_w := 38.0
	var office_d := minf(64.0, v_back - v_cross - 5.0)
	var office := {"origin": Vector2(s_bridge - office_w * 0.5, v_cross + 3.2), "f": Vector2(1.0, 0.0),
		"n": Vector2(0.0, 1.0), "width": office_w, "depth": office_d, "level": STREET}
	var office_centre: Vector2 = office.origin + Vector2(office_w * 0.5, office_d * 0.5)
	var reserved: Array[PackedVector2Array] = [_rect(s_bridge - office_w * 0.5 - 0.5, v_cross + 3.0,
		s_bridge + office_w * 0.5 + 0.5, v_cross + 3.2 + office_d + 0.6)]
	var s_pagoda := clampf(s_bridge + 60.0, u0 + 14.0, u1 - 14.0)
	var s_ting := clampf(s_bridge - 56.0, u0 + 10.0, u1 - 10.0)
	var v_pagoda := v_cross + 30.0
	var v_ting := v_cross + 38.0
	reserved.append(_rect(s_pagoda - 9.0, v_pagoda - 9.0, s_pagoda + 9.0, v_pagoda + 9.0))
	reserved.append(_rect(s_ting - 5.5, v_ting - 5.5, s_ting + 5.5, v_ting + 5.5))

	# Lanes: SE L-System, mirrored about the gate axis with SYM of local asymmetry.
	var growth := TownStreetGrowth.new(g, rng)
	growth.zone = _rect(u0, v_street, u1, v_back)
	growth.axis = s_bridge
	growth.sym = settings.symmetry
	growth.obstacles = reserved
	growth.grid = 6.0
	growth.min_spacing = 19.0
	growth.min_length = 9.0
	growth.max_level = LANE
	growth.level_rules[LANE].length = Vector2(18.0, 30.0)
	growth.level_rules[LANE].branch = 0.55
	growth.add_seed(Vector2(s_bridge - 30.0, v_cross), Vector2(0.0, 1.0), LANE)
	growth.add_seed(Vector2(s_bridge - 30.0, v_street), Vector2(0.0, 1.0), LANE)
	growth.add_seed(Vector2(s_bridge, (v_street + v_cross) * 0.5), Vector2(-1.0, 0.0), LANE)
	growth.add_seed(Vector2(s_bridge - 70.0, v_cross), Vector2(0.0, 1.0), LANE)
	growth.grow()

	var levels := TownLevelField.new(settings.random_seed)
	levels.extent = 240.0
	levels.foci = [office_centre]

	# 市: the biggest parcel on the axis inside the gate (parcels arrive largest first).
	var market := {"taken": false}
	var use_of := func(_parcel: Dictionary, rect: Dictionary, centre: Vector2) -> int:
		match int(rect.level):
			MAIN:
				if not market.taken and centre.y < v_cross:
					market.taken = true
					return USE.MARKET
				return USE.SHOP
			STREET:
				return USE.SHOP if rng.randf() < 0.45 else USE.RESIDENCE
			-1:
				return USE.GARDEN
		return USE.RESIDENCE
	_fill_blocks(side, g, reserved, TownParcels.rules({"width_min": 10.8, "width_max": 21.6, "depth_max": 32.0}),
		levels, use_of, Callable())

	var office_level := levels.level(office_centre, 5, 6)
	_emit_courtyard(side, TownCourtyards.plan(_metric_rect(side, office), TownCourtyards.Kind.OFFICIAL, USE.OFFICIAL,
		office_level, rng))
	_add_marker(_world(side, office_centre), "office")
	_emit_streets(side, g)
	_last_city_graph = g

	# Landmarks on their reserved plots.
	var pg := _world(side, Vector2(s_pagoda, v_pagoda))
	_claim(pg, 7.0)
	_add_storeyed(pg, _yaw_to(-_inland(s_pagoda, side)), 10.0, 10.0, ROOF_PYRAMIDAL, 3, TILE_GLAZED, 4, LOT_LANDMARK,
		{ "bays_x": 5, "bays_z": 5, "storey_setback": 1, "storey_balcony": true, "upper_column_scale": 0.75 })
	var tg := _world(side, Vector2(s_ting, v_ting))
	_claim(tg, 4.5)
	var ting := _house(2, 6.0, 6.0, ROOF_PYRAMIDAL, MAT_TRADITIONAL)
	ting.merge({ "sides": 6, "walls": false, "fence": false, "steps": true, "railing": 2,
		"hanging_fascia": true }, true)
	_add_lot(tg, _yaw_to(-_inland(s_ting, side)), 2, LOT_LANDMARK, -1, ting)

	# 牌坊 on the axis inside the gate and before the 衙署.
	for v in [pier_in + 5.0 if gate_on else v_street + 6.0, v_cross - 7.0]:
		_add_prop(_world(side, Vector2(s_bridge, v)), _yaw_to(_inland(s_bridge, side)), PROP_ARCH)

	# People on the axis and the street inside the wall.
	for i in roundi(22.0 * settings.density):
		var q := Vector2(s_bridge + rng.randf_range(-main_w * 0.4, main_w * 0.4), rng.randf_range(pier_in, v_cross))
		if i % 2 == 1:
			q = Vector2(rng.randf_range(u0, u1), v_street + rng.randf_range(-2.2, 2.2))
		_add_prop(_world(side, q), rng.randf() * 360.0, PROP_FIGURE_A + (i % 2))

	# Woods on the hill behind the town and beyond its ends.
	for i in roundi(40.0 * settings.density):
		var s_tree := rng.randf_range(_s_town0 - 30.0, _s_town1 + 30.0)
		var off := rng.randf_range(v_back + 4.0, v_back + 60.0)
		if s_tree < _s_town0 or s_tree > _s_town1:
			off = rng.randf_range(6.0, v_back + 40.0)
		var tp := _bank(s_tree, side, off)
		if _claim(tp, 2.0):
			_add_tree(tp, rng.randf_range(0.9, 1.7))
			_trees.back().merge({"woodland": true, "bank_side": side, "bank_pos": Vector2(s_tree, off), "town_edge": v_back})


## Waterfront: the 河街 along the river row, a 后街 behind, a bridge street inland from the
## bridgehead, and a comb of lanes square to the river — each one either running on inland or
## ending at the water as a 河埠头.
func _layout_waterfront(s_bridge: float, pavilions: PackedFloat32Array) -> void:
	var side := SIDE_WATERFRONT
	var g := TownStreetGraph.new()
	var sw: float = settings.street_width
	var v_quay := 0.3
	var v_street := 9.9 + sw * 0.5
	var v_back := v_street + sw * 0.5 + 36.0
	var v_edge := v_back + 1.8 + 26.0
	var ua := _s_town0 - 2.0
	var ub := _s_town1 + 2.0

	g.insert_segment(Vector2(_s_town0 - 10.0, v_street), Vector2(_s_town1 + 10.0, v_street), STREET, sw)
	g.insert_segment(Vector2(ua, v_back), Vector2(ub, v_back), LANE, 3.6)
	g.insert_segment(Vector2(ua, v_quay), Vector2(ub, v_quay), BOUNDARY, 0.2)
	g.insert_segment(Vector2(ua, v_quay), Vector2(ua, v_edge), BOUNDARY, 0.2)
	g.insert_segment(Vector2(ub, v_quay), Vector2(ub, v_edge), BOUNDARY, 0.2)
	g.insert_segment(Vector2(ua, v_edge), Vector2(ub, v_edge), BOUNDARY, 0.2)
	if settings.bridge:
		g.insert_segment(Vector2(s_bridge, v_street), Vector2(s_bridge, v_edge), MAIN, 7.0)

	var reserved: Array[PackedVector2Array] = []
	if settings.bridge:
		reserved.append(_rect(s_bridge - 9.5, v_quay - 1.0, s_bridge + 9.5, v_street - sw * 0.5))
	for s_x in pavilions:
		reserved.append(_rect(s_x - 6.0, v_quay - 1.0, s_x + 6.0, 5.5))

	# The comb. Lanes keep clear of the bridge street, the pavilions' stairs and the town ends.
	var busy := func(u: float) -> bool:
		if settings.bridge and absf(u - s_bridge) < 14.0:
			return true
		for s_x in pavilions:
			if absf(u - s_x) < 8.0:
				return true
		return u < ua + 8.0 or u > ub - 8.0
	var teeth := PackedFloat32Array()
	var landings := PackedFloat32Array()
	var u := ua + rng.randf_range(10.0, 18.0)
	while u < ub - 10.0:
		if not busy.call(u):
			g.insert_segment(Vector2(u, v_street), Vector2(u, v_back), LANE, 3.2)
			teeth.append(u)
			if rng.randf() < 0.6:
				g.insert_segment(Vector2(u, v_back), Vector2(u, v_edge), LANE, 2.8)
			if rng.randf() < 0.5:
				g.insert_segment(Vector2(u, v_quay), Vector2(u, v_street), LANE, 2.6)
				landings.append(u)
		u += rng.randf_range(24.0, 40.0)
	# More 河埠头 lanes between the teeth, so the river row breaks every 20–40 m.
	u = ua + rng.randf_range(20.0, 30.0)
	while u < ub - 10.0:
		var clear: bool = not busy.call(u)
		for l in landings:
			if absf(l - u) < 12.0:
				clear = false
		for t in teeth:
			if absf(t - u) < 10.0:
				clear = false
		if clear:
			g.insert_segment(Vector2(u, v_quay), Vector2(u, v_street), LANE, 2.6)
			landings.append(u)
		u += rng.randf_range(22.0, 34.0)
	for l in landings:
		_landing(l, side)
	# A comb tooth near the bridge, for the lane-level camera.
	var best := INF
	var lane_u := -1.0
	for t in teeth:
		if absf(t - (s_bridge - 45.0)) < best:
			best = absf(t - (s_bridge - 45.0))
			lane_u = t
	if lane_u >= 0.0:
		_add_marker(_world(side, Vector2(lane_u, v_street + sw * 0.5 + 3.0)), "lane")

	var levels := TownLevelField.new(settings.random_seed + 1)
	levels.extent = 200.0
	levels.foci = [Vector2(s_bridge, v_street)]

	var market := {"taken": false}
	var use_of := func(_parcel: Dictionary, rect: Dictionary, centre: Vector2) -> int:
		match int(rect.level):
			MAIN:
				if not market.taken and centre.y < v_back:
					market.taken = true
					return USE.MARKET
				return USE.SHOP
			STREET:
				return USE.SHOP
			-1:
				return USE.GARDEN
		return USE.RESIDENCE
	# A 重楼 now and then among the 河房: two storeys, red roof, 平座 over the street.
	var tall := func(plan: Dictionary) -> void:
		for b in plan.buildings:
			if b.role == "shop" and _is_river_row(b, v_street) and b.w >= 7.0 and rng.randf() < 0.14:
				b["storeys"] = 2
				b["tile"] = TILE_RED
				b["balcony"] = true
	_fill_blocks(side, g, reserved, TownParcels.rules({"width_min": 6.0, "width_max": 11.0, "depth_max": 22.0}),
		levels, use_of, tall)
	_emit_streets(side, g)
	_last_waterfront_graph = g

	# Market life on the 河街: stalls at the river-row edge, crowds in the middle.
	var street_edge := v_street - sw * 0.5
	for i in roundi(16.0 * settings.density):
		var s := rng.randf_range(_s_town0 + 6.0, _s_town1 - 6.0)
		var sp := _bank(s, side, street_edge + 1.1)
		if _claim(sp, 1.1):
			_add_prop(sp, _yaw_to(_inland(s, side)), PROP_STALL)
	for i in roundi(80.0 * settings.density):
		var s := rng.randf_range(_s_town0, _s_town1)
		_add_prop(_bank(s, side, street_edge + rng.randf_range(0.6, sw - 0.6)), rng.randf() * 360.0,
			PROP_FIGURE_A + (i % 2))
	# 牌坊 across the 河街 at both ends of the town.
	for s_end in [_s_town0 + 2.0, _s_town1 - 2.0]:
		_add_prop(_bank(s_end, side, v_street), _yaw_to(_at(s_end).t), PROP_ARCH)
	# Woodland candidates become bamboo patches and scattered peach trees after both banks are built.
	for i in roundi(46.0 * settings.density):
		var s := rng.randf_range(_s_town0 - 40.0, _s_town1 + 40.0)
		var off := rng.randf_range(v_edge + 3.0, v_edge + 50.0)
		if s < ua or s > ub:
			off = rng.randf_range(3.0, v_edge + 40.0)
		var tp := _bank(s, side, off)
		if _claim(tp, 2.0):
			_add_tree(tp, rng.randf_range(0.8, 1.6))
			_trees.back().merge({"woodland": true, "bank_side": side, "bank_pos": Vector2(s, off), "town_edge": v_edge})


## Continuous woodland behind both banks. Sample in physical metres, compensating for the
## curved river frame, and use a spatial hash so dense planting does not become quadratic.
## This RNG never advances the town planner's state.
func _grow_bamboo_patches(s_bridge: float) -> void:
	var plants := RandomNumberGenerator.new()
	plants.seed = settings.random_seed + 711
	var occupied := {}
	for claim in _claims:
		_bamboo_reserve(occupied, claim)
	var buildings: Array[Rect2] = []
	for lot in _lots:
		var basis := Basis(Vector3.UP, deg_to_rad(lot.yaw))
		var extent := basis.x.abs() * float(lot.params.width) * 0.5 + basis.z.abs() * float(lot.params.depth) * 0.5
		var half_size := Vector2(extent.x, extent.z) + Vector2.ONE
		buildings.append(Rect2(Vector2(lot.pos.x, lot.pos.z) - half_size, half_size * 2.0))
	var spacing: float = settings.bamboo_spacing / sqrt(settings.density)
	var clearance := spacing * 0.27
	var u0 := _s_town0 + 10.0
	var u1 := _s_town1 - 10.0
	var bamboo_anchor := Vector3.ZERO
	var anchor_score := INF
	for side in [SIDE_CITY, SIDE_WATERFRONT]:
		var edge := -1.0
		for tree in _trees:
			if tree.get("woodland", false) and tree.bank_side == side:
				edge = tree.town_edge
				break
		if edge < 0.0:
			continue
		var depth: float = settings.bamboo_forest_depth * (0.65 if side == SIDE_CITY else 1.0)
		var grove_id := 0 if side == SIDE_CITY else 1
		for tree in _trees:
			if not tree.get("woodland", false) or tree.bank_side != side:
				continue
			var q: Vector2 = tree.bank_pos
			if q.x > u0 and q.x < u1 and q.y > edge + 7.0 and q.y < edge + 7.0 + depth:
				tree.merge({"species": Vegetation.Species.BAMBOO, "grove": grove_id,
					"scale": plants.randf_range(0.82, 1.24)}, true)
		var v := edge + 7.0 + spacing * 0.5
		while v < edge + 7.0 + depth:
			var u := u0 + plants.randf() * spacing
			while u < u1:
				var step := spacing / maxf(_stretch(side, Vector2(u, v), Vector2.RIGHT), 0.30)
				var q := Vector2(u + plants.randf_range(-0.34, 0.34) * step, v + plants.randf_range(-0.34, 0.34) * spacing)
				u += step
				var end_fade := smoothstep(0.0, 14.0, minf(q.x - u0, u1 - q.x))
				var outer := edge + 7.0 + depth * end_fade * (0.92 + 0.08 * sin(q.x * 0.073 + side))
				if q.y > outer or q.y < edge + 7.0:
					continue
				var point := _world(side, q)
				var blocked := false
				for footprint in buildings:
					if footprint.has_point(Vector2(point.x, point.z)):
						blocked = true
						break
				if blocked or not _bamboo_has_space(occupied, point, clearance):
					continue
				var claim := Vector3(point.x, point.z, clearance)
				_bamboo_reserve(occupied, claim)
				_claims.append(claim)
				_trees.append({"pos": point, "scale": plants.randf_range(0.82, 1.24),
					"species": Vegetation.Species.BAMBOO, "grove": grove_id,
					"woodland": true, "bank_side": side, "bank_pos": q, "town_edge": edge})
				var score := absf(q.x - s_bridge) + absf(q.y - edge - 12.0)
				if side == SIDE_WATERFRONT and score < anchor_score:
					anchor_score = score
					bamboo_anchor = point
			v += spacing
	if anchor_score < INF:
		_add_marker(bamboo_anchor, "bamboo_grove")
	# Keep a close-up camera near a peach at the populated town edge.
	var peach_distance := INF
	var peach_pos := Vector3.ZERO
	for tree in _trees:
		if tree.get("species", Vegetation.Species.PEACH) != Vegetation.Species.PEACH or not tree.get("woodland", false):
			continue
		var q: Vector2 = tree.bank_pos
		var distance := absf(q.x - s_bridge) + q.y * 0.1
		if tree.bank_side == SIDE_WATERFRONT and distance < peach_distance:
			peach_distance = distance
			peach_pos = tree.pos
	if peach_distance < INF:
		_add_marker(peach_pos, "peach_garden")


const BAMBOO_CLAIM_CELL := 4.0


func _bamboo_reserve(occupied: Dictionary, claim: Vector3) -> void:
	for x in range(floori((claim.x - claim.z) / BAMBOO_CLAIM_CELL), floori((claim.x + claim.z) / BAMBOO_CLAIM_CELL) + 1):
		for z in range(floori((claim.y - claim.z) / BAMBOO_CLAIM_CELL), floori((claim.y + claim.z) / BAMBOO_CLAIM_CELL) + 1):
			var cell := Vector2i(x, z)
			if not occupied.has(cell):
				occupied[cell] = []
			occupied[cell].append(claim)


func _bamboo_has_space(occupied: Dictionary, point: Vector3, radius: float) -> bool:
	for x in range(floori((point.x - radius) / BAMBOO_CLAIM_CELL), floori((point.x + radius) / BAMBOO_CLAIM_CELL) + 1):
		for z in range(floori((point.z - radius) / BAMBOO_CLAIM_CELL), floori((point.z + radius) / BAMBOO_CLAIM_CELL) + 1):
			for claim in occupied.get(Vector2i(x, z), []):
				var offset := Vector2(point.x - claim.x, point.z - claim.y)
				if offset.length_squared() < (radius + claim.z) * (radius + claim.z):
					return false
	return true


## A building of the river row (between the water and the 河街). Its layout position is in the
## bank frame, so v is simply its offset.
static func _is_river_row(b: Dictionary, v_street: float) -> bool:
	return (b.pos as Vector2).y < v_street


# =========================================================================
# Packing
# =========================================================================

func _pack_trees() -> FlowData.Data:
	var data := super._pack_trees()
	var species := PackedInt32Array()
	var variants := PackedInt32Array()
	for tree in _trees:
		var kind: int = tree.get("species", Vegetation.Species.PEACH)
		species.append(kind)
		variants.append(Vegetation.variant_for(kind, FlowData.point_seed(tree.pos, settings.random_seed)))
	data.registerStream(Vegetation.SPECIES_ATTRIBUTE, species, FlowData.DataType.Int)
	data.registerStream(Vegetation.VARIANT_ATTRIBUTE, variants, FlowData.DataType.Int)
	return data
