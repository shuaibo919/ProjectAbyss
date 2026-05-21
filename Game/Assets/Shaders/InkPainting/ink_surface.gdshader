shader_type spatial;

// Suppress PBR specular and ambient reflections
render_mode diffuse_lambert, specular_disabled;

// === Color ===
uniform vec4 base_color : source_color = vec4(0.898, 0.8588, 0.8235, 1.0);
uniform sampler2D base_color_tex : hint_default_white;
uniform vec4 outline_color : source_color = vec4(0.05, 0.05, 0.05, 1.0);
uniform float ran_close : hint_range(0.0, 2.5) = 1.0;
uniform float ran_far : hint_range(-1.0, 1.0) = 0.5;

// === Textures ===
uniform sampler2D color_remap_tex : hint_default_black;
uniform sampler2D matcap_tex : hint_default_black;
uniform sampler2D noise_tex : hint_default_white;
uniform sampler2D stroke_tex : hint_default_white;

// === Rim Control ===
uniform float rim_rate : hint_range(0.0, 20.0) = 2.0;
uniform float rim_rate_close : hint_range(0.0, 20.0) = 2.5;
uniform float rim_rate_range : hint_range(0.0, 20.0) = 8.0;
uniform float rim_offset : hint_range(-1.0, 1.0) = 0.0;
uniform float max_distance = 256.0;

// === Stroke Control ===
// stroke_ctrl: x=size, y=cun strength, z=ca threshold, w=ran threshold
uniform vec4 stroke_ctrl = vec4(2.0, 0.6, 0.25, 1.0);
// noise_ctrl: x=size, y=cun noise, z=ca noise, w=ran noise
uniform vec4 noise_ctrl = vec4(2.0, 0.6, 1.0, 1.0);

// === Inline Control ===
// inline_ctrl: x=close power, y=far power, zw=offset scale
uniform vec4 inline_ctrl = vec4(1.0, 1.0, 10.0, 10.0);

// === Light Integration ===
uniform float lambert_rim_blend : hint_range(0.0, 1.0) = 1.0;
uniform float shadow_influence : hint_range(0.0, 1.0) = 0.5;

// === Varyings ===
varying vec2 v_ws_uv;
varying vec2 v_matcap_uv;
varying vec2 v_uv;
varying float v_rim0;
varying float v_rim1;
varying float v_dist_normalized;

void vertex() {
	vec3 world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 world_normal = normalize(mat3(MODEL_MATRIX) * NORMAL);
	vec3 world_view_dir = normalize(CAMERA_POSITION_WORLD - world_pos);
	float dist = length(world_pos - CAMERA_POSITION_WORLD);

	// MatCap UV via cross product in view space
	vec3 map_vec = cross(world_normal, world_view_dir);
	map_vec = mat3(VIEW_MATRIX) * map_vec;
	float l = length(map_vec);
	map_vec = normalize(map_vec) * pow(l, mix(inline_ctrl.x, inline_ctrl.y, dist / 128.0));

	// Rim 0: distance-adaptive (for cun/stroke area)
	float rim0 = clamp(1.0 - clamp(dot(world_view_dir, world_normal) + rim_offset, 0.0, 1.0), 0.0, 1.0);
	float rr = mix(rim_rate_close, rim_rate, clamp(dist / max_distance * rim_rate_range, 0.0, 1.0));
	rim0 = clamp(pow(rim0, rr) * 2.0, 0.0, 1.0);

	// Rim 1: fixed rate (for ran/ink wash)
	float rim1 = clamp(1.0 - clamp(dot(world_view_dir, world_normal), 0.0, 1.0), 0.0, 1.0);
	rim1 = clamp(pow(rim1, 1.5) * 2.0, 0.0, 1.0);

	// Pass mesh UV for base_color_tex sampling in light()
	v_uv = UV;
	// World-space UV (pseudo-3D projection)
	v_ws_uv = world_pos.xy + world_pos.yz + world_pos.xz;
	v_matcap_uv = map_vec.xy * inline_ctrl.zw / 10.0 * 0.5 + 0.5;
	v_rim0 = rim0;
	v_rim1 = rim1;
	v_dist_normalized = dist / max_distance;
}

void fragment() {
	// Sample base color: uniform tint * texture (white if no tex assigned)
	vec3 base = base_color.rgb * texture(base_color_tex, UV).rgb;
	ALBEDO = base;
	METALLIC = 0.0;
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
}

void light() {
	// === Base color (watercolor tint from texture) ===
	vec3 base = base_color.rgb * texture(base_color_tex, v_uv).rgb;

	// === Mix rim with Lambert ===
	float NdotL = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
	float area0 = mix(1.0 - NdotL, v_rim0, lambert_rim_blend);
	float area1 = mix(1.0 - NdotL, v_rim1, lambert_rim_blend);

	// Shadow adds to ink area
	area0 = clamp(area0 + (1.0 - ATTENUATION) * shadow_influence, 0.0, 1.0);
	area1 = clamp(area1 + (1.0 - ATTENUATION) * shadow_influence, 0.0, 1.0);

	// === Texture sampling ===
	float remap_val = texture(color_remap_tex, vec2(v_dist_normalized, v_dist_normalized)).r;
	vec3 remap = remap_val * outline_color.rgb * 2.0;

	float stroke_cun_0 = texture(stroke_tex, v_ws_uv / stroke_ctrl.x).a;
	float noise_val = texture(noise_tex, v_ws_uv / noise_ctrl.x).b;
	float stroke_ran = texture(stroke_tex, v_ws_uv / 10.0).r;
	float stroke_cun_1 = texture(stroke_tex, v_ws_uv / stroke_ctrl.x * 2.0).a;
	float in_line = texture(matcap_tex, v_matcap_uv).g;

	// === Compositing (watercolor style: base texture shows through, ink strokes darken) ===

	// Start with watercolor base (texture color instead of pure paper white)
	vec3 col = base;

	// 1. Cun stroke 0 (皴)
	float cun_factor = clamp((area0 - stroke_cun_0 * stroke_ctrl.y - noise_val * noise_ctrl.y) * 2.0, 0.0, 1.0);
	col = mix(col, remap, cun_factor);
	col = clamp(col, vec3(0.0), vec3(1.0));

	// 2. Inline (内轮廓)
	col = mix(col, remap, in_line * area0);
	col = clamp(col, vec3(0.0), vec3(1.0));

	// 3. Ca stroke (擦)
	float k = (area1 - stroke_ctrl.z) / max(1.0 - stroke_ctrl.z, 0.001);
	float z = mix(noise_ctrl.z, -0.06, clamp(v_dist_normalized * 8.0 - 1.0, 0.0, 1.0));
	col = mix(col, remap, clamp((k - stroke_cun_1 * 1.2 - noise_val * z) * 1.5, 0.0, 1.0));

	// 4. Ran (染/墨晕)
	float r_val = mix(stroke_ctrl.w, -5.0, v_dist_normalized);
	float n_val = clamp(mix(noise_ctrl.w, 0.0, v_dist_normalized), 0.5, 10.0);
	k = (area1 - r_val) / max(1.0 - r_val, 0.001) * mix(ran_close, ran_far, clamp(pow(v_dist_normalized, 0.25), 0.0, 1.0));
	col = col * (1.0 - clamp(k * 1.5 - stroke_ran * 0.75 - noise_val * n_val, 0.0, 1.0) * 0.5);

	// Output: write ink color to diffuse, attenuated by shadow
	DIFFUSE_LIGHT += col * ATTENUATION;
}
