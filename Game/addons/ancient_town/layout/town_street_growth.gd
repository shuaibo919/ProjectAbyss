@tool
class_name TownStreetGrowth
extends RefCounted

## Street network growth — Qin et al. 2023's SE L-System ("Procedural modeling and layout
## method for a generic ancient Chinese city", MTAP 83), run in the Parish–Müller queue form
## rather than as string rewriting. The two are the same machine: a module R(del, …) is a queued
## proposal whose delay orders it, globalGoals makes the proposals, localConstraints edits or
## drops them. The queue is simply easier to debug.
##
## What Qin adds over Müller, all implemented here:
##   - gridF (Eq 1): a proposal's end snaps to a square grid, E = round(E0/scale)·scale, so the
##     network keeps to 横平竖直 with a little local irregularity;
##   - perpendicular welding (Fig 5): a road that crosses or nearly reaches another one is
##     re-aimed to meet it square, instead of being cut or extended where it happens to land;
##   - mirror generation about an axis (§4.2.2), with road pruning by level and a symmetry factor
##     SYM ∈ [0, 1] (§4.2.3, Table 4): a road of level `iter − 2 ≥ SYM·10` loses its mirror when
##     `dice > SYM`, and so do its descendants; both sides then regrow on their own. SYM = 1 is a
##     mirror image, SYM = 0 an asymmetric town.
##
## Everything happens in the town's 2D layout frame — for the river town that is (arc length,
## bank offset), so "grid" means "square to the river". The caller maps to world at the end.

const MAX_ITEMS := 4000

## Per-level growth rules, indexed by TownStreetGraph level.
var level_rules: Array[Dictionary] = [
	{"width": 9.0, "length": Vector2(24.0, 36.0), "branch": 0.9, "delay": 1, "same_level": 0.0},
	{"width": 6.0, "length": Vector2(18.0, 30.0), "branch": 0.7, "delay": 3, "same_level": 0.15},
	{"width": 3.2, "length": Vector2(12.0, 24.0), "branch": 0.45, "delay": 6, "same_level": 0.5},
	{"width": 1.6, "length": Vector2(8.0, 14.0), "branch": 0.0, "delay": 9, "same_level": 1.0},
]
var graph: TownStreetGraph
var zone := PackedVector2Array()                   ## CCW; streets stay inside
var obstacles: Array[PackedVector2Array] = []      ## reserved ground no street may enter
var grid := 6.0
var axis := 0.0                                    ## mirror line u = axis
var symmetric := true
var sym := 0.6
var max_level := TownStreetGraph.LANE
var min_length := 7.0
var snap_radius := 5.0
var min_spacing := 11.0
var min_angle := deg_to_rad(40.0)
var max_segments := 300

var _queue: Array[Dictionary] = []
var _serial := 0
var _segments := 0
var rng: RandomNumberGenerator


func _init(target: TownStreetGraph, random: RandomNumberGenerator) -> void:
	graph = target
	rng = random


## Queues a road leaving point `from` (already on the network) in direction `dir`.
func add_seed(from: Vector2, dir: Vector2, level: int, mirrored: bool = true) -> void:
	var id := graph.attach_point(from)
	_push(0.0, id, dir.normalized(), level, 0, mirrored and symmetric)


func grow() -> void:
	var guard := MAX_ITEMS
	while not _queue.is_empty() and guard > 0 and _segments < max_segments:
		guard -= 1
		_step(_pop())


## Perpendicular lanes off a straight spine v = `v0` running to v = `v1`, spaced along u between
## `u0` and `u1` — the 河街 comb: lanes square to the river, each ending at the water.
func comb(u0: float, u1: float, v0: float, v1: float, spacing: Vector2, level: int) -> PackedFloat32Array:
	var at := PackedFloat32Array()
	var u := u0 + rng.randf_range(spacing.x, spacing.y) * 0.5
	var width: float = level_rules[level].width
	while u < u1 - spacing.x * 0.3:
		var p := Vector2(u, v0)
		var q := Vector2(u, v1)
		if not _blocked(p, q, width * 0.5):
			graph.insert_segment(p, q, level, width)
			at.append(u)
		u += rng.randf_range(spacing.x, spacing.y)
	return at


## Fraction of streets whose mirror image about the axis is also a street — 1 for a perfect
## mirror. Streets on the axis count as their own mirror.
func symmetry_score() -> float:
	var total := 0.0
	var matched := 0.0
	for e in graph.alive_edges():
		var a := graph.points[graph.edge_a[e]]
		var b := graph.points[graph.edge_b[e]]
		var length := a.distance_to(b)
		total += length
		var m := mirror((a + b) * 0.5)
		var near := graph.nearest_on_edges(m, 0.5)
		if not near.is_empty():
			matched += length
	return matched / maxf(total, 1e-3)


