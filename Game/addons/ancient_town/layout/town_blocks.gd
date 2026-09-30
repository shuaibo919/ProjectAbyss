@tool
class_name TownBlocks
extends RefCounted

## Blocks (街区 / 坊) read off a planar `TownStreetGraph`.
##
## A block is a bounded face of the street graph with every street's corridor (half its width
## either side, round ends) subtracted. Subtracting corridors rather than insetting the face
## edge by edge is what makes the three awkward cases fall out for free: streets of different
## widths around one block, dead-end lanes poking into it (they notch it instead of being
## ignored), and junctions where streets meet at an angle (round ends close the wedge a butt
## end would leave on the outside of the bend; the block corner itself stays square).
##
## Polygons are counter-clockwise (positive shoelace area) throughout.

const MIN_FACE_AREA := 4.0


## Bounded faces as CCW loops of point ids, dead-end spurs removed.
static func face_loops(graph: TownStreetGraph) -> Array[PackedInt32Array]:
	# Incident edges of every point, sorted counter-clockwise by direction.
	var order: Array[PackedInt32Array] = []
	for p in graph.point_count():
		var around: Array = []
		for e in graph.edges_at(p):
			var o := graph.other_end(e, p)
			var d := graph.points[o] - graph.points[p]
			around.append([atan2(d.y, d.x), o])
		around.sort_custom(func(x: Array, y: Array) -> bool: return x[0] < y[0])
		var ids := PackedInt32Array()
		for entry in around:
			ids.append(entry[1])
		order.append(ids)

	var used := {}
	var loops: Array[PackedInt32Array] = []
	for e in graph.alive_edges():
		for dir in 2:
			var u := graph.edge_a[e] if dir == 0 else graph.edge_b[e]
			var v := graph.other_end(e, u)
			if used.has(Vector2i(u, v)):
				continue
			var loop := PackedInt32Array()
			var su := u
			var sv := v
			var guard := graph.edge_count() * 2 + 4
			while guard > 0:
				guard -= 1
				used[Vector2i(su, sv)] = true
				loop.append(su)
				# Leave v by the edge next clockwise from the one we arrived on: the face stays
				# on the left, so bounded faces come out counter-clockwise.
				var ring: PackedInt32Array = order[sv]
				var at := ring.find(su)
				var w := ring[(at - 1 + ring.size()) % ring.size()]
				su = sv
				sv = w
				if su == u and sv == v:
					break
			var clean := _drop_spurs(loop)
			if clean.size() >= 3 and _loop_area(graph, clean) > MIN_FACE_AREA:
				loops.append(clean)
	return loops


static func face_polygons(graph: TownStreetGraph) -> Array[PackedVector2Array]:
	var out: Array[PackedVector2Array] = []
	for loop in face_loops(graph):
		var poly := PackedVector2Array()
		for id in loop:
			poly.append(graph.points[id])
		out.append(poly)
	return out


## Every face minus the street corridors. A face may break into several blocks (a lane running
## right through it); pieces smaller than `min_area` are dropped.
static func blocks(graph: TownStreetGraph, min_area: float = 20.0) -> Array[PackedVector2Array]:
	var corridors: Array = []
	var bounds: Array[Rect2] = []
	for e in graph.alive_edges():
		var line := PackedVector2Array([graph.points[graph.edge_a[e]], graph.points[graph.edge_b[e]]])
		var half := graph.edge_width[e] * 0.5
		for poly in Geometry2D.offset_polyline(line, half, Geometry2D.JOIN_ROUND, Geometry2D.END_ROUND):
			corridors.append(poly)
			bounds.append(_bounds(poly))

	var out: Array[PackedVector2Array] = []
	for face in face_polygons(graph):
		var pieces: Array[PackedVector2Array] = [face]
		var face_box := _bounds(face)
		for i in corridors.size():
			if not face_box.intersects(bounds[i]):
				continue
			var next: Array[PackedVector2Array] = []
			for piece in pieces:
				for part in Geometry2D.clip_polygons(piece, corridors[i]):
					# Clipper hands holes back with the opposite winding. A hole here would need a
					# street island inside the face, which a connected network cannot have.
					if signed_area(part) > 0.0:
						next.append(part)
			pieces = next
		for piece in pieces:
			if signed_area(piece) >= min_area:
				out.append(piece)
	return out


## Street level fronting each edge of `poly` (edge i runs from vertex i to i+1), or -1 where the
## block edge faces no street (a wall, the river, the town boundary).
static func frontage_levels(poly: PackedVector2Array, graph: TownStreetGraph, slack: float = 0.35) -> PackedInt32Array:
	var out := PackedInt32Array()
	for i in poly.size():
		var m := (poly[i] + poly[(i + 1) % poly.size()]) * 0.5
		var best := -1
		var best_d := INF
		for e in graph.edges_near(m, m, 8.0):
			if graph.edge_level[e] >= TownStreetGraph.BOUNDARY:
				continue
			var q := Geometry2D.get_closest_point_to_segment(m, graph.points[graph.edge_a[e]], graph.points[graph.edge_b[e]])
			var d := q.distance_to(m) - graph.edge_width[e] * 0.5
			if d <= slack and d < best_d:
				best_d = d
				best = graph.edge_level[e]
		out.append(best)
	return out


static func signed_area(poly: PackedVector2Array) -> float:
	var a := 0.0
	for i in poly.size():
		var p := poly[i]
		var q := poly[(i + 1) % poly.size()]
		a += p.x * q.y - q.x * p.y
	return a * 0.5


static func _loop_area(graph: TownStreetGraph, loop: PackedInt32Array) -> float:
	var a := 0.0
	for i in loop.size():
		var p := graph.points[loop[i]]
		var q := graph.points[loop[(i + 1) % loop.size()]]
		a += p.x * q.y - q.x * p.y
	return a * 0.5


## Removes out-and-back runs (… a, b, a …) left where a face walks round a dead end.
static func _drop_spurs(loop: PackedInt32Array) -> PackedInt32Array:
	var ids := loop.duplicate()
	var changed := true
	while changed and ids.size() >= 3:
		changed = false
		var n := ids.size()
		for i in n:
			if ids[(i - 1 + n) % n] == ids[(i + 1) % n]:
				# Drop the tip and one copy of its base.
				var tip := i
				var base := (i + 1) % n
				if tip > base:
					ids.remove_at(tip)
					ids.remove_at(base)
				else:
					ids.remove_at(base)
					ids.remove_at(tip)
				changed = true
				break
	return ids


static func _bounds(poly: PackedVector2Array) -> Rect2:
	var r := Rect2(poly[0], Vector2.ZERO)
	for p in poly:
		r = r.expand(p)
	return r
