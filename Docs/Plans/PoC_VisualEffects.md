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

## Reference
1. [仿宋代水墨山水画风格 3D 渲染的 Unity 实现](
https://indienova.com/indie-game-development/3d-rendering-of-imitation-song-dynasty-style-ink-landscape-painting-by-unity/)
2. [一个简单的水墨渲染方法](https://zhuanlan.zhihu.com/p/98948117)