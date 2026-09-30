@tool
class_name TownCourtyards
extends RefCounted

## Courtyard (院落) plans for one parcel: which buildings, where, facing which way, and the
## enclosing walls — the "区域 → 闾里 → 子庭院" last step of the Tencent GDC 2025 city toolkit, with
## that talk's two layout rules made hard constraints:
##   - a gap between two things on the ground is either 0 (they touch: shared gable, wall butting
##     a platform) or a real passage ≥ PASSAGE — never the unusable sliver in between;
##   - an official compound reserves its main hall first, and only then fits the rest round it.
##
## Everything is planned in the parcel's usable rectangle (TownParcels.usable_rect): u along the
## frontage 0..W, v inward 0..D, the street at v < 0. Results are in the layout frame.
##
## Ground footprint of a building = its body (w × d, the column lines) + PLATFORM on every side
## (+ the stair run on its front when it has steps). Roof footprint = body + `overhang()`, fitted
## to baked AncientBuildings. Roofs of one parcel never overlap and stay inside the parcel except
## over the street, and across a shared 硬山 gable, where two neighbours' tile edges meet: the
## gable reaches ~0.1–0.2 m past the platform (GABLE_SHARE), which is the joint, not a clash.

const ROOF_FLUSH_GABLE := 0       # 硬山
const ROOF_GABLE_AND_HIP := 1     # 歇山
const ROOF_HIP := 2               # 庑殿
const ROOF_OVERHANGING := 3       # 悬山
const ROOF_ROUND_RIDGE := 4       # 卷棚

enum Kind { SINGLE, SHOP_HOUSE, THREE_SIDED, FOUR_SIDED, OFFICIAL, OPEN }
enum Use { RESIDENCE, SHOP, OFFICIAL, MARKET, GARDEN }

const PASSAGE := 1.2              ## 最小可通行间隙（腾讯 GDC 2025 的"不许过窄通道"）, 待定标
const PLATFORM := 0.7             ## 台基出沿 beyond the column lines (river town PLATFORM_MARGIN)
const WALL_T := 0.45              ## 院墙 thickness
const WALL_H := 2.6
const GATE := 2.4                 ## 院门 opening
const GABLE_SHARE := 0.25        ## how far a 硬山 gable may reach past its parcel edge
const STEP_RUN := 0.27            ## stair run per metre of width (Table 1: 3.52 D, measured 0.265 w)
const SIDE := WALL_T + PASSAGE    ## setback from a walled parcel edge that leaves a 备弄 inside the wall


## Picks the courtyard kind a parcel can hold for its use and level.
static func choose(rect: Dictionary, use: int, level: int) -> int:
	var w: float = rect.width
	var d: float = rect.depth
	match use:
		Use.MARKET, Use.GARDEN:
			return Kind.OPEN
		Use.OFFICIAL:
			if w >= 24.0 and d >= 36.0:
				return Kind.OFFICIAL
	if use == Use.SHOP:
		return Kind.SHOP_HOUSE if d >= 19.0 else Kind.SINGLE
	if w >= 18.0 and d >= 30.0 and level >= 3:
		return Kind.FOUR_SIDED
	if w >= 18.0 and d >= 21.0:
		return Kind.THREE_SIDED
	if d >= 19.0:
		return Kind.SHOP_HOUSE
	return Kind.SINGLE


## Plans the parcel. `level` is the 05-contract level (民居 2..4, 官式 5..6).
## Returns {kind, buildings: [Dictionary], walls: [Dictionary], trees: PackedVector2Array,
## props: [Dictionary]}; buildings are {pos, facing, w, d, role, roof, level, storeys, steps}.
static func plan(rect: Dictionary, kind: int, use: int, level: int, rng: RandomNumberGenerator) -> Dictionary:
	var p := _Plan.new(rect)
	match kind:
		Kind.SINGLE:
			_single(p, use, level, rng)
		Kind.SHOP_HOUSE:
			if not _shop_house(p, use, level, rng):
				_single(p, use, level, rng)
		Kind.THREE_SIDED:
			if not _courtyard(p, false, level, rng):
				_single(p, use, level, rng)
		Kind.FOUR_SIDED:
			if not _courtyard(p, true, level, rng):
				if not _courtyard(p, false, level, rng):
					_single(p, use, level, rng)
		Kind.OFFICIAL:
			if not _official(p, level, rng):
				_courtyard(p, true, mini(level, 4), rng)
		Kind.OPEN:
			_open(p, use, rng)
	return p.result(kind)


