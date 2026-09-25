shader_type spatial;

// Mesh-edge cartoon outline with Pencil+4-style stroke parameterization.
// CartoonOutlineBuilder bakes six vertices (one quad) per unique mesh edge into
// CUSTOM0-3 AND chains edges into strokes (brush paths) at bake time, so every
// fragment knows its position along the stroke (arc length u), its offset across
// the line (v), the stroke length and a per-stroke seed — the data Pencil+4
// recovers per pixel with CPU soft rasterization, here baked for free.
//
// Baked layout (see CartoonOutlineBuilder):
//   CUSTOM0 = (edge vertex A.xyz, end_t)     end_t: 0 = endpoint A, 1 = endpoint B
//   CUSTOM1 = (edge vertex B.xyz, side)      side: +1 / -1 across the line
//   CUSTOM2 = (face A third vertex.xyz, has_face_b)
//   CUSTOM3 = (face B third vertex.xyz, unused)
//   UV      = (arc distance along stroke, side)
//   UV2     = (stroke length — negative for closed loops, stroke seed 0..1)
//
// Antialiasing: all coverage decisions are analytic alpha, never hard discard —
// the quad is expanded in NDC so v_side is ~screen-linear and the distance to the
// line boundary is computable in pixels; brush thresholding is softened over the
// pixel footprint (fwidth). alpha_to_coverage maps that alpha onto MSAA samples,
// which keeps depth_draw_opaque semantics (no blending, no sorting). Needs MSAA
// on the viewport (msaa_3d); without MSAA the lines render fully opaque, i.e.
// the pre-AA look.

render_mode unshaded, cull_disabled, depth_draw_opaque, shadows_disabled, alpha_to_coverage;

uniform vec4 line_color : source_color = vec4(0.02, 0.02, 0.02, 1.0);
// Full line width in screen pixels (constant at any distance).
uniform float line_width_px : hint_range(0.5, 16.0) = 2.0;
// End-cap extension as a fraction of line_width_px, so strokes meeting at a vertex
// overlap instead of leaving pinholes. Only visible when end_taper is 0.
uniform float end_extend : hint_range(0.0, 2.0) = 0.5;
// cos of the minimum dihedral angle treated as a crease. 0.5 = 60 degrees, the
// Unity demo's hardcoded threshold.
uniform float crease_cos_threshold : hint_range(-1.0, 1.0) = 0.5;
// Boundary edges (one adjacent face) always drawing is right for closed meshes,
// but wrong for stacked-open-shell geometry like AncientBuilding, where open
// panel rims are everywhere by design and would all ink black.
uniform bool draw_boundary_edges = true;
// NDC pull toward the camera so the line wins the depth test against its own
// surface (replaces the Unity demo's 0.001 normal offset). Godot 4 uses reverse-Z:
// near = 1, far = 0, so "toward the camera" is +z.
uniform float depth_bias : hint_range(0.0, 0.01) = 0.0002;

// === Stroke brush (Pencil+4-style; all inert at defaults) ===
// 收笔: taper the line to a point over this many world units at each stroke end.
// 0 = off. Closed loops never taper (no stroke ends).
uniform float end_taper = 0.0;
// Per-stroke width variation, ±fraction from the seed.
uniform float width_jitter : hint_range(0.0, 1.0) = 0.0;
// Brush texture sampled with (u / tile_size + seed offset, v) — streaks in the
// texture's x direction run ALONG the stroke. brush_amount 0 = solid line.
uniform sampler2D brush_tex : hint_default_white, filter_linear_mipmap;
uniform float brush_amount : hint_range(0.0, 1.0) = 0.0;
uniform float brush_tile_size = 1.0;
// 飞白: coverage below this threshold is discarded. Raise to dry the brush out.
uniform float alpha_cut : hint_range(0.0, 1.0) = 0.5;
// Width profile along the stroke: sampled at (u_normalized, 0.5), R = width scale
// (0..1). Default white = uniform width. This is Pencil+4's freeform width curve —
// e.g. thin head / thick belly / pointed tail is a smooth bump texture.
uniform sampler2D width_profile_tex : hint_default_white, filter_linear;

// === Line LOD (anti-moire; off at 0) ===
// Caps the line width in world units. Once the capped width drops below one
// pixel, the line stays at one pixel and fades out via alpha instead — the line
// version of mipmapping, and the only real fix for line-spacing moire (distant
// roof tiles): AA cannot reconstruct undersampled strokes.
uniform float max_world_width = 0.0;

varying float v_u;    // arc distance along the stroke (world units)
varying float v_side; // -1..1 across the line
varying float v_len;  // total stroke length, negative for closed loops
varying float v_seed; // per-stroke random 0..1
varying float v_half_width_px; // post-jitter, post-LOD half width in pixels
varying float v_alpha_lod;     // sub-pixel fade factor from the LOD cap

