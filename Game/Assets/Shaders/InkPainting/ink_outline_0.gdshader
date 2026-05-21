shader_type spatial;

// Primary outline: inverted hull with MatCap mask and flying-white clip
render_mode unshaded, cull_front, depth_draw_opaque;

// === Outline Control ===
uniform float outline_width : hint_range(0.0, 10.0) = 1.5;
uniform float persp_exp : hint_range(0.0, 2.0) = 0.8;
uniform vec4 outline_color : source_color = vec4(0.05, 0.05, 0.05, 1.0);
uniform float max_distance = 256.0;

// === Pressure Variation (sine wave simulates brush pressure) ===
uniform float pressure_period : hint_range(0.1, 20.0) = 5.0;
uniform float pressure_amount : hint_range(0.0, 1.0) = 0.3;

// === Flying White (飞白) ===
uniform float feibai_threshold : hint_range(0.0, 1.0) = 0.6;

// === Textures ===
uniform sampler2D color_remap_tex : hint_default_black;
uniform sampler2D matcap_tex : hint_default_black;
uniform sampler2D noise_tex : hint_default_white;

// === Varyings ===
varying float v_dist_normalized;
varying float v_matcap_mask;
varying float v_noise;

void vertex() {
	vec3 world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 world_normal = normalize(mat3(MODEL_MATRIX) * NORMAL);
	vec3 world_view_dir = normalize(CAMERA_POSITION_WORLD - world_pos);
	float dist = length(world_pos - CAMERA_POSITION_WORLD);

	// Clip-space position
	vec4 clip_pos = PROJECTION_MATRIX * MODELVIEW_MATRIX * vec4(VERTEX, 1.0);
	float depth = clip_pos.w;

	// Normal in clip space for extrusion direction
	vec3 view_normal = normalize(mat3(MODELVIEW_MATRIX) * NORMAL);
	vec2 clip_normal = normalize((PROJECTION_MATRIX * vec4(view_normal, 0.0)).xy);

	// Perspective-corrected width
	float width = outline_width * 0.001 * pow(depth, persp_exp);

	// Pressure variation (sine wave along world position for organic feel)
	float pressure = 1.0 + sin(length(world_pos) * pressure_period) * pressure_amount;
	width *= pressure;

	// Extrude in clip space
	clip_pos.xy += clip_normal * width * clip_pos.w;
	POSITION = clip_pos;

	// MatCap UV for mask (R channel controls where outline appears)
	vec3 map_vec = cross(world_normal, world_view_dir);
	map_vec = mat3(VIEW_MATRIX) * map_vec;
	vec2 matcap_uv = map_vec.xy * 0.5 + 0.5;
	v_matcap_mask = texture(matcap_tex, matcap_uv).r;

	// Noise for flying white
	vec2 ws_uv = world_pos.xy + world_pos.yz + world_pos.xz;
	v_noise = texture(noise_tex, ws_uv / 4.0).r;

	v_dist_normalized = dist / max_distance;
}

void fragment() {
	// Flying white: discard fragments where noise < threshold (simulates dry brush)
	if (v_noise < feibai_threshold * v_matcap_mask) {
		discard;
	}

	// MatCap mask: discard where mask is too low (outline only on silhouette edges)
	if (v_matcap_mask < 0.05) {
		discard;
	}

	// Distance-based color intensity
	float remap_val = texture(color_remap_tex, vec2(v_dist_normalized, v_dist_normalized)).r;
	vec3 col = outline_color.rgb * mix(1.0, remap_val * 2.0, 0.5);

	// Fade out at distance
	float alpha = mix(1.0, 0.3, clamp(v_dist_normalized * 2.0, 0.0, 1.0));

	ALBEDO = col;
	ALPHA = alpha;
}
