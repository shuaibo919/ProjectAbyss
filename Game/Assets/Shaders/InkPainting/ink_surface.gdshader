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
uniform sampler2D noise_tex : hint_default_white;
uniform sampler2D stroke_tex : hint_default_white;

// === Legacy Rim Control ===
// Kept for existing material compatibility. Surface ink is now lighting-driven.
uniform float rim_rate : hint_range(0.0, 20.0) = 2.0;
uniform float rim_rate_close : hint_range(0.0, 20.0) = 2.5;
uniform float rim_rate_range : hint_range(0.0, 20.0) = 8.0;
uniform float rim_offset : hint_range(-1.0, 1.0) = 0.0;
uniform float max_distance = 256.0;

// === Stroke Control ===
// stroke_ctrl: x=size, y=cun strength, z=ca threshold, w=ran threshold (must stay < 1;
// the stagger ratio (area1 - w) / (1 - w) flips sign above 1 and would darken everything)
uniform vec4 stroke_ctrl = vec4(3.2, 0.42, 0.25, 0.85);
// noise_ctrl: x=size, y=cun noise, z=ca noise, w=ran noise
uniform vec4 noise_ctrl = vec4(4.0, 0.35, 0.65, 0.8);

// === Inline Control ===
// inline_ctrl: x=close power, y=far power, zw=offset scale
uniform vec4 inline_ctrl = vec4(1.0, 1.0, 10.0, 10.0);

// === Light Integration ===
uniform float lambert_rim_blend : hint_range(0.0, 1.0) = 0.0;
uniform float shadow_influence : hint_range(0.0, 1.0) = 0.5;
uniform float light_cun_power : hint_range(0.1, 5.0) = 1.0;
uniform float light_ran_power : hint_range(0.1, 5.0) = 1.25;
uniform float light_inline_strength : hint_range(0.0, 2.0) = 0.22;

// === Fill light bypass ===
// 与 fill_light_dir 同向的平行光直接给平面漫反射, 不走墨色闸门。墨水风格
// 的"暗部=墨"由主太阳光承担; 补光只负责把阴影抬到可读的深灰, 否则低角度
// 补光会被 light_ink 判成阴面涂成全墨 (城镇俯拍场景使用, 默认关)。
uniform vec3 fill_light_dir = vec3(0.0);
// 第二旁路方向: 低角度水平补光 (南侧), 只提亮竖直面 (地面 NdotL=0 不受影响),
// 用于把市场摊位等被建筑阴影盖住的竖直面从近黑抬到可读深灰 (2026-09-26
// round-4 评审: 市场广场中央摊位黑色块)。
uniform vec3 fill_light_dir2 = vec3(0.0);

// === Vertex colour ===
// The procedural generators (AncientBuilding, SlowTree/TreeGen) are asset-free: every part's
// basecolor rides on COLOR, and each tree or hall is now a SINGLE surface, so there is no way to
// give trunk / tile / leaf / blossom different base_color uniforms. Without this the whole mesh
// collapses to one flat paper tone.
//
// Carried through a varying rather than read directly because light() has no COLOR built-in.
// COLOR defaults to opaque white, so materials that do not supply vertex colours are unchanged.
uniform bool use_vertex_color = true;

// === Varyings ===
varying vec2 v_ws_uv;
varying vec2 v_uv;
varying float v_dist_normalized;
varying vec3 v_vertex_color;

void vertex() {
	vec3 world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float dist = length(world_pos - CAMERA_POSITION_WORLD);

	// Pass mesh UV for base_color_tex sampling in light()
	v_uv = UV;
	// World-space UV, projected onto the dominant world axis of the face. The old
	// pseudo-3D sum (xy + yz + xz) stretched the ink patterns diagonally across every
	// face, which reads as smeared stains on big planes; dominant-axis keeps each face
	// sampling at its true aspect.
	vec3 world_normal = normalize(mat3(MODEL_MATRIX) * NORMAL);
	vec3 aw = abs(world_normal);
	if (aw.x >= aw.y && aw.x >= aw.z) {
		v_ws_uv = world_pos.yz;
	} else if (aw.y >= aw.z) {
		v_ws_uv = world_pos.xz;
	} else {
		v_ws_uv = world_pos.xy;
	}
	v_dist_normalized = dist / max_distance;
	v_vertex_color = use_vertex_color ? COLOR.rgb : vec3(1.0);
}

void fragment() {
	// Sample base color: uniform tint * texture (white if no tex assigned)
	vec3 base = base_color.rgb * texture(base_color_tex, UV).rgb * v_vertex_color;
	ALBEDO = base;
	METALLIC = 0.0;
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
}

