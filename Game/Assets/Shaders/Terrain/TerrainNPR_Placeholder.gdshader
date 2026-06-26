shader_type spatial;

// ---------------------------------------------------------------------------
// 彩墨 (ink-wash) NPR PLACEHOLDER terrain shader for HTerrain.
//
// This is a stand-in until the real project-wide NPR pipeline lands; it keeps
// HTerrain's required vertex contract (height displacement + normal unpack +
// colormap alpha for water holes) but swaps the lit fragment for a posterized,
// paper/ink stylization so the terrain reads in the whitebox without looking
// like a PBR ground. Forked from addons/zylann.hterrain/shaders/low_poly.gdshader.
// Wire it on the HTerrain node as a Custom shader (the heightmap/normalmap/
// colormap + transform uniforms below are auto-fed by HTerrain).
// ---------------------------------------------------------------------------

#include "res://addons/zylann.hterrain/shaders/include/heightmap.gdshaderinc"

// --- Fed automatically by HTerrain (do not rename) ---
uniform sampler2D u_terrain_heightmap;
uniform sampler2D u_terrain_normalmap;
uniform sampler2D u_terrain_colormap : source_color;
uniform mat4 u_terrain_inverse_transform;
uniform mat3 u_terrain_normal_basis;

// --- Ink-wash stylization controls ---
uniform vec3 u_paper_color : source_color = vec3(0.78, 0.72, 0.56);  // lit/paper tone
uniform vec3 u_ink_color : source_color = vec3(0.14, 0.17, 0.22);    // shadow/ink tone
uniform float u_band_count = 3.0;          // number of posterized light bands
uniform float u_ink_strength = 0.6;        // how far shadows drift toward ink
uniform float u_slope_ink = 0.7;           // extra ink on steep slopes (rock edges)
uniform float u_tint_mix = 0.7;            // how much the colormap tints the paper

varying vec3 v_world_normal;
varying flat vec4 v_tint;

vec3 unpack_normal(vec4 rgba) {
	vec3 n = rgba.xzy * 2.0 - vec3(1.0);
	// Negate Z because it comes from Y in the normal map (OpenGL-style, Y-up).
	n.z *= -1.0;
	return n;
}

void vertex() {
	vec2 cell_coords = (u_terrain_inverse_transform * MODEL_MATRIX * vec4(VERTEX, 1)).xz;
	// Half-offset so we sample pixel centres (avoids bilinear seams, HTerrain #183).
	cell_coords += vec2(0.5);
	UV = cell_coords / vec2(textureSize(u_terrain_heightmap, 0));

	float h = sample_heightmap(u_terrain_heightmap, UV);
	VERTEX.y = h;

	v_tint = texture(u_terrain_colormap, UV);
	v_world_normal = normalize(u_terrain_normal_basis * unpack_normal(texture(u_terrain_normalmap, UV)));
	NORMAL = v_world_normal;
}

void fragment() {
	// Water holes: the colormap alpha is authored to 0 outside the land mask.
	if (v_tint.a < 0.5) {
		discard;
	}

	vec3 n = normalize(v_world_normal);

	// Cheap key light so the placeholder still has form without a real sun dep.
	// (NORMAL is in view space here; use the interpolated world normal instead.)
	vec3 key_dir = normalize(vec3(-0.4, 0.85, 0.3));
	float ndl = clamp(dot(n, key_dir), 0.0, 1.0);

	// Posterize the diffuse term into a few flat bands (cel / 彩墨 wash steps).
	float bands = max(u_band_count, 1.0);
	float stepped = floor(ndl * bands) / bands;

	// Base paper colour, tinted by the authored ground colour, then pushed toward
	// ink in the shadow bands.
	vec3 paper = mix(u_paper_color, u_paper_color * v_tint.rgb * 2.0, u_tint_mix);
	float ink_amt = (1.0 - stepped) * u_ink_strength;

	// Steep faces (low world-up component) gather extra ink, like rock washes.
	float slope = 1.0 - clamp(n.y, 0.0, 1.0);
	ink_amt = clamp(ink_amt + slope * u_slope_ink, 0.0, 1.0);

	ALBEDO = mix(paper, u_ink_color, ink_amt);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
	// Flat-shade the lit geometry so the bands read crisply (low-poly wash look).
	NORMAL = normalize(cross(dFdy(VERTEX), dFdx(VERTEX)));
}
