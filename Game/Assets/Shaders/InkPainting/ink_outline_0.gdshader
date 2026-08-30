shader_type spatial;

// Primary outline: inverted hull with MatCap mask and flying-white clip
render_mode unshaded, cull_front, depth_draw_opaque;

// === Outline Control ===
uniform float outline_width : hint_range(0.0, 10.0) = 1.5;
uniform float persp_exp : hint_range(0.0, 2.0) = 0.0;
uniform vec4 outline_color : source_color = vec4(0.05, 0.05, 0.05, 1.0);
uniform float max_distance = 256.0;

// === Base-colour outline (描边随底色) ===
// The ink line of an object is rarely pure black — blossom edges carry a hint of
// pink, a tiled roof a hint of green. Instead of pinning a new black per object,
// derive the line from the same base colour the surface uses: line = base_color *
// vertex_colour * darken. Default is off, so existing materials are unchanged.
uniform vec4 base_color : source_color = vec4(1.0, 1.0, 1.0, 1.0);
uniform bool use_base_color = false;
uniform bool use_vertex_color = false;
uniform float base_color_darken : hint_range(0.02, 1.0) = 0.28;

// === Pressure Variation (sine wave simulates brush pressure) ===
uniform float pressure_period : hint_range(0.1, 20.0) = 5.0;
uniform float pressure_amount : hint_range(0.0, 1.0) = 0.18;

// === Flying White (飞白) ===
uniform float feibai_threshold : hint_range(0.0, 1.0) = 0.5;

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
	// Normal in clip space for extrusion direction
	vec3 view_normal = normalize(mat3(MODELVIEW_MATRIX) * NORMAL);
	vec2 clip_normal = normalize((PROJECTION_MATRIX * vec4(view_normal, 0.0)).xy);

	// Stable screen-space width. The legacy persp_exp uniform is kept for material compatibility.
	float width = outline_width * 0.001;

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

	// Noise for flying white — dominant-axis projection keeps the dry-brush breaks
	// aligned with the face instead of smearing diagonally. (world_normal is already
	// computed above for the MatCap mask.)
	vec3 aw = abs(world_normal);
	vec2 ws_uv;
	if (aw.x >= aw.y && aw.x >= aw.z) {
		ws_uv = world_pos.yz;
	} else if (aw.y >= aw.z) {
		ws_uv = world_pos.xz;
	} else {
		ws_uv = world_pos.xy;
	}
	v_noise = texture(noise_tex, ws_uv / 5.5).r;

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

	// Base-colour line: the same tint the surface uses, darkened into ink.
	if (use_base_color) {
		vec3 vc = use_vertex_color ? COLOR.rgb : vec3(1.0);
		col = base_color.rgb * vc * base_color_darken;
	}

	// Fade out at distance
	float alpha = mix(1.0, 0.3, clamp(v_dist_normalized * 2.0, 0.0, 1.0));

	ALBEDO = col;
	ALPHA = alpha;
}
