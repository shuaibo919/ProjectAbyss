@tool
extends FlowNodeBase

# Ancient Town Lots — generates a whole settlement layout in one deterministic
# pass and emits it as five point streams:
#
#   Out 0 "Buildings": one point per building lot. Carries the `ab_*` parameter
#         override streams consumed by the Ancient Building node (width, depth,
#         roof, bays, material...), plus level / lot_type / ward and a per-lot
#         colour tint for Spawn Meshes.
#   Out 1 "Roads":     one point per road segment (position, yaw, size = len/thickness/width).
#   Out 2 "Walls":     city-wall and palace-wall segments (position, yaw, size = len/height/thickness).
#   Out 3 "Props":     stalls, wells and 牌坊 (prop_type stream picks the mesh).
#   Out 4 "Trees":     tree placement points (size stream carries per-tree scale).
#
# Layout language (Hu & Qin 2020 component grammars + Qin et al. 2023 city
# layout + Müller 2006 CGA mass models, see ProjectAbyssWiki/documentation/systems/AncientBuilding_ExecutionPlan):
#   - 聚落: a winding lane walk with houses alternating on both sides.
#   - 村镇: main street + side lanes, temple at the vista end, 牌坊 at the entry.
#   - 市集: 3x3 blocks around a central market square crossed by two main streets.
#   - 城市: 4x4 里坊 grid on a 朱雀大街 axis, palace ward at the center, wall
#           ring with 4 gate towers and 4 corner towers, 东市/西市 stall clusters.
#
# Levels follow the Qing roof hierarchy (Dong et al. 2021): 民居 硬山/悬山 →
# shops 悬山/歇山 → temples 歇山/庑殿 → palace 庑殿, 亭/角楼 攒尖.

const TownLotsSettings = preload(
	"res://addons/ancient_town/nodes/town_lots_settings.gd")

# Roof type ids (AncientBuildingParameters.ERoofType).
const ROOF_FLUSH_GABLE := 0       # 硬山
const ROOF_GABLE_AND_HIP := 1     # 歇山
const ROOF_HIP := 2               # 庑殿
const ROOF_OVERHANGING := 3       # 悬山
const ROOF_ROUND_RIDGE := 4       # 卷棚
const ROOF_PYRAMIDAL := 6         # 攒尖
const ROOF_ROUND := 7             # 圆攒尖
const ROOF_HELMET := 8            # 盔顶

const MAT_TRADITIONAL := 0        # 官式
const MAT_THATCHED := 1           # 茅草
const MAT_EARTHEN := 2            # 土木

# lot_type values carried on the Buildings output.
const LOT_RESIDENCE := 0
const LOT_SHOP := 1
const LOT_LANDMARK := 2           # temple / palace halls
const LOT_TOWER := 3              # gate towers / corner pavilions

# prop_type values carried on the Props output (Spawn Meshes mesh selector).
const PROP_STALL := 0
const PROP_WELL := 1
const PROP_ARCH := 2

# Internal accumulation buffers.
var _lots: Array[Dictionary] = []
var _roads: Array[Dictionary] = []
var _walls: Array[Dictionary] = []
var _props: Array[Dictionary] = []
var _trees: Array[Dictionary] = []


func _init() -> void:
	meta_node = {
		"title": "Ancient Town Lots",
		"settings": TownLotsSettings,
		"ins": [],
		"outs": [
			{"label": "Buildings"},
			{"label": "Roads"},
			{"label": "Walls"},
			{"label": "Props"},
			{"label": "Trees"},
		],
		"aliases": ["Settlement", "City Layout", "Ward Grid"],
		"category": "Generator",
		"tooltip": "Generates a deterministic ancient settlement layout (聚落/村镇/市集/城市)\n"
			+ "and emits buildings, roads, walls, props and trees as point streams.\n"
			+ "Feed Buildings into the Ancient Building node and Spawn Meshes.",
	}


func getTitle() -> String:
	var names := ["Hamlet 聚落", "Village 村镇", "Market 市集", "City 城市"]
	return "Ancient Town - %s" % names[clampi(settings.settlement_type, 0, 3)]


func execute(_ctx: FlowData.EvaluationContext) -> void:
	_lots.clear()
	_roads.clear()
	_walls.clear()
	_props.clear()
	_trees.clear()

	match settings.settlement_type:
		0: _gen_hamlet()
		1: _gen_village()
		2: _gen_market()
		3: _gen_city()

	set_output(0, _pack_buildings())
	set_output(1, _pack_strips(_roads))
	set_output(2, _pack_strips(_walls))
	set_output(3, _pack_props())
	set_output(4, _pack_trees())


# =========================================================================
# Building parametrisation — level -> roof / plan / material (Dong et al. 2021)
# =========================================================================

## Picks a full parameter set for one building of `level` (1..5) that still
## fits a lot of roughly `max_w` x `max_d` metres.
func _params_for_level(level: int, max_w: float, max_d: float, rural: bool = false, urban: bool = false) -> Dictionary:
	var r := rng
	var p := {}

	var w: float
	var roof: int
	var bays: int
	var material: int = MAT_TRADITIONAL

	match level:
		1:
			if urban:
				w = r.randf_range(7.5, minf(10.0, max_w))
				bays = 3
			else:
				w = r.randf_range(5.5, minf(8.0, max_w))
				bays = 1 + r.randi() % 2
			roof = [ROOF_FLUSH_GABLE, ROOF_OVERHANGING, ROOF_ROUND_RIDGE][r.randi() % 3]
			if rural:
				material = [MAT_TRADITIONAL, MAT_THATCHED, MAT_EARTHEN][r.randi() % 3]
			else:
				material = MAT_TRADITIONAL if r.randf() < 0.7 else MAT_EARTHEN
		2:
			if urban:
				w = r.randf_range(9.5, minf(12.5, max_w))
				bays = 3
			else:
				w = r.randf_range(8.0, minf(10.5, max_w))
				bays = 3
			roof = ROOF_OVERHANGING if r.randf() < 0.75 else ROOF_FLUSH_GABLE
		3:
			w = r.randf_range(9.0, minf(13.0, max_w))
			roof = ROOF_GABLE_AND_HIP if r.randf() < 0.7 else ROOF_OVERHANGING
			bays = 3 if r.randf() < 0.5 else 5
		4:
			w = r.randf_range(11.0, minf(17.0, max_w))
			roof = ROOF_GABLE_AND_HIP if r.randf() < 0.75 else ROOF_HIP
			bays = 5
		_:
			w = r.randf_range(13.0, minf(21.0, max_w))
			roof = ROOF_HIP if r.randf() < 0.8 else ROOF_GABLE_AND_HIP
			bays = 5 if r.randf() < 0.6 else 7

	w = minf(w, max_w)
	var d: float = minf(w * r.randf_range(0.55, 0.72), max_d)

	p["width"] = w
	p["depth"] = d
	p["roof_type"] = roof
	p["bays_x"] = bays
	p["bays_z"] = 2 if bays >= 3 else 1
	p["material_style"] = material
	p["rafter_courses"] = clampi(roundi(w / 2.2), 3, 9)
	p["tile_coverage"] = 1.0
	p["tile_course_width"] = 0.34
	p["corner_rise_scale"] = r.randf_range(1.3, 1.9)
	p["fence"] = level >= 2 and not rural
	p["walls"] = level >= 3
	p["steps"] = level >= 2
	p["fence_lambda"] = r.randi_range(0, 2) if level >= 2 else 0
	return p


