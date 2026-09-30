@tool
extends "res://addons/ancient_town/nodes/town_lots.gd"

# Town Layout base — what every 街巷 / 院落 town node shares, whatever frame it lays out in:
# claims, the building parameter sets (_house / _add_storeyed), and the parcel → courtyard →
# stream pipeline over the layout library in addons/ancient_town/layout/.
#
# A subclass decides the layout frame by overriding `_world(side, q)`: layout point → world point
# with ground height. The default is flat world XZ at the settings origin. `side` is an opaque
# tag the subclass may use (the river town passes the bank).
#
# Streams beyond town_lots: Structures (Resource meshes: street surfaces and one-offs) and Yard
# Walls (院墙 unit segments), plus the per-lot structure fields (`ab_platform`, `ab_tile_color`,
# 多层 / 城台 / 亭台 streams) on Buildings — see `_pack_layout_buildings`.

const Meshes = preload("res://addons/ancient_town/town_meshes.gd")

const PROP_MERLON := 3
const PROP_BOAT := 4
const PROP_CARGO := 5
const PROP_FIGURE_A := 6
const PROP_FIGURE_B := 7
const PROP_STEPS := 8
const PROP_YARD_GATE := 9

const MAIN := TownStreetGraph.MAIN
const STREET := TownStreetGraph.STREET
const LANE := TownStreetGraph.LANE
const BOUNDARY := TownStreetGraph.BOUNDARY
const USE := TownCourtyards.Use

const TILE_GREY := Color(0.26, 0.29, 0.31)
const TILE_GLAZED := Color(0.22, 0.31, 0.40)   # gate tower, 衙署 hall
const TILE_RED := Color(0.56, 0.30, 0.21)      # riverside 重楼, bridgehead

var _structures: Array[Dictionary] = []
var _yard_walls: Array[Dictionary] = []
var _parcel_count := 0
# Footprints already taken: (x, z, radius).
var _claims := PackedVector3Array()


## Clears everything this base accumulates, plus town_lots' buffers.
func _reset_layout() -> void:
	_lots.clear()
	_roads.clear()
	_walls.clear()
	_props.clear()
	_trees.clear()
	_structures.clear()
	_yard_walls.clear()
	_parcel_count = 0
	_claims.clear()


## Rejects a footprint that overlaps an earlier one. Circles, because a curved layout frame
## turns every axis-aligned box at a bend.
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


## A hidden marker inside a masonry body, so a scene can still find a landmark by `town_role`
## now that the landmark itself is part of a building mesh.
func _add_marker(pos: Vector3, role: String) -> void:
	var box := BoxMesh.new()
	box.size = Vector3(0.05, 0.05, 0.05)
	_add_structure(pos + Vector3(0.0, 1.0, 0.0), 0.0, box, role)


## Layout point → world point, ground height included. Flat XZ at the settings origin here;
## subclasses with a curved frame or terrain override it.
func _world(_side: float, q: Vector2) -> Vector3:
	var o: Vector3 = settings.origin
	return Vector3(o.x + q.x, o.y, o.z + q.y)


## World direction of the layout direction `d` at `q`.
func _world_dir(side: float, q: Vector2, d: Vector2) -> Vector3:
	var w := _world(side, q + d * 0.5) - _world(side, q - d * 0.5)
	w.y = 0.0
	return w.normalized()


## A curved layout frame is not metric: in the river town, off the centreline an arc-length metre
## is (1 ± κ·offset) world metres, down to ~0.3 on the inside of a bend deep in the city. A parcel
## rectangle is therefore re-expressed in world metres before a courtyard is planned in it, taking
## the tightest scale anywhere on it, so a plan that fits the rectangle fits the ground — at worst
## a little loose on its stretched side, never overlapping its neighbour.
func _metric_rect(side: float, rect: Dictionary) -> Dictionary:
	var f: Vector2 = rect.f
	var n: Vector2 = rect.n
	var o: Vector2 = rect.origin
	var kf := INF
	var kn := INF
	for t in [0.0, 0.5, 1.0]:
		for r in [0.0, 1.0]:
			var q: Vector2 = o + f * rect.width * t + n * rect.depth * r
			kf = minf(kf, _stretch(side, q, f))
			kn = minf(kn, _stretch(side, q, n))
	var out := rect.duplicate()
	out["f"] = f / kf
	out["n"] = n / kn
	out["width"] = float(rect.width) * kf
	out["depth"] = float(rect.depth) * kn
	return out


