@tool
class_name TownStreetGraph
extends RefCounted

## A planar street network in a 2D layout plane (the town's local frame, not world space).
##
## Every mutation keeps the graph planar: `insert_segment` splits whatever it crosses, and
## points closer than `weld` merge. That invariant is what lets `TownBlocks` read the blocks
## straight off the faces — a crossing that is not a node would silently fuse two blocks.
##
## Edges are never erased in place (ids stay stable while callers hold them); a split kills the
## old edge and appends two new ones. Iterate with `alive_edges()`.

## Street hierarchy. Lower is more important; widths come from the caller.
const MAIN := 0        # 大街: gate axes, the cross street
const STREET := 1      # 街 / 河街
const LANE := 2        # 巷
const ALLEY := 3       # 弄 / 备弄
## Not a street: closes a block where the town meets water, a wall or open country, so the face
## exists. Never paved, never frontage.
const BOUNDARY := 9

const CELL := 16.0

var weld := 0.05
var points := PackedVector2Array()
var edge_a := PackedInt32Array()
var edge_b := PackedInt32Array()
var edge_level := PackedInt32Array()
var edge_width := PackedFloat32Array()
var edge_alive := PackedByteArray()

var _adjacent: Array[PackedInt32Array] = []
var _edge_cells := {}
var _point_cells := {}


func point_count() -> int:
	return points.size()


func edge_count() -> int:
	return edge_a.size()


func is_alive(e: int) -> bool:
	return edge_alive[e] != 0


func alive_edges() -> PackedInt32Array:
	var out := PackedInt32Array()
	for e in edge_a.size():
		if edge_alive[e] != 0:
			out.append(e)
	return out


func edges_at(p: int) -> PackedInt32Array:
	return _adjacent[p]


func degree(p: int) -> int:
	return _adjacent[p].size()


func other_end(e: int, p: int) -> int:
	return edge_b[e] if edge_a[e] == p else edge_a[e]


func edge_length(e: int) -> float:
	return points[edge_a[e]].distance_to(points[edge_b[e]])


## Returns the point within `weld` of `p`, or adds a new one.
func add_point(p: Vector2) -> int:
	var found := find_point(p, weld)
	if found >= 0:
		return found
	points.append(p)
	_adjacent.append(PackedInt32Array())
	var id := points.size() - 1
	var key := _cell(p)
	var bucket: PackedInt32Array = _point_cells.get(key, PackedInt32Array())
	bucket.append(id)
	_point_cells[key] = bucket
	return id


func find_point(p: Vector2, radius: float) -> int:
	var best := -1
	var best_d := radius
	var c := _cell(p)
	var reach := int(ceil(radius / CELL))
	for dx in range(-reach, reach + 1):
		for dz in range(-reach, reach + 1):
			for id in _point_cells.get(c + Vector2i(dx, dz), PackedInt32Array()):
				var d := points[id].distance_to(p)
				if d <= best_d:
					best_d = d
					best = id
	return best


## Connects two existing points. The caller guarantees the segment crosses nothing — use
## `insert_segment` when it might.
func add_edge(a: int, b: int, level: int, width: float) -> int:
	if a == b:
		return -1
	var existing := find_edge(a, b)
	if existing >= 0:
		# A second, more important street over the same line upgrades the first.
		if level < edge_level[existing]:
			edge_level[existing] = level
		edge_width[existing] = maxf(edge_width[existing], width)
		return existing
	edge_a.append(a)
	edge_b.append(b)
	edge_level.append(level)
	edge_width.append(width)
	edge_alive.append(1)
	var e := edge_a.size() - 1
	_adjacent[a].append(e)
	_adjacent[b].append(e)
	for key in _cells_of(points[a], points[b]):
		var bucket: PackedInt32Array = _edge_cells.get(key, PackedInt32Array())
		bucket.append(e)
		_edge_cells[key] = bucket
	return e


func find_edge(a: int, b: int) -> int:
	for e in _adjacent[a]:
		if other_end(e, a) == b:
			return e
	return -1