## A pavilion / 亭: square plan, 攒尖-family roof (Eq 8).
func _params_for_pavilion(size: float) -> Dictionary:
	var p := {}
	var roof: int = [ROOF_PYRAMIDAL, ROOF_ROUND, ROOF_HELMET][rng.randi() % 3]
	p["width"] = size
	p["depth"] = size
	p["roof_type"] = roof
	p["bays_x"] = 3
	p["bays_z"] = 3
	p["material_style"] = MAT_TRADITIONAL
	p["rafter_courses"] = clampi(roundi(size / 2.2), 3, 9)
	p["tile_coverage"] = 1.0
	p["tile_course_width"] = 0.34
	p["corner_rise_scale"] = rng.randf_range(1.5, 2.2)
	p["fence"] = false
	p["walls"] = false
	p["steps"] = false
	p["fence_lambda"] = 0
	return p


## A 城门楼 / wall tower: no fence, walls or steps — it sits on top of the wall.
func _params_for_tower(w: float, d: float, roof: int) -> Dictionary:
	var p := _params_for_level(5, w, d)
	p["roof_type"] = roof
	p["bays_x"] = 5
	p["bays_z"] = 3
	p["fence"] = false
	p["walls"] = false
	p["steps"] = false
	return p


func _add_lot(pos: Vector3, yaw: float, level: int, lot_type: int, ward: int, params: Dictionary) -> void:
	var tint := Color.from_hsv(
		rng.randf_range(0.04, 0.10),
		rng.randf_range(0.0, 0.3),
		rng.randf_range(0.82, 1.0))
	_lots.append({
		"pos": pos, "yaw": yaw, "level": level,
		"lot_type": lot_type, "ward": ward, "params": params, "tint": tint,
	})


func _add_road(center: Vector3, yaw: float, length: float, width: float) -> void:
	# 道路条带长度沿局部 X; 交叉路口两条路错开 2cm, 避免共面闪烁。
	var y := 0.05 + (0.02 if absf(yaw) > 45.0 else 0.0)
	_roads.append({"pos": Vector3(center.x, y, center.z), "yaw": yaw, "len": length, "width": width})


func _add_wall(center: Vector3, yaw: float, length: float, height: float, thick: float) -> void:
	_walls.append({"pos": center, "yaw": yaw, "len": length, "height": height, "thick": thick})


func _add_prop(pos: Vector3, yaw: float, kind: int) -> void:
	_props.append({"pos": pos, "yaw": yaw, "kind": kind})


func _add_tree(pos: Vector3, scale: float) -> void:
	_trees.append({"pos": pos, "scale": scale})


## Yaw (degrees) that makes the building's +Z front face `dir` (XZ, not normalised ok).
func _yaw_to(dir: Vector3) -> float:
	if dir.length() < 0.0001:
		return 0.0
	return rad_to_deg(atan2(dir.x, dir.z))


## Yaw (degrees) that makes a road strip's local +X run along `dir`.
## Road boxes are scaled (len, 0.1, width) — length along local X, so this is
## NOT the same convention as `_yaw_to` (local +Z).
func _road_yaw(dir: Vector3) -> float:
	if dir.length() < 0.0001:
		return 0.0
	return rad_to_deg(atan2(-dir.z, dir.x))


# =========================================================================
# 聚落 — winding lanes
# =========================================================================

func _gen_hamlet() -> void:
	var o: Vector3 = settings.origin

	# Lane 1: a random walk from the origin.
	var lanes: Array = []
	var pos := Vector3(o.x, 0.0, o.z)
	var angle := rng.randf() * TAU
	var dir := Vector3(cos(angle), 0.0, sin(angle))
	var segs: Array[Dictionary] = []
	var n_seg := rng.randi_range(4, 7)
	for i in n_seg:
		var seg_len := rng.randf_range(16.0, 26.0)
		segs.append({"pos": pos, "dir": dir, "len": seg_len})
		pos += dir * seg_len
		angle += rng.randf_range(-0.55, 0.55)
		dir = Vector3(cos(angle), 0.0, sin(angle))
	lanes.append(segs)

	# Lane 2: a short branch off a mid segment.
	if rng.randf() < 0.9:
		var k := rng.randi_range(1, segs.size() - 2)
		var s0: Dictionary = segs[k]
		var perp := Vector3(-s0.dir.z, 0.0, s0.dir.x)
		var branch_pos: Vector3 = s0.pos + s0.dir * (s0.len * 0.55)
		var b_dir: Vector3 = perp * (1.0 if rng.randf() < 0.5 else -1.0)
		var b_segs: Array[Dictionary] = []
		for i in rng.randi_range(2, 3):
			var seg_len := rng.randf_range(12.0, 20.0)
			b_segs.append({"pos": branch_pos, "dir": b_dir, "len": seg_len, "main_dir": s0.dir})
			branch_pos += b_dir * seg_len
			var a := atan2(b_dir.z, b_dir.x) + rng.randf_range(-0.4, 0.4)
			b_dir = Vector3(cos(a), 0.0, sin(a))
		lanes.append(b_segs)

	# Lots alternating on both sides of each lane.
	var first_house := true
	var lane_idx := 0
	for seg_list in lanes:
		var is_main := lane_idx == 0
		lane_idx += 1
		var side := 1 if rng.randf() < 0.5 else -1
		var first_lot := true
		for seg in seg_list:
			var s0: Vector3 = seg.pos
			var d0: Vector3 = seg.dir
			var seg_len: float = seg.len
			var perp := Vector3(-d0.z, 0.0, d0.x)
			var t := rng.randf_range(3.0, 8.0)
			if not is_main and first_lot:
				# 支路第一栋离路口远一点, 并放到主路前进方向的背侧,
				# 避免与主路路口附近的房屋挤在楔形夹角里。
				t = rng.randf_range(8.0, 11.0)
				if seg.has("main_dir"):
					var main_dir: Vector3 = seg.main_dir
					side = -1 if perp.dot(main_dir) > 0.0 else 1
			var spacing := rng.randf_range(8.5, 13.5)
			while t < seg_len - 2.0:
				var p := s0 + d0 * t
				var depth: float = rng.randf_range(4.0, 6.0)
				var off: float = 3.4 + depth * 0.5 + rng.randf_range(0.3, 2.2)
				var lot_pos := p + perp * (off * side)
				var face := -perp * float(side)
				var level := 1
				var lot_type := LOT_RESIDENCE
				var params: Dictionary
				if first_house:
					# 祠堂 landmark anchors the hamlet.
					level = 3
					lot_type = LOT_LANDMARK
					params = _params_for_level(level, 12.0, 9.0)
					first_house = false
				else:
					level = 2 if rng.randf() < 0.12 else 1
					params = _params_for_level(level, 8.0, 6.5, true)
				_add_lot(lot_pos, _yaw_to(face), level, lot_type, 0, params)
				t += spacing + rng.randf_range(0.5, 4.0)
				if rng.randf() < 0.75:
					side = -side
			first_lot = false

	# Roads: the lane segments themselves (strips run along local X).
	for seg_list in lanes:
		for seg in seg_list:
			var center: Vector3 = seg.pos + seg.dir * (seg.len * 0.5)
			center.y = 0.05
			_add_road(center, _road_yaw(seg.dir), seg.len + 2.0, 3.4)

	# A well just off the lane at the hamlet start (road slab is 3.4 wide).
	var start: Dictionary = lanes[0][0]
	var start_dir: Vector3 = start.dir
	var start_perp := Vector3(-start_dir.z, 0.0, start_dir.x)
	_add_prop(start.pos + start_perp * 3.6, 0.0, PROP_WELL)

	# Trees around and through the cluster.
	var centroid := Vector3.ZERO
	for lot in _lots:
		centroid += lot.pos
	centroid /= float(_lots.size())
	var avg_r := 0.0
	for lot in _lots:
		avg_r += (lot.pos - centroid).length()
	avg_r /= float(_lots.size())
	var tree_count := roundi((10.0 + 10.0 * settings.density))
	for i in tree_count:
		var a := rng.randf() * TAU
		var rr := avg_r * rng.randf_range(1.25, 2.1) + 4.0
		var tp := centroid + Vector3(cos(a), 0.0, sin(a)) * rr
		_add_tree(tp, rng.randf_range(0.8, 1.6))
	var gap_count := roundi(4.0 * settings.density)
	for i in gap_count:
		if _lots.is_empty():
			break
		var lot: Dictionary = _lots[rng.randi() % _lots.size()]
		var a := rng.randf() * TAU
		var tp: Vector3 = lot.pos + Vector3(cos(a), 0.0, sin(a)) * rng.randf_range(4.0, 7.0)
		_add_tree(tp, rng.randf_range(0.7, 1.3))