## World metres per layout metre along `d` at `q`.
func _stretch(side: float, q: Vector2, d: Vector2) -> float:
	var a := _world(side, q - d * 0.5)
	var b := _world(side, q + d * 0.5)
	return maxf(Vector2(b.x - a.x, b.z - a.z).length(), 0.05)


static func _rect(u0: float, v0: float, u1: float, v1: float) -> PackedVector2Array:
	return PackedVector2Array([Vector2(u0, v0), Vector2(u1, v0), Vector2(u1, v1), Vector2(u0, v1)])


## Blocks → parcels → courtyards over one street graph. `reserved` plots (landmarks) are cut out of the
## blocks first, so no parcel builds over them. `use_of(parcel, rect, centre) -> Use` picks what a
## parcel is for; `decorate(plan)`, when valid, may adjust a plan before it is emitted.
func _fill_blocks(side: float, graph: TownStreetGraph, reserved: Array[PackedVector2Array], rules: Dictionary,
		levels: TownLevelField, use_of: Callable, decorate: Callable) -> void:
	var parcels: Array[Dictionary] = []
	for block in TownBlocks.blocks(graph):
		for piece in _cut_reserved(block, reserved):
			parcels.append_array(TownParcels.split(piece, TownBlocks.frontage_levels(piece, graph), rules, rng))
	# Largest first, so a "the biggest parcel on …" choice in use_of sees the big ones early.
	parcels.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return a.area > b.area)
	for parcel in parcels:
		var rect := TownParcels.usable_rect(parcel)
		if rect.is_empty() or rect.depth < 4.0:
			continue
		var centre: Vector2 = rect.origin + rect.f * rect.width * 0.5 + rect.n * rect.depth * 0.5
		rect = _metric_rect(side, rect)
		var use: int = use_of.call(parcel, rect, centre)
		var level := levels.level(centre, 2, 4)
		var kind := TownCourtyards.choose(rect, use, level)
		var plan := TownCourtyards.plan(rect, kind, use, level, rng)
		if decorate.is_valid():
			decorate.call(plan)
		_emit_courtyard(side, plan)
		_parcel_count += 1


## Subtracts the reserved plots from a block. A plot lying wholly inside would come back as a
## hole, which the rest of the pipeline cannot represent, so the block is first cut through the
## plot's centre and each half clipped on its own.
func _cut_reserved(block: PackedVector2Array, reserved: Array[PackedVector2Array]) -> Array[PackedVector2Array]:
	var pieces: Array[PackedVector2Array] = [block]
	for plot in reserved:
		var next: Array[PackedVector2Array] = []
		for piece in pieces:
			var parts := Geometry2D.clip_polygons(piece, plot)
			var holed := false
			for part in parts:
				if TownBlocks.signed_area(part) < 0.0:
					holed = true
			if holed:
				var c := Vector2.ZERO
				for q in plot:
					c += q / float(plot.size())
				var zero := PackedInt32Array()
				zero.resize(piece.size())
				parts = []
				for half in TownParcels.cut({"poly": piece, "front": zero}, c, Vector2(0.0, 1.0)):
					parts.append_array(Geometry2D.clip_polygons(half.poly, plot))
			for part in parts:
				if TownBlocks.signed_area(part) > 20.0:
					next.append(part)
		pieces = next
	return pieces