## Roof overhang of a building beyond its column lines: x across its gable ends, y past its front
## and back eaves. Linear fits to baked AncientBuildings (Develop/Tools/probe_building_extents.gd,
## w 5–12 m), each a little above every measured point. Table 1's bare 2.6 D eave comes out
## 0.15–0.3 m short: the tile skin, 瓦当 and 翼角 all reach past the rafter ends.
static func overhang(w: float, roof: int) -> Vector2:
	var eave := 0.211 * w + 0.05
	var gable := 0.0125 * w + 0.71          # 硬山: the gable wall and its tile edge, ~ the platform
	match roof:
		ROOF_FLUSH_GABLE:
			return Vector2(gable, eave)
		ROOF_OVERHANGING, ROOF_ROUND_RIDGE:
			return Vector2(maxf(gable, 0.197 * w - 0.15), eave)
	var hip := 0.257 * w + 0.07              # 歇山 / 庑殿: the corner flip reaches out both ways
	return Vector2(hip, hip)


## Ground footprint (platform, plus the stair run in front) in the layout frame.
static func ground_footprint(b: Dictionary) -> PackedVector2Array:
	var run: float = STEP_RUN * b.w if b.steps else 0.0
	return _box(b.pos, b.facing, b.w * 0.5 + PLATFORM, b.d * 0.5 + PLATFORM, run)


static func roof_footprint(b: Dictionary) -> PackedVector2Array:
	var o := overhang(b.w, b.roof)
	return _box(b.pos, b.facing, b.w * 0.5 + o.x, b.d * 0.5 + o.y, 0.0)


static func wall_footprint(wall: Dictionary) -> PackedVector2Array:
	var a: Vector2 = wall.a
	var b: Vector2 = wall.b
	var f := (b - a).normalized()
	var half: float = wall.thick * 0.5
	var n := Vector2(-f.y, f.x) * half
	return PackedVector2Array([a - n, b - n, b + n, a + n])


## Rectangle round `c`: `half_x` across `facing`, `half_y` along it, stretched `front` further
## towards `facing`. CCW.
static func _box(c: Vector2, facing: Vector2, half_x: float, half_y: float, front: float) -> PackedVector2Array:
	var y := facing.normalized()
	var x := Vector2(y.y, -y.x)
	var lo := -half_y
	var hi := half_y + front
	var poly := PackedVector2Array([c + x * -half_x + y * lo, c + x * half_x + y * lo,
		c + x * half_x + y * hi, c + x * -half_x + y * hi])
	if TownBlocks.signed_area(poly) < 0.0:
		poly.reverse()
	return poly


# -------------------------------------------------------------------------
# Kinds
# -------------------------------------------------------------------------

