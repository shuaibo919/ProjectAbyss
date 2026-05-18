# Rendering System Architecture (Godot 4.7-beta)

## 1. 整体架构总览

Godot 的渲染系统采用**分层架构**，从上层场景抽象到底层 GPU 驱动，共 8 层：

```
+----------------------------------------------------------+
| Layer 1: RenderingServer (公共 API 单例)                   |
|   servers/rendering/rendering_server.h                    |
+----------------------------------------------------------+
| Layer 2: RenderingMethod (场景管理 / 可见性剔除)            |
|   servers/rendering/rendering_method.h                    |
|   servers/rendering/renderer_scene_cull.h                 |
+----------------------------------------------------------+
| Layer 3: RendererSceneRender (渲染后端抽象)                 |
|   servers/rendering/renderer_scene_render.h               |
+----------------------------------------------------------+
| Layer 4: RendererSceneRenderRD (RD 后端实现)               |
|   servers/rendering/renderer_rd/renderer_scene_render_rd.h|
|   (后处理效果链, Sky, GI 系统均在此层)                      |
+----------------------------------------------------------+
| Layer 5: RenderForwardClustered / RenderForwardMobile     |
|   renderer_rd/forward_clustered/render_forward_clustered.h|
|   renderer_rd/forward_mobile/render_forward_mobile.h      |
+----------------------------------------------------------+
| Layer 6: RendererCompositor (工厂 + 帧生命周期管理)         |
|   servers/rendering/renderer_compositor.h                 |
|   renderer_rd/renderer_compositor_rd.h                    |
+----------------------------------------------------------+
| Layer 7: RenderingDevice (GPU 操作抽象层)                  |
|   servers/rendering/rendering_device.h                    |
|   rendering_device_graph.h (命令图, 自动 barrier)          |
+----------------------------------------------------------+
| Layer 8: RenderingDeviceDriver (GPU API 后端)              |
|   drivers/vulkan/rendering_device_driver_vulkan.h         |
|   drivers/d3d12/rendering_device_driver_d3d12.h           |
|   drivers/gles3/rasterizer_gles3.h (独立路径)              |
+----------------------------------------------------------+
```

### 核心原理

- **RenderingServer** 是唯一对外暴露的接口，所有场景节点（MeshInstance3D, Light3D, Camera3D 等）通过 RID 与它交互
- **RendererSceneCull** 做视锥体剔除（Frustum Culling）和遮挡剔除（Occlusion Culling），将可见几何体列表传递给下层
- **RenderForwardClustered** 是桌面端默认的渲染方法，采用 **Forward+ (Clustered Forward)** 架构 —— 通过将屏幕空间划分为 3D Cluster，在每个 Cluster 中记录影响它的灯光列表，从而在 Fragment Shader 中高效遍历灯光
- **RenderingDevice** 封装了所有 GPU 操作（纹理、缓冲区、Shader、Pipeline、Framebuffer、DrawList / ComputeList），底层使用 **命令图（RenderingDeviceGraph）** 自动管理 barrier 和命令重排

---

## 2. Forward+ Clustered 渲染管线详解

### 2.1 管线主函数

```
RenderForwardClustered::_render_scene()
  文件: servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:1704
```

### 2.2 完整渲染流程