## Splits edge `e` at `p` (projected onto it) and returns the new point.
func split_edge(e: int, p: Vector2) -> int:
	var a := edge_a[e]
	var b := edge_b[e]
	var q := Geometry2D.get_closest_point_to_segment(p, points[a], points[b])
	if q.distance_to(points[a]) <= weld:
		return a
	if q.distance_to(points[b]) <= weld:
		return b
	var level := edge_level[e]
	var width := edge_width[e]
	_kill(e)
	var m := add_point(q)
	add_edge(a, m, level, width)
	add_edge(m, b, level, width)
	return m


## Inserts the segment p→q, splitting every street it crosses and every street whose end
## lies on it. Returns the chain of points along the new street.
func insert_segment(p: Vector2, q: Vector2, level: int, width: float) -> PackedInt32Array:
	var cuts: Array[Vector2] = []    # (t along p→q, point id)
	var a := _point_on_network(p)
	var b := _point_on_network(q)
	cuts.append(Vector2(0.0, a))
	cuts.append(Vector2(1.0, b))
	var length := p.distance_to(q)
	if length <= weld:
		return PackedInt32Array([a])
	for e in edges_near(p, q, weld):
		if edge_alive[e] == 0:
			continue
		var ea := points[edge_a[e]]
		var eb := points[edge_b[e]]
		var hit = Geometry2D.segment_intersects_segment(p, q, ea, eb)
		if hit != null:
			var t: float = (hit as Vector2).distance_to(p) / length
			if t > 1e-4 and t < 1.0 - 1e-4:
				cuts.append(Vector2(t, split_edge(e, hit)))
	# Ends of existing streets that touch the new one without crossing it.
	for id in points_near(p, q, weld):
		var on := Geometry2D.get_closest_point_to_segment(points[id], p, q)
		if on.distance_to(points[id]) <= weld:
			var t := on.distance_to(p) / length
			if t > 1e-4 and t < 1.0 - 1e-4:
				cuts.append(Vector2(t, id))
	cuts.sort_custom(func(u: Vector2, v: Vector2) -> bool: return u.x < v.x)
	var chain := PackedInt32Array()
	for cut in cuts:
		var id := int(cut.y)
		if chain.is_empty() or chain[chain.size() - 1] != id:
			chain.append(id)
	for i in range(1, chain.size()):
		add_edge(chain[i - 1], chain[i], level, width)
	return chain


## Candidate edges whose cells overlap the segment's bounds grown by `margin`.
func edges_near(p: Vector2, q: Vector2, margin: float) -> PackedInt32Array:
	var seen := {}
	var out := PackedInt32Array()
	var lo := Vector2(minf(p.x, q.x), minf(p.y, q.y)) - Vector2(margin, margin)
	var hi := Vector2(maxf(p.x, q.x), maxf(p.y, q.y)) + Vector2(margin, margin)
	var c0 := _cell(lo)
	var c1 := _cell(hi)
	for cx in range(c0.x, c1.x + 1):
		for cz in range(c0.y, c1.y + 1):
			for e in _edge_cells.get(Vector2i(cx, cz), PackedInt32Array()):
				if edge_alive[e] != 0 and not seen.has(e):
					seen[e] = true
					out.append(e)
	return out


func points_near(p: Vector2, q: Vector2, margin: float) -> PackedInt32Array:
	var out := PackedInt32Array()
	var c0 := _cell(Vector2(minf(p.x, q.x), minf(p.y, q.y)) - Vector2(margin, margin))
	var c1 := _cell(Vector2(maxf(p.x, q.x), maxf(p.y, q.y)) + Vector2(margin, margin))
	for cx in range(c0.x, c1.x + 1):
		for cz in range(c0.y, c1.y + 1):
			out.append_array(_point_cells.get(Vector2i(cx, cz), PackedInt32Array()))
	return out


## Nearest point on any street within `radius` of `p`: {edge, point, distance}, or {} if none.
func nearest_on_edges(p: Vector2, radius: float) -> Dictionary:
	var best := {}
	var best_d := radius
	for e in edges_near(p, p, radius):
		var q := Geometry2D.get_closest_point_to_segment(p, points[edge_a[e]], points[edge_b[e]])
		var d := q.distance_to(p)
		if d <= best_d:
			best_d = d
			best = {"edge": e, "point": q, "distance": d}
	return best


