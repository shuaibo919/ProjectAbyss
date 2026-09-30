@tool
class_name TownParcels
extends RefCounted

## Parcels (地块) cut from a block, every one keeping a street frontage.
##
## Recursive oriented splitting in the spirit of Vanegas et al. 2012 ("Procedural generation of
## parcels in urban modeling"), with the frame taken from the parcel's main frontage instead of
## its minimum bounding box, because what has to end up square is the house to its street, not
## the parcel to itself:
##   - wider than `width_max` along the frontage → cut across it, at a bay-quantised width;
##   - deeper than `depth_max` and fronting a street at the back too → cut along it (a through
##     lot becomes two);
##   - a cut that would leave a side without frontage, or a sliver, is not made.
##
## A piece is {poly: PackedVector2Array (CCW), front: PackedInt32Array} where front[i] is the
## street level along edge i (vertex i → i+1), -1 for none. Cut edges carry -1.

const EDGE_MATCH := 0.02
const FAR := 1.0e4


## Default rules; callers merge their own over these.
static func rules(overrides: Dictionary = {}) -> Dictionary:
	var r := {
		"width_min": 6.0,        # narrowest frontage worth a house
		"width_max": 12.0,
		"depth_max": 30.0,
		"bay": 3.6,              # 开间, frontage widths snap to multiples of it
		"min_area": 30.0,
		"frontage_min": 3.0,     # a child needs this much street edge to count as fronting
	}
	r.merge(overrides, true)
	return r


static func split(poly: PackedVector2Array, front: PackedInt32Array, r: Dictionary, rng: RandomNumberGenerator) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	_split({"poly": poly, "front": front}, r, rng, out, 0)
	for piece in out:
		_describe(piece)
	return out


static func _split(piece: Dictionary, r: Dictionary, rng: RandomNumberGenerator, out: Array[Dictionary], depth: int) -> void:
	var frame := main_frontage(piece)
	if frame.is_empty() or depth > 24:
		out.append(piece)
		return
	var f: Vector2 = frame.f
	var n: Vector2 = frame.n
	var o: Vector2 = frame.origin
	var ext := _extent(piece.poly, o, f, n)
	var width: float = ext.size.x
	var deep: float = ext.size.y

	if width > r.width_max:
		var bay: float = r.bay
		var target := rng.randf_range(r.width_min, r.width_max)
		target = maxf(bay, roundf(target / bay) * bay)
		if width - target < r.width_min:
			target = width * 0.5
		var at := o + f * (ext.position.x + target)
		var kids := cut(piece, at, n)
		if _all_front(kids, r):
			for k in kids:
				_split(k, r, rng, out, depth + 1)
			return

	if deep > r.depth_max and _fronts_back(piece, n):
		var at := o + n * (ext.position.y + deep * 0.5)
		var kids := cut(piece, at, f)
		if _all_front(kids, r):
			for k in kids:
				_split(k, r, rng, out, depth + 1)
			return

	out.append(piece)


## The frontage the house faces: the most important street, then the longest straight run.
## {origin, f (along the street), n (into the parcel), level, length}, or {} for an inner piece.
static func main_frontage(piece: Dictionary) -> Dictionary:
	var poly: PackedVector2Array = piece.poly
	var front: PackedInt32Array = piece.front
	var best := {}
	for i in poly.size():
		if front[i] < 0:
			continue
		var a := poly[i]
		var b := poly[(i + 1) % poly.size()]
		var length := a.distance_to(b)
		if length < 1e-3:
			continue
		# Straight neighbours on the same street count towards the run (round corners are many
		# short edges).
		var f := (b - a) / length
		var run := length
		for step in [-1, 1]:
			var j := i
			while true:
				j = (j + step + poly.size()) % poly.size()
				if j == i or front[j] != front[i]:
					break
				var c := poly[j]
				var d := poly[(j + 1) % poly.size()]
				var lj := c.distance_to(d)
				if lj < 1e-3 or (d - c).dot(f) / lj < 0.985:
					break
				run += lj
		var score := -float(front[i]) * 1.0e5 + run
		if best.is_empty() or score > best.score:
			# CCW polygon: the interior is on the left of each edge.
			best = {"origin": a, "f": f, "n": Vector2(-f.y, f.x), "level": front[i], "length": run, "score": score}
	return best


## Splits a piece by the line through `at` along `dir`; returns the non-empty sides.
static func cut(piece: Dictionary, at: Vector2, dir: Vector2) -> Array[Dictionary]:
	var d := dir.normalized()
	var side := Vector2(-d.y, d.x)
	var out: Array[Dictionary] = []
	for s in [1.0, -1.0]:
		var half := PackedVector2Array([
			at - d * FAR, at + d * FAR,
			at + d * FAR + side * s * FAR, at - d * FAR + side * s * FAR])
		if TownBlocks.signed_area(half) < 0.0:
			half.reverse()
		for part in Geometry2D.intersect_polygons(piece.poly, half):
			if TownBlocks.signed_area(part) <= 0.0:
				continue
			out.append({"poly": part, "front": _inherit(part, piece)})
	return out


## Largest frontage-anchored rectangle inside the parcel, in its frontage frame:
## {origin, f, n, width, depth}. The house plan is laid out in this rectangle.
static func usable_rect(parcel: Dictionary) -> Dictionary:
	var frame := main_frontage(parcel)
	if frame.is_empty():
		return {}
	var f: Vector2 = frame.f
	var n: Vector2 = frame.n
	var ext := _extent(parcel.poly, frame.origin, f, n)
	var o: Vector2 = frame.origin + f * ext.position.x + n * ext.position.y
	var u0 := 0.0
	var u1 := ext.size.x
	# Shrink the width until a shallow strip fits (round curb corners, skewed side cuts).
	var shallow := minf(1.0, ext.size.y)
	for i in 12:
		if _fits(parcel.poly, o, f, n, u0, u1, shallow):
			break
		u0 += 0.25
		u1 -= 0.25
	if u1 - u0 < 1.0:
		return {}
	var lo := 0.0
	var hi := ext.size.y
	for i in 18:
		var mid := (lo + hi) * 0.5
		if _fits(parcel.poly, o, f, n, u0, u1, mid):
			lo = mid
		else:
			hi = mid
	return {"origin": o + f * u0, "f": f, "n": n, "width": u1 - u0, "depth": lo, "level": frame.level}


static func area(poly: PackedVector2Array) -> float:
	return TownBlocks.signed_area(poly)


static func _fits(poly: PackedVector2Array, o: Vector2, f: Vector2, n: Vector2, u0: float, u1: float, v: float) -> bool:
	var rect := PackedVector2Array([o + f * u0, o + f * u1, o + f * u1 + n * v, o + f * u0 + n * v])
	var outside := 0.0
	for part in Geometry2D.clip_polygons(rect, poly):
		outside += absf(TownBlocks.signed_area(part))
	return outside <= 0.01 * (u1 - u0) * maxf(v, 0.1)


static func _extent(poly: PackedVector2Array, o: Vector2, f: Vector2, n: Vector2) -> Rect2:
	var lo := Vector2(INF, INF)
	var hi := Vector2(-INF, -INF)
	for p in poly:
		var q := Vector2((p - o).dot(f), (p - o).dot(n))
		lo = Vector2(minf(lo.x, q.x), minf(lo.y, q.y))
		hi = Vector2(maxf(hi.x, q.x), maxf(hi.y, q.y))
	return Rect2(lo, hi - lo)


static func _all_front(kids: Array[Dictionary], r: Dictionary) -> bool:
	if kids.size() < 2:
		return false
	for k in kids:
		if area(k.poly) < r.min_area:
			return false
		if _frontage_length(k) < r.frontage_min:
			return false
	return true


static func _frontage_length(piece: Dictionary) -> float:
	var poly: PackedVector2Array = piece.poly
	var total := 0.0
	for i in poly.size():
		if piece.front[i] >= 0:
			total += poly[i].distance_to(poly[(i + 1) % poly.size()])
	return total


## True when some frontage faces back along `n` (a street behind the parcel too).
static func _fronts_back(piece: Dictionary, n: Vector2) -> bool:
	var poly: PackedVector2Array = piece.poly
	for i in poly.size():
		if piece.front[i] < 0:
			continue
		var a := poly[i]
		var b := poly[(i + 1) % poly.size()]
		var len_ab := a.distance_to(b)
		if len_ab < 2.0:
			continue
		var fi := (b - a) / len_ab
		var ni := Vector2(-fi.y, fi.x)
		if ni.dot(n) < -0.9:
			return true
	return false


## Edge attributes of a clipped part: an edge lying along a parent edge keeps its level, a new
## one (the cut) gets -1.
static func _inherit(part: PackedVector2Array, parent: Dictionary) -> PackedInt32Array:
	var src: PackedVector2Array = parent.poly
	var out := PackedInt32Array()
	for i in part.size():
		var a := part[i]
		var b := part[(i + 1) % part.size()]
		var m := (a + b) * 0.5
		var level := -1
		for j in src.size():
			var c := src[j]
			var d := src[(j + 1) % src.size()]
			if Geometry2D.get_closest_point_to_segment(m, c, d).distance_to(m) <= EDGE_MATCH \
					and Geometry2D.get_closest_point_to_segment(a, c, d).distance_to(a) <= EDGE_MATCH:
				level = parent.front[j]
				break
		out.append(level)
	return out


static func _describe(piece: Dictionary) -> void:
	piece["area"] = area(piece.poly)
	var frame := main_frontage(piece)
	piece["level"] = frame.get("level", -1)
	piece["frontage"] = frame.get("length", 0.0)
