## 技法分析


## Plan to do

### 添加自定义 Diffuse / Specular Shading Model 所需改动

以下分析基于本地 Godot 引擎源码。添加一个新的 diffuse 模式（如 `DIFFUSE_INK`）和一个新的 specular 模式（如 `SPECULAR_INK`）需要改动以下文件：

#### 1. C++ 枚举定义

| 文件 | 改动 |
|---|---|
| `scene/resources/material.h:284-297` | 在 `DiffuseMode` 枚举的 `DIFFUSE_MAX` 前添加新值，在 `SpecularMode` 枚举的 `SPECULAR_MAX` 前添加新值 |

#### 2. C++ 序列化与编辑器绑定

| 文件 | 改动位置 | 改动内容 |
|---|---|---|
| `scene/resources/material.cpp:798-826` | shader code generation switch | 添加 `case DIFFUSE_INK: code += ", diffuse_ink";` 和对应 specular case |
| `scene/resources/material.cpp:3572-3573` | ADD_PROPERTY 枚举提示 | 在 `PROPERTY_HINT_ENUM` 的逗号分隔字符串中追加新模式名（这同时控制编辑器 Inspector 下拉菜单） |
| `scene/resources/material.cpp:3862-3869` | BIND_ENUM_CONSTANT | 添加新常量的 ClassDB 枚举绑定 |

#### 3. Shader 语言 render_mode 注册

| 文件 | 改动 |
|---|---|
| `servers/rendering/shader_types.cpp:246-247` | 在 diffuse/specular 的 modes 列表中追加新变体名（如 `"ink"`），引擎会自动拼接为 `diffuse_ink` / `specular_ink` |

#### 4. render_mode → #define 映射（三个渲染后端）

| 文件 | 说明 |
|---|---|
| `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp:870-881` | Forward+ (Vulkan 桌面端主路径) |
| `servers/rendering/renderer_rd/forward_mobile/scene_shader_forward_mobile.cpp:803-814` | Forward Mobile (Vulkan 移动端) |
| `drivers/gles3/storage/material_storage.cpp:1422-1432` | GLES3 兼容渲染器 |

每个文件中添加 `"diffuse_ink" → "#define DIFFUSE_INK"` 和 `"specular_ink" → "#define SPECULAR_INK"` 的映射。

#### 5. GLSL Shader 实现（核心 BRDF 代码）

这是实际编写光照数学的地方。每个文件中有多处需要添加 `#elif defined(DIFFUSE_INK)` / `#elif defined(SPECULAR_INK)` 分支：

| 文件 | 需改动的位置 |
|---|---|
| `servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl` | **主光照计算**：diffuse BRDF 选择 (L221-248)、specular BRDF 选择 (L252-292)；**区域光** (L1259-1313) |
| `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` | **环境光/间接光 specular** (L2248-2278) |
| `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl` | **环境光/间接光 specular** (L1883-1916) |
| `servers/rendering/renderer_rd/shaders/scene_forward_vertex_lights_inc.glsl` | **顶点光照简化路径** (L1-39) |
| `drivers/gles3/shaders/scene.glsl` | **GLES3 光照计算**：diffuse (L1634-1650)、specular (L1669-1707)；**区域光** (L1866-1914)；**环境光** (L2587-2604)；**顶点光照** (L400-425) |

#### 6. 文档

| 文件 | 改动 |
|---|---|
| `doc/classes/BaseMaterial3D.xml:781-803` | 添加新枚举常量的描述文档 |

#### 改动总览

```
总计需要改动 11 个文件：
  C++ 头文件 ......... 1 (material.h)
  C++ 源文件 ......... 4 (material.cpp, 3 个 render_mode 映射文件)
  Shader 注册 ........ 1 (shader_types.cpp)
  GLSL 着色器 ........ 4 (lights_inc, clustered, mobile, scene.glsl + vertex_lights_inc)
  XML 文档 ........... 1 (BaseMaterial3D.xml)
```

#### 建议实施顺序

1. 先改 `material.h` 添加枚举 → `material.cpp` 添加序列化和绑定
2. 改 `shader_types.cpp` + 三个 render_mode 映射文件
3. 在 `scene_forward_lights_inc.glsl` 中实现 BRDF 数学（这是核心，Forward+ 和 Mobile 共享此文件）
4. 在 `drivers/gles3/shaders/scene.glsl` 中复制相同的 BRDF 实现
5. 处理环境光/间接光的特殊分支
6. 更新文档
7. 编译测试