func _emit_courtyard(side: float, plan: Dictionary) -> void:
	for b in plan.buildings:
		_emit_building(side, b)
	for w in plan.walls:
		_emit_yard_wall(side, w.a, w.b, w.height, w.thick)
	for t in plan.trees:
		var tp := _world(side, t)
		if _claim(tp, 1.0):
			_add_tree(tp, rng.randf_range(0.7, 1.1))
	for pr in plan.props:
		var at := _world(side, pr.pos)
		var yaw := _yaw_to(_world_dir(side, pr.pos, pr.facing))
		match pr.kind:
			"gate":
				_add_prop(at, yaw, PROP_YARD_GATE)
			"stall":
				if _claim(at, 1.0):
					_add_prop(at, yaw, PROP_STALL)
			"well":
				if _claim(at, 1.0):
					_add_prop(at, yaw, PROP_WELL)


func _emit_building(side: float, b: Dictionary) -> void:
	# On the terrace slope a building stands at its lowest corner: the uphill side sinks into the
	# hill (a terraced house), nothing floats.
	var y := INF
	for q in TownCourtyards.ground_footprint(b):
		y = minf(y, _world(side, q).y)
	var pos := _world(side, b.pos)
	pos.y = y
	var yaw := _yaw_to(_world_dir(side, b.pos, b.facing))
	# The planner already sized on the 0.5 m grid the variant key bins by; flooring again only
	# catches the few sizes it derives from a parcel edge (a row house wall to wall).
	var w := floorf(float(b.w) * 2.0 + 1e-4) * 0.5
	var d := floorf(float(b.d) * 2.0 + 1e-4) * 0.5
	var level := clampi(int(b.level) - 1, 1, 5)
	var material := MAT_EARTHEN if int(b.level) <= 2 and rng.randf() < 0.2 else MAT_TRADITIONAL
	var p := _house(level, w, d, b.roof, material)
	p["steps"] = b.steps
	p["fence_lambda"] = 0
	if w >= 13.0:
		p["bays_x"] = 5
	var lot := LOT_RESIDENCE
	match b.role:
		"shop":
			lot = LOT_SHOP
		"hall":
			lot = LOT_LANDMARK
			p["fence"] = true
			p["tile_color"] = TILE_GLAZED
		"rear", "gate", "side":
			lot = LOT_LANDMARK
	_claim(pos, 0.5 * minf(w, d))
	if int(b.storeys) > 1:
		_add_storeyed(pos, yaw, w, d, b.roof, b.storeys, b.get("tile", TILE_GREY), level, lot,
			{ "storey_setback": 0, "storey_balcony": b.get("balcony", false), "upper_column_scale": 0.8,
				"fence": false, "fence_lambda": 0, "steps": b.steps })
	else:
		_add_lot(pos, yaw, level, lot, 0, p)


## A 院墙 run as ≤ 4 m pieces, each standing on the lower of its two ends and buried a little, so
## the wall steps down a slope instead of hanging off it.
func _emit_yard_wall(side: float, a: Vector2, b: Vector2, height: float, thick: float) -> void:
	var length := a.distance_to(b)
	var pieces := maxi(ceili(length / 4.0), 1)
	var bury := 0.4
	for k in pieces:
		var wa := _world(side, a.lerp(b, float(k) / float(pieces)))
		var wb := _world(side, a.lerp(b, float(k + 1) / float(pieces)))
		var base := minf(wa.y, wb.y) - bury
		var top := maxf(wa.y, wb.y) + height
		var mid := (wa + wb) * 0.5
		var run := Vector2(wb.x - wa.x, wb.z - wa.z).length()
		_yard_walls.append({"pos": Vector3(mid.x, (base + top) * 0.5, mid.z), "yaw": _road_yaw(wb - wa),
			"len": run + 0.04, "height": top - base, "thick": thick})


func _emit_streets(side: float, graph: TownStreetGraph) -> void:
	var to_world := func(q: Vector2) -> Vector3: return _world(side, q)
	_add_structure(Vector3.ZERO, 0.0, Meshes.street_surface(graph, to_world))


func _pack_layout_buildings() -> FlowData.Data:
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
