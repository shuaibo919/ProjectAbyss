@tool
extends "res://addons/ancient_town/nodes/town_lots.gd"

# River Town Lots — a 水城 layout in one deterministic pass: a river through the
# middle, a walled city on the +side bank (wall, gate pier + tiered gate tower,
# water gate, watch towers, a terraced inner city climbing to a temple), a
# waterfront street of 河房 on the −side bank (river row with landing steps,
# street, back rows, riverside 重楼), and an arch bridge carrying a covered
# gallery between the two gates.
#
# Reuses the Ancient Town node's building parametrisation and stream packing
# (this script extends town_lots.gd), so the Buildings output feeds the Ancient
# Building node exactly the same way. Outputs:
#
#   0 "Buildings"  one point per building — `ab_*` streams, plus `ab_tile_color`
#                  and a per-point `ab_platform` (tiered towers stack buildings
#                  without a 台基, so the flag is no longer level-derived).
#   1 "Roads"      paving strips (unit box, size = len / thickness / width).
#   2 "Walls"      city-wall segments (unit box, size = len / height / thickness).
#   3 "Props"      prop_type: 0 stall, 1 well, 2 牌坊, 3 垛口 merlon, 4 乌篷船,
#                  5 cargo junk, 6/7 figures, 8 landing steps.
#   4 "Trees"      tree points, `size` carries per-tree scale.
#   5 "Structures" one-off whitebox meshes (water, land, 驳岸, piers, bridge,
#                  distant ridges) in a Resource stream — spawn with
#                  mesh_attribute = settings.structure_mesh_attribute.
#
# Optional input "River Path": ordered points (e.g. Sample Spline on a Path3D)
# used as the river centreline instead of the built-in meander.
#
# Frame convention: `n` = (t.z, 0, -t.x) is the building-local +X when a lot faces
# along the river tangent t, and points at the city bank. `side` is +1 for the
# city bank, -1 for the waterfront bank; bank offsets are measured from the
# revetment line into the land.

const RiverSettings = preload("res://addons/ancient_town/nodes/river_town_lots_settings.gd")
const Meshes = preload("res://addons/ancient_town/town_meshes.gd")

const PROP_MERLON := 3
const PROP_BOAT := 4
const PROP_CARGO := 5
const PROP_FIGURE_A := 6
const PROP_FIGURE_B := 7
const PROP_STEPS := 8

const SIDE_CITY := 1.0
const SIDE_WATERFRONT := -1.0

# Row kinds. Width palettes are disjoint where the rows differ in fields the
# Ancient Building node leaves out of its variant key (steps, fence), so two
# kinds can never end up sharing one baked mesh.
const ROW_WATERFRONT := 0     # 河房: backs on the river, faces the street, no steps
const ROW_STREET := 1         # shops across the street, with steps
const ROW_BACK := 2           # plain dwellings behind a lane
const ROW_CITY := 3           # inner city on the terrace

const SAMPLE_STEP := 4.0
const TOWPATH := 2.0
const PLATFORM_MARGIN := 0.7
const LAND_OFFSETS := [0.0, 4.0, 10.0, 16.0, 24.0, 32.0, 40.0, 50.0, 60.0, 70.0,
	80.0, 95.0, 115.0, 140.0, 180.0, 240.0, 320.0]

const TILE_GREY := Color(0.26, 0.29, 0.31)
const TILE_GLAZED := Color(0.22, 0.31, 0.40)   # gate tower
const TILE_RED := Color(0.56, 0.30, 0.21)      # riverside 重楼, bridgehead
const ROBE_A := Color(0.30, 0.33, 0.38)
const ROBE_B := Color(0.66, 0.58, 0.44)

var _structures: Array[Dictionary] = []

# River frame, sampled every SAMPLE_STEP metres of arc length.
var _c := PackedVector3Array()
var _t := PackedVector3Array()
var _n := PackedVector3Array()
var _s := PackedFloat32Array()
var _hw := PackedFloat32Array()
var _s_town0 := 0.0
var _s_town1 := 0.0
var _water_y := 0.0
# Footprints already taken: (x, z, radius).
var _claims := PackedVector3Array()


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
	_lots.clear()
	_roads.clear()
	_walls.clear()
	_props.clear()
	_trees.clear()
	_structures.clear()
	_claims.clear()

	if not _build_frame():
		setError("River path needs at least two points.")
		return

	var s_bridge: float = lerpf(_s_town0, _s_town1, settings.bridge_position)
	_build_river()
	_build_land()
	_build_bridge(s_bridge)
	_build_city_bank(s_bridge)
	_build_waterfront_bank(s_bridge)
	_build_boats(s_bridge)
	_build_backdrop()

	set_output(0, _pack_river_buildings())
	set_output(1, _pack_strips(_roads))
	set_output(2, _pack_strips(_walls))
	set_output(3, _pack_props())
	set_output(4, _pack_trees())
	set_output(5, _pack_structures())


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