### Godot NPR之官方Toon Shading分析

> 基于本地引擎源码 `Engine/` 目录分析。核心实现位于：
> - `servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl` (Vulkan/RD 主光照)
> - `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` (环境光/间接光)

#### 1. DIFFUSE_TOON — 漫反射

**源码** (`scene_forward_lights_inc.glsl:228-230`)：
```glsl
#elif defined(DIFFUSE_TOON)
    diffuse_brdf_NL = smoothstep(-roughness, max(roughness, half(0.01)), NdotL) * half(1.0 / M_PI);
```

**原理**：用 `smoothstep` 替代标准 Lambert 的 `max(N·L, 0)` 余弦衰减，将明暗过渡变为可控宽度的阶梯函数。

| roughness | smoothstep 下界 | smoothstep 上界 | 效果 |
|-----------|----------------|----------------|------|
| 0 | 0.0 | 0.01 | 近乎硬切割的二值化明暗（经典赛璐珞） |
| 0.5 | -0.5 | 0.5 | 中等宽度的柔和过渡 |
| 1.0 | -1.0 | 1.0 | 过渡覆盖整个 NdotL 范围，接近 Lambert |

对比标准 Lambert：`diffuse_brdf_NL = cNdotL * (1.0 / M_PI)`

关键区别：
- 明暗过渡中心始终在 `NdotL = 0`（终止线位置）
- `roughness` 控制过渡带宽度，而非微表面散射
- 乘以 `1/PI` 做基本的能量守恒归一化

#### 2. SPECULAR_TOON — 高光

**源码** (`scene_forward_lights_inc.glsl:257-263`)：
```glsl
#if defined(SPECULAR_TOON)
    hvec3 R = normalize(-reflect(L, N));
    half RdotV = dot(R, V);
    half mid = half(1.0) - roughness;
    mid *= mid;
    half intensity = smoothstep(mid - roughness * half(0.5), mid + roughness * half(0.5), RdotV) * mid;
    diffuse_light += light_color * intensity * attenuation * specular_amount;
    // write to diffuse_light, as in toon shading you generally want no reflection
```

**原理**：基于 Phong 反射模型 (`R·V`)，用 `smoothstep` 做阈值化。

计算步骤：
1. `R = reflect(-L, N)` — 光线关于法线的反射方向
2. `RdotV = dot(R, V)` — 反射方向与视线方向的对齐度
3. `mid = (1 - roughness)^2` — 高光阈值中心点
4. `smoothstep(mid - roughness*0.5, mid + roughness*0.5, RdotV)` — 过渡带宽度 = roughness
5. 最终强度乘以 `mid`，roughness 越大高光越暗

| roughness | mid (阈值) | 过渡带 | 表现 |
|-----------|-----------|--------|------|
| 0 | 1.0 | [1.0, 1.0] | 极小极亮的镜面反射高光 |
| 0.5 | 0.25 | [0.0, 0.5] | 中等大小的高光区域 |
| 1.0 | 0.0 | [-0.5, 0.5] | intensity 恒为 0，无高光 |

**关键设计**：高光写入 `diffuse_light` 而非 `specular_light`，这意味着 Toon 高光不参与环境反射计算，避免环境贴图反射破坏卡通风格。

对比标准 GGX：完全跳过了微表面分布函数(D)、菲涅尔项(F)、几何遮蔽项(G)，用简单的 Phong + smoothstep 替代整个 microfacet BRDF。

#### 3. 环境光 / 间接光特殊处理

**源码** (`scene_forward_clustered.glsl:2248-2278`)：
```glsl
#if defined(DIFFUSE_TOON)
    //simplify for toon, as
    indirect_specular_light *= specular * metallic * albedo * 2.0;
#else
    // Full PBR: Lazarov 2013 Environment BRDF approximation
    // ... complex F0/F90 + env_brdf lookup ...
#endif
```

