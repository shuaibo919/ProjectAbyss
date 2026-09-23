shader_type spatial;

// Mesh-edge cartoon outline — Godot port of UnitySimpleCartoonLine
// (Reference/UnitySimpleCartoonLine), baked variant: CartoonOutlineBuilder bakes
// six vertices (one quad) per unique mesh edge into CUSTOM0-3, this shader does the
// silhouette/crease classification and the clip-space quad expansion in vertex()
// (Godot has no geometry shaders; the Unity original expanded in a GS — the math
// here is its per-vertex equivalent).
//
// Complements ink_outline_0.gdshader (inverted hull): that one only catches
// silhouettes, this one also draws interior crease/boundary lines — roof ridges,
// tile seams, plank borders. Feed it a mesh from
// CartoonOutlineBuilder.build_outline_mesh(source_mesh); on a plain mesh it reads
// garbage CUSTOM data and draws noise.
//
// Expected vertex packing (set by CartoonOutlineBuilder):
//   CUSTOM0 = (edge vertex A.xyz, end_t)     end_t: 0 = endpoint A, 1 = endpoint B
//   CUSTOM1 = (edge vertex B.xyz, side)      side: +1 / -1 across the line
//   CUSTOM2 = (face A third vertex.xyz, has_face_b)
//   CUSTOM3 = (face B third vertex.xyz, unused)

render_mode unshaded, cull_disabled, depth_draw_opaque, shadows_disabled;

uniform vec4 line_color : source_color = vec4(0.02, 0.02, 0.02, 1.0);
// Full line width in screen pixels (constant at any distance).
uniform float line_width_px : hint_range(0.5, 16.0) = 2.0;
// End-cap extension as a fraction of line_width_px, so strokes meeting at a vertex
// overlap instead of leaving pinholes (the Unity demo's PctExtend).
uniform float end_extend : hint_range(0.0, 2.0) = 0.5;
// cos of the minimum dihedral angle treated as a crease. 0.5 = 60 degrees, the
// Unity demo's hardcoded threshold.
uniform float crease_cos_threshold : hint_range(-1.0, 1.0) = 0.5;
// NDC pull toward the camera so the line wins the depth test against its own
// surface (replaces the Unity demo's 0.001 normal offset). Godot 4 uses reverse-Z:
// near = 1, far = 0, so "toward the camera" is +z.
uniform float depth_bias : hint_range(0.0, 0.01) = 0.0002;

void vertex() {
	vec3 pa = CUSTOM0.xyz;
	float end_t = CUSTOM0.w;
	vec3 pb = CUSTOM1.xyz;
	float side = CUSTOM1.w;
	vec3 face_a = CUSTOM2.xyz;
	bool has_face_b = CUSTOM2.w > 0.5;
	vec3 face_b = CUSTOM3.xyz;

	vec3 va = (MODELVIEW_MATRIX * vec4(pa, 1.0)).xyz;
	vec3 vb = (MODELVIEW_MATRIX * vec4(pb, 1.0)).xyz;

	// Boundary edges (one adjacent face) always draw.
	bool is_edge = true;
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

		// Pixel-constant width: NDC spans 2 units across the viewport.
		vec2 px_to_ndc = 2.0 / VIEWPORT_SIZE;
		vec2 half_w = perp * (line_width_px * 0.5) * px_to_ndc;
		vec2 ext = dir_n * (line_width_px * end_extend) * px_to_ndc * (end_t < 0.5 ? -1.0 : 1.0);

		vec3 ndc = end_t < 0.5 ? ndc_a : ndc_b;
		ndc.xy += half_w * side + ext;
		ndc.z += depth_bias;

		float w = end_t < 0.5 ? clip_a.w : clip_b.w;
		POSITION = vec4(ndc.xy * w, ndc.z * w, w);
	}
}

void fragment() {
	ALBEDO = line_color.rgb;
}
