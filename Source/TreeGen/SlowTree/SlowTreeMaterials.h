#pragma once

// SlowTree MaterialParams → fork StandardMaterial3D 映射(全部结论已对照 fork 引擎源码验证):
//  - roughness/metallic: 标量或 R=rough/G=metal 打包贴图(roughness_texture_channel /
//    metallic_texture_channel 为零预处理直通)
//  - albedo 贴图是替换语义, 顶点色 albedo 是乘法语义 → 叶卡有贴图+顶点色时开
//    FLAG_ALBEDO_FROM_VERTEX_COLOR(乘法); 枝干无顶点色, 只用贴图/纯色
//  - opacity 蒙版 R 通道 → 需 R→A 预处理(Image 拷贝), 配合 alpha_scissor_threshold
//  - SSS → subsurface_scattering_strength(fork 属性)
//  - 叶片双面(CULL_DISABLED), 枝干背面剔除
//  - aoStrength 无直接对应(StandardMaterial3D 无 AO 强度标量), 不映射(文档化差异)

#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/string.hpp>

struct MaterialParams;

namespace godot
{
	namespace SlowTreeMaterials
	{
		/** Builds a StandardMaterial3D from SlowTree material params. */
		Ref<StandardMaterial3D> Create(const MaterialParams& Params, bool bIsLeaf);

		/**
		 * Resolves a .vtree texture path (may be an absolute path from the design machine):
		 * try as-is, then the basename under res://textures/treegen/ and res://addons/abyss/textures/.
		 * Returns null when nothing loads — callers fall back to flat colour.
		 */
		Ref<Texture2D> ResolveTexture(const String& Path);

		/** ResolveTexture + R→A alpha preprocess for opacity masks. */
		Ref<Texture2D> ResolveOpacityTexture(const String& Path);
	} // namespace SlowTreeMaterials
} // namespace godot
