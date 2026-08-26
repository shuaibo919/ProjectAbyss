#include "SlowTreeMaterials.h"
#include "SlowTreeTypes.h"

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace
{
	// .vtree 里贴图路径是设计机上的绝对路径; 先按原路径加载(兼容 res:// 约定),
	// 失败则按 basename 在标准目录里搜索。
	Ref<Texture2D> LoadTextureBestEffort(const String& Path)
	{
		if (Path.is_empty())
		{
			return Ref<Texture2D>();
		}

		// 原样路径优先(res:// 或仍存在的绝对路径)。
		if (ResourceLoader::get_singleton()->exists(Path))
		{
			return ResourceLoader::get_singleton()->load(Path);
		}

		// basename 搜索兜底。
		const String File = Path.get_file();
		if (File.is_empty())
		{
			return Ref<Texture2D>();
		}
		static const char* SEARCH_DIRS[] = {
			"res://textures/treegen/",
			"res://addons/abyss/textures/",
		};
		for (const char* Dir : SEARCH_DIRS)
		{
			const String Candidate = String(Dir) + File;
			if (ResourceLoader::get_singleton()->exists(Candidate))
			{
				UtilityFunctions::push_warning(
					"SlowTree: texture '", Path, "' resolved to '", Candidate, "'");
				return ResourceLoader::get_singleton()->load(Candidate);
			}
		}
		UtilityFunctions::push_warning(
			"SlowTree: texture '", Path, "' not found; falling back to flat colour");
		return Ref<Texture2D>();
	}
} // namespace

namespace godot
{
	namespace SlowTreeMaterials
	{
		Ref<StandardMaterial3D> Create(const MaterialParams& Params, bool bIsLeaf)
		{
			Ref<StandardMaterial3D> Mat;
			Mat.instantiate();

			Mat->set_albedo(Color(Params.albedo.x, Params.albedo.y, Params.albedo.z));
			Mat->set_roughness(Params.roughness);
			Mat->set_metallic(Params.metallic);
			Mat->set_subsurface_scattering_strength(Params.sssStrength);

			// 贴图(缺失自动降级纯色, 见 LoadTextureBestEffort)。
			const Ref<Texture2D> AlbedoTex = ResolveTexture(String(Params.baseColorTex.c_str()));
			if (AlbedoTex.is_valid())
			{
				Mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, AlbedoTex);
				// 贴图是替换语义; 叶卡顶点色(COLOR)是乘法语义, 二者同用 = 叶纹理 × 叶色,
				// 与 SlowTree 渲染一致。枝干无顶点色通道, 不设该 flag。
				if (bIsLeaf)
				{
					Mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
				}
			}
			else if (bIsLeaf)
			{
				// 无贴图: 叶色纯靠顶点色。
				Mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			}

			const Ref<Texture2D> PackTex = ResolveTexture(String(Params.roughnessTex.c_str()));
			if (PackTex.is_valid())
			{
				Mat->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, PackTex);
				Mat->set_texture(BaseMaterial3D::TEXTURE_METALLIC, PackTex);
				// fork 打包贴图通道: R=roughness, G=metallic(零预处理直通)。
				Mat->set_roughness_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_RED);
				Mat->set_metallic_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_GREEN);
			}

			const Ref<Texture2D> NormalTex = ResolveTexture(String(Params.normalTex.c_str()));
			if (NormalTex.is_valid())
			{
				Mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, NormalTex);
			}

			const Ref<Texture2D> OpacityTex = ResolveOpacityTexture(String(Params.opacityTex.c_str()));
			if (OpacityTex.is_valid())
			{
				Mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, OpacityTex);
				// 蒙版已预处理成 alpha; 走 alpha scissor。
				Mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
				Mat->set_alpha_scissor_threshold(Params.alphaCutoff);
				// fork 属性名是 set_alpha_antialiasing(官方 4.x 为 set_alpha_antialiasing_mode)。
				Mat->set_alpha_antialiasing(BaseMaterial3D::ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE);
			}

			// 叶片双面, 枝干背面剔除(与 SlowTree 渲染一致)。
			Mat->set_cull_mode(bIsLeaf ? BaseMaterial3D::CULL_DISABLED : BaseMaterial3D::CULL_BACK);

			return Mat;
		}

		Ref<Texture2D> ResolveTexture(const String& Path)
		{
			return LoadTextureBestEffort(Path);
		}

		Ref<Texture2D> ResolveOpacityTexture(const String& Path)
		{
			// 蒙版源: 优先同名 opacityTex; 语义为 R 通道 → 预处理器拷成 alpha。
			const Ref<Texture2D> Source = LoadTextureBestEffort(Path);
			if (Source.is_null())
			{
				return Ref<Texture2D>();
			}

			const Ref<Image> SrcImg = Source->get_image();
			if (SrcImg.is_null() || SrcImg->is_empty())
			{
				return Ref<Texture2D>();
			}

			Ref<Image> Out = SrcImg->duplicate();
			Out->convert(Image::FORMAT_RGBA8);
			// R→A: 保留 RGB(供无预乘的 alpha blend 使用), alpha 取 R。
			for (int64_t y = 0; y < Out->get_height(); ++y)
			{
				for (int64_t x = 0; x < Out->get_width(); ++x)
				{
					const Color c = Out->get_pixel(x, y);
					Out->set_pixel(x, y, Color(c.r, c.r, c.r, c.r));
				}
			}

			return ImageTexture::create_from_image(Out);
		}
	} // namespace SlowTreeMaterials
} // namespace godot