```
                       _render_scene() 入口
                              |
              +---------------+---------------+
              |                               |
     1. 准备阶段 (Prepare)              2. 环境设置 (Setup)
     - SDFGI/VoxelGI 准备               - VRS 更新
     - ClusterBuilder 初始化             - 环境 UBO 更新
                                         - 灯光/Lightmap 设置
              |                               |
              +---------------+---------------+
                              |
                 3. 填充渲染列表 (Fill Render Lists)
                    遍历所有可见几何实例，分入 4 个列表:
                    - RENDER_LIST_OPAQUE     (不透明)
                    - RENDER_LIST_MOTION     (不透明+运动)
                    - RENDER_LIST_ALPHA      (半透明)
                    - RENDER_LIST_SECONDARY  (阴影等)
                              |
                 4. 排序 (Sort)
                    - Opaque/Motion: 按 Shader Key 排序 (减少状态切换)
                    - Alpha: 按深度逆序 + Priority 排序 (正确混合)
                              |
                 5. 填充实例数据到 GPU 缓冲区
                              |
                 6. 确定 Depth Prepass 模式
                    - PASS_MODE_DEPTH (仅深度)
                    - PASS_MODE_DEPTH_NORMAL_ROUGHNESS (深度+法线+粗糙度)
                    - PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI (含 VoxelGI)
                              |
                 7. Sky 设置 (Radiance 缓冲区, 半/四分辨率 Sky)
                              |
    ======================== GPU 渲染阶段 ========================
                              |
                 8. Depth Prepass
                    - 渲染所有不透明几何的深度 (可选法线/粗糙度)
                    - 为 SSAO/SSIL/SSR/SDFGI 提供数据
                              |
                 9. CompositorEffect: PRE_OPAQUE
                              |
                10. 预不透明计算 (Pre-Opaque)
                    - SSAO (屏幕空间环境光遮蔽)
                    - SSIL (屏幕空间间接光照)
                    - GI (全局光照整合)
                              |
                11. **不透明 Pass (Opaque Pass)** ← 核心渲染 Pass
                    - 渲染所有不透明几何
                    - 完整光照: Clustered 灯光 + GI + Shadow
                    - PBR/GGX BRDF
                              |
                12. MSAA Resolve (如启用)
                              |
                13. CompositorEffect: POST_OPAQUE
                              |
                14. Sky 渲染 (绘制天空到 Color Buffer)
                              |
                15. CompositorEffect: POST_SKY
                              |
                16. 屏幕空间效果
                    - SSR (屏幕空间反射)
                    - Screen Texture Copy
                    - Depth Texture Copy
                              |
                17. CompositorEffect: PRE_TRANSPARENT
                              |
                18. **半透明 Pass (Transparent Pass)**
                    - 按深度逆序渲染半透明几何
                              |
                19. CompositorEffect: POST_TRANSPARENT
                              |
    ======================== 后处理阶段 ========================
                              |
                20. 后处理 + Tonemapping
                    - DOF (景深)
                    - Auto Exposure (自动曝光)
                    - Glow (辉光, 7 级 Mip Gaussian Blur)
                    - Temporal Upscaling (FSR2 / MetalFX)
                    - Spatial Upscaling (FSR1)
                    - SMAA / FXAA
                    - Tonemapping (Linear/Reinhard/Filmic/ACES/AgX)
                    - Color Correction (LUT)
                    - Debanding
```

### 2.3 Pass 模式定义

```cpp
// render_forward_clustered.h:200
enum PassMode {
    PASS_MODE_COLOR,                           // 颜色绘制 (不透明+半透明)
    PASS_MODE_SHADOW,                          // 阴影绘制
    PASS_MODE_SHADOW_DP,                       // 双面阴影 (Dual Paraboloid)
    PASS_MODE_DEPTH,                           // 仅深度
    PASS_MODE_DEPTH_NORMAL_ROUGHNESS,          // 深度+法线+粗糙度
    PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI, // 深度+法线+粗糙度+VoxelGI
    PASS_MODE_DEPTH_MATERIAL,                  // 深度+材质信息
    PASS_MODE_SDF,                             // SDF 生成
    PASS_MODE_MAX
};
```

### 2.4 Uniform Set 布局

```cpp
SCENE_UNIFORM_SET = 0,        // 全局场景数据 (Camera, Time, Fog, GI...)
RENDER_PASS_UNIFORM_SET = 1,  // 每 Pass 数据 (灯光列表, GI, Shadow Atlas)
TRANSFORMS_UNIFORM_SET = 2,   // 每实例变换矩阵
MATERIAL_UNIFORM_SET = 3,     // 每材质数据 (Texture, Params)
```

---

## 3. Render Scene Buffers 系统

### 3.1 架构

```
RenderSceneBuffers (基类, RefCounted)
  └── RenderSceneBuffersRD (RD 实现)
        ├── 命名纹理系统: (Context, BufferName) → Texture
        ├── 自定义数据: RenderBufferCustomDataRD
        └── Slice 访问: (Layer, Mipmap, View)
```

### 3.2 核心缓冲区

| 名称 | 用途 |
|------|------|
| `RB_TEX_COLOR` / `_MSAA` | 主颜色目标 |
| `RB_TEX_DEPTH` / `_MSAA` | 深度缓冲 |
| `RB_TEX_VELOCITY` / `_MSAA` | 运动向量 (TAA/FSR2) |
| `RB_TEX_SPECULAR` | 分离式镜面反射 (SSS 用) |
| `RB_TEX_NORMAL_ROUGHNESS` | G-Buffer 法线+粗糙度 (屏幕空间效果) |
| `RB_TEX_VOXEL_GI` | VoxelGI 数据 |
| `RB_TEX_BLUR_0` / `_1` | Glow/Blur 链 |
| `RB_TEX_COLOR_UPSCALED` | 上采样输出 (FSR2/MetalFX) |

自定义 Pass 可以通过 `create_texture()` 在 `RenderSceneBuffersRD` 中注册新的命名纹理。