func mirror(p: Vector2) -> Vector2:
	return Vector2(2.0 * axis - p.x, p.y)


func _push(t: float, from: int, dir: Vector2, level: int, iter: int, mirrored: bool) -> void:
	_serial += 1
	_queue.append({"t": t, "k": _serial, "from": from, "dir": dir, "level": level, "iter": iter,
		"symm": mirrored})


func _pop() -> Dictionary:
	var best := 0
	for i in range(1, _queue.size()):
		var a: Dictionary = _queue[i]
		var b: Dictionary = _queue[best]
		if a.t < b.t or (a.t == b.t and a.k < b.k):
			best = i
	var item: Dictionary = _queue[best]
	_queue.remove_at(best)
	return item


func _step(item: Dictionary) -> void:
	var from: int = item.from
	var p := graph.points[from]
	var dir: Vector2 = item.dir
	var level: int = item.level
	var rule: Dictionary = level_rules[level]
	var length := rng.randf_range(rule.length.x, rule.length.y)

	# globalGoals + gridF: the end snaps to the grid, square to the axis.
	var e := _snap(p + dir * length)
	if (e - p).length() < min_length or (e - p).normalized().dot(dir) < 0.9:
		e = p + dir * maxf(length, min_length)

	# A mirrored road grows on the near side of the axis only; its image covers the other.
	if item.symm and dir.x > 1e-3 and e.x > axis:
		if p.x >= axis - 1e-3:
			return
		e = p + dir * ((axis - p.x) / dir.x)

	var proposal := _constrain(from, p, e, rule.width)
	if proposal.is_empty():
		return
	var end: Vector2 = proposal.end
	var chain := graph.insert_segment(p, end, level, rule.width)
	_segments += 1
	var end_id := chain[chain.size() - 1]

	if item.symm and absf(p.x - axis) + absf(end.x - axis) > 1e-3:
		var mp := mirror(p)
		var me := mirror(end)
		var mirrored := _constrain(graph.find_point(mp, graph.weld), mp, me, rule.width)
		if not mirrored.is_empty():
			graph.insert_segment(mp, mirrored.end, level, rule.width)
			_segments += 1

	if proposal.terminal:
		return

	var iter: int = item.iter + 1
	# Road pruning (Qin Eq 2, Table 4 p1–p3): the level of the road is its iteration less two.
	var child_symm: bool = item.symm
	if child_symm and float(iter - 2) >= sym * 10.0 and rng.randf() > sym:
		child_symm = false
		# The pruned side regrows on its own from the mirrored end (Table 4 p2).
		var twin := graph.find_point(mirror(end), graph.weld)
		if twin >= 0:
			_push(item.t + 1.0, twin, Vector2(-dir.x, dir.y), level, iter, false)

	_push(item.t + 1.0, end_id, dir, level, iter, child_symm)
	for side in [-1.0, 1.0]:
		if rng.randf() >= rule.branch:
			continue
		var branch_level := level
		if level < max_level and rng.randf() >= rule.same_level:
			branch_level = level + 1
		var perp: Vector2 = Vector2(-dir.y, dir.x) * side
		_push(item.t + float(level_rules[branch_level].delay), end_id, perp, branch_level, iter, child_symm)


func _snap(q: Vector2) -> Vector2:
	var local := q - Vector2(axis, 0.0)
	return Vector2(roundf(local.x / grid) * grid + axis, roundf(local.y / grid) * grid)


## localConstraints. Returns {end, terminal} or {} when the proposal is dropped.
func _constrain(from: int, p: Vector2, e: Vector2, width: float) -> Dictionary:
	if not Geometry2D.is_point_in_polygon(p, zone) and _zone_distance(p) > 0.5:
		return {}
	var terminal := false

	# Leaving the zone: stop at its edge (the boundary streets are in the graph already, so this
	# only matters where the zone has no street, e.g. against the river or a hill).
	var exit = _zone_exit(p, e)
	if exit != null:
		e = exit
		terminal = true

	# Crossing a road: meet it at the foot of the perpendicular when that is on the road (Fig 5a),
	# else where it crosses.
	var hit := graph.first_hit(p, e, from)
	if not hit.is_empty():
		e = _square_to(p, hit.edge, hit.point)
		terminal = true
	else:
		# Nearly reaching a road or a junction (Fig 5b/c): weld to it, square where possible.
		var node := graph.find_point(e, snap_radius)
		if node >= 0 and node != from:
			e = graph.points[node]
			terminal = true
		else:
			var near := graph.nearest_on_edges(e, snap_radius)
			if not near.is_empty() and graph.edge_a[near.edge] != from and graph.edge_b[near.edge] != from:
				e = _square_to(p, near.edge, near.point)
				terminal = true

	if p.distance_to(e) < min_length:
		return {}
	if _blocked(p, e, width * 0.5):
		return {}
	# Whatever the end was moved to, the road must reach it without crossing anything.
	var across := graph.first_hit(p, e, from)
	if not across.is_empty() and across.t < 0.98:
		return {}
	if not _angles_ok(from, p, e):
		return {}
	if _too_close(from, p, e):
		return {}
	return {"end": e, "terminal": terminal}