## One building on the frontage (河房, a shop, a small house), yard and wall behind.
static func _single(p: _Plan, use: int, level: int, rng: RandomNumberGenerator) -> void:
	var W := p.width
	var D := p.depth
	if W < 4.0 + 2.0 * PLATFORM:
		p.trees_in(Rect2(0.0, 0.0, W, D), 1, rng)
		return
	# Narrow parcels build wall to wall (硬山, shared gables); wide ones keep a passage each side.
	var side := 0.0
	var w := W - 2.0 * PLATFORM
	if w > 13.0:
		w = _q(minf(W - 2.0 * (SIDE + PLATFORM), 13.0))
		side = (W - w) * 0.5 - PLATFORM
	var roof := _house_roof(level, side, w)
	# The back eave has to stay over this parcel; on a shallow parcel a narrower house has a
	# shorter eave, so shrink before giving up.
	var eave_room := maxf(0.0, overhang(w, roof).y - PLATFORM)
	var d := _q(minf(rng.randf_range(6.0, 8.5), D - 2.0 * PLATFORM - eave_room))
	while d < 4.0 and w > 5.5:
		w = _q(w * 0.85)
		side = (W - w) * 0.5 - PLATFORM
		if side > 0.0 and side < SIDE:
			w = _q(W - 2.0 * (SIDE + PLATFORM))
			side = (W - w) * 0.5 - PLATFORM
		roof = _house_roof(level, side, w)
		eave_room = maxf(0.0, overhang(w, roof).y - PLATFORM)
		d = _q(minf(rng.randf_range(6.0, 8.5), D - 2.0 * PLATFORM - eave_room))
	if d < 4.0 or w < 4.0:
		p.trees_in(Rect2(0.0, 0.0, W, D), 1, rng)
		return
	var storeys := 2 if use == Use.SHOP and level >= 3 and rng.randf() < 0.3 else 1
	p.building(Vector2(W * 0.5, PLATFORM + d * 0.5), Vector2(0.0, -1.0), w, d,
		"shop" if use == Use.SHOP else "house", roof, level, storeys, false)
	var back := PLATFORM * 2.0 + d
	if D - back >= 3.0:
		p.enclose(Rect2(0.0, back, W, D - back), [], rng)
		p.trees_in(Rect2(0.6, back + 0.8, W - 1.2, D - back - 1.6), 1, rng)


## 前店后宅: a shop (or front house) on the street, a 天井 court, the dwelling behind facing it.
static func _shop_house(p: _Plan, use: int, level: int, rng: RandomNumberGenerator) -> bool:
	var W := p.width
	var D := p.depth
	if W < 4.0 + 2.0 * PLATFORM:
		return false
	var side := 0.0
	var w := W - 2.0 * PLATFORM
	if w > 13.0:
		w = _q(minf(W - 2.0 * (SIDE + PLATFORM), 13.0))
		side = (W - w) * 0.5 - PLATFORM
	var roof := _house_roof(level, side, w)
	var e := overhang(w, roof).y
	var d1 := _q(rng.randf_range(6.0, 7.5))
	var d2 := _q(rng.randf_range(6.0, 7.5))
	# The court has to clear both eaves (they face each other across it) and be walkable.
	var court := maxf(PASSAGE, 2.0 * e - 2.0 * PLATFORM + 0.2)
	court = maxf(court, rng.randf_range(3.0, 5.0))
	var need := 4.0 * PLATFORM + d1 + court + d2 + maxf(0.0, e - PLATFORM)
	if need > D:
		return false
	var storeys := 2 if level >= 3 and rng.randf() < (0.45 if use == Use.SHOP else 0.2) else 1
	p.building(Vector2(W * 0.5, PLATFORM + d1 * 0.5), Vector2(0.0, -1.0), w, d1,
		"shop" if use == Use.SHOP else "house", roof, level, storeys, false)
	var v2 := 2.0 * PLATFORM + d1 + court + PLATFORM + d2 * 0.5
	p.building(Vector2(W * 0.5, v2), Vector2(0.0, -1.0), w, d2, "dwelling", roof, level, 1, false)
	# Court walls run between the two platforms.
	var c0 := 2.0 * PLATFORM + d1
	var c1 := c0 + court
	if side <= 0.0:
		p.wall(Vector2(WALL_T * 0.5, c0), Vector2(WALL_T * 0.5, c1))
		p.wall(Vector2(W - WALL_T * 0.5, c0), Vector2(W - WALL_T * 0.5, c1))
	var back := 4.0 * PLATFORM + d1 + court + d2
	if D - back >= 3.0:
		p.enclose(Rect2(0.0, back, W, D - back), [], rng)
		p.trees_in(Rect2(0.6, back + 0.8, W - 1.2, D - back - 1.6), 1, rng)
	p.trees_in(Rect2(W * 0.3, c0 + 0.6, W * 0.4, court - 1.2), 1, rng)
	return true