void vertex() {
	vec3 pa = CUSTOM0.xyz;
	float end_t = CUSTOM0.w;
	vec3 pb = CUSTOM1.xyz;
	float side = CUSTOM1.w;
	vec3 face_a = CUSTOM2.xyz;
	bool has_face_b = CUSTOM2.w > 0.5;
	vec3 face_b = CUSTOM3.xyz;

	v_u = UV.x;
	v_side = UV.y;
	v_len = UV2.x;
	v_seed = UV2.y;
	v_half_width_px = 1.0;
	v_alpha_lod = 1.0;

	vec3 va = (MODELVIEW_MATRIX * vec4(pa, 1.0)).xyz;
	vec3 vb = (MODELVIEW_MATRIX * vec4(pb, 1.0)).xyz;

	// Boundary edges (one adjacent face) draw unconditionally unless disabled.
	bool is_edge = draw_boundary_edges;
	if (has_face_b) {
		vec3 fa = (MODELVIEW_MATRIX * vec4(face_a, 1.0)).xyz;
		vec3 fb = (MODELVIEW_MATRIX * vec4(face_b, 1.0)).xyz;

		vec3 centroid = (va + vb + fa) / 3.0;
		// View space: the camera sits at the origin.
		vec3 view_dir = normalize(-centroid);

		vec3 na = normalize(cross(vb - va, fa - va));
		vec3 nb = normalize(cross(fb - va, vb - va));

		// Silhouette: the two faces straddle the view ray.
		bool is_silhouette = dot(na, view_dir) * dot(nb, view_dir) < 0.0;
		bool is_crease = dot(na, nb) < crease_cos_threshold;
		is_edge = is_silhouette || is_crease;
	}

	vec4 clip_a = PROJECTION_MATRIX * vec4(va, 1.0);
	vec4 clip_b = PROJECTION_MATRIX * vec4(vb, 1.0);

	if (!is_edge) {
		// Collapse the quad to a single point; zero-area triangles are culled.
		// (No early return: Godot forbids `return` in processor functions.)
		POSITION = vec4(0.0, 0.0, 0.0, 1.0);
	} else {
		vec3 ndc_a = clip_a.xyz / clip_a.w;
		vec3 ndc_b = clip_b.xyz / clip_b.w;

		vec2 dir = ndc_b.xy - ndc_a.xy;
		float len = length(dir);
		vec2 dir_n = len > 1e-6 ? dir / len : vec2(1.0, 0.0);
		vec2 perp = vec2(-dir_n.y, dir_n.x);

		// Per-stroke width jitter, from the baked seed.
		float jittered_width = line_width_px * (1.0 + (v_seed - 0.5) * width_jitter);

		float w_corner = end_t < 0.5 ? clip_a.w : clip_b.w;

		// Line LOD: world_per_px = world units per pixel at this depth
		// (ndc = view.y * P[1][1] / -z_view, and NDC spans 2 units vertically).
		if (max_world_width > 0.0) {
			float world_per_px = w_corner * 2.0 / (VIEWPORT_SIZE.y * PROJECTION_MATRIX[1][1]);
			float capped_px = min(jittered_width, max_world_width / world_per_px);
			// Sub-pixel: clamp to 1px, fade via alpha instead (premultiplied filter).
			v_alpha_lod = clamp(capped_px, 0.0, 1.0);
			jittered_width = max(capped_px, 1.0);
		}
		v_half_width_px = jittered_width * 0.5;

		// Pixel-constant width: NDC spans 2 units across the viewport.
		vec2 px_to_ndc = 2.0 / VIEWPORT_SIZE;
		vec2 half_w = perp * (jittered_width * 0.5) * px_to_ndc;
		vec2 ext = dir_n * (jittered_width * end_extend) * px_to_ndc * (end_t < 0.5 ? -1.0 : 1.0);

		vec3 ndc = end_t < 0.5 ? ndc_a : ndc_b;
		ndc.xy += half_w * side + ext;
		ndc.z += depth_bias;

		POSITION = vec4(ndc.xy * w_corner, (ndc.z) * w_corner, w_corner);
	}
}

void fragment() {
	// Width shaping. The quad was expanded at full width in vertex(), so both the
	// end taper and the width profile reduce the effective half width here.
	float width_mask = 1.0;
	// 收笔: shrink the stroke width toward its ends. Loops (negative length) have
	// no ends and skip this.
	if (end_taper > 0.0 && v_len > 0.0) {
		float d_end = min(v_u, v_len - v_u);
		width_mask *= smoothstep(0.0, end_taper, d_end);
	}
	// Freeform width profile along the stroke (normalized u; wraps on loops).
	if (abs(v_len) > 1e-5) {
		float u_norm = clamp(v_u / abs(v_len), 0.0, 1.0);
		width_mask *= texture(width_profile_tex, vec2(u_norm, 0.5)).r;
	}

	// Analytic edge coverage instead of discard: 1 unit of v_side spans
	// v_half_width_px pixels, so the pixel distance to the (shaped) boundary is
	// exact up to the quad's screen-linearity.
	float edge_dist_px = (width_mask - abs(v_side)) * v_half_width_px;
	float alpha_edge = clamp(edge_dist_px + 0.5, 0.0, 1.0);

	// Brush texture / 飞白: streaks run along the stroke, offset by the per-stroke
	// seed so parallel strokes don't share the same bristle pattern. The threshold
	// is softened over the pixel footprint — fwidth is a free analytic prefilter.
	float brush = mix(1.0, texture(brush_tex, vec2(v_u / brush_tile_size + v_seed * 7.13,
			v_side * 0.5 + 0.5)).r, brush_amount);
	float brush_w = max(fwidth(brush), 1e-4);
	float alpha_brush = smoothstep(alpha_cut - brush_w, alpha_cut + brush_w, brush);

	float coverage = alpha_edge * alpha_brush * v_alpha_lod;
	if (coverage <= 0.0) {
		discard;
	}

	ALBEDO = line_color.rgb;
	ALPHA = coverage;
}