# =========================================================================
# 村镇 — main street + side lanes
# =========================================================================

func _gen_village() -> void:
	var o: Vector3 = settings.origin
	var L: float = 78.0 * (0.85 + 0.3 * settings.density)
	var main_w := 6.0

	# Main street along X; buildings face it on both sides.
	var side := 1 if rng.randf() < 0.5 else -1
	var x: float = -L + rng.randf_range(6.0, 10.0)
	var spacing := rng.randf_range(12.0, 16.0)
	var plaza_half := 9.0
	while x < L - 19.0:  # 留出庙宇前院 (庙西面约 L-15, 半宽 ≤5.5 的房屋东缘止于 L-13.5)
		var depth: float = rng.randf_range(5.0, 7.0)
		var off: float = main_w * 0.5 + 1.6 + depth * 0.5 + rng.randf_range(0.2, 2.0)
		var lot_pos := Vector3(o.x + x, 0.0, o.z + off * side)
		var face := Vector3(0.0, 0.0, -float(side))
		var near_center: bool = absf(x) < L * 0.45
		var level: int
		var lot_type: int
		if absf(x) < plaza_half:
			# leave the centre plaza open (well + stalls live there)
			x += spacing
			continue
		if near_center and rng.randf() < 0.65:
			level = rng.randi_range(2, 3)
			lot_type = LOT_SHOP
		else:
			level = 2 if rng.randf() < 0.2 else 1
			lot_type = LOT_RESIDENCE
		var params := _params_for_level(level, 11.0, 7.5, false)
		_add_lot(lot_pos, _yaw_to(face), level, lot_type, 0, params)
		x += spacing + rng.randf_range(0.5, 2.0)
		if rng.randf() < 0.8:
			side = -side

	# Side lanes. Pick lane x-positions clear of each other (lots reach ~7.5m
	# off the lane), and start lots past the main-street house depth band
	# (main lots end ~14m from the street) so lane and street houses can't
	# collide.
	var n_lanes := rng.randi_range(2, 3)
	var lane_xs: Array[float] = []
	for lane_i in n_lanes:
		var best_lx := 0.0
		var best_d := -1.0
		for cand in 12:
			var clx := rng.randf_range(-L * 0.6, L * 0.6)
			var dmin := 1e9
			for lx2 in lane_xs:
				dmin = minf(dmin, absf(clx - lx2))
			if dmin > best_d:
				best_d = dmin
				best_lx = clx
		lane_xs.append(best_lx)
		var lx := best_lx
		var l_side := 1.0 if (lane_i % 2 == 0) else -1.0
		var l_len := rng.randf_range(26.0, 46.0)
		var d0 := Vector3(0.0, 0.0, l_side)
		var t := rng.randf_range(18.0, 21.0)
		var l_spacing := rng.randf_range(11.0, 15.0)
		while t < l_len - 3.0:
			var p := Vector3(o.x + lx, 0.0, o.z) + d0 * t
			var perp := Vector3(-1.0, 0.0, 0.0)
			var l2_side := 1 if rng.randf() < 0.5 else -1
			var depth: float = rng.randf_range(4.5, 6.5)
			var off: float = 2.4 + depth * 0.5 + rng.randf_range(0.2, 1.6)
			var lot_pos := p + perp * (off * l2_side)
			var face := -perp * float(l2_side)
			var level := 2 if rng.randf() < 0.15 else 1
			var params := _params_for_level(level, 8.5, 7.0, false)
			_add_lot(lot_pos, _yaw_to(face), level, LOT_RESIDENCE, 0, params)
			t += l_spacing + rng.randf_range(0.5, 3.0)
		# Lane road (strip runs along local X → road yaw convention).
		var lane_center := Vector3(o.x + lx, 0.05, o.z + d0.z * l_len * 0.5)
		_add_road(lane_center, _road_yaw(d0), l_len + 2.0, 3.6)

	# Temple terminates the east vista.
	var temple_pos := Vector3(o.x + L - 7.0, 0.0, o.z)
	var temple_params := _params_for_level(4, 16.0, 11.0)
	_add_lot(temple_pos, _yaw_to(Vector3(-1.0, 0.0, 0.0)), 4, LOT_LANDMARK, 0, temple_params)

	# 牌坊 at the west entry.
	_add_prop(Vector3(o.x - L + 2.0, 0.0, o.z), 90.0, PROP_ARCH)

	# Main street road: runs from the west entry to just before the temple's
	# west face, so it never touches the landmark. (All absolute: road_end
	# already contains o.x.)
	var road_end: float = temple_pos.x - temple_params.width * 0.5 - 2.0
	var main_center := Vector3((o.x - L - 1.0 + road_end) * 0.5, 0.05, o.z)
	_add_road(main_center, 0.0, road_end - o.x + L + 1.0, main_w)

	# Plaza props beside the road slab (road occupies z in [-3, 3]).
	# Stalls face the street (+Z 是摊档正面)。
	_add_prop(Vector3(o.x + 0.0, 0.0, o.z + 4.2), 0.0, PROP_WELL)
	_add_prop(Vector3(o.x - 5.0, 0.0, o.z + 4.5), 180.0, PROP_STALL)
	_add_prop(Vector3(o.x + 5.0, 0.0, o.z - 4.5), 0.0, PROP_STALL)

	# Trees on the perimeter.
	var tree_count := roundi((14.0 + 14.0 * settings.density))
	for i in tree_count:
		var a := rng.randf() * TAU
		var rr := rng.randf_range(L * 0.75, L * 1.3)
		var tp := Vector3(o.x + cos(a) * rr, 0.0, o.z + sin(a) * rr)
		_add_tree(tp, rng.randf_range(0.8, 1.6))