## 三合院 (正房 + 两厢, gate in the street wall), or with a 倒座 on the street: 四合院.
static func _courtyard(p: _Plan, four: bool, level: int, rng: RandomNumberGenerator) -> bool:
	p.clear()
	var W := p.width
	var D := p.depth
	var main_w := _q(minf(W - 2.0 * (SIDE + PLATFORM), 14.0))
	if main_w < 7.0:
		return false
	var main_roof := _house_roof(level, SIDE, main_w)
	var main_d := _q(clampf(main_w * 0.62, 6.0, 8.5))
	var e_main := overhang(main_w, main_roof).y
	var back := maxf(SIDE, e_main - PLATFORM + 0.1)
	var main_steps := level >= 3
	var run := STEP_RUN * main_w if main_steps else 0.0

	var wing_d := 4.5
	var wing_w := _q(rng.randf_range(6.0, 8.0))
	var wing_roof := ROOF_FLUSH_GABLE if level <= 3 else ROOF_OVERHANGING
	var wing_o := overhang(wing_w, wing_roof)
	# Wings stand in front of the main hall: clear its front eave and its stair.
	var gap_main := maxf(PASSAGE, maxf(e_main + wing_o.x - 2.0 * PLATFORM, run) + 0.2)

	var front := WALL_T + PASSAGE
	var front_d := 0.0
	var front_w := 0.0
	var front_roof := ROOF_FLUSH_GABLE
	if four:
		front_w = _q(minf(W - 2.0 * (SIDE + PLATFORM) - GATE - PASSAGE, 13.0))
		front_d = 4.5
		if front_w < 6.0:
			return false
		# The 倒座 faces into the court; its eave there must clear the wings' gables.
		var e_front := overhang(front_w, front_roof).y
		front = 2.0 * PLATFORM + front_d + maxf(PASSAGE, e_front + wing_o.x - 2.0 * PLATFORM + 0.2)

	var v_main := D - back - PLATFORM - main_d * 0.5
	var main_front := v_main - main_d * 0.5 - PLATFORM - run
	var wing_hi := main_front - gap_main
	var wing_lo := wing_hi - (wing_w + 2.0 * PLATFORM)
	if wing_lo < front:
		# Shorten the wings before giving up.
		wing_w = _q(wing_hi - front - 2.0 * PLATFORM)
		wing_lo = wing_hi - (wing_w + 2.0 * PLATFORM)
		if wing_w < 4.5:
			return false
	var court_w := W - 2.0 * (SIDE + 2.0 * PLATFORM + wing_d)
	if court_w < 3.0:
		return false

	p.building(Vector2(W * 0.5, v_main), Vector2(0.0, -1.0), main_w, main_d, "main", main_roof, level, 1,
		main_steps)
	var wing_v := (wing_lo + wing_hi) * 0.5
	for s in [-1.0, 1.0]:
		var u: float = W * 0.5 + s * (W * 0.5 - SIDE - PLATFORM - wing_d * 0.5)
		p.building(Vector2(u, wing_v), Vector2(-s, 0.0), wing_w, wing_d, "wing", wing_roof, maxi(level - 1, 2), 1,
			false)

	var gate_u := W * 0.5
	var occupied: Array[Vector2] = []
	if four:
		# 倒座 against the street, the gate at the 巽 corner beside it.
		var fu := SIDE + PLATFORM + front_w * 0.5
		var right := rng.randf() < 0.5
		if right:
			fu = W - fu
		p.building(Vector2(fu, PLATFORM + front_d * 0.5), Vector2(0.0, 1.0), front_w, front_d, "front",
			front_roof, maxi(level - 1, 2), 1, false)
		occupied.append(Vector2(fu - front_w * 0.5 - PLATFORM, fu + front_w * 0.5 + PLATFORM))
		gate_u = W - SIDE - GATE * 0.5 if not right else SIDE + GATE * 0.5
	p.enclose(Rect2(0.0, 0.0, W, D), [Vector2(gate_u - GATE * 0.5, gate_u + GATE * 0.5)] + occupied, rng)
	p.props.append({"pos": p.at(Vector2(gate_u, 0.0)), "facing": p.dir(Vector2(0.0, -1.0)), "kind": "gate"})
	p.trees_in(Rect2(SIDE + 2.0 * PLATFORM + wing_d + 0.5, wing_lo + 0.5, court_w - 1.0, wing_w), 2, rng)
	if back > 3.0:
		p.trees_in(Rect2(1.0, D - back + 0.5, W - 2.0, back - 1.0), 1, rng)
	return true


