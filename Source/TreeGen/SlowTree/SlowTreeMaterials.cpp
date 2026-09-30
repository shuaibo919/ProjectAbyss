#include "SlowTreeMaterials.h"
#include "SlowTreeTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <vector>

using namespace godot;

namespace
{
ObjectID BambooFiberId;
std::array<std::array<ObjectID, 2>, 7> BarkIds;

float BarkHash(int32_t X, int32_t Y, int32_t PeriodX, int32_t PeriodY)
{
	uint32_t Value = uint32_t((X % PeriodX + PeriodX) % PeriodX) * 73856093u ^
					 uint32_t((Y % PeriodY + PeriodY) % PeriodY) * 19349663u;
	Value ^= Value >> 16;
	Value *= 0x7feb352du;
	Value ^= Value >> 15;
	return float(Value & 0xffffu) / 65535.0f;
}

float BarkNoise(float U, float V, int32_t Columns, int32_t Rows)
{
	const float X = U * Columns;
	const float Y = V * Rows;
	const int32_t CellX = int32_t(std::floor(X));
	const int32_t CellY = int32_t(std::floor(Y));
	const float FracX = X - std::floor(X);
	const float FracY = Y - std::floor(Y);
	const float BlendX = FracX * FracX * (3.0f - 2.0f * FracX);
	const float BlendY = FracY * FracY * (3.0f - 2.0f * FracY);
	const float Lower =
		BarkHash(CellX, CellY, Columns, Rows) * (1.0f - BlendX) + BarkHash(CellX + 1, CellY, Columns, Rows) * BlendX;
	const float Upper = BarkHash(CellX, CellY + 1, Columns, Rows) * (1.0f - BlendX) +
						BarkHash(CellX + 1, CellY + 1, Columns, Rows) * BlendX;
	return Lower + (Upper - Lower) * BlendY;
}
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
			UtilityFunctions::push_warning("SlowTree: texture '", Path, "' resolved to '", Candidate, "'");
			return ResourceLoader::get_singleton()->load(Candidate);
		}
	}
	UtilityFunctions::push_warning("SlowTree: texture '", Path, "' not found; falling back to flat colour");
	return Ref<Texture2D>();
}
} // namespace