void light() {
	// === Base color (watercolor tint from texture) ===
	// 顶点色已折进 fragment 的 ALBEDO (fragment 里 vertex→fragment varying 正常)。
	// 本 fork 的 D3D12 渲染器不把 varying 插值进 light() —— v_vertex_color/v_uv
	// 在 light() 里永远是默认白, 导致所有顶点色网格 (道路/城墙/建筑/摊位/树)
	// 都渲染成白纸 (2026-09-26 城市道路缺失调查: m3/m4 探针证实)。
	vec3 base = ALBEDO;

	// === Fill light bypass ===
	// 补光不走墨色闸门, 直接给平面漫反射 (见 uniform fill_light_dir 注释)。
	vec3 lw = (INV_VIEW_MATRIX * vec4(LIGHT, 0.0)).xyz;
	bool is_fill = (fill_light_dir != vec3(0.0) && abs(dot(lw, fill_light_dir)) > 0.999)
			|| (fill_light_dir2 != vec3(0.0) && abs(dot(lw, fill_light_dir2)) > 0.999)
			// 水平补光旁路: 东西向等低角度补光不逐一注册 fill_light_dir, 直接按
			// 方向高度旁路 (|dir.y|≈0 的平行光只打竖直面, 地面/路面 NdotL=0 不受
			// 影响)。2026-09-26 round-4 评审续: 南侧 FillLight2 只提亮南北朝向
			// 立面, 市场东西两侧摊位前脸 (朝东/朝西面) 仍只剩 ambient 的近黑。
			|| abs(lw.y) < 0.1;
	if (is_fill) {
		DIFFUSE_LIGHT += base * clamp(dot(NORMAL, LIGHT), 0.0, 1.0) * LIGHT_COLOR * ATTENUATION;
	} else {
		// === Lighting-driven ink area ===
		float NdotL = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
		float light_ink = clamp((1.0 - NdotL) + (1.0 - ATTENUATION) * shadow_influence, 0.0, 1.0);
		float area0 = clamp(pow(light_ink, light_cun_power), 0.0, 1.0);
		float area1 = clamp(pow(light_ink, light_ran_power), 0.0, 1.0);

		// Ink gates: brush strokes belong in the shaded part only. A uniformly lit plane
		// (area0 ~ 0.26 for our sun) must stay clean paper, or the texture tiling reads as
		// stained patchwork — this gate is what keeps big clean surfaces clean.
		float ink_gate0 = smoothstep(0.32, 0.55, area0);
		float ink_gate1 = smoothstep(0.38, 0.62, area1);

		// === Texture sampling ===
		float remap_val = texture(color_remap_tex, vec2(area1, area1)).r;
		vec3 remap = remap_val * outline_color.rgb * 2.0;

		float stroke_cun_0 = texture(stroke_tex, v_ws_uv / stroke_ctrl.x).a;
		float noise_val = texture(noise_tex, v_ws_uv / noise_ctrl.x).b;
		// Gate the fine noise: below the gate it contributes nothing, so clean lit areas
		// stay clean instead of picking up specks everywhere.
		noise_val = smoothstep(0.45, 0.95, noise_val);
		float stroke_ran = texture(stroke_tex, v_ws_uv / 16.0).r;
		float stroke_cun_1 = texture(stroke_tex, v_ws_uv / stroke_ctrl.x * 1.6).a;
		float in_line = smoothstep(0.55, 0.95, area0) * mix(0.8, 1.0, noise_val) * light_inline_strength;

		// === Compositing (watercolor style: base texture shows through, ink strokes darken) ===

		// Start with watercolor base (texture color instead of pure paper white)
		vec3 col = base;

		// 1. Cun stroke 0 (皴)
		float cun_factor = clamp((area0 - stroke_cun_0 * stroke_ctrl.y - noise_val * noise_ctrl.y) * 2.0, 0.0, 1.0);
		col = mix(col, remap, cun_factor * ink_gate0);
		col = clamp(col, vec3(0.0), vec3(1.0));

		// 2. Inline (内轮廓)
		col = mix(col, remap, in_line * ink_gate0);
		col = clamp(col, vec3(0.0), vec3(1.0));

		// 3. Ca stroke (擦)
		float k = (area1 - stroke_ctrl.z) / max(1.0 - stroke_ctrl.z, 0.001);
		float z = noise_ctrl.z;
		col = mix(col, remap, clamp((k - stroke_cun_1 * 1.2 - noise_val * z) * 1.5, 0.0, 1.0) * ink_gate1);

		// 4. Ran (染/墨晕)
		float r_val = stroke_ctrl.w;
		float n_val = clamp(noise_ctrl.w, 0.5, 10.0);
		k = (area1 - r_val) / max(1.0 - r_val, 0.001) * mix(ran_close, ran_far, area1);
		col = col * (1.0 - clamp(k * 1.3 - stroke_ran * 0.75 - noise_val * n_val, 0.0, 1.0) * 0.42);

		// Output: write ink color to diffuse, attenuated by shadow.
		// NdotL × LIGHT_COLOR 必须显式乘上 (自定义 light() 不会自动套朗伯):
		// 旧代码 col*ATTEN 每盏灯都把整个 base 叠上来, 日照面 ≈ 1.0+ambient+fill
		// ≈ 1.9×base, 任何 base>0.52 的表面直接 clamp 到 255 —— 2026-09-26 v4
		// 实测: 朱雀大街日照面 (255,255,246) 全白, 同一条路的阴影段 (146,127,115)
		// 正常。乘上 NdotL 后纸面回到可读的纸白, 墨色过渡也更陡 (更像水墨)。
		// 注意 LIGHT_COLOR 已含 color×energy (forward_clustered 调用 light_compute
		// 时传入 color*energy, ATTENUATION 只装阴影); 本 fork 空间 light() 没有
		// LIGHT_ENERGY built-in (那是 2D canvas 才有)。
		DIFFUSE_LIGHT += col * NdotL * LIGHT_COLOR * ATTENUATION;
	}
}