## 官署: gate hall → court with 两庑 → main hall (reserved first) → rear hall, on one axis.
## Priority when the parcel is short: the main hall at full width, then the 两庑, then the rear
## hall; only when even hall + court will not fit does the hall shrink.
static func _official(p: _Plan, level: int, rng: RandomNumberGenerator) -> bool:
	var lv := clampi(level, 5, 6)
	var hall_w := _q(minf(p.width * 0.6, 18.0))
	var passes := [[true, true], [false, true], [true, false], [false, false]]   # [rear, sides]
	for attempt in 6:
		for option in passes:
			if _try_official(p, lv, hall_w, option[0], option[1], rng):
				return true
		hall_w = _q(hall_w * 0.88)
	return false


static func _try_official(p: _Plan, lv: int, hall_w: float, rear: bool, sides: bool,
		rng: RandomNumberGenerator) -> bool:
	var W := p.width
	var D := p.depth
	var hall_roof := ROOF_HIP if lv >= 6 else ROOF_GABLE_AND_HIP
	var gate_w := _q(minf(minf(W * 0.42, 12.0), hall_w * 0.75))
	var gate_d := 5.5
	var side_d := 5.0
	var rear_w := _q(minf(minf(W * 0.5, 14.0), hall_w * 0.8))
	var rear_d := 7.0
	var hall_d := _q(clampf(hall_w * 0.62, 8.0, 12.0))
	var e_hall := overhang(hall_w, hall_roof)
	var run_h := STEP_RUN * hall_w
	var back := maxf(SIDE, e_hall.y - PLATFORM + 0.1)
	var e_rear := overhang(rear_w, ROOF_GABLE_AND_HIP)
	var rear_block := (2.0 * PLATFORM + rear_d + maxf(PASSAGE + STEP_RUN * rear_w,
		e_hall.y + e_rear.y - 2.0 * PLATFORM + 0.2)) if rear else 0.0
	var gate_block := 1.0 + 2.0 * PLATFORM + gate_d
	var court := D - back - rear_block - (2.0 * PLATFORM + hall_d + run_h) - gate_block
	var e_gate := overhang(gate_w, ROOF_GABLE_AND_HIP).y
	if court < maxf(8.0, e_gate + e_hall.y + 2.0) or hall_w + 2.0 * (SIDE + PLATFORM) > W:
		return false
	var v_hall := D - back - rear_block - PLATFORM - hall_d * 0.5
	# 两庑 along the court, clear of both eaves and the hall stair (their gables at most 14 m wide).
	var side_gable := overhang(14.0, ROOF_OVERHANGING).x
	var c0 := gate_block + maxf(PASSAGE, e_gate + side_gable - 2.0 * PLATFORM + 0.2)
	var c1 := v_hall - hall_d * 0.5 - PLATFORM - run_h - maxf(PASSAGE, e_hall.y + side_gable - 2.0 * PLATFORM + 0.2)
	var side_w := _q(minf(c1 - c0 - 2.0 * PLATFORM, 14.0))
	var sides_fit := side_w >= 5.0 and W - 2.0 * (SIDE + 2.0 * PLATFORM + side_d) - hall_w >= -1.0e-3
	if sides and not sides_fit:
		return false

	p.clear()
	p.building(Vector2(W * 0.5, v_hall), Vector2(0.0, -1.0), hall_w, hall_d, "hall", hall_roof, lv, 1, true)
	if rear:
		p.building(Vector2(W * 0.5, D - back - PLATFORM - rear_d * 0.5), Vector2(0.0, -1.0), rear_w, rear_d, "rear",
			ROOF_GABLE_AND_HIP, lv - 1, 1, true)
	p.building(Vector2(W * 0.5, 1.0 + PLATFORM + gate_d * 0.5), Vector2(0.0, -1.0), gate_w, gate_d, "gate",
		ROOF_GABLE_AND_HIP, lv - 1, 1, false)
	if sides:
		for s in [-1.0, 1.0]:
			var u: float = W * 0.5 + s * (W * 0.5 - SIDE - PLATFORM - side_d * 0.5)
			p.building(Vector2(u, (c0 + c1) * 0.5), Vector2(-s, 0.0), side_w, side_d, "side",
				ROOF_OVERHANGING, lv - 2, 1, false)
	var gate_span := Vector2(W * 0.5 - gate_w * 0.5 - PLATFORM, W * 0.5 + gate_w * 0.5 + PLATFORM)
	p.enclose(Rect2(0.0, 0.0, W, D), [gate_span], rng, 1.0)
	p.trees_in(Rect2(W * 0.5 - hall_w * 0.5, gate_block + 1.0, hall_w, maxf(court - 2.0, 1.0)), 4, rng, true)
	return true