# =========================================================================
# 市集 — 3x3 blocks around a market square
# =========================================================================

func _gen_market() -> void:
	var o: Vector3 = settings.origin
	var B := 27.0
	var S_main := 8.0
	var S_out := 5.0
	var pitch := B + S_out

	# Buildings: the 8 blocks around the central square, 2x2 lots each.
	for bi in [-1, 0, 1]:
		for bj in [-1, 0, 1]:
			if bi == 0 and bj == 0:
				continue  # market square
			var bc := Vector3(o.x + pitch * bi, 0.0, o.z + pitch * bj)
			var near_square: bool = absi(bi) + absi(bj) == 1
			for sx in [-1.0, 1.0]:
				for sz in [-1.0, 1.0]:
					var lot_pos := bc + Vector3(sx * (B * 0.25 + 0.4), 0.0, sz * (B * 0.25 + 0.4))
					# 面向广场一侧的铺面朝向广场, 其余面朝较近的外侧街道。
					# (对角线朝街会让 45° 屋檐戳出街区边缘, 改走轴向。)
					var face := Vector3.ZERO
					if bi == 0 and sz == -signi(bj):
						face = Vector3(0.0, 0.0, -float(signi(bj)))
					elif bj == 0 and sx == -signi(bi):
						face = Vector3(-float(signi(bi)), 0.0, 0.0)
					elif bi != 0 and bj != 0 and sx == -signi(bi) and sz == -signi(bj):
						face = -Vector3(float(signi(bi)), 0.0, float(signi(bj))).normalized()
					elif absf(float(bi) + 0.4 * sx) >= absf(float(bj) + 0.4 * sz):
						var fx: float = sx if bi == 0 else float(signi(bi))
						face = Vector3(fx, 0.0, 0.0)
					else:
						var fz: float = sz if bj == 0 else float(signi(bj))
						face = Vector3(0.0, 0.0, fz)
					var level: int
					var lot_type: int
					if near_square and rng.randf() < 0.8:
						level = rng.randi_range(2, 3)
						lot_type = LOT_SHOP
					elif rng.randf() < 0.35:
						level = 2
						lot_type = LOT_SHOP
					else:
						level = 2 if rng.randf() < 0.3 else 1
						lot_type = LOT_RESIDENCE
					var params := _params_for_level(level, 11.5, 8.5, false)
					_add_lot(lot_pos, _yaw_to(face), level, lot_type, 0, params)

	# Stalls fill the square in four quadrants, each facing its main street.
	var stall_rows := 2
	var stall_spacing := 4.4
	var stall_count := 0
	var stall_max := roundi(20.0 * settings.density)
	for qx in [-1.0, 1.0]:
		for qz in [-1.0, 1.0]:
			for rx in range(1, stall_rows + 1):
				for rz in range(1, stall_rows + 1):
					if stall_count >= stall_max:
						break
					var sp := Vector3(
						o.x + qx * (3.6 + rx * stall_spacing),
						0.0,
						o.z + qz * (3.6 + rz * stall_spacing))
					# Face the street line the stall is closest to.
					var face := -Vector3(0.0, 0.0, qz) if absf(sp.z - o.z) < absf(sp.x - o.x) \
						else -Vector3(qx, 0.0, 0.0)
					_add_prop(sp, _yaw_to(face), PROP_STALL)
					stall_count += 1

	# Well off the road crossing (roads occupy |x|<=4 and |z|<=4).
	_add_prop(Vector3(o.x + 6.0, 0.0, o.z + 6.0), 0.0, PROP_WELL)

	# Roads: two main strips crossing through the square + the outer ring.
	var Lx: float = 1.5 * pitch
	var Lz: float = 1.5 * pitch
	_add_road(Vector3(o.x, 0.05, o.z), 90.0, 2.0 * Lz, S_main)          # N-S through the square
	_add_road(Vector3(o.x, 0.05, o.z), 0.0, 2.0 * Lx, S_main)           # E-W through the square
	_add_road(Vector3(o.x, 0.05, o.z - Lz), 0.0, 2.0 * Lx + 2.0, S_out)
	_add_road(Vector3(o.x, 0.05, o.z + Lz), 0.0, 2.0 * Lx + 2.0, S_out)
	_add_road(Vector3(o.x - Lx, 0.05, o.z), 90.0, 2.0 * Lz + 2.0, S_out)
	_add_road(Vector3(o.x + Lx, 0.05, o.z), 90.0, 2.0 * Lz + 2.0, S_out)

	# 牌坊 at the west and east entries of the main street.
	_add_prop(Vector3(o.x - Lx + 1.5, 0.0, o.z), 90.0, PROP_ARCH)
	_add_prop(Vector3(o.x + Lx - 1.5, 0.0, o.z), 90.0, PROP_ARCH)

	# Sparse perimeter trees.
	var tree_count := roundi(8.0 + 6.0 * settings.density)
	for i in tree_count:
		var a := rng.randf() * TAU
		var rr := rng.randf_range(Lx * 0.9, Lx * 1.15)
		var tp := Vector3(o.x + cos(a) * rr, 0.0, o.z + sin(a) * rr)
		_add_tree(tp, rng.randf_range(0.8, 1.5))