---

## 4. Shader 系统

### 4.1 编译流程

```
Godot Shader Language (.gdshader)
        |
        v
ShaderPreprocessor (预处理, #include 展开)
        |
        v
ShaderLanguage (词法/语法分析, AST)
        |
        v
ShaderCompiler (生成 GLSL)
        |
        v
GLSL → SPIR-V (VulkanContext) / DXIL (D3D12)
        |
        v
RenderingShaderContainer (二进制 Shader 缓存)
```

### 4.2 场景 Shader 文件

| 文件 | 职责 |
|------|------|
| `scene_forward_clustered.glsl` | 主顶点/片元着色器 |
| `scene_forward_clustered_inc.glsl` | 公共数据结构和 Uniform 声明 |
| `scene_forward_lights_inc.glsl` | PBR 光照 (GGX BRDF, 区域光, SSS) |
| `scene_forward_gi_inc.glsl` | GI 整合 (SDFGI, VoxelGI, Lightmap) |
| `scene_forward_aa_inc.glsl` | AA 整合 |

### 4.3 PBR 光照模型核心

```
scene_forward_lights_inc.glsl:

BRDF:
  D = GGX Distribution (各向异性/各向同性)
  V = Smith GGX Visibility
  F = Schlick Fresnel

Diffuse:
  "PBR Diffuse Lighting for GGX+Smith Microsurfaces" (Earl Hammon, Jr.)

SSS:
  - Skin 模式: Multi-Gaussian 近似
  - 通用模式: Transmittance
```

### 4.4 Shader Specialization 系统

`SceneShaderForwardClustered` 使用 **Specialization Constants** 在编译时确定变体：
- GI 类型 (Disabled / SDFGI / VoxelGI / Both)
- 阴影质量
- 雾模式
- MultiMesh 标志
- Ubershader 回退

这避免了传统 uber-shader 的分支开销，同时支持后台异步编译 Pipeline（`PipelineDeferredRD`）。

---

## 5. Compositor Effect 系统 (自定义 Pass 的首选方案)

### 5.1 原理

`CompositorEffect` 是 Godot 4.x 提供的**无需修改引擎源码**即可注入自定义渲染逻辑的机制。

```
scene/resources/compositor.h

CompositorEffect : Resource
  ├── effect_callback_type: 5 个注入点
  ├── 资源需求标志 (resolved color/depth, motion vectors, normal/roughness...)
  └── _render_callback(type, RenderData*) ← 虚函数, 你的入口
```

### 5.2 注入点

```
Depth Prepass
    ↓
[PRE_OPAQUE]        ← 可在此注入自定义全屏 Pass
    ↓
SSAO / SSIL / GI
    ↓
Opaque Pass
    ↓
MSAA Resolve
    ↓
[POST_OPAQUE]       ← 可读取不透明渲染结果
    ↓
Sky Rendering
    ↓
[POST_SKY]          ← 可在 Sky 后注入效果
    ↓
SSR / Screen Copy
    ↓
[PRE_TRANSPARENT]   ← 半透明之前
    ↓
Transparent Pass
    ↓
[POST_TRANSPARENT]  ← 所有 3D 渲染完毕后, 后处理之前
```

### 5.3 使用方式 (GDScript / C++)

```gdscript
class_name MyOutlineEffect extends CompositorEffect

func _init():
    effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
    access_resolved_color = true
    access_resolved_depth = true
    needs_normal_roughness = true

func _render_callback(callback_type: int, render_data: RenderData):
    var rd = RenderingServer.get_rendering_device()
    var scene_buffers = render_data.get_render_scene_buffers()
    var rb = scene_buffers as RenderSceneBuffersRD

    # 获取缓冲纹理
    var color_tex = rb.get_color_texture()
    var depth_tex = rb.get_depth_texture()

    # 使用 RenderingDevice API 执行自定义渲染
    # rd.draw_list_begin(...) / rd.compute_list_begin(...)
    # ...
```

### 5.4 挂载到场景

```
Camera3D / WorldEnvironment
  └── Compositor (Resource)
        └── effects: [MyOutlineEffect, ...]
```

---

## 6. 引擎级自定义 Pass 实现流程

当 `CompositorEffect` 的能力不足（例如需要修改不透明 Pass 的渲染行为本身、需要新的 G-Buffer 输出、或需要全新的 Pass 模式），则需要修改引擎源码。

### 6.1 添加新 Pass Mode

**Step 1**: 在 `render_forward_clustered.h` 添加新的 PassMode 枚举：

