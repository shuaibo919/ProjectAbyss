shader_type spatial;

// Secondary outline: wider, no MatCap mask, softer
render_mode unshaded, cull_front, depth_draw_opaque;

// === Outline Control ===
uniform float outline_width : hint_range(0.0, 20.0) = 3.0;
uniform float persp_exp : hint_range(0.0, 2.0) = 0.8;
uniform vec4 outline_color : source_color = vec4(0.08, 0.06, 0.05, 1.0);
uniform float max_distance = 256.0;

// === Pressure Variation ===
uniform float pressure_period : hint_range(0.1, 20.0) = 3.0;
uniform float pressure_amount : hint_range(0.0, 1.0) = 0.4;

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
	float depth = clip_pos.w;

	// Normal in clip space for extrusion direction
	vec3 view_normal = normalize(mat3(MODELVIEW_MATRIX) * NORMAL);
	vec2 clip_normal = normalize((PROJECTION_MATRIX * vec4(view_normal, 0.0)).xy);

	// Perspective-corrected width (wider than outline_0)
	float width = outline_width * 0.001 * pow(depth, persp_exp);

	// Pressure variation
	float pressure = 1.0 + sin(length(world_pos) * pressure_period) * pressure_amount;
	width *= pressure;

	// Extrude in clip space
	clip_pos.xy += clip_normal * width * clip_pos.w;
	POSITION = clip_pos;

	// Noise UV
	vec2 ws_uv = world_pos.xy + world_pos.yz + world_pos.xz;
	v_noise = texture(noise_tex, ws_uv / 6.0).r;

	v_dist_normalized = dist / max_distance;
}

void fragment() {
	// Distance-based color
	float remap_val = texture(color_remap_tex, vec2(v_dist_normalized, v_dist_normalized)).r;
	vec3 col = outline_color.rgb * mix(1.0, remap_val * 2.0, 0.5);

	// Softer alpha, fades more with distance
	float alpha = mix(0.6, 0.1, clamp(v_dist_normalized * 2.0, 0.0, 1.0));

	// Noise-based alpha variation for organic edge
	alpha *= mix(0.7, 1.0, v_noise);

	ALBEDO = col;
	ALPHA = alpha;
}