## Rejects a footprint that overlaps an earlier one. Circles, because rows bend
## with the river and an axis-aligned test would be wrong at every curve.
func _claim(p: Vector3, radius: float) -> bool:
	for c in _claims:
		if Vector2(p.x - c.x, p.z - c.y).length() < radius + c.z:
			return false
	_claims.append(Vector3(p.x, p.z, radius))
	return true


## `role` is stamped on the mesh as `town_role` meta so a scene can find the
## landmarks (gate pier, bridge…) in the spawned MultiMeshes and frame cameras
## on them without re-deriving the layout.
func _add_structure(pos: Vector3, yaw: float, mesh: Mesh, role: String = "") -> void:
	if role != "":
		mesh.set_meta("town_role", role)
	_structures.append({"pos": pos, "yaw": yaw, "mesh": mesh})


# =========================================================================
# Building parameters
# =========================================================================

func _house(level: int, w: float, d: float, roof: int, material: int) -> Dictionary:
	return {
		"width": w,
		"depth": d,
		"roof_type": roof,
		"bays_x": 3 if w >= 7.0 else 1,
		"bays_z": 2 if d >= 5.5 else 1,
		"material_style": material,
		"rafter_courses": clampi(roundi(w / 2.2), 3, 9),
		"tile_coverage": 1.0,
		"tile_course_width": 0.34,
		"corner_rise_scale": 1.6,
		"fence": false,
		"walls": true,
		"steps": false,
		"fence_lambda": 0,
		"platform": true,
		"tile_color": TILE_GREY,
	}


func _pick_house(kind: int) -> Dictionary:
	var r := rng
	var p: Dictionary
	match kind:
		ROW_WATERFRONT:
			var w: float = [7.0, 8.0, 9.0][r.randi() % 3]
			var roof := ROOF_FLUSH_GABLE if r.randf() < 0.6 else ROOF_OVERHANGING
			p = _house(1, w, snappedf(w * 0.72, 0.5), roof, MAT_EARTHEN if r.randf() < 0.15 else MAT_TRADITIONAL)
			p["level"] = 1
		ROW_STREET:
			var w: float = [8.5, 9.5, 10.5][r.randi() % 3]
			var roof := ROOF_OVERHANGING if r.randf() < 0.7 else ROOF_GABLE_AND_HIP
			p = _house(2, w, snappedf(w * 0.68, 0.5), roof, MAT_TRADITIONAL)
			p["steps"] = true
			p["level"] = 2
		ROW_BACK:
			var w: float = [6.0, 6.5][r.randi() % 2]
			var roof: int = [ROOF_FLUSH_GABLE, ROOF_OVERHANGING, ROOF_ROUND_RIDGE][r.randi() % 3]
			p = _house(1, w, snappedf(w * 0.75, 0.5), roof, MAT_EARTHEN if r.randf() < 0.3 else MAT_TRADITIONAL)
			p["level"] = 1
		_:
			var w: float = [7.5, 8.5][r.randi() % 2]
			var u := r.randf()
			var roof := ROOF_FLUSH_GABLE if u < 0.45 else (ROOF_OVERHANGING if u < 0.9 else ROOF_GABLE_AND_HIP)
			p = _house(2 if roof == ROOF_GABLE_AND_HIP else 1, w, snappedf(w * 0.72, 0.5), roof, MAT_TRADITIONAL)
			p["level"] = 2 if roof == ROOF_GABLE_AND_HIP else 1
	return p


## Extent of a building's front stair run, Table 1: ω × 1.1 with ω = 3.2D.
static func _steps_depth(p: Dictionary) -> float:
	if not p.steps:
		return 0.0
	return 3.2 * p.width * 0.8 / 11.0 * 1.1