# =========================================================================
# 城市 — 4x4 里坊 grid, palace ward, wall ring
# =========================================================================

func _gen_city() -> void:
	var o: Vector3 = settings.origin
	var Ex: float = settings.extent_x * 2.0
	var Ez: float = settings.extent_z * 2.0
	var wall_h: float = settings.wall_height
	var wall_t: float = settings.wall_thickness
	var gate: float = settings.gate_width
	var wards := 4
	var ward: float = Ex / float(wards)

	# Street widths by hierarchy: 朱雀大街 main 16, secondaries 10, ring 6.
	var W_MAIN := 16.0
	var W_SEC := 10.0
	var W_RING := 6.0

	# --- Wards ---------------------------------------------------------------
	# Ward (i, j): i = column (x), j = row (z); palace occupies the central 2x2.
	# Grid is symmetric about the origin: centre = ward * (i - (wards-1)/2).
	for i in wards:
		for j in wards:
			var wc := Vector3(o.x + ward * (float(i) - 1.5),
				0.0, o.z + ward * (float(j) - 1.5))
			var is_palace: bool = (i == 1 or i == 2) and (j == 1 or j == 2)
			if is_palace:
				continue
			var dist_center: float = Vector3(wc.x - o.x, 0.0, wc.z - o.z).length()
			var dmax: float = Vector3(Ex * 0.5, 0.0, Ez * 0.5).length()
			var centrality: float = clampf(1.0 - dist_center / dmax, 0.0, 1.0)
			var ward_level := clampi(1 + roundi(centrality * 3.0), 1, 4)

			var half := ward * 0.5
			# 街道半宽最大 8m (朱雀大街); 10m 退距给台阶/栅栏/尺寸抖动留余量。
			var margin := 10.0
			# North row (faces -Z) and south row (faces +Z): 5 lots + a staggered
			# interior row behind, so the street frontage reads continuous.
			for k in [-2, -1, 0, 1, 2]:
				var lx: float = wc.x + k * ward * 0.18
				var depth_n: float = rng.randf_range(7.0, 9.5)
				var z_n: float = wc.z - half + margin + depth_n * 0.5
				var level_n: int = clampi(ward_level + (1 if rng.randf() < 0.25 else 0), 1, 4)
				var pn := _params_for_level(level_n, ward * 0.3, depth_n, false, true)
				_add_lot(Vector3(lx, 0.0, z_n), 180.0, level_n,
					LOT_SHOP if level_n >= 2 and rng.randf() < 0.4 else LOT_RESIDENCE, i * wards + j, pn)
				var depth_n2: float = rng.randf_range(6.5, 9.0)
				var z_n2: float = z_n - depth_n * 0.5 - 7.0 - depth_n2 * 0.5
				var level_n2: int = clampi(ward_level, 1, 4)
				var pn2 := _params_for_level(level_n2, ward * 0.3, depth_n2, false, true)
				_add_lot(Vector3(lx, 0.0, z_n2), 180.0, level_n2, LOT_RESIDENCE, i * wards + j, pn2)

				var depth_s: float = rng.randf_range(7.0, 9.5)
				var z_s: float = wc.z + half - margin - depth_s * 0.5
				var level_s: int = clampi(ward_level + (1 if rng.randf() < 0.25 else 0), 1, 4)
				var ps := _params_for_level(level_s, ward * 0.3, depth_s, false, true)
				_add_lot(Vector3(lx, 0.0, z_s), 0.0, level_s,
					LOT_SHOP if level_s >= 2 and rng.randf() < 0.4 else LOT_RESIDENCE, i * wards + j, ps)
				var depth_s2: float = rng.randf_range(6.5, 9.0)
				var z_s2: float = z_s + depth_s * 0.5 + 7.0 + depth_s2 * 0.5
				var level_s2: int = clampi(ward_level, 1, 4)
				var ps2 := _params_for_level(level_s2, ward * 0.3, depth_s2, false, true)
				_add_lot(Vector3(lx, 0.0, z_s2), 0.0, level_s2, LOT_RESIDENCE, i * wards + j, ps2)
			# East row (faces +X) and west row (faces -X): 3 lots + interior row.
			for k in [-1, 0, 1]:
				var lz: float = wc.z + k * ward * 0.26
				var depth_e: float = rng.randf_range(7.0, 9.5)
				var x_e: float = wc.x + half - margin - depth_e * 0.5
				var level_e: int = clampi(ward_level, 1, 4)
				var pe := _params_for_level(level_e, ward * 0.3, depth_e, false, true)
				_add_lot(Vector3(x_e, 0.0, lz), 90.0, level_e, LOT_RESIDENCE, i * wards + j, pe)
				var depth_e2: float = rng.randf_range(6.5, 9.0)
				var x_e2: float = x_e - depth_e * 0.5 - 7.0 - depth_e2 * 0.5
				var pe2 := _params_for_level(clampi(ward_level, 1, 4), ward * 0.3, depth_e2, false, true)
				_add_lot(Vector3(x_e2, 0.0, lz), 90.0, ward_level, LOT_RESIDENCE, i * wards + j, pe2)

				var depth_w: float = rng.randf_range(7.0, 9.5)
				var x_w: float = wc.x - half + margin + depth_w * 0.5
				var level_w: int = clampi(ward_level, 1, 4)
				var pw := _params_for_level(level_w, ward * 0.3, depth_w, false, true)
				_add_lot(Vector3(x_w, 0.0, lz), -90.0, level_w, LOT_RESIDENCE, i * wards + j, pw)
				var depth_w2: float = rng.randf_range(6.5, 9.0)
				var x_w2: float = x_w + depth_w * 0.5 + 7.0 + depth_w2 * 0.5
				var pw2 := _params_for_level(clampi(ward_level, 1, 4), ward * 0.3, depth_w2, false, true)
				_add_lot(Vector3(x_w2, 0.0, lz), -90.0, ward_level, LOT_RESIDENCE, i * wards + j, pw2)

			# Courtyard trees in the ward centre.
			var tree_n := rng.randi_range(2, 4)
			for t in tree_n:
				var a := rng.randf() * TAU
				var rr := rng.randf_range(0.0, ward * 0.22)
				_add_tree(wc + Vector3(cos(a), 0.0, sin(a)) * rr, rng.randf_range(0.8, 1.4))

	# --- Palace ward ---------------------------------------------------------
	# 御道 (朱雀大街) enters from the south gate and passes through the palace
	# between the front pavilions; the main hall faces it from the north.
	var hall_params := _params_for_level(5, 24.0, 15.0)
	_add_lot(Vector3(o.x, 0.0, o.z + 17.0), 180.0, 5, LOT_LANDMARK, -1, hall_params)
	for sx in [-1.0, 1.0]:
		var side_params := _params_for_level(4, 13.0, 9.0)
		_add_lot(Vector3(o.x + sx * 15.0, 0.0, o.z + 15.0), 180.0, 4, LOT_LANDMARK, -1, side_params)
		var front_pav := _params_for_pavilion(8.5)
		_add_lot(Vector3(o.x + sx * 19.0, 0.0, o.z - 6.0), 0.0, 5, LOT_LANDMARK, -1, front_pav)
		var back_pav := _params_for_pavilion(8.0)
		_add_lot(Vector3(o.x + sx * 13.0, 0.0, o.z + 30.0), 0.0, 5, LOT_LANDMARK, -1, back_pav)

	# Inner palace wall (3.5 high) with gate openings on all four cardinal sides.
	var px := 30.0
	var pz0 := -12.0
	var pz1 := 38.0
	var p_h := 3.5
	var p_t := 0.8
	var palace_gate := 16.0  # 与朱雀大街同宽, 御道直入宫门
	# North / south walls (along X).
	for z_w in [pz0, pz1]:
		var seg_len := (2.0 * px - palace_gate) * 0.5
		var off := (palace_gate * 0.5 + seg_len * 0.5)
		_add_wall(Vector3(o.x - off, p_h * 0.5, o.z + z_w), 0.0, seg_len, p_h, p_t)
		_add_wall(Vector3(o.x + off, p_h * 0.5, o.z + z_w), 0.0, seg_len, p_h, p_t)
	# East / west walls (along Z), gate openings centred on the E-W avenue
	# (z=0, W_MAIN 半宽 8m) — 旧门位 z∈[5,21] 与道路错开, 成了死门。
	for x_w in [-px, px]:
		_add_wall(Vector3(o.x + x_w, p_h * 0.5, o.z + (pz0 - 8.0) * 0.5), 90.0, -pz0 - 8.0, p_h, p_t)
		_add_wall(Vector3(o.x + x_w, p_h * 0.5, o.z + (8.0 + pz1) * 0.5), 90.0, pz1 - 8.0, p_h, p_t)

	# --- City wall ring ------------------------------------------------------
	# Wall corners sit at ±R; segments must span from the gate edge (gate*0.5)
	# to the corner (R), so the corner towers stand on a closed corner.
	var R: float = Ex * 0.5 + 3.0
	var seg_len := (2.0 * R - gate) * 0.5
	var seg_off := (gate * 0.5 + seg_len * 0.5)
	# North / south (along X), gate on the axis.
	_add_wall(Vector3(o.x - seg_off, wall_h * 0.5, o.z - R), 0.0, seg_len, wall_h, wall_t)
	_add_wall(Vector3(o.x + seg_off, wall_h * 0.5, o.z - R), 0.0, seg_len, wall_h, wall_t)
	_add_wall(Vector3(o.x - seg_off, wall_h * 0.5, o.z + R), 0.0, seg_len, wall_h, wall_t)
	_add_wall(Vector3(o.x + seg_off, wall_h * 0.5, o.z + R), 0.0, seg_len, wall_h, wall_t)
	# East / west (along Z).
	var seg_len_z := (2.0 * R - gate) * 0.5
	var seg_off_z := (gate * 0.5 + seg_len_z * 0.5)
	_add_wall(Vector3(o.x - R, wall_h * 0.5, o.z - seg_off_z), 90.0, seg_len_z, wall_h, wall_t)
	_add_wall(Vector3(o.x - R, wall_h * 0.5, o.z + seg_off_z), 90.0, seg_len_z, wall_h, wall_t)
	_add_wall(Vector3(o.x + R, wall_h * 0.5, o.z - seg_off_z), 90.0, seg_len_z, wall_h, wall_t)
	_add_wall(Vector3(o.x + R, wall_h * 0.5, o.z + seg_off_z), 90.0, seg_len_z, wall_h, wall_t)

	# Gate towers sit on top of the wall over the gate openings.
	var gate_roof: int = ROOF_GABLE_AND_HIP if rng.randf() < 0.6 else ROOF_HIP
	for yaw_g in [0.0, 180.0, 90.0, -90.0]:
		var gpos: Vector3
		match yaw_g:
			0.0: gpos = Vector3(o.x, wall_h, o.z - R)
			180.0: gpos = Vector3(o.x, wall_h, o.z + R)
			90.0: gpos = Vector3(o.x - R, wall_h, o.z)
			_: gpos = Vector3(o.x + R, wall_h, o.z)
		var tp := _params_for_tower(rng.randf_range(13.0, 16.0), rng.randf_range(8.0, 10.0), gate_roof)
		_add_lot(gpos, yaw_g, 5, LOT_TOWER, -1, tp)

	# Corner towers: 攒尖 pavilions on the wall corners.
	for sx in [-1.0, 1.0]:
		for sz in [-1.0, 1.0]:
			var cp := _params_for_pavilion(rng.randf_range(7.0, 8.5))
			_add_lot(Vector3(o.x + sx * R, wall_h, o.z + sz * R), 0.0, 5, LOT_TOWER, -1, cp)

	# --- Roads ---------------------------------------------------------------
	# Interior street lines (x = -Ex/4, +Ex/4 and same for z).
	for k in [-1, 1]:
		var rx: float = o.x + k * Ex * 0.25
		_add_road(Vector3(rx, 0.05, o.z), 90.0, Ez - 2.0, W_SEC)
		var rz: float = o.z + k * Ez * 0.25
		_add_road(Vector3(o.x, 0.05, rz), 0.0, Ex - 2.0, W_SEC)
	# 中央两轴在宫城区断开: 朱雀大街南段自南环道(城墙内 4m)止于宫门, 北段
	# 自宫墙后至北环道; 东西横街让出宫墙。
	_add_road(Vector3(o.x, 0.05, o.z + (pz0 - Ez * 0.5) * 0.5), 90.0, Ez * 0.5 + pz0, W_MAIN)
	_add_road(Vector3(o.x, 0.05, o.z + (pz1 + Ez * 0.5) * 0.5), 90.0, Ez * 0.5 - pz1, W_MAIN)
	_add_road(Vector3(o.x - (px + Ex * 0.5) * 0.5, 0.05, o.z), 0.0, Ex * 0.5 - px, W_MAIN)
	_add_road(Vector3(o.x + (px + Ex * 0.5) * 0.5, 0.05, o.z), 0.0, Ex * 0.5 - px, W_MAIN)
	# Ring roads just inside the wall (wall at Ex/2+3, 建筑前脸在 Ex/2-8)。
	_add_road(Vector3(o.x, 0.05, o.z - Ex * 0.5 + 4.0), 0.0, Ex - 4.0, W_RING)
	_add_road(Vector3(o.x, 0.05, o.z + Ex * 0.5 - 4.0), 0.0, Ex - 4.0, W_RING)
	_add_road(Vector3(o.x - Ez * 0.5 + 4.0, 0.05, o.z), 90.0, Ez - 4.0, W_RING)
	_add_road(Vector3(o.x + Ez * 0.5 - 4.0, 0.05, o.z), 90.0, Ez - 4.0, W_RING)

	# --- 东市 / 西市 -----------------------------------------------------------
	# 御道两侧的宫前广场 (宫城区内、宫墙以南的空地)。
	for side in [-1.0, 1.0]:
		var mcx: float = o.x + side * 65.0
		var mcz: float = o.z - 65.0
		var stall_n := roundi(9.0 * settings.density)
		for s in stall_n:
			var sp := Vector3(
				mcx + rng.randf_range(-18.0, 18.0),
				0.0,
				mcz + rng.randf_range(-18.0, 18.0))
			# Face the 朱雀大街 axis.
			var face := Vector3(-signf(sp.x - o.x), 0.0, 0.0)
			_add_prop(sp, _yaw_to(face), PROP_STALL)

	# 宫前御道两侧的树列, 强化轴线仪式感。
	for side in [-1.0, 1.0]:
		var z_t := o.z - 118.0
		while z_t < o.z - 18.0:
			_add_tree(Vector3(o.x + side * 14.0, 0.0, z_t), rng.randf_range(1.0, 1.5))
			z_t += rng.randf_range(10.0, 14.0)

	# --- 官署带 (宫城区内) ------------------------------------------------------
	# 御道两侧对称的官署院落填补宫前广场两侧, 隔 26m 一行, 面向御道。
	for row in 4:
		var oz_: float = o.z - 24.0 - row * 26.0
		for side in [-1.0, 1.0]:
			if rng.randf() < 0.75:
				var op := _params_for_level(4, 14.0, 10.0)
				_add_lot(Vector3(o.x + side * 28.0, 0.0, oz_), -90.0 * side, 4,
					LOT_LANDMARK, -1, op)

	# 宫城后苑: 北部两处园林亭榭 + 树丛 (宫城墙北侧至里坊北界之间)。
	for side in [-1.0, 1.0]:
		var gp := _params_for_pavilion(7.5)
		_add_lot(Vector3(o.x + side * 45.0, 0.0, o.z + 68.0), 0.0, 4, LOT_LANDMARK, -1, gp)
		for i in 6:
			_add_tree(Vector3(
				o.x + side * (35.0 + rng.randf_range(-14.0, 14.0)), 0.0,
				o.z + rng.randf_range(48.0, 88.0)), rng.randf_range(0.8, 1.4))

	# --- Trees ---------------------------------------------------------------
	# A sparse belt outside the wall, denser away from the gates.
	var tree_count := roundi((22.0 + 14.0 * settings.density))
	for i in tree_count:
		var a := rng.randf() * TAU
		var rr := R + rng.randf_range(7.0, 26.0)
		var tp := Vector3(o.x + cos(a) * rr, 0.0, o.z + sin(a) * rr)
		_add_tree(tp, rng.randf_range(0.8, 1.7))