```cpp
enum PassMode {
    // ... 现有枚举 ...
    PASS_MODE_NPR_OUTLINE,   // 新增: NPR 描边 Pass
    PASS_MODE_MAX
};
```

**Step 2**: 在 `SceneShaderForwardClustered` 中为新 Pass 注册 Shader 变体：

```
scene_shader_forward_clustered.h / .cpp
  - 添加对应的 shader_version
  - 在 shader 编译表中注册新的 #define
```

**Step 3**: 编写 GLSL Shader（或在现有 Shader 中添加 #ifdef 分支）：

```
shaders/forward_clustered/scene_forward_clustered.glsl
  - 在 fragment() 中根据 pass_mode 输出不同数据
```

**Step 4**: 在 `_render_scene()` 中的适当位置调用新 Pass：

```cpp
// render_forward_clustered.cpp, _render_scene() 内
// 在 Opaque Pass 之后、POST_OPAQUE compositor 之前插入
_render_list(..., PASS_MODE_NPR_OUTLINE, ...);
```

**Step 5**: 管理新 Pass 所需的 Render Target（在 `RenderSceneBuffersRD` 中注册）。

### 6.2 添加新的渲染效果模块

参照 `servers/rendering/renderer_rd/effects/` 下现有效果的模式：

```
effects/
  ├── bokeh_dof.h/cpp         (景深)
  ├── copy_effects.h/cpp      (复制/转换)
  ├── ss_effects.h/cpp        (SSAO/SSIL)
  ├── tone_mapper.h/cpp       (色调映射)
  ├── smaa.h/cpp              (抗锯齿)
  ├── fsr2.h/cpp              (超分辨率)
  └── ...
```

创建新效果的模板：

```cpp
// effects/npr_effects.h
class NPREffects {
    RenderingDevice *rd;
    RID shader;
    RID pipeline;

public:
    void setup(RenderingDevice *p_rd);
    void process(RID p_source_color, RID p_source_depth,
                 RID p_source_normal, RID p_dest, const Size2i &p_size);
};
```

### 6.3 修改光照模型

直接修改 `scene_forward_lights_inc.glsl`：

```glsl
// 在 light_compute() 或 light_process_*() 中:
#ifdef NPR_LIGHTING
    // 替换 PBR BRDF 为 NPR 光照
    float ndotl = dot(normal, light_direction);
    float ramp = texture(npr_ramp_texture, vec2(ndotl * 0.5 + 0.5, 0.0)).r;
    diffuse_light += light_color * ramp * attenuation;
#else
    // 原有 PBR 逻辑
#endif
```

---

## 7. NPR 风格渲染 Future Plan

基于以上分析，以下是在此引擎中实现 NPR 风格渲染的完整计划。

### Phase 0: 技术选型与分层策略

| 需求 | 实现层 | 理由 |
|------|--------|------|
| 描边 (Outline) | CompositorEffect (POST_OPAQUE) | 全屏后处理，无需改引擎 |
| NPR 光照 (Ramp/Cel) | Shader 层 (gdshader / 引擎 GLSL) | 需替换 BRDF |
| 色彩控制 (Tone/Palette) | CompositorEffect (POST_TRANSPARENT) | 全屏 LUT/色彩映射 |
| 笔触/纹理化 | CompositorEffect + 自定义 Shader | Hatching / Stippling |
| 面部阴影控制 (SDF Face Shadow) | 材质 Shader + 自定义 Uniform | 角色级别控制 |

### Phase 1: 描边系统 (Outline)

**方法**: 多层描边策略

1. **屏幕空间描边 (Screen-Space Edge Detection)**
   - 实现为 `CompositorEffect`，注入点: `POST_OPAQUE`
   - 需求标志: `access_resolved_depth = true`, `needs_normal_roughness = true`
   - 算法: Roberts Cross / Sobel 算子检测深度和法线不连续
   - 输出: 边缘遮罩纹理

2. **背面扩展描边 (Inverted Hull / Back-Face Outline)**
   - 在材质的 `next_pass` 中使用反面剔除 + 顶点沿法线外扩的 Shader
   - 优点: 支持逐物体粗细控制，风格更明确
   - 用于角色和重要物体

3. **描边控制参数**
   - 全局: 线宽、颜色、深度/法线阈值
   - 逐物体: 线宽倍率、颜色覆盖、是否启用
   - 逐材质: 内轮廓线开关

### Phase 2: NPR 光照模型

**方法 A (非侵入式): 自定义 GDShader**