## 市 / 园: open ground with stalls along the frontage or trees.
static func _open(p: _Plan, use: int, rng: RandomNumberGenerator) -> void:
	var W := p.width
	var D := p.depth
	if use == Use.MARKET:
		var u := 1.5
		while u < W - 1.5:
			p.props.append({"pos": p.at(Vector2(u, 1.4)), "facing": p.dir(Vector2(0.0, -1.0)), "kind": "stall"})
			u += rng.randf_range(2.6, 3.6)
		p.props.append({"pos": p.at(Vector2(W * 0.5, D * 0.5)), "facing": p.dir(Vector2(0.0, -1.0)), "kind": "well"})
		p.trees_in(Rect2(1.0, D * 0.6, W - 2.0, D * 0.4 - 1.0), 2, rng)
	else:
		p.enclose(Rect2(0.0, 0.0, W, D), [Vector2(W * 0.5 - GATE * 0.5, W * 0.5 + GATE * 0.5)], rng)
		p.trees_in(Rect2(1.0, 1.0, W - 2.0, D - 2.0), maxi(2, roundi(W * D / 90.0)), rng)


## Building sizes land on a 0.5 m grid before anything is positioned from them: the Ancient
## Building node shares one baked mesh per 0.5 m size bin, so an off-grid size would be built at
## its bin's size and open or close the gaps planned here. Rounds down — a gap may only grow.
static func _q(x: float) -> float:
	return floorf(x * 2.0 + 1e-4) * 0.5


## 05 契约 roof for a dwelling at `level`, forced to 硬山 when it builds to a parcel edge (a
## shared gable has no overhang to spare) or when its gable overhang would leave the parcel.
static func _house_roof(level: int, side: float, w: float) -> int:
	var roof := ROOF_FLUSH_GABLE
	if level <= 2:
		roof = ROOF_ROUND_RIDGE
	elif level >= 4:
		roof = ROOF_OVERHANGING
	if overhang(w, roof).x > side + PLATFORM + GABLE_SHARE:
		roof = ROOF_FLUSH_GABLE
	return roof