## A multi-storey building (重檐 / 楼 / 阁) as one lot — AncientBuilding grows the storeys
## natively, each from the column grid below, so nothing is stacked or sunk into a roof.
## `extra` overrides any field of the base parameter set.
func _add_storeyed(pos: Vector3, yaw: float, w: float, d: float, roof: int, storeys: int, tile: Color,
		level: int, lot_type: int, extra: Dictionary = {}) -> void:
	var p := _house(level, w, d, roof, MAT_TRADITIONAL)
	p["storey_count"] = storeys
	p["fence"] = true
	p["fence_lambda"] = 1
	p["tile_color"] = tile
	p["corner_rise_scale"] = 1.9
	p.merge(extra, true)
	_add_lot(pos, yaw, level, lot_type, -1, p)


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
	return mesh


## A hidden marker inside a masonry body, so a scene can still find a landmark by `town_role`
## now that the landmark itself is part of a building mesh.
func _add_marker(pos: Vector3, role: String) -> void:
	var box := BoxMesh.new()
	box.size = Vector3(0.05, 0.05, 0.05)
	_add_structure(pos + Vector3(0.0, 1.0, 0.0), 0.0, box, role)


# =========================================================================
# River, land, backdrop
# =========================================================================

func _build_river() -> void:
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

	if settings.city_wall:
		# 城门楼 over the bridge landing: one building — its 城台 with the gate passage, and the
		# storeyed tower growing out of the terrace top.
		var tower_w := 18.0
		var tower_d := 11.5
		var margin := 3.0
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
		var s_wg := _s_town0 + len_town * 0.16
		if absf(s_wg - s_bridge) > 40.0:
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

	# Street inside the wall.
	var street0 := _wall_inner() + 0.6
	_road_along(SIDE_CITY, street0 + 3.0, 6.0, _s_town0 + 4.0, _s_town1 - 4.0, 0.3)

	# Gate axis street up the terrace to the temple.
	var axis_len := 0.0
	var row_edge := street0 + 6.6
	var rows := 4
	var axis_half := 5.0
	var skip_axis: Array[Vector2] = [Vector2(s_bridge - axis_half - 1.0, s_bridge + axis_half + 1.0)]
	for r in rows:
		var depth_max := 7.5
		var skips := skip_axis.duplicate()
		if r == 0:
			for g in gaps:
				skips.append(g)
		# Rows alternate facing so pairs stand back to back across a lane, the way
		# a 坊 block fills in, instead of every house staring at the next one's back.
		_fill_row(SIDE_CITY, row_edge, r % 2 == 0, _s_town0 + 7.0, _s_town1 - 7.0, ROW_CITY, skips)
		row_edge += PLATFORM_MARGIN * 2.0 + depth_max + 0.8
		# Lane behind the row.
		_road_along(SIDE_CITY, row_edge + 2.0, 4.0, _s_town0 + 7.0, _s_town1 - 7.0, 0.3)
		row_edge += 4.4
	axis_len = row_edge - street0

	var axis_from := street0
	var step := 4.0
	var k := 0.0
	while k < axis_len:
		var a := _bank(s_bridge, SIDE_CITY, axis_from + k)
		var b := _bank(s_bridge, SIDE_CITY, axis_from + minf(k + step, axis_len))
		_roads.append({"pos": (a + b) * 0.5 + Vector3(0.0, 0.1, 0.0), "yaw": _road_yaw(b - a),
			"len": a.distance_to(b) + 0.4, "width": axis_half * 2.0, "height": 0.4})
		k += step

	# Temple compound at the head of the axis: main hall facing the gate, two side halls.
	var hall_off := row_edge + 9.0
	var hall := _bank(s_bridge, SIDE_CITY, hall_off)
	var n_axis: Vector3 = _at(s_bridge).n
	var t_axis: Vector3 = _at(s_bridge).t
	var hp := _house(4, 16.0, 10.5, ROOF_HIP, MAT_TRADITIONAL)
	hp["fence"] = true
	hp["steps"] = true
	hp["fence_lambda"] = 1
	hp["tile_color"] = TILE_GLAZED
	_add_lot(hall, _yaw_to(-n_axis), 4, LOT_LANDMARK, -1, hp)
	_claim(hall, 11.0)
	for sd in [-1.0, 1.0]:
		var sh := _bank(s_bridge + sd * 16.0, SIDE_CITY, hall_off - 12.0)
		var sp := _house(3, 10.0, 6.5, ROOF_GABLE_AND_HIP, MAT_TRADITIONAL)
		sp["steps"] = true
		sp["fence_lambda"] = 0
		if _claim(sh, 5.0):
			_add_lot(sh, _yaw_to(-t_axis * sd), 3, LOT_LANDMARK, -1, sp)
	for i in 6:
		var tp := _bank(s_bridge + rng.randf_range(-26.0, 26.0), SIDE_CITY, hall_off + rng.randf_range(10.0, 22.0))
		_add_tree(tp, rng.randf_range(1.0, 1.6))

	# 楼阁 on the hill, off-axis: three storeys stepping in, a 平座 on each, under a 攒尖.
	var s_pagoda := clampf(s_bridge + 62.0, _s_town0 + 20.0, _s_town1 - 20.0)
	var pg := _bank(s_pagoda, SIDE_CITY, hall_off + 4.0)
	if _claim(pg, 7.0):
		_add_storeyed(pg, _yaw_to(-_at(s_pagoda).n), 10.0, 10.0, ROOF_PYRAMIDAL, 3, TILE_GLAZED, 4, LOT_LANDMARK,
			{ "bays_x": 5, "bays_z": 5, "storey_setback": 1, "storey_balcony": true, "upper_column_scale": 0.75 })

	# A 亭 on the slope below it, open, with 美人靠.
	var s_ting := clampf(s_bridge - 48.0, _s_town0 + 20.0, _s_town1 - 20.0)
	var tg := _bank(s_ting, SIDE_CITY, hall_off + 6.0)
	if _claim(tg, 4.5):
		var ting := _house(2, 6.0, 6.0, ROOF_PYRAMIDAL, MAT_TRADITIONAL)
		ting.merge({ "sides": 6, "walls": false, "fence": false, "steps": true, "railing": 2,
			"hanging_fascia": true }, true)
		_add_lot(tg, _yaw_to(-_at(s_ting).n), 2, LOT_LANDMARK, -1, ting)

	# Figures along the axis and the inner street.
	for i in roundi(22.0 * settings.density):
		var on_axis := i % 2 == 0
		var fp: Vector3
		if on_axis:
			fp = _bank(s_bridge + rng.randf_range(-axis_half + 0.8, axis_half - 0.8), SIDE_CITY,
				axis_from + rng.randf_range(2.0, axis_len))
		else:
			fp = _bank(rng.randf_range(_s_town0 + 8.0, _s_town1 - 8.0), SIDE_CITY,
				street0 + rng.randf_range(0.8, 5.2))
		_add_prop(fp, rng.randf() * 360.0, PROP_FIGURE_A + (i % 2))

	# Trees: along the inside of the wall and on the hillside beyond the rows.
	for i in roundi(34.0 * settings.density):
		var s_tree := rng.randf_range(_s_town0 - 30.0, _s_town1 + 30.0)
		var off := rng.randf_range(hall_off + 8.0, hall_off + 60.0)
		if s_tree < _s_town0 or s_tree > _s_town1:
			off = rng.randf_range(6.0, hall_off + 40.0)
		var tp := _bank(s_tree, SIDE_CITY, off)
		if _claim(tp, 2.0):
			_add_tree(tp, rng.randf_range(0.9, 1.7))


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