标准 PBR 使用 Lazarov 2013 的 split-sum 近似 + 环境 BRDF 查找表；Toon 模式直接简化为 `specular * metallic * albedo * 2.0`：
- 非金属 (`metallic = 0`) → 间接高光为零，无环境反射
- 金属表面 → 间接高光受 albedo 染色
- 避免环境贴图反射破坏平面卡通感

#### 4. 面光源 (Area Light) 中的 Toon 处理

面光源路径中同样有 Toon 特殊分支（`scene_forward_lights_inc.glsl:1274-1283`）：

```glsl
#if defined(DIFFUSE_TOON)
    // 额外计算背面 LTC diffuse 以获得准确的 NdotL
    ltc_evaluate(vec3(-normal), ..., backface_ltc_diffuse, ...);
    half NdotL = half((ltc_diffuse - backface_ltc_diffuse) / (max(solid_angle, 0.001) / M_PI));
    half diffuse_brdf_NL = smoothstep(-roughness, max(roughness, half(0.01)), NdotL) * half(1.0 / M_PI);
    diffuse_light += diffuse_brdf_NL * isotropic_light_color * color * area * light_attenuation * cc_attenuation;
```

通过正面减背面的 LTC 积分值来恢复出等效 `NdotL`，再套用同一个 `smoothstep` 公式。

#### 5. 已知局限

- **每个光源独立 smoothstep**：多光源场景下，每盏灯各自产生一个明暗分界，无法合并为统一的阴影边界
- **无多色阶控制**：只有一个 smoothstep 过渡，不支持自定义 ramp 纹理或多级色阶
- **无阴影颜色控制**：暗部颜色由引擎环境光决定，不能自定义暗部色调
- **环境光容易冲淡效果**：官方建议关闭或降低 environment 的 ambient light 贡献


### Try1 纯ShaderMaterial方案

> 移植自 Unity 工程 `D:\Work\3D_ChineseInkPaintingStyleShader`（CIPR_01_3_Finish.shader）

#### 源工程分析

原始 Unity shader 采用**全 unlit 多 Pass 前向渲染**，不依赖标准光照管线。渲染分为三个 Pass：

| Pass | 作用 | 方法 |
|------|------|------|
| Pass 0 (表面) | 皴擦染 + inline | Rim Light 驱动笔触区域，世界空间 UV 采样笔触纹理 |
| Pass 1 (轮廓 0) | 主轮廓线 | Cull Front + 法线外扩，MatCap mask 控制出现位置，飞白 clip |
| Pass 2 (轮廓 1) | 副轮廓线 | 同上但更宽，无 MatCap mask |

#### 核心技法拆解

**1. Rim Light 作为主要明暗信号（非 Lambert）**
```
rim = pow(1 - saturate(dot(V, N)), rimRate)
rimRate = lerp(rimRateClose, rimRateFar, distance/maxDistance)
```
- 近处 rimRate 大 → rim 区域窄（细线条）
- 远处 rimRate 小 → rim 区域宽（大面积淡墨）
- 这是整个水墨效果的核心驱动信号

**2. 皴 (Cun) — 笔触纹理**
- 笔触纹理 RGBA 四通道存四个方向的笔触
- 世界空间 UV 采样，按法线方向混合（三平面投影）
- `smoothstep(areaBegin, areaEnd, rimLight)` 控制笔触只出现在暗部

**3. 擦 (Ca) — 干笔效果**
- 与皴类似，用负向世界空间 UV（`-(xy+yz+xz)`）采样不同方向的笔触
- 出现在比皴更深的暗部区域

**4. 染 (Ran) — 墨晕**
- 基于 rim1（固定 rimRate=1.5 的 rim）
- `col *= 1 - clamp(k*1.5 - stroke_Ran*0.75 - noise*n, 0, 1) * 0.5`
- 远处 Ran 效果减弱（模拟空气透视的留白）

**5. 勾 (Gou) — 轮廓线**
- **外轮廓**: 顶点法线外扩（inverted hull），`vertex.xy += extend * width * pow(depth, perspExp) * rand`
  - `pow(depth, perspExp)`: 透视矫正
  - `rand = 1 + sin(length(worldPos) * period) * amount`: 正弦噪声模拟运笔压力变化
  - MatCap mask（R通道）控制轮廓出现位置
  - 飞白效果: `clip(noise - threshold)` 丢弃片元模拟枯笔