## First street crossed by the open segment p→q, ignoring streets incident to `from_point`:
## {edge, point, t}, or {} if none.
func first_hit(p: Vector2, q: Vector2, from_point: int) -> Dictionary:
	var best := {}
	var best_t := 2.0
	var length := p.distance_to(q)
	if length <= 0.0:
		return best
	for e in edges_near(p, q, 0.0):
		if from_point >= 0 and (edge_a[e] == from_point or edge_b[e] == from_point):
			continue
		var hit = Geometry2D.segment_intersects_segment(p, q, points[edge_a[e]], points[edge_b[e]])
		if hit == null:
			continue
		var t: float = (hit as Vector2).distance_to(p) / length
		# A street through p itself is where we leave from, not something we run into.
		if t < 1e-3:
			continue
		if t < best_t:
			best_t = t
			best = {"edge": e, "point": hit, "t": t}
	return best


## True when no two live edges cross away from a shared point. O(E·k) with the cell hash.
func is_planar() -> bool:
	return crossing_count() == 0


func crossing_count() -> int:
	var count := 0
	for e in alive_edges():
		var pa := points[edge_a[e]]
		var pb := points[edge_b[e]]
		for f in edges_near(pa, pb, 0.0):
			if f <= e:
				continue
			if edge_a[f] == edge_a[e] or edge_a[f] == edge_b[e] or edge_b[f] == edge_a[e] or edge_b[f] == edge_b[e]:
				continue
			if Geometry2D.segment_intersects_segment(pa, pb, points[edge_a[f]], points[edge_b[f]]) != null:
				count += 1
	return count


## Number of connected components among points that carry at least one street.
func component_count() -> int:
	var seen := PackedByteArray()
	seen.resize(points.size())
	var count := 0
	for start in points.size():
		if seen[start] != 0 or _adjacent[start].is_empty():
			continue
		count += 1
		var stack := [start]
		seen[start] = 1
		while not stack.is_empty():
			var p: int = stack.pop_back()
			for e in _adjacent[p]:
				var o := other_end(e, p)
				if seen[o] == 0:
					seen[o] = 1
					stack.append(o)
	return count


## Removes streets shorter than `min_length` that end in nothing (a degree-1 stub left by a
## rejected continuation), repeatedly.
func prune_stubs(min_length: float) -> void:
	var changed := true
	while changed:
		changed = false
		for e in alive_edges():
			var da := degree(edge_a[e])
			var db := degree(edge_b[e])
			if (da == 1 or db == 1) and edge_length(e) < min_length:
				_kill(e)
				changed = true


## The point at `p`: an existing one within `weld`, a split of the street under it, or new.
func attach_point(p: Vector2) -> int:
	return _point_on_network(p)


func _point_on_network(p: Vector2) -> int:
	var id := find_point(p, weld)
	if id >= 0:
		return id
	var near := nearest_on_edges(p, weld)
	if not near.is_empty():
		return split_edge(near.edge, p)
	return add_point(p)


func _kill(e: int) -> void:
	edge_alive[e] = 0
	for p in [edge_a[e], edge_b[e]]:
		var list: PackedInt32Array = _adjacent[p]
		var at := list.find(e)
		if at >= 0:
			list.remove_at(at)
		_adjacent[p] = list


func _cell(p: Vector2) -> Vector2i:
	return Vector2i(floori(p.x / CELL), floori(p.y / CELL))


func _cells_of(p: Vector2, q: Vector2) -> Array[Vector2i]:
	var out: Array[Vector2i] = []
	var c0 := _cell(Vector2(minf(p.x, q.x), minf(p.y, q.y)))
	var c1 := _cell(Vector2(maxf(p.x, q.x), maxf(p.y, q.y)))
	for cx in range(c0.x, c1.x + 1):
		for cz in range(c0.y, c1.y + 1):
			out.append(Vector2i(cx, cz))
	return out