## Paving strip following the river at a constant bank offset.
func _road_along(side: float, centre_off: float, width: float, s0: float, s1: float, height: float) -> void:
	var step := 8.0
	var s := s0
	while s < s1 - 0.01:
		var s_next := minf(s + step, s1)
		var a := _bank(s, side, centre_off)
		var b := _bank(s_next, side, centre_off)
		_roads.append({"pos": (a + b) * 0.5 + Vector3(0.0, 0.05, 0.0), "yaw": _road_yaw(b - a),
			"len": a.distance_to(b) + 0.6, "width": width, "height": height})
		s = s_next


# =========================================================================
# Rows of houses
# =========================================================================

## Walks one row of lots along the river on `side`. `edge` is the row's near
## boundary (bank offset); houses face the river when `face_river`, else inland.
## Returns nothing — the caller advances offsets by the row's maximum depth.
func _fill_row(side: float, edge: float, face_river: bool, s0: float, s1: float, kind: int,
		skips: Array) -> void:
	var s := s0 + rng.randf_range(0.0, 3.0)
	var since_gap := 0
	var gap_after := rng.randi_range(3, 5)
	var since_tower := 0
	var tower_after := rng.randi_range(6, 9)
	while s < s1:
		# Waterfront rhythm: landing steps every few houses, a red 重楼 now and then.
		if kind == ROW_WATERFRONT and since_gap >= gap_after:
			if _landing(s + 3.5, side):
				s += 7.0
				since_gap = 0
				gap_after = rng.randi_range(3, 5)
				continue
		var tower := kind == ROW_WATERFRONT and since_tower >= tower_after
		var p: Dictionary
		if tower:
			p = _house(3, 9.5, 7.0, ROOF_GABLE_AND_HIP, MAT_TRADITIONAL)
		else:
			p = _pick_house(kind)
		var w: float = p.width
		var d: float = p.depth
		var ext := w * 1.3 + 0.8
		if s + ext > s1:
			break
		var blocked := false
		for g in skips:
			if s + ext > g.x and s < g.y:
				s = g.y + 1.0
				blocked = true
				break
		if blocked:
			continue

		var sc := s + ext * 0.5
		var steps := _steps_depth(p)
		var off := edge + PLATFORM_MARGIN + d * 0.5 + (steps if face_river else 0.0)
		var pos := _bank(sc, side, off)
		var inland := _inland(sc, side)
		var face := -inland if face_river else inland
		if _claim(pos, 0.42 * maxf(w, d)):
			if tower:
				# 重楼: two storeys, 叉柱造, a 平座 on the street side.
				_add_storeyed(pos, _yaw_to(face), w, d, ROOF_GABLE_AND_HIP, 2, TILE_RED, 3, LOT_SHOP,
					{ "storey_setback": 0, "storey_balcony": true, "upper_column_scale": 0.8, "fence": false })
				since_tower = 0
				tower_after = rng.randi_range(6, 9)
			else:
				_add_lot(pos, _yaw_to(face), p.level, LOT_SHOP if kind == ROW_STREET else LOT_RESIDENCE,
					0, p)
				since_tower += 1
			since_gap += 1
		s += ext


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
	var bridge_skip: Array = [Vector2(s_bridge - 12.0, s_bridge + 12.0)] if settings.bridge else []

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

	# River row: backs on the water, fronts on the street.
	var edge := 0.3
	var river_row_depth := 7.0
	_fill_row(side, edge, false, _s_town0, _s_town1, ROW_WATERFRONT, bridge_skip)
	edge += PLATFORM_MARGIN * 2.0 + river_row_depth + 1.2

	# The street.
	var street_w: float = settings.street_width
	_road_along(side, edge + street_w * 0.5, street_w, _s_town0 - 10.0, _s_town1 + 10.0, 0.1)
	var street_edge := edge
	edge += street_w + 0.4

	# Shops across the street, then back rows behind lanes.
	_fill_row(side, edge, true, _s_town0 + 4.0, _s_town1 - 4.0, ROW_STREET, [])
	edge += 3.0 + PLATFORM_MARGIN * 2.0 + 7.5 + 0.6
	for r in settings.back_rows:
		_road_along(side, edge + 1.75, 3.5, _s_town0 + 8.0, _s_town1 - 8.0, 0.1)
		edge += 4.0
		_fill_row(side, edge, true, _s_town0 + 8.0 + r * 6.0, _s_town1 - 8.0 - r * 6.0, ROW_BACK, [])
		edge += PLATFORM_MARGIN * 2.0 + 5.0 + 0.6

	# Market life on the street: stalls at the river-row edge, crowds in the middle.
	for i in roundi(16.0 * settings.density):
		var s := rng.randf_range(_s_town0 + 6.0, _s_town1 - 6.0)
		var sp := _bank(s, side, street_edge + 1.1)
		if _claim(sp, 1.1):
			_add_prop(sp, _yaw_to(_inland(s, side)), PROP_STALL)
	for i in roundi(80.0 * settings.density):
		var s := rng.randf_range(_s_town0, _s_town1)
		var fp := _bank(s, side, street_edge + rng.randf_range(0.6, street_w - 0.6))
		_add_prop(fp, rng.randf() * 360.0, PROP_FIGURE_A + (i % 2))
	var w_s := _bank(lerpf(_s_town0, _s_town1, 0.3), side, street_edge + street_w + 1.2)
	if _claim(w_s, 1.0):
		_add_prop(w_s, 0.0, PROP_WELL)

	# 牌坊 across the street at both ends of the town.
	for s_end in [_s_town0 + 2.0, _s_town1 - 2.0]:
		_add_prop(_bank(s_end, side, street_edge + street_w * 0.5), _yaw_to(_at(s_end).t), PROP_ARCH)

	# Groves behind the last row, and willows beyond the town ends.
	for i in roundi(46.0 * settings.density):
		var s := rng.randf_range(_s_town0 - 40.0, _s_town1 + 40.0)
		var off := rng.randf_range(edge + 3.0, edge + 50.0)
		if s < _s_town0 or s > _s_town1:
			off = rng.randf_range(3.0, edge + 40.0)
		var tp := _bank(s, side, off)
		if _claim(tp, 2.0):
			_add_tree(tp, rng.randf_range(0.8, 1.6))


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
# Packing
# =========================================================================