## The point on edge `edge` where a road from `p` meets it square, if that lands on the edge
## within reach of `fallback`; else `fallback`.
func _square_to(p: Vector2, edge: int, fallback: Vector2) -> Vector2:
	var a := graph.points[graph.edge_a[edge]]
	var b := graph.points[graph.edge_b[edge]]
	var foot := Geometry2D.get_closest_point_to_segment(p, a, b)
	var ab := b - a
	var t := (foot - a).dot(ab) / maxf(ab.length_squared(), 1e-6)
	if t > 0.02 and t < 0.98 and foot.distance_to(fallback) <= snap_radius * 2.0:
		return foot
	return fallback


func _angles_ok(from: int, p: Vector2, e: Vector2) -> bool:
	var d := (e - p).normalized()
	if from >= 0:
		for edge in graph.edges_at(from):
			var o := graph.points[graph.other_end(edge, from)]
			if absf(d.angle_to((o - p).normalized())) < min_angle:
				return false
	var end := graph.find_point(e, graph.weld)
	if end >= 0:
		for edge in graph.edges_at(end):
			var o := graph.points[graph.other_end(edge, end)]
			if absf((-d).angle_to((o - e).normalized())) < min_angle:
				return false
	else:
		var near := graph.nearest_on_edges(e, graph.weld * 4.0)
		if not near.is_empty():
			var a := graph.points[graph.edge_a[near.edge]]
			var b := graph.points[graph.edge_b[near.edge]]
			var along := absf(d.dot((b - a).normalized()))
			if along > cos(min_angle):
				return false
	return true


## A parallel street closer than `min_spacing` would leave a block too thin to build on.
func _too_close(from: int, p: Vector2, e: Vector2) -> bool:
	var d := (e - p).normalized()
	var mid := (p + e) * 0.5
	var length := p.distance_to(e)
	for edge in graph.edges_near(p, e, min_spacing):
		if from >= 0 and (graph.edge_a[edge] == from or graph.edge_b[edge] == from):
			continue
		var a := graph.points[graph.edge_a[edge]]
		var b := graph.points[graph.edge_b[edge]]
		var dir := (b - a).normalized()
		if absf(dir.dot(d)) < 0.94:
			continue
		# Distance between the two lines where they overlap along their common direction.
		var s0 := (a - p).dot(d)
		var s1 := (b - p).dot(d)
		if maxf(s0, s1) < 0.5 or minf(s0, s1) > length - 0.5:
			continue
		var q := Geometry2D.get_closest_point_to_segment(mid, a, b)
		var gap := absf((q - p).dot(Vector2(-d.y, d.x)))
		if gap < min_spacing:
			return true
	return false


func _blocked(p: Vector2, e: Vector2, half: float) -> bool:
	for poly in obstacles:
		if Geometry2D.is_point_in_polygon(e, poly) or Geometry2D.is_point_in_polygon((p + e) * 0.5, poly):
			return true
		for i in poly.size():
			if Geometry2D.segment_intersects_segment(p, e, poly[i], poly[(i + 1) % poly.size()]) != null:
				return true
		# A street skimming a reserved plot still eats its frontage.
		for q in poly:
			if Geometry2D.get_closest_point_to_segment(q, p, e).distance_to(q) < half:
				return true
	return false


func _zone_exit(p: Vector2, e: Vector2):
	var best = null
	var best_d := INF
	for i in zone.size():
		var hit = Geometry2D.segment_intersects_segment(p, e, zone[i], zone[(i + 1) % zone.size()])
		if hit != null:
			var d := p.distance_to(hit)
			if d > 1e-3 and d < best_d:
				best_d = d
				best = hit
	return best


func _zone_distance(p: Vector2) -> float:
	var best := INF
	for i in zone.size():
		best = minf(best, Geometry2D.get_closest_point_to_segment(p, zone[i], zone[(i + 1) % zone.size()]).distance_to(p))
	return best