# =========================================================================
# Stream packing
# =========================================================================

func _pack_buildings() -> FlowData.Data:
	var d := FlowData.Data.new()
	var n := _lots.size()
	var pos := PackedVector3Array()
	var rot := PackedVector3Array()
	var size := PackedVector3Array()
	var width := PackedFloat32Array()
	var depth := PackedFloat32Array()
	var roof := PackedInt32Array()
	var bays_x := PackedInt32Array()
	var bays_z := PackedInt32Array()
	var material := PackedInt32Array()
	var courses := PackedInt32Array()
	var coverage := PackedFloat32Array()
	var tcw := PackedFloat32Array()
	var corner := PackedFloat32Array()
	var fence := PackedByteArray()
	var walls := PackedByteArray()
	var steps := PackedByteArray()
	var lambda := PackedInt32Array()
	# 民居形制 (2026-09-27)：逐点写流 ⇒ 形制固化在 PCG 数据里，而不是靠脚本往节点灌设置。
	var dado := PackedFloat32Array()
	var dado_trim := PackedFloat32Array()
	var col_base := PackedFloat32Array()
	var plat := PackedByteArray()
	var p_top_joints := PackedByteArray()
	var p_edge_lip := PackedByteArray()
	var paving := PackedByteArray()
	var paving_joints := PackedByteArray()
	var step_cheek := PackedByteArray()
	var col_square := PackedByteArray()
	var level := PackedInt32Array()
	var lot_type := PackedInt32Array()
	var ward := PackedInt32Array()
	var tint := PackedColorArray()
	var seeds := PackedInt32Array()
	pos.resize(n); rot.resize(n); size.resize(n)
	width.resize(n); depth.resize(n); roof.resize(n); bays_x.resize(n); bays_z.resize(n)
	material.resize(n); courses.resize(n); coverage.resize(n); tcw.resize(n); corner.resize(n)
	fence.resize(n); walls.resize(n); steps.resize(n); lambda.resize(n)
	level.resize(n); lot_type.resize(n); ward.resize(n); tint.resize(n); seeds.resize(n)
	dado.resize(n); dado_trim.resize(n); col_base.resize(n)
	plat.resize(n); p_top_joints.resize(n); p_edge_lip.resize(n)
	paving.resize(n); paving_joints.resize(n); step_cheek.resize(n); col_square.resize(n)

	for i in n:
		var lot: Dictionary = _lots[i]
		var p: Dictionary = lot.params
		pos[i] = lot.pos
		rot[i] = Vector3(0.0, lot.yaw, 0.0)
		size[i] = Vector3.ONE
		width[i] = p.width
		depth[i] = p.depth
		roof[i] = p.roof_type
		bays_x[i] = p.bays_x
		bays_z[i] = p.bays_z
		material[i] = p.material_style
		courses[i] = p.rafter_courses
		coverage[i] = p.tile_coverage
		tcw[i] = p.tile_course_width
		corner[i] = p.corner_rise_scale
		fence[i] = 1 if p.fence else 0
		walls[i] = 1 if p.walls else 0
		steps[i] = 1 if p.steps else 0
		lambda[i] = p.fence_lambda
		level[i] = lot.level

		# 形制按等级分档（**起点策略，按需调整**）：台基/台面/铺地对所有等级开——
		# 没有台基不成建筑；民居（≤2）另有下碱砖带、踏步侧挡与方形柱础；官式（3）本轮
		# 没有对应参考依据，这几项先关，等资料到位再补。
		var b_dwelling: bool = lot.level <= 2
		plat[i] = 1
		p_top_joints[i] = 1
		p_edge_lip[i] = 1
		paving[i] = 1
		paving_joints[i] = 1
		step_cheek[i] = 1 if b_dwelling else 0
		col_square[i] = 1 if b_dwelling else 0
		dado[i] = 0.25 if b_dwelling else 0.0
		dado_trim[i] = 1.0 if b_dwelling else 0.0
		col_base[i] = 0.35 if b_dwelling else 0.0
		lot_type[i] = lot.lot_type
		ward[i] = lot.ward
		tint[i] = lot.tint
		seeds[i] = FlowData.point_seed(lot.pos, settings.random_seed)

	d.registerStream(FlowData.AttrPosition, pos, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrRotation, rot, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrSize, size, FlowData.DataType.Vector)
	d.registerStream("ab_width", width, FlowData.DataType.Float)
	d.registerStream("ab_depth", depth, FlowData.DataType.Float)
	d.registerStream("ab_roof_type", roof, FlowData.DataType.Int)
	d.registerStream("ab_bays_x", bays_x, FlowData.DataType.Int)
	d.registerStream("ab_bays_z", bays_z, FlowData.DataType.Int)
	d.registerStream("ab_material_style", material, FlowData.DataType.Int)
	d.registerStream("ab_rafter_courses", courses, FlowData.DataType.Int)
	d.registerStream("ab_tile_coverage", coverage, FlowData.DataType.Float)
	d.registerStream("ab_tile_course_width", tcw, FlowData.DataType.Float)
	d.registerStream("ab_corner_rise_scale", corner, FlowData.DataType.Float)
	d.registerStream("ab_fence", fence, FlowData.DataType.Bool)
	d.registerStream("ab_walls", walls, FlowData.DataType.Bool)
	d.registerStream("ab_steps", steps, FlowData.DataType.Bool)
	d.registerStream("ab_fence_lambda", lambda, FlowData.DataType.Int)
	# 民居形制：读到这些流的 Ancient Building 节点会按点烘焙，并把它们并入组合键。
	d.registerStream("ab_platform", plat, FlowData.DataType.Bool)
	d.registerStream("ab_platform_top_joints", p_top_joints, FlowData.DataType.Bool)
	d.registerStream("ab_platform_edge_lip", p_edge_lip, FlowData.DataType.Bool)
	d.registerStream("ab_paving", paving, FlowData.DataType.Bool)
	d.registerStream("ab_paving_joint_geometry", paving_joints, FlowData.DataType.Bool)
	d.registerStream("ab_step_side_cheek", step_cheek, FlowData.DataType.Bool)
	d.registerStream("ab_column_base_square", col_square, FlowData.DataType.Bool)
	d.registerStream("ab_dado_height_ratio", dado, FlowData.DataType.Float)
	d.registerStream("ab_dado_top_trim", dado_trim, FlowData.DataType.Float)
	d.registerStream("ab_column_base_height_scale", col_base, FlowData.DataType.Float)
	d.registerStream("level", level, FlowData.DataType.Int)
	d.registerStream("lot_type", lot_type, FlowData.DataType.Int)
	d.registerStream("ward", ward, FlowData.DataType.Int)
	d.registerStream(settings.color_attribute, tint, FlowData.DataType.Color)
	d.registerStream(FlowData.AttrSeed, seeds, FlowData.DataType.Int)
	return d


