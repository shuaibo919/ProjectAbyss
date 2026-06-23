// Ocean water — spatial shader for Godot 4.
// Ported & adapted from afl_ext's Shadertoy "Ocean" (MIT, 2017-2024,
// https://www.shadertoy.com/view/MdXyzX): the wavedx / getwaves octave-sum wave
// field drives BOTH vertex displacement and a per-pixel analytic normal.
//
// Designed as a reusable whitebox/stylized water material. Tunable in the
// inspector: wave size, wave height, speed, and the base water colors.
//
// Requires a subdivided mesh (PlaneMesh with subdivisions) — a flat quad has no
// interior vertices to displace.
shader_type spatial;
render_mode cull_disabled, specular_schlick_ggx;

// ---- Tunables -------------------------------------------------------------
group_uniforms waves;
// Characteristic wavelength scale, in metres. Larger = bigger, longer waves.
uniform float wave_size : hint_range(0.3, 20.0) = 3.0;
// Peak-to-trough vertical displacement, in metres.
uniform float wave_height : hint_range(0.0, 6.0) = 1.0;
// Animation speed multiplier.
uniform float wave_speed : hint_range(0.0, 4.0) = 1.0;
// How much each octave pulls/steepens the wave (the original DRAG_MULT).
uniform float wave_drag : hint_range(0.0, 1.0) = 0.38;

group_uniforms color;
uniform vec3 deep_color : source_color = vec3(0.04, 0.13, 0.20);
uniform vec3 shallow_color : source_color = vec3(0.12, 0.32, 0.40);
uniform vec3 crest_color : source_color = vec3(0.55, 0.68, 0.72);
uniform float water_roughness : hint_range(0.0, 0.5) = 0.06;
uniform float fresnel_strength : hint_range(0.0, 1.0) = 0.9;

// Octave counts. Vertex can be cheaper than the per-pixel normal field.
const int ITER_VERTEX = 10;
const int ITER_NORMAL = 18;

varying vec3 world_pos;

// Single directional wave: returns (wave value, derivative along direction).
vec2 wavedx(vec2 position, vec2 direction, float frequency, float timeshift) {
	float x = dot(direction, position) * frequency + timeshift;
	float wave = exp(sin(x) - 1.0);
	return vec2(wave, -wave * cos(x));
}

// Sum octaves of directional waves into a single 0..1 height field.
float getwaves(vec2 position, int iterations, float t) {
	float wave_phase = length(position) * 0.1;
	float iter = 0.0;
	float frequency = 1.0;
	float time_mult = 2.0;
	float weight = 1.0;
	float sum_values = 0.0;
	float sum_weights = 0.0;
	for (int i = 0; i < iterations; i++) {
		vec2 p = vec2(sin(iter), cos(iter));
		vec2 res = wavedx(position, p, frequency, t * time_mult + wave_phase);
		position += p * res.y * weight * wave_drag;
		sum_values += res.x * weight;
		sum_weights += weight;
		weight = mix(weight, 0.0, 0.2);
		frequency *= 1.18;
		time_mult *= 1.07;
		iter += 1232.399963;
	}
	return sum_values / sum_weights;
}

// Sample the height field in metres at a world-XZ position.
float sample_height(vec2 world_xz, int iters, float t) {
	float h = getwaves(world_xz / wave_size, iters, t);
	return (h - 0.5) * wave_height;
}

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float t = TIME * wave_speed;
	VERTEX.y += sample_height(world_pos.xz, ITER_VERTEX, t);
	world_pos.y += VERTEX.y;
}

void fragment() {
	float t = TIME * wave_speed;
	vec2 p = world_pos.xz;

	// Analytic normal via central differences of the height field. The step `e`
	// is floored by the pixel's world-space footprint (fwidth) so the high-freq
	// wave field isn't undersampled into shimmer when many wavelengths fall in
	// one pixel (distant / top-down views over the large 10x plane).
	float footprint = max(fwidth(p.x), fwidth(p.y));
	float e = max(wave_size * 0.08, footprint * 1.5);
	float hL = sample_height(p - vec2(e, 0.0), ITER_NORMAL, t);
	float hR = sample_height(p + vec2(e, 0.0), ITER_NORMAL, t);
	float hD = sample_height(p - vec2(0.0, e), ITER_NORMAL, t);
	float hU = sample_height(p + vec2(0.0, e), ITER_NORMAL, t);
	vec3 n = normalize(vec3(hL - hR, 2.0 * e, hD - hU));

	// Fade detail to flat with distance so far water doesn't shimmer/alias.
	float dist = length(world_pos - CAMERA_POSITION_WORLD);
	n = normalize(mix(n, vec3(0.0, 1.0, 0.0), clamp(sqrt(dist * 0.01) * 0.7, 0.0, 0.9)));
	NORMAL = (VIEW_MATRIX * vec4(n, 0.0)).xyz;

	vec3 view_dir = normalize(CAMERA_POSITION_WORLD - world_pos);
	// Fresnel from VIEW ANGLE (not wave height): water looks deep/dark when seen
	// from straight above and bright/reflective at grazing angles. Driving the
	// deep->shallow blend off wave height instead made troughs render near-black,
	// which read as pulsating dark blobs rolling with the waves.
	float ndv = clamp(dot(n, view_dir), 0.0, 1.0);
	float fresnel = fresnel_strength * pow(1.0 - ndv, 5.0);

	// Base body color: deep when looking down into it, shallow/brighter at the
	// grazing reflection. Deep color is a real sea-blue, never near-black.
	vec3 body = mix(deep_color, shallow_color, fresnel);

	// Thin foam/crest accent only on the very tops of waves (subtle, additive).
	float crest = clamp(sample_height(p, ITER_NORMAL, t) / max(0.001, wave_height) + 0.5, 0.0, 1.0);
	float foam = smoothstep(0.82, 1.0, crest) * 0.35;

	ALBEDO = mix(body, crest_color, foam);
	METALLIC = 0.0;
	ROUGHNESS = water_roughness;
	SPECULAR = 0.6;
}