## Accumulates one parcel's plan in its rectangle frame.
class _Plan:
	var width := 0.0
	var depth := 0.0
	var origin := Vector2.ZERO
	var f := Vector2.RIGHT
	var n := Vector2.UP
	var buildings: Array[Dictionary] = []
	var walls: Array[Dictionary] = []
	var trees := PackedVector2Array()
	var props: Array[Dictionary] = []
	var _local: Array[Rect2] = []   # ground footprints in the rect frame, for the wall runs

	func _init(rect: Dictionary) -> void:
		width = rect.width
		depth = rect.depth
		origin = rect.origin
		f = rect.f
		n = rect.n

	func at(q: Vector2) -> Vector2:
		return origin + f * q.x + n * q.y

	func dir(q: Vector2) -> Vector2:
		return (f * q.x + n * q.y).normalized()

	func clear() -> void:
		buildings.clear()
		walls.clear()
		trees.clear()
		props.clear()
		_local.clear()

	func building(c: Vector2, facing: Vector2, w: float, d: float, role: String, roof: int, level: int,
			storeys: int, steps: bool) -> void:
		# No size snapping here: positions were derived from these exact sizes, and a snapped size
		# turns a touching pair into a sliver. The variant key quantises downstream.
		buildings.append({"pos": at(c), "facing": dir(facing), "w": w, "d": d, "role": role, "roof": roof,
			"level": level, "storeys": storeys, "steps": steps})
		var run: float = TownCourtyards.STEP_RUN * w if steps else 0.0
		var hx := (w * 0.5 if absf(facing.y) > 0.5 else d * 0.5) + TownCourtyards.PLATFORM
		var hy := (d * 0.5 if absf(facing.y) > 0.5 else w * 0.5) + TownCourtyards.PLATFORM
		var r := Rect2(c - Vector2(hx, hy), Vector2(hx, hy) * 2.0)
		if steps:
			if facing.y < -0.5:
				r = r.grow_side(SIDE_TOP, run)
			elif facing.y > 0.5:
				r = r.grow_side(SIDE_BOTTOM, run)
			elif facing.x < -0.5:
				r = r.grow_side(SIDE_LEFT, run)
			else:
				r = r.grow_side(SIDE_RIGHT, run)
		_local.append(r)

	func wall(a: Vector2, b: Vector2) -> void:
		if a.distance_to(b) < 0.5:
			return
		walls.append({"a": at(a), "b": at(b), "thick": TownCourtyards.WALL_T, "height": TownCourtyards.WALL_H})

	## Walls round `area` (rect frame), just inside its edges, leaving `gaps` (u intervals on the
	## street edge v = area.position.y) and stopping at every building footprint touching an edge.
	func enclose(area: Rect2, gaps: Array, _rng: RandomNumberGenerator, street_inset: float = 0.0) -> void:
		var t := TownCourtyards.WALL_T
		var h := t * 0.5
		var x0 := area.position.x
		var x1 := area.end.x
		var y0 := area.position.y + street_inset
		var y1 := area.end.y
		# (fixed coordinate, from, to, axis) — axis 0 runs along u, 1 along v.
		var runs := [[y0 + h, x0, x1, 0, true], [y1 - h, x0, x1, 0, false],
			[x0 + h, y0, y1, 1, false], [x1 - h, y0, y1, 1, false]]
		for run in runs:
			var blocked: Array[Vector2] = []
			if run[4]:
				for g in gaps:
					blocked.append(g)
			for r in _local:
				var lo := r.position
				var hi := r.end
				if run[3] == 0:
					if run[0] + h > lo.y - 0.01 and run[0] - h < hi.y + 0.01:
						blocked.append(Vector2(lo.x, hi.x))
				else:
					if run[0] + h > lo.x - 0.01 and run[0] - h < hi.x + 0.01:
						blocked.append(Vector2(lo.y, hi.y))
			for span in _free(run[1], run[2], blocked):
				if run[3] == 0:
					wall(Vector2(span.x, run[0]), Vector2(span.y, run[0]))
				else:
					wall(Vector2(run[0], span.x), Vector2(run[0], span.y))

	## Up to `count` trees in `area` (rect frame), at least 2.5 m from any footprint.
	func trees_in(area: Rect2, count: int, rng: RandomNumberGenerator, paired: bool = false) -> void:
		if area.size.x < 1.0 or area.size.y < 1.0:
			return
		for i in count * 4:
			if count <= 0:
				return
			var q := area.position + Vector2(rng.randf() * area.size.x, rng.randf() * area.size.y)
			if paired:
				q.x = area.position.x + area.size.x * (0.2 if i % 2 == 0 else 0.8)
			var free := true
			for r in _local:
				if r.grow(2.0).has_point(q):
					free = false
					break
			if free:
				trees.append(at(q))
				count -= 1

	func result(kind: int) -> Dictionary:
		return {"kind": kind, "buildings": buildings, "walls": walls, "trees": trees, "props": props}

	static func _free(a: float, b: float, blocked: Array[Vector2]) -> Array[Vector2]:
		blocked.sort_custom(func(x: Vector2, y: Vector2) -> bool: return x.x < y.x)
		var out: Array[Vector2] = []
		var cur := a
		for span in blocked:
			if span.x > cur:
				out.append(Vector2(cur, minf(span.x, b)))
			cur = maxf(cur, span.y)
			if cur >= b:
				break
		if cur < b:
			out.append(Vector2(cur, b))
		var kept: Array[Vector2] = []
		for s in out:
			if s.y - s.x >= 0.5:
				kept.append(s)
		return kept