```gdshader
shader_type spatial;
render_mode unshaded;  // 关闭默认 PBR

uniform sampler2D ramp_texture : filter_linear;
uniform float shadow_threshold = 0.5;
uniform vec3 shadow_color : source_color = vec3(0.6, 0.4, 0.7);

void fragment() {
    vec3 normal = NORMAL;
    vec3 light_dir = ...; // 从 uniform 或 light hint 获取
    float ndotl = dot(normal, light_dir);

    // 阶梯化光照
    float ramp = texture(ramp_texture, vec2(ndotl * 0.5 + 0.5, 0.0)).r;
    float cel = step(shadow_threshold, ramp);

    ALBEDO = base_color * mix(shadow_color, vec3(1.0), cel);
}
```

> 缺点: `unshaded` 模式下失去所有引擎光照特性（多灯光、阴影、GI）

**方法 B (引擎级): 修改光照计算**

在 `scene_forward_lights_inc.glsl` 中添加 NPR 光照路径：

1. 添加 `render_mode npr_lighting;` 支持：
   - `shader_compiler.cpp` 中注册新的 `render_mode_defines["npr_lighting"] = "#define NPR_LIGHTING\n"`
2. 在 `light_compute()` 中实现分支：
   - Ramp Texture 采样替代 GGX BRDF
   - 阶梯化衰减
   - 保留阴影接收（但阴影也做阶梯化）
3. 保留 Clustered Lighting 的多灯光支持

**推荐**: Phase 2 先用方法 A 做原型验证效果，确认美术方向后再用方法 B 做引擎级集成。

### Phase 3: 面部阴影控制 (SDF Face Shadow)

针对角色面部的特殊阴影需求：

1. **SDF 面部阴影贴图**
   - 预计算面部法线的 SDF (Signed Distance Field)
   - 在 Fragment Shader 中用 SDF 采样替代 N dot L
   - 实现平滑、可控的面部明暗分界线

2. **实现方式**
   - 使用自定义 GDShader，配合面部 SDF 纹理
   - 通过 UV2 或额外 Uniform 传入 SDF 数据
   - 支持面部朝向与光照方向的旋转矩阵计算

### Phase 4: 色彩与后处理风格化

1. **色彩分级 (Color Grading)**
   - 利用现有 Tonemapping 系统的 3D LUT 或自定义 CompositorEffect
   - 限定色板 (Palette Quantization)
   - 饱和度/对比度调整

2. **笔触效果 (Hatching / Cross-Hatching)**
   - CompositorEffect 在 `POST_TRANSPARENT`
   - 根据光照强度选择不同密度的笔触纹理 (TAM - Tonal Art Maps)
   - 屏幕空间或切线空间映射

3. **大气/雾风格化**
   - 修改雾效计算，使用渐变色/阶梯化替代物理衰减

### Phase 5: 工程化与工具链

1. **NPR Material Inspector**
   - 自定义 EditorPlugin，提供 NPR 材质参数的可视化编辑
   - Ramp Texture 预览和编辑器
   - 描边参数实时预览

2. **NPR Preset System**
   - 预设系统: 赛璐珞、水彩、素描等风格一键切换
   - 存储为 Resource，可在不同场景间复用

3. **性能优化**
   - 描边效果使用半分辨率渲染
   - Ramp Texture 打包为 Texture Array
   - 减少 Shader 变体数量

### 实施优先级与里程碑

```
M1 (原型): GDShader NPR 光照 + 屏幕空间描边 CompositorEffect
           → 验证美术方向，无需改引擎

M2 (集成): 引擎级 NPR 光照模式 + 背面描边 + SDF 面部阴影
           → 完整光照支持，角色品质提升

M3 (完善): 色彩风格化后处理 + 笔触效果 + 编辑器工具
           → 完整 NPR 管线

M4 (优化): 性能调优 + Shader 变体管理 + 预设系统
           → 生产就绪
```

### 关键文件修改清单 (M2 阶段)

| 文件 | 修改内容 |
|------|----------|
| `render_forward_clustered.h` | 添加 NPR 相关 PassMode (如描边 Pass) |
| `render_forward_clustered.cpp` | `_render_scene()` 中插入 NPR Pass 调用 |
| `scene_shader_forward_clustered.h/cpp` | 注册 NPR Shader 变体 |
| `scene_forward_lights_inc.glsl` | 添加 `#ifdef NPR_LIGHTING` 分支 |
| `shader_compiler.cpp` | 注册 `render_mode npr_lighting` |
| `renderer_scene_render_rd.cpp` | 后处理链中添加 NPR 色彩处理 |
| `effects/` 新增 `npr_effects.h/cpp` | 描边检测、笔触效果等 Compute Shader |
| `scene/resources/` 新增 `npr_material.h` | NPR 材质资源定义 (可选) |
