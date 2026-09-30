shader_type spatial;
render_mode cull_disabled, diffuse_burley, specular_disabled, alpha_to_coverage;

uniform sampler2D foliage_atlas : source_color, filter_linear_mipmap, repeat_disable;
uniform float season = 2.0;
uniform float wind_strength = 0.0;
uniform float wind_time = 0.0;
varying flat float phase;

void vertex() {
	phase = CUSTOM0.y;
	vec3 offset = VERTEX - CUSTOM1.xyz;
	float sway = sin(TIME * 1.7 + wind_time + phase) * wind_strength * 0.018;
	VERTEX += vec3(sway, sway * 0.2, sway * 0.4) * length(offset);
}

void fragment() {
	vec2 atlas_size = vec2(textureSize(foliage_atlas, 0));
	vec2 dx = dFdx(UV);
	vec2 dy = dFdy(UV);
	vec2 texel_uv = UV * atlas_size;
	float footprint = max(length(dx * atlas_size), length(dy * atlas_size));
	// Atlas coverage is corrected down to four texels per botanical tile. Coarser
	// levels both lose the slender leaves and mix neighbouring tile silhouettes.
	float mip_limit = min(1.0, 64.0 / max(footprint, 1.0));
	vec4 texel = textureGrad(foliage_atlas, UV, dx * mip_limit, dy * mip_limit);
	float distant = smoothstep(2.5, 6.0, log2(max(footprint, 1.0)));
	ALBEDO = COLOR.rgb * texel.rgb;
	ROUGHNESS = 0.86;
	BACKLIGHT = ALBEDO * 0.22;
	if (!FRONT_FACING) { NORMAL = -NORMAL; }
	ALPHA = texel.a;
	// The near material is unchanged. At minification the original 0.5 cut plus
	// 0.15 A2C edge removes sub-pixel leaves, even from coverage-corrected mips.
	ALPHA_SCISSOR_THRESHOLD = mix(0.5, 0.28, distant);
	ALPHA_ANTIALIASING_EDGE = mix(0.15, 0.0, distant);
	ALPHA_TEXTURE_COORDINATE = texel_uv;
}
