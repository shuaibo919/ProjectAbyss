shader_type spatial;

// Secondary outline: wider, no MatCap mask, softer
render_mode unshaded, cull_front, depth_draw_opaque;

// === Outline Control ===
uniform float outline_width : hint_range(0.0, 20.0) = 3.0;
uniform float persp_exp : hint_range(0.0, 2.0) = 0.0;
uniform vec4 outline_color : source_color = vec4(0.08, 0.06, 0.05, 1.0);
uniform float max_distance = 256.0;

// === Base-colour outline (描边随底色) — see ink_outline_0.gdshader ===
uniform vec4 base_color : source_color = vec4(1.0, 1.0, 1.0, 1.0);
uniform bool use_base_color = false;
uniform bool use_vertex_color = false;
uniform float base_color_darken : hint_range(0.02, 1.0) = 0.28;

// === Pressure Variation ===
uniform float pressure_period : hint_range(0.1, 20.0) = 3.0;
uniform float pressure_amount : hint_range(0.0, 1.0) = 0.22;

// === Textures ===
uniform sampler2D color_remap_tex : hint_default_black;
uniform sampler2D noise_tex : hint_default_white;

// === Varyings ===
varying float v_dist_normalized;
varying float v_noise;

void vertex() {
	vec3 world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float dist = length(world_pos - CAMERA_POSITION_WORLD);

	// Clip-space position
	vec4 clip_pos = PROJECTION_MATRIX * MODELVIEW_MATRIX * vec4(VERTEX, 1.0);
	// Normal in clip space for extrusion direction
	vec3 view_normal = normalize(mat3(MODELVIEW_MATRIX) * NORMAL);
	vec2 clip_normal = normalize((PROJECTION_MATRIX * vec4(view_normal, 0.0)).xy);

	// Stable screen-space width.
	float width = outline_width * 0.001;

	// Pressure variation
	float pressure = 1.0 + sin(length(world_pos) * pressure_period) * pressure_amount;
	width *= pressure;

	// Extrude in clip space
	clip_pos.xy += clip_normal * width * clip_pos.w;
	POSITION = clip_pos;

	// Noise UV — dominant-axis projection (see ink_outline_0.gdshader).
	vec3 world_normal = normalize(mat3(MODEL_MATRIX) * NORMAL);
	vec3 aw = abs(world_normal);
	vec2 ws_uv;
	if (aw.x >= aw.y && aw.x >= aw.z) {
		ws_uv = world_pos.yz;
	} else if (aw.y >= aw.z) {
		ws_uv = world_pos.xz;
	} else {
		ws_uv = world_pos.xy;
	}
	v_noise = texture(noise_tex, ws_uv / 8.0).r;

	v_dist_normalized = dist / max_distance;
}

void fragment() {
	// Distance-based color
	float remap_val = texture(color_remap_tex, vec2(v_dist_normalized, v_dist_normalized)).r;
	vec3 col = outline_color.rgb * mix(1.0, remap_val * 2.0, 0.5);

	// Base-colour line (see ink_outline_0.gdshader).
	if (use_base_color) {
		vec3 vc = use_vertex_color ? COLOR.rgb : vec3(1.0);
		col = base_color.rgb * vc * base_color_darken;
	}

	// Softer alpha, fades more with distance
	float alpha = mix(0.6, 0.1, clamp(v_dist_normalized * 2.0, 0.0, 1.0));

	// Noise-based alpha variation for organic edge
	alpha *= mix(0.7, 1.0, v_noise);

	ALBEDO = col;
	ALPHA = alpha;
}