- **内轮廓**: `step((rimLight - b)/a + noise * cutoff, threshold)`，rim light + 噪声扰动产生有机感的内部轮廓

**6. 距离控制 / 空气透视**
- `ColorRemap` 纹理：1D ramp，距离 → 墨色浓淡（近浓远淡）
- 轮廓颜色、笔触密度、rim 宽度全部随距离变化
- 模拟中国画的"近实远虚"

**7. 纹理资源**

| 纹理 | 用途 |
|------|------|
| ColorRemap | 距离→墨色浓淡 ramp |
| MatCap | R=外轮廓 mask, G=内轮廓 mask |
| Stroke | RGBA 四通道存四方向笔触 |
| Noise | 噪声（飞白、笔触扰动） |

#### Godot 移植方案

**可行性**: 完全可行。采用方案 B（rim + 灯光混合），表面 Pass 接入 Godot 光照管线（使用 `light()` 函数），轮廓 Pass 保持 unshaded。

**设计选择 — 为什么不用纯 unshaded**:
- 纯 unshaded 完全绕开光照管线，效果最可控，但场景灯光和阴影无法参与水墨效果
- 方案 B 在 `light()` 中混合 rim 和 N·L 信号，通过 `lambert_rim_blend` 参数控制混合比例：
  - `= 1.0` 时等同于原版纯 rim 效果（向后兼容）
  - `= 0.0` 时完全由场景灯光驱动
  - `ATTENUATION` 中自带阴影信息，阴影边界自然参与墨色分布

**架构映射**:

```
ShaderMaterial (主材质, cull_back)
  → fragment(): 计算 rim、准备世界空间 UV、采样笔触/噪声纹理、设置 ALBEDO
  → light(): 混合 rim + N·L 作为明暗信号，驱动皴擦染 + inline，写入 DIFFUSE_LIGHT
  → next_pass: ShaderMaterial (render_mode unshaded, cull_front)
      → Outline 0: 法线外扩 + MatCap mask + 飞白 clip
      → next_pass: ShaderMaterial (render_mode unshaded, cull_front)
          → Outline 1: 更宽外扩轮廓
```

**主表面 shader 伪代码**:

```glsl
shader_type spatial;
// 不用 render_mode unshaded，接入光照管线
// 用 IRRADIANCE 覆盖环境光避免 PBR 环境光冲淡效果

// fragment() —— 准备数据
void fragment() {
    vec3 world_pos = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
    float dist = length(world_pos - CAMERA_POSITION_WORLD);

    // rim light 信号（视角相关，与灯光无关）
    float rim_rate = mix(rim_rate_close, rim_rate_far, clamp(dist / max_distance, 0.0, 1.0));
    float rim = clamp(pow(1.0 - clamp(dot(VIEW, NORMAL), 0.0, 1.0), rim_rate), 0.0, 1.0);

    // 世界空间 UV
    vec2 ws_uv = world_pos.xy + world_pos.yz + world_pos.xz;

    // 采样笔触和噪声（存到 varying 传给 light()）
    // ...

    ALBEDO = base_color.rgb;
    // 覆盖环境光，避免 PBR 间接光冲淡水墨效果
    IRRADIANCE = vec4(vec3(0.0), 1.0); // alpha=1 表示完全覆盖
}

// light() —— 逐光源调用，驱动皴擦染
void light() {
    float NdotL = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
    // 混合 rim 和 Lambert
    float area = mix(1.0 - NdotL, rim, lambert_rim_blend);
    // 阴影通过 ATTENUATION 参与
    area = clamp(area + (1.0 - ATTENUATION) * shadow_influence, 0.0, 1.0);

    // 皴 — 笔触纹理 × smoothstep 区域控制
    float cun = mix(1.0, stroke_cun, smoothstep(area_begin_cun, area_end_cun, area));
    // 擦 — 干笔
    float ca = mix(1.0, stroke_ca, smoothstep(area_begin_ca, area_end_ca, area));
    // inline — rim + 噪声
    float inline_v = step((rim - inline_b) / inline_a + noise * inline_cutoff, inline_threshold);

    float stroke = inline_v * cun * ca;
    vec3 remap = texture(color_remap, vec2(dist_normalized)).r * outline_color.rgb * 2.0;

    DIFFUSE_LIGHT += mix(remap, LIGHT_COLOR * ALBEDO, stroke) * ATTENUATION;
}
```

