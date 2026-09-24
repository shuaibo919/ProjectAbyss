extends RefCounted

# Procedural brush textures for ink_mesh_edge_outline.gdshader. Two kinds:
#   brush_tex          256x64, x runs ALONG the stroke (u), y ACROSS it (v),
#                      R = coverage (below alpha_cut is discarded)
#   width_profile_tex  64x1, x = normalized stroke position 0..1, R = width scale
# Any imported PNG/JPG works in either slot as long as it follows these
# conventions — these generators just save the roundtrip.

const WIDTH := 256
const HEIGHT := 64


# Bristle brush: high-frequency streaks across the stroke become hairs, a slow
# wander along it becomes dry-brush gaps.
static func make_brush_texture(seed: int = 7) -> ImageTexture:
	var streaks := FastNoiseLite.new()
	streaks.noise_type = FastNoiseLite.TYPE_SIMPLEX
	streaks.seed = seed

	var gaps := FastNoiseLite.new()
	gaps.noise_type = FastNoiseLite.TYPE_SIMPLEX
	gaps.seed = seed * 31 + 5

	var img := Image.create(WIDTH, HEIGHT, false, Image.FORMAT_L8)
	for y in HEIGHT:
		var v := float(y) / HEIGHT
		for x in WIDTH:
			var u := float(x) / WIDTH
			# Bristles: vary fast across the stroke, slow along it.
			var bristle := streaks.get_noise_2d(u * 3.0, v * 40.0) * 0.5 + 0.5
			# Dry-brush gaps: a slow wander along the stroke.
			var gap := gaps.get_noise_2d(u * 5.0, 0.0) * 0.5 + 0.5
			var value: float = clamp(bristle * (0.4 + 0.6 * gap) + 0.15, 0.0, 1.0)
			img.set_pixel(x, y, Color(value, value, value))
	return ImageTexture.create_from_image(img)


# Speckled "burnt ink" (焦墨) brush: sparse grains that mostly hold the line but
# pepper it with holes.
static func make_grain_texture(seed: int = 21) -> ImageTexture:
	var grains := FastNoiseLite.new()
	grains.noise_type = FastNoiseLite.TYPE_SIMPLEX
	grains.seed = seed

	var img := Image.create(WIDTH, HEIGHT, false, Image.FORMAT_L8)
	for y in HEIGHT:
		var v := float(y) / HEIGHT
		for x in WIDTH:
			var u := float(x) / WIDTH
			var n := grains.get_noise_2d(u * 24.0, v * 18.0) * 0.5 + 0.5
			# Sharpen into sparse speckles.
			var value: float = clamp((n - 0.35) * 4.0, 0.0, 1.0)
			img.set_pixel(x, y, Color(value, value, value))
	return ImageTexture.create_from_image(img)


# Width profiles (x = normalized stroke position, R = width scale 0..1):
#   taper_both  pointed at both ends, full in the middle (classic 收笔)
#   tail        full at the head, thinning to a dry tail
#   pulse       alternating thick/thin, like a shaking hand
static func make_width_profile(kind: StringName) -> ImageTexture:
	const LENGTH := 64
	var img := Image.create(LENGTH, 1, false, Image.FORMAT_L8)
	for x in LENGTH:
		var t := float(x) / (LENGTH - 1)
		var value: float
		match kind:
			&"taper_both":
				value = sin(t * PI)
			&"tail":
				value = 1.0 - smoothstep(0.3, 1.0, t)
			&"pulse":
				value = 0.55 + 0.45 * sin(t * TAU * 3.0)
			_:
				value = 1.0
		img.set_pixel(x, 0, Color(value, value, value))
	return ImageTexture.create_from_image(img)