func _pack_river_buildings() -> FlowData.Data:
	var d := _pack_buildings()
	# The gallery bays stand on the bridge deck without a 台基, so the level-derived default
	# from town_lots is replaced by each lot's own flag.
	var plat := PackedByteArray()
	var tile := PackedColorArray()
	plat.resize(_lots.size())
	tile.resize(_lots.size())
	for i in _lots.size():
		var p: Dictionary = _lots[i].params
		plat[i] = 1 if p.get("platform", true) else 0
		tile[i] = p.get("tile_color", TILE_GREY)
	d.registerStream("ab_platform", plat, FlowData.DataType.Bool)
	d.registerStream("ab_tile_color", tile, FlowData.DataType.Color)

	# Structure (多层 / 城台 / 亭台榭): one stream per field, the default written for ordinary
	# houses so every point says what it is. [lot key, stream, default]
	var fields := [
		["sides", "ab_sides", 4], ["storey_count", "ab_storey_count", 1],
		["storey_setback", "ab_storey_setback", 0], ["upper_column_scale", "ab_upper_column_scale", 0.8],
		["storey_balcony", "ab_storey_balcony", false], ["base_kind", "ab_base_kind", 0],
		["base_height", "ab_base_height", 7.0], ["base_margin", "ab_base_margin", 4.0],
		["base_arches", "ab_base_arches", 1], ["base_parapet", "ab_base_parapet", 1],
		["masonry_storeys", "ab_masonry_storeys", 0], ["railing", "ab_railing", 0],
		["hanging_fascia", "ab_hanging_fascia", false], ["module_span", "ab_module_span", 0.0],
		["stilt_depth", "ab_stilt_depth", 2.0],
	]
	for field in fields:
		var default_value = field[2]
		var container
		var data_type: int
		match typeof(default_value):
			TYPE_BOOL:
				container = PackedByteArray()
				data_type = FlowData.DataType.Bool
			TYPE_FLOAT:
				container = PackedFloat32Array()
				data_type = FlowData.DataType.Float
			_:
				container = PackedInt32Array()
				data_type = FlowData.DataType.Int
		container.resize(_lots.size())
		for i in _lots.size():
			var value = _lots[i].params.get(field[0], default_value)
			container[i] = (1 if value else 0) if data_type == FlowData.DataType.Bool else value
		d.registerStream(field[1], container, data_type)
	return d


func _pack_structures() -> FlowData.Data:
	var d := FlowData.Data.new()
	var n := _structures.size()
	var pos := PackedVector3Array()
	var rot := PackedVector3Array()
	var size := PackedVector3Array()
	var tint := PackedColorArray()
	pos.resize(n)
	rot.resize(n)
	size.resize(n)
	tint.resize(n)
	var meshes = d.newContainerOfType(FlowData.DataType.Resource)
	meshes.resize(n)
	for i in n:
		var st: Dictionary = _structures[i]
		pos[i] = st.pos
		rot[i] = Vector3(0.0, st.yaw, 0.0)
		size[i] = Vector3.ONE
		# White instance colour: forces use_colors on, see _pack_strips.
		tint[i] = Color.WHITE
		meshes[i] = st.mesh
	d.registerStream(FlowData.AttrPosition, pos, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrRotation, rot, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrSize, size, FlowData.DataType.Vector)
	d.registerStream("color", tint, FlowData.DataType.Color)
	d.registerStream(settings.structure_mesh_attribute, meshes, FlowData.DataType.Resource)
	return d