**Unity → Godot 语法对照**:

| Unity HLSL/CG | Godot GLSL |
|----------------|------------|
| `mul(unity_ObjectToWorld, v.vertex)` | `(MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz` |
| `UnityObjectToWorldNormal(v.normal)` | `mat3(MODEL_MATRIX) * NORMAL` |
| `UnityWorldSpaceViewDir(worldPos)` | `CAMERA_POSITION_WORLD - worldPos` |
| `UnityObjectToClipPos(v.vertex)` | `PROJECTION_MATRIX * MODELVIEW_MATRIX * vec4(VERTEX, 1.0)` |
| `UNITY_MATRIX_V` | `VIEW_MATRIX` |
| `UNITY_MATRIX_IT_MV` | `MODELVIEW_NORMAL_MATRIX` |
| `TransformViewToProjection(xy)` | `(PROJECTION_MATRIX * vec4(xy, 0.0, 0.0)).xy` |
| `_WorldSpaceCameraPos` | `CAMERA_POSITION_WORLD` |
| `_WorldSpaceLightPos0` | `LIGHT`（在 light() 中可用） |
| `tex2D(sampler, uv)` | `texture(sampler, uv)` |
| `fixed/half` | `float` |
| `lerp(a,b,t)` | `mix(a,b,t)` |
| `saturate(x)` | `clamp(x, 0.0, 1.0)` |
| `clip(x)` | `if (x < 0.0) discard;` |
| `mul((float3x3)M, v)` | `mat3(M) * v` |
| `SV_POSITION` (fragment) | `FRAGCOORD.xy` |

**关键移植注意点**:

1. **fragment() → light() 数据传递**: Godot 没有自定义 varying 从 fragment 传到 light，需要用全局 uniform 或在 light() 中重新计算（rim、笔触采样等）。可通过 `instance uniform` 或在 light() 中用 `NORMAL`/`VIEW` 重算 rim
2. **Outline 顶点外扩**: 轮廓 Pass 仍然 unshaded，在 `vertex()` 中手动 MVP 变换后偏移 `POSITION.xy`
3. **Screen-space UV**: Unity 的 `i.vertex.xy` (SV_POSITION) 对应 Godot 的 `FRAGCOORD.xy`
4. **深度偏移**: Unity `o.vertex.z -= 0.001` 对应 Godot 在 vertex 中调整 `POSITION.z`
5. **纹理格式**: Stroke 纹理的 RGBA 四通道需保持线性导入（关闭 sRGB）
6. **环境光抑制**: 在 fragment() 中设置 `IRRADIANCE = vec4(0.0, 0.0, 0.0, 1.0)` 完全覆盖引擎环境光，防止 PBR 间接光冲淡水墨效果
7. **light() 中重算 rim**: 因为 Godot 的 light() 无法从 fragment() 接收自定义 varying，rim 等信号需要在 light() 中用 `VIEW`/`NORMAL` 重新计算（开销不大，都是简单点积）

#### 实施步骤

1. **复制纹理资源**: 从 Unity 工程复制 ColorRemap、MatCap、Stroke、Noise 到 `Game/Assets/Shaders/InkPainting/Textures/`
2. **编写主表面 shader**: `ink_surface.gdshader` — fragment() 准备数据 + light() 驱动皴擦染
3. **编写轮廓 shader 0**: `ink_outline_0.gdshader` — render_mode unshaded + cull_front, 法线外扩 + MatCap mask + 飞白
4. **编写轮廓 shader 1**: `ink_outline_1.gdshader` — 同上但更宽
5. **组装材质**: 创建 ShaderMaterial → next_pass → next_pass 链
6. **场景测试**: 用 TripoModels 中的岩石模型测试效果，场景中放一盏 DirectionalLight3D
7. **调参**: `lambert_rim_blend` 从 1.0（纯 rim）开始，逐步混入灯光观察效果

## Reference
1. [仿宋代水墨山水画风格 3D 渲染的 Unity 实现](
https://indienova.com/indie-game-development/3d-rendering-of-imitation-song-dynasty-style-ink-landscape-painting-by-unity/)
2. [一个简单的水墨渲染方法](https://zhuanlan.zhihu.com/p/98948117)