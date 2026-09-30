@tool
class_name TownPlanImage
extends RefCounted

## Plan-view debug raster for town layouts: filled polygons and stroked segments in a layout
## frame, written to PNG. Headless-safe (Image only, no viewport), so layout tests and tools can
## drop a picture next to their numbers.

var image: Image
var origin := Vector2.ZERO
var scale := 2.0          ## pixels per metre


func _init(bounds: Rect2, pixels_per_metre: float = 2.0, background: Color = Color(0.96, 0.94, 0.88)) -> void:
	scale = pixels_per_metre
	origin = bounds.position
	var size := Vector2i(ceili(bounds.size.x * scale) + 1, ceili(bounds.size.y * scale) + 1)
	image = Image.create(maxi(size.x, 1), maxi(size.y, 1), false, Image.FORMAT_RGBA8)
	image.fill(background)


func to_pixel(p: Vector2) -> Vector2:
	# Flip v so +v (inland / north) is up in the picture.
	return Vector2((p.x - origin.x) * scale, image.get_height() - 1 - (p.y - origin.y) * scale)


func fill_polygon(poly: PackedVector2Array, color: Color) -> void:
	if poly.size() < 3:
		return
	var px := PackedVector2Array()
	var lo := Vector2(INF, INF)
	var hi := Vector2(-INF, -INF)
	for p in poly:
		var q := to_pixel(p)
		px.append(q)
		lo = Vector2(minf(lo.x, q.x), minf(lo.y, q.y))
		hi = Vector2(maxf(hi.x, q.x), maxf(hi.y, q.y))
	var y0 := clampi(floori(lo.y), 0, image.get_height() - 1)
	var y1 := clampi(ceili(hi.y), 0, image.get_height() - 1)
	for y in range(y0, y1 + 1):
		var yc := float(y) + 0.5
		var xs: Array[float] = []
		for i in px.size():
			var a := px[i]
			var b := px[(i + 1) % px.size()]
			if (a.y <= yc and b.y > yc) or (b.y <= yc and a.y > yc):
				xs.append(a.x + (yc - a.y) / (b.y - a.y) * (b.x - a.x))
		xs.sort()
		for k in range(0, xs.size() - 1, 2):
			var x0 := clampi(roundi(xs[k]), 0, image.get_width() - 1)
			var x1 := clampi(roundi(xs[k + 1]) - 1, 0, image.get_width() - 1)
			for x in range(x0, x1 + 1):
				image.set_pixel(x, y, color)


func stroke_polygon(poly: PackedVector2Array, color: Color, width_px: float = 1.0) -> void:
	for i in poly.size():
		line(poly[i], poly[(i + 1) % poly.size()], color, width_px)


func line(a: Vector2, b: Vector2, color: Color, width_px: float = 1.0) -> void:
	var pa := to_pixel(a)
	var pb := to_pixel(b)
	var steps := maxi(ceili(pa.distance_to(pb)), 1)
	var r := maxi(roundi(width_px * 0.5) - 1, 0)
	for s in steps + 1:
		var q := pa.lerp(pb, float(s) / float(steps))
		for dx in range(-r, r + 1):
			for dy in range(-r, r + 1):
				var x := roundi(q.x) + dx
				var y := roundi(q.y) + dy
				if x >= 0 and y >= 0 and x < image.get_width() and y < image.get_height():
					image.set_pixel(x, y, color)


func dot(p: Vector2, color: Color, radius_px: int = 2) -> void:
	var q := to_pixel(p)
	for dx in range(-radius_px, radius_px + 1):
		for dy in range(-radius_px, radius_px + 1):
			var x := roundi(q.x) + dx
			var y := roundi(q.y) + dy
			if dx * dx + dy * dy <= radius_px * radius_px and x >= 0 and y >= 0 \
					and x < image.get_width() and y < image.get_height():
				image.set_pixel(x, y, color)


## Streets at their true width, darker for more important ones (drawn last, so they read on top).
## Boundary edges are a thin dashed-looking brown line.
func streets(graph: TownStreetGraph) -> void:
	for e in graph.alive_edges():
		if graph.edge_level[e] >= TownStreetGraph.BOUNDARY:
			line(graph.points[graph.edge_a[e]], graph.points[graph.edge_b[e]], Color(0.55, 0.4, 0.3), 1.0)
	for level in range(TownStreetGraph.ALLEY, TownStreetGraph.MAIN - 1, -1):
		for e in graph.alive_edges():
			if graph.edge_level[e] != level:
				continue
			var shade := 0.25 + 0.15 * float(level)
			line(graph.points[graph.edge_a[e]], graph.points[graph.edge_b[e]], Color(shade, shade, shade),
				maxf(1.0, graph.edge_width[e] * scale))


func save(path: String) -> Error:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	return image.save_png(path)