func _pack_strips(strips: Array) -> FlowData.Data:
	var d := FlowData.Data.new()
	var n := strips.size()
	var pos := PackedVector3Array()
	var rot := PackedVector3Array()
	var size := PackedVector3Array()
	var tint := PackedColorArray()
	pos.resize(n); rot.resize(n); size.resize(n); tint.resize(n)
	for i in n:
		var s: Dictionary = strips[i]
		pos[i] = s.pos
		rot[i] = Vector3(0.0, s.yaw, 0.0)
		var h: float = s.get("height", 0.1)
		size[i] = Vector3(s.len, h, s.get("width", s.get("thick", 1.0)))
		# 引擎 fork 的 D3D12 MMI 在 use_colors=false 且实例绕 Y 旋转 ~90° 时会丢失
		# 网格顶点色 (probe2 实测 P2 全白)。注册白色实例色流 -> spawn_meshes 开
		# use_colors 并写入实例色; 白色为中性, ink 材质不使用 INSTANCE_CUSTOM。
		tint[i] = Color.WHITE
	d.registerStream(FlowData.AttrPosition, pos, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrRotation, rot, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrSize, size, FlowData.DataType.Vector)
	d.registerStream("color", tint, FlowData.DataType.Color)
	return d


func _pack_props() -> FlowData.Data:
	var d := FlowData.Data.new()
	var n := _props.size()
	var pos := PackedVector3Array()
	var rot := PackedVector3Array()
	var size := PackedVector3Array()
	var kind := PackedInt32Array()
	var seeds := PackedInt32Array()
	var tint := PackedColorArray()
	pos.resize(n); rot.resize(n); size.resize(n); kind.resize(n); seeds.resize(n); tint.resize(n)
	for i in n:
		var p: Dictionary = _props[i]
		pos[i] = p.pos
		rot[i] = Vector3(0.0, p.yaw, 0.0)
		size[i] = Vector3.ONE
		kind[i] = p.kind
		seeds[i] = FlowData.point_seed(p.pos, settings.random_seed)
		tint[i] = Color.WHITE
	d.registerStream(FlowData.AttrPosition, pos, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrRotation, rot, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrSize, size, FlowData.DataType.Vector)
	d.registerStream("prop_type", kind, FlowData.DataType.Int)
	d.registerStream(FlowData.AttrSeed, seeds, FlowData.DataType.Int)
	# 白色实例色流: 强制 use_colors=true, 规避 fork MMI 旋转实例丢顶点色 (见 _pack_strips)。
	d.registerStream("color", tint, FlowData.DataType.Color)
	return d


func _pack_trees() -> FlowData.Data:
	var d := FlowData.Data.new()
	var n := _trees.size()
	var pos := PackedVector3Array()
	var rot := PackedVector3Array()
	var size := PackedVector3Array()
	var seeds := PackedInt32Array()
	var tint := PackedColorArray()
	pos.resize(n); rot.resize(n); size.resize(n); seeds.resize(n); tint.resize(n)
	for i in n:
		var t: Dictionary = _trees[i]
		pos[i] = t.pos
		rot[i] = Vector3(0.0, rng.randf() * 360.0, 0.0)
		size[i] = Vector3.ONE * t.scale
		seeds[i] = FlowData.point_seed(t.pos, settings.random_seed)
		tint[i] = Color.WHITE
	d.registerStream(FlowData.AttrPosition, pos, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrRotation, rot, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrSize, size, FlowData.DataType.Vector)
	d.registerStream(FlowData.AttrSeed, seeds, FlowData.DataType.Int)
	# 白色实例色流: 强制 use_colors=true, 规避 fork MMI 旋转实例丢顶点色 (见 _pack_strips)。
	d.registerStream("color", tint, FlowData.DataType.Color)
	return d