namespace godot
{
namespace SlowTreeMaterials
{
Ref<Texture2D> GetBambooFiberTexture()
{
	if (ImageTexture* Cached = Object::cast_to<ImageTexture>(ObjectDB::get_instance(BambooFiberId)))
	{
		return Ref<ImageTexture>(Cached);
	}
	constexpr int32_t Size = 256;
	constexpr float Tau = 6.28318530718f;
	PackedByteArray Pixels;
	Pixels.resize(Size * Size * 3);
	uint8_t* Bytes = Pixels.ptrw();
	for (int32_t Y = 0; Y < Size; ++Y)
	{
		for (int32_t X = 0; X < Size; ++X)
		{
			const float U = float(X) / Size;
			const float V = float(Y) / Size;
			const float Fibers = std::sin(U * Tau * 37.0f + 0.4f * std::sin(V * Tau)) * 0.035f +
								 std::cos(U * Tau * 81.0f + 0.2f * std::cos(V * Tau * 3.0f)) * 0.018f;
			const float Mottle =
				std::sin(U * Tau * 7.0f + std::cos(V * Tau * 2.0f)) * std::cos(V * Tau * 9.0f) * 0.018f;
			const uint8_t Value = uint8_t(std::clamp(0.94f + Fibers + Mottle, 0.0f, 1.0f) * 255.0f);
			const int32_t Offset = (Y * Size + X) * 3;
			Bytes[Offset] = Bytes[Offset + 1] = Bytes[Offset + 2] = Value;
		}
	}
	Ref<Image> ImageData = Image::create_from_data(Size, Size, false, Image::FORMAT_RGB8, Pixels);
	ImageData->generate_mipmaps();
	Ref<ImageTexture> Texture = ImageTexture::create_from_image(ImageData);
	BambooFiberId = Texture->get_instance_id();
	return Texture;
}

Ref<Texture2D> GetBarkTexture(int32_t Preset, EBarkTexture Type)
{
	const int32_t Species = std::clamp(Preset, 0, 6);
	const size_t Slot = size_t(Type);
	if (ImageTexture* Cached = Object::cast_to<ImageTexture>(ObjectDB::get_instance(BarkIds[Species][Slot])))
	{
		return Ref<ImageTexture>(Cached);
	}
	constexpr int32_t Size = 512;
	std::vector<float> Relief(size_t(Size * Size));
	const std::array<int32_t, 7> BarkColumns{17, 21, 11, 19, 1, 27, 13};
	const std::array<int32_t, 7> BarkRows{4, 3, 7, 5, 1, 2, 17};
	for (int32_t Y = 0; Y < Size; ++Y)
	{
		for (int32_t X = 0; X < Size; ++X)
		{
			const float U = float(X) / Size;
			const float V = float(Y) / Size;
			const int32_t Columns = BarkColumns[Species];
			const int32_t Rows = BarkRows[Species];
			const float Warped = U + (BarkNoise(U, V, 4, 6) - 0.5f) * 0.035f;
			const float CellX = Warped * Columns;
			const float CellY = V * Rows;
			float First = 100.0f;
			float Second = 100.0f;
			if (Species == 2)
			{
				for (int32_t Dy = -1; Dy <= 1; ++Dy)
				{
					for (int32_t Dx = -1; Dx <= 1; ++Dx)
					{
						const int32_t TileX = int32_t(std::floor(CellX)) + Dx;
						const int32_t TileY = int32_t(std::floor(CellY)) + Dy;
						const float Jitter = BarkHash(TileX, TileY, Columns, Rows);
						const float OffsetX = TileX + 0.15f + 0.70f * Jitter - CellX;
						const float OffsetY =
							TileY + 0.18f + 0.64f * BarkHash(TileX + 7, TileY + 11, Columns, Rows) - CellY;
						const float Distance = OffsetX * OffsetX + OffsetY * OffsetY;
						if (Distance < First)
						{
							Second = First;
							First = Distance;
						}
						else
						{
							Second = std::min(Second, Distance);
						}
					}
				}
			}
			const float PlateCrack = std::exp(-(Second - First) * 35.0f);
			const float GrainValley = BarkNoise(Warped, V, Columns + 8, 4);
			const float LongCrack = std::pow(std::clamp((0.37f - GrainValley) / 0.37f, 0.0f, 1.0f), 0.65f);
			const float Crack = Species == 2 ? PlateCrack : LongCrack;
			const float Grain = BarkNoise(Warped, V, 127, 13);
			const float Flecks = BarkNoise(U, V, 193, 181);
			Relief[size_t(Y * Size + X)] =
				0.65f + 0.20f * Grain + 0.09f * Flecks - (Species == 6 ? 0.22f : 0.38f) * Crack;
			if (Species == 6)
			{
				// Prunus bark is comparatively smooth, with fine horizontal lenticels.
				const float Lenticel =
					std::pow(std::max(0.0f, 1.0f - std::abs(BarkNoise(U, V, 11, 47) - 0.45f) * 20.0f), 4.0f);
				Relief[size_t(Y * Size + X)] -= 0.08f * Lenticel;
			}
		}
	}
	PackedByteArray Pixels;
	Pixels.resize(Size * Size * 3);
	uint8_t* Bytes = Pixels.ptrw();
	for (int32_t Y = 0; Y < Size; ++Y)
	{
		for (int32_t X = 0; X < Size; ++X)
		{
			const int32_t Offset = (Y * Size + X) * 3;
			Vector3 Sample;
			if (Type == EBarkTexture::Normal)
			{
				const float Dx =
					Relief[size_t(Y * Size + (X + 1) % Size)] - Relief[size_t(Y * Size + (X + Size - 1) % Size)];
				const float Dy =
					Relief[size_t(((Y + 1) % Size) * Size + X)] - Relief[size_t(((Y + Size - 1) % Size) * Size + X)];
				Sample = Vector3(-Dx * 4.0f, -Dy * 2.3f, 1.0f).normalized() * 0.5f + Vector3(0.5f, 0.5f, 0.5f);
			}
			else
			{
				const float Value = Relief[size_t(Y * Size + X)];
				Sample = Vector3(Value, Value, Value);
			}
			for (int32_t Component = 0; Component < 3; ++Component)
			{
				Bytes[Offset + Component] = uint8_t(std::clamp(Sample[Component], 0.0f, 1.0f) * 255.0f);
			}
		}
	}
	Ref<Image> ImageData = Image::create_from_data(Size, Size, false, Image::FORMAT_RGB8, Pixels);
	ImageData->generate_mipmaps();
	Ref<ImageTexture> Texture = ImageTexture::create_from_image(ImageData);
	BarkIds[Species][Slot] = Texture->get_instance_id();
	return Texture;
}

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
