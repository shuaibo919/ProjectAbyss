#include "SlowTreeFoliage.h"

#include "CylinderSegment.h"
#include "NodeGraph.h"
#include "Nodes.h"
#include "SlowTreeMeshData.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <numeric>

using namespace godot;

namespace
{
constexpr int32_t TileSize = 256;
constexpr int32_t AtlasColumns = 4;
constexpr int32_t AtlasRows = 4;
constexpr int32_t AtlasVariants = 2;
constexpr float Pi = 3.14159265358979323846f;
ObjectID AtlasId;
ObjectID ShaderId;

uint32_t Hash(uint32_t Value)
{
	Value ^= Value >> 16;
	Value *= 0x7feb352du;
	Value ^= Value >> 15;
	Value *= 0x846ca68bu;
	return Value ^ (Value >> 16);
}

float Unit(uint32_t Value)
{
	return float(Hash(Value) & 0xffffffu) / 16777216.0f;
}

uint32_t PositionSeed(const Vector3& Position, int32_t Seed)
{
	return Hash(uint32_t(int32_t(Position.x * 1024.0f)) * 73856093u ^
				uint32_t(int32_t(Position.y * 1024.0f)) * 19349663u ^
				uint32_t(int32_t(Position.z * 1024.0f)) * 83492791u ^ uint32_t(Seed));
}

ETreeFoliageShape GetShape(int32_t Preset, const MaterialParams& Material)
{
	if (Material.albedo.x > Material.albedo.y)
	{
		return ETreeFoliageShape::Blossom;
	}
	return static_cast<ETreeFoliageShape>(std::clamp(Preset, 0, 6));
}

struct MaskLeaf
{
	Vector2 Base;
	Vector2 Tip;
	float HalfWidth;
	bool bFan = false;
	float Shade = 1.0f;
	float Curvature = 0.0f;
	bool bLanceolate = false;
	bool bVeins = false;
};

float LeafCoverage(const MaskLeaf& Leaf, const Vector2& Point, float& Shade)
{
	const Vector2 Axis = Leaf.Tip - Leaf.Base;
	const Vector2 Delta = Point - Leaf.Base;
	const float Length = Axis.length();
	const float Along = Delta.dot(Axis) / (Length * Length);
	if (Along <= 0.0f || Along >= 1.0f)
	{
		return 0.0f;
	}
	float CenteredDistance = Delta.cross(Axis) / Length;
	if (Leaf.Curvature != 0.0f)
	{
		CenteredDistance -= Leaf.Curvature * std::sin(Pi * Along);
	}
	const float Across = std::abs(CenteredDistance);
	float Profile = std::pow(std::sin(Pi * Along), 0.8f);
	if (Leaf.bLanceolate)
	{
		// A short petiole followed by a narrow, asymmetric blade with an extended pointed tip.
		const float Blade = std::clamp((Along - 0.12f) / 0.88f, 0.0f, 1.0f);
		Profile = Along < 0.12f ? 0.055f : std::pow(std::sin(Pi * std::pow(Blade, 0.76f)), 1.15f);
	}
	if (Leaf.bFan)
	{
		// A sector-shaped blade above a long petiole, with two rounded lobes at the distal rim.
		const float Blade = std::max(0.0f, (Along - 0.42f) / 0.58f);
		Profile =
			Along < 0.42f ? 0.025f : std::min(Blade * 2.5f, std::sqrt(std::max(0.0f, 1.0f - Blade * Blade)) / 0.70f);
		if (Along > 0.93f && Across < Leaf.HalfWidth * (Along - 0.93f) * 2.6f)
		{
			return 0.0f;
		}
	}
	if (Leaf.bVeins)
	{
		// Marginal irregularity stays below a texel on fine needles.
		Profile *= 1.0f - 0.035f * std::pow(std::sin(Along * Pi * 15.0f), 2.0f);
	}
	const float Edge = Leaf.HalfWidth * Profile - Across;
	if (Edge <= -0.5f / TileSize)
	{
		return 0.0f;
	}
	Shade = (0.76f + 0.18f * Along + 0.04f * std::cos(Across * 320.0f)) * Leaf.Shade;
	if (Across < 0.003f)
	{
		Shade *= 0.85f;
	}
	if (Leaf.bVeins)
	{
		const float VeinPhase = Leaf.bFan ? std::atan2(CenteredDistance, (Along - 0.42f) * Length) * 30.0f
										  : Along * 64.0f - Across / std::max(0.001f, Leaf.HalfWidth) * 3.0f;
		const float Vein = std::pow(0.5f + 0.5f * std::cos(VeinPhase), 12.0f);
		Shade *= 0.90f + 0.10f * (1.0f - Vein) + 0.06f * CenteredDistance / std::max(0.001f, Leaf.HalfWidth);
	}
	return std::clamp(0.5f + Edge * TileSize, 0.0f, 1.0f);
}

std::vector<MaskLeaf> MakeSpray(int32_t Shape, int32_t Variant)
{
	std::vector<MaskLeaf> Leaves;
	if (Shape == int32_t(ETreeFoliageShape::Bamboo))
	{
		// Small ultimate shoots, not a many-leaved feather. Blades remain separate in close-up.
		if (Variant == 0)
		{
			Leaves = {{{0.5f, 0.22f}, {0.10f, 0.60f}, 0.025f, false, 0.95f, 0.025f, true},
					  {{0.5f, 0.39f}, {0.91f, 0.73f}, 0.026f, false, 1.02f, -0.020f, true},
					  {{0.5f, 0.60f}, {0.58f, 0.96f}, 0.024f, false, 1.06f, 0.018f, true}};
		}
		else
		{
			Leaves = {{{0.5f, 0.20f}, {0.16f, 0.43f}, 0.021f, false, 0.91f, 0.016f, true},
					  {{0.5f, 0.33f}, {0.90f, 0.64f}, 0.026f, false, 0.99f, -0.024f, true},
					  {{0.5f, 0.50f}, {0.23f, 0.83f}, 0.024f, false, 1.07f, 0.023f, true},
					  {{0.5f, 0.64f}, {0.65f, 0.97f}, 0.023f, false, 1.02f, -0.014f, true}};
		}
		return Leaves;
	}
	if (Shape == int32_t(ETreeFoliageShape::Ginkgo))
	{
		const int32_t Count = Variant == 0 ? 5 : 4;
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			const uint32_t Seed = uint32_t(91 + Index * 13 + Variant * 47);
			const float Angle = -1.40f + 2.8f * Index / float(Count - 1) + 0.15f * Unit(Seed);
			const Vector2 Base(0.5f, 0.17f + 0.06f * Unit(Seed + 5));
			const float Span = 0.44f + 0.20f * Unit(Seed + 3);
			Leaves.push_back({Base, Base + Vector2(std::sin(Angle) * 0.55f, std::cos(Angle)) * Span,
							  0.15f + 0.025f * Unit(Seed + 7), true, 0.86f + 0.20f * Unit(Seed + 9), 0.0f, false,
							  true});
		}
		return Leaves;
	}
	if (Shape == int32_t(ETreeFoliageShape::Pine))
	{
		// Fascicles spread around a young shoot; two different projections avoid a repeated fern outline.
		const int32_t Fascicles = Variant == 0 ? 28 : 22;
		for (int32_t Index = 0; Index < Fascicles; ++Index)
		{
			const uint32_t Seed = uint32_t(311 + Index * 13 + Variant * 97);
			const float Height = 0.21f + 0.46f * Unit(Seed + 1);
			const float Side = Index % 2 == 0 ? -1.0f : 1.0f;
			const Vector2 Base(0.5f, Height);
			for (int32_t Needle = 0; Needle < 2; ++Needle)
			{
				const float Span = 0.16f + 0.23f * Unit(Seed + 3) + Needle * 0.015f;
				const Vector2 Tip(Base.x + Side * Span,
								  std::min(0.94f, Height + 0.05f + 0.29f * Unit(Seed + 5) + Needle * 0.033f));
				Leaves.push_back({Base, Tip, 0.0065f, false, 0.78f + 0.30f * Unit(Seed + 7),
								  Side * (0.01f + 0.01f * Unit(Seed + 11))});
			}
		}
		return Leaves;
	}
	if (Shape == int32_t(ETreeFoliageShape::Metasequoia))
	{
		const int32_t Pairs = Variant == 0 ? 12 : 10;
		for (int32_t Node = 0; Node < Pairs; ++Node)
		{
			const uint32_t Seed = uint32_t(719 + Node * 13 + Variant * 97);
			const float Height = 0.14f + 0.69f * float(Node) / Pairs;
			const float Span =
				(0.31f + 0.04f * Unit(Seed)) * std::pow(std::sin(Pi * (0.20f + 0.76f * Node / Pairs)), 0.65f);
			for (const float Side : {-1.0f, 1.0f})
			{
				Leaves.push_back(
					{Vector2(0.5f, Height), Vector2(0.5f + Side * Span, Height + 0.025f + 0.055f * Unit(Seed + 1)),
					 0.016f + 0.003f * Unit(Seed + 3), false, 0.85f + 0.15f * Unit(Seed + 5), 0.008f * Side});
			}
		}
		return Leaves;
	}
	const bool bWillow = Shape == int32_t(ETreeFoliageShape::Willow);
	const bool bPeach = Shape == int32_t(ETreeFoliageShape::Peach);
	const int32_t Count = bWillow ? (Variant == 0 ? 12 : 10) : (Variant == 0 ? 6 : 5);
	for (int32_t Index = 0; Index < Count; ++Index)
	{
		const uint32_t Seed = uint32_t(53 + Index * 31 + Variant * 173);
		const float Side = Index % 2 == 0 ? -1.0f : 1.0f;
		const float Height = 0.16f + (Index + 0.30f * Unit(Seed)) * 0.62f / Count;
		const float Span = 0.20f + 0.21f * Unit(Seed + 1);
		const Vector2 Base(0.5f, Height);
		Leaves.push_back({Base, Vector2(0.5f + Side * Span, Height + 0.10f + 0.075f * Unit(Seed + 2)),
						  (bWillow ? 0.026f : (bPeach ? 0.030f : 0.067f)) * (0.85f + 0.30f * Unit(Seed + 3)), false,
						  0.80f + 0.26f * Unit(Seed + 4), Side * (0.015f + 0.015f * Unit(Seed + 5)), true, true});
	}
	return Leaves;
}

/** Rasterize rounded, overlapping petals only inside their small pixel bounds. */
class BlossomTilePainter
{
public:
	BlossomTilePainter(uint8_t* InPixels, int32_t InWidth, int32_t Cell)
		: Pixels(InPixels), Width(InWidth), OriginX(Cell % AtlasColumns * TileSize),
		  OriginY(Cell / AtlasColumns * TileSize)
	{
		for (int32_t Y = 0; Y < TileSize; ++Y)
		{
			for (int32_t X = 0; X < TileSize; ++X)
			{
				const int32_t Offset = ((OriginY + Y) * Width + OriginX + X) * 4;
				Pixels[Offset] = Pixels[Offset + 1] = Pixels[Offset + 2] = 255;
				Pixels[Offset + 3] = 0;
			}
		}
	}

	void PaintStem(const Vector2& Start, const Vector2& End, float Radius)
	{
		const Vector2 Axis = End - Start;
		const float LengthSquared = Axis.length_squared();
		const int32_t MinX = ClampPixel(std::min(Start.x, End.x) - Radius - 1.0f / TileSize);
		const int32_t MaxX = ClampPixel(std::max(Start.x, End.x) + Radius + 1.0f / TileSize);
		const int32_t MinY = ClampPixel(std::min(Start.y, End.y) - Radius - 1.0f / TileSize);
		const int32_t MaxY = ClampPixel(std::max(Start.y, End.y) + Radius + 1.0f / TileSize);
		for (int32_t Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32_t X = MinX; X <= MaxX; ++X)
			{
				const Vector2 Point((X + 0.5f) / TileSize, (Y + 0.5f) / TileSize);
				const float Along = std::clamp((Point - Start).dot(Axis) / LengthSquared, 0.0f, 1.0f);
				const float Distance = Point.distance_to(Start + Axis * Along);
				BlendPixel(X, Y, Vector3(0.31f, 0.22f, 0.15f),
						   std::clamp(0.5f + (Radius * (1.0f - Along * 0.4f) - Distance) * TileSize, 0.0f, 1.0f));
			}
		}
	}

	void PaintPetal(const Vector2& Center, const Vector2& Axis, float Length, float HalfWidth, uint32_t Seed)
	{
		const Vector2 Side(-Axis.y, Axis.x);
		const float Extent = std::max(Length, HalfWidth) + 1.0f / TileSize;
		for (int32_t Y = ClampPixel(Center.y - Extent); Y <= ClampPixel(Center.y + Extent); ++Y)
		{
			for (int32_t X = ClampPixel(Center.x - Extent); X <= ClampPixel(Center.x + Extent); ++X)
			{
				const Vector2 Delta = Vector2((X + 0.5f) / TileSize, (Y + 0.5f) / TileSize) - Center;
				const float Along = Delta.dot(Axis) / Length;
				const float Across = Delta.dot(Side) / HalfWidth;
				const float Radius = std::sqrt(Along * Along + Across * Across);
				const float Coverage =
					std::clamp(0.5f + (1.0f - Radius) * std::min(Length, HalfWidth) * TileSize, 0.0f, 1.0f);
				if (Coverage <= 0.0f)
				{
					continue;
				}
				const float Tip = std::clamp(0.5f + Along * 0.5f, 0.0f, 1.0f);
				const float Fold = std::sqrt(std::max(0.0f, 1.0f - Across * Across));
				const float Vein = std::pow(0.5f + 0.5f * std::cos(Across * 31.0f + Along * 7.0f), 6.0f);
				const float Shade =
					(0.76f + Fold * 0.20f + Across * 0.07f - Vein * 0.045f) * (0.91f + Unit(Seed) * 0.12f);
				const Vector3 Color =
					Vector3(0.67f, 0.18f, 0.39f).lerp(Vector3(1.0f, 0.94f, 0.98f), 0.25f + Tip * 0.72f) * Shade;
				BlendPixel(X, Y, Color, Coverage);
			}
		}
	}

	void PaintFlower(const Vector2& Center, float Radius, float Angle, float Aspect, uint32_t Seed)
	{
		for (int32_t Petal = 0; Petal < 5; ++Petal)
		{
			const float Direction = Angle + Petal * Pi * 0.4f + (Unit(Seed + Petal * 23u) - 0.5f) * 0.16f;
			const Vector2 Projected(std::cos(Direction), std::sin(Direction) * Aspect);
			PaintPetal(Center + Projected * (Radius * 0.48f), Projected.normalized(),
					   Radius * (0.62f + Unit(Seed + Petal * 7u) * 0.10f) * Projected.length(),
					   Radius * 0.44f * std::sqrt(Aspect), Seed + Petal * 31u);
		}
		for (int32_t Stamen = 0; Stamen < 7; ++Stamen)
		{
			const float Direction = Stamen * Pi * 2.0f / 7.0f;
			const Vector2 Tip = Center + Vector2(std::cos(Direction), std::sin(Direction) * Aspect) * Radius * 0.16f;
			const int32_t X = ClampPixel(Tip.x);
			const int32_t Y = ClampPixel(Tip.y);
			BlendPixel(X, Y, Vector3(1.0f, 0.83f, 0.43f), 0.90f);
		}
	}

private:
	uint8_t* Pixels;
	int32_t Width;
	int32_t OriginX;
	int32_t OriginY;

	static int32_t ClampPixel(float Coordinate)
	{
		return std::clamp(int32_t(std::floor(Coordinate * TileSize)), 0, TileSize - 1);
	}

	void BlendPixel(int32_t X, int32_t Y, const Vector3& Color, float Coverage)
	{
		if (Coverage <= 0.0f)
		{
			return;
		}
		const int32_t Offset = ((OriginY + Y) * Width + OriginX + X) * 4;
		const float PreviousAlpha = Pixels[Offset + 3] / 255.0f;
		const float Alpha = Coverage + PreviousAlpha * (1.0f - Coverage);
		for (int32_t Channel = 0; Channel < 3; ++Channel)
		{
			const float Value =
				(Color[Channel] * Coverage + Pixels[Offset + Channel] / 255.0f * PreviousAlpha * (1.0f - Coverage)) /
				Alpha;
			Pixels[Offset + Channel] = uint8_t(std::clamp(Value, 0.0f, 1.0f) * 255.0f);
		}
		Pixels[Offset + 3] = uint8_t(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f);
	}
};

void PaintBlossomTile(uint8_t* Pixels, int32_t Width, int32_t Cell)
{
	BlossomTilePainter Painter(Pixels, Width, Cell);
	const auto AxisPosition = [](float Height)
	{ return Vector2(0.5f + 0.032f * std::sin((Height - 0.035f) * Pi * 2.0f), Height); };
	for (int32_t Segment = 0; Segment < 8; ++Segment)
	{
		Painter.PaintStem(AxisPosition(0.035f + Segment * 0.116f), AxisPosition(0.035f + (Segment + 1) * 0.116f),
						  0.0055f);
	}
	const int32_t Variant = Cell % AtlasVariants;
	for (int32_t Organ = 0; Organ < 9; ++Organ)
	{
		const uint32_t Seed = Hash(uint32_t(Organ + 1) * 379u + Variant * 2017u);
		const float Height = 0.17f + Organ * 0.087f + (Unit(Seed + 11) - 0.5f) * 0.050f;
		const float Side = Organ % 2 == Variant ? -1.0f : 1.0f;
		const Vector2 Center(0.5f + Side * (0.09f + Unit(Seed) * 0.19f), Height);
		Painter.PaintStem(AxisPosition(Height - 0.07f), Center, 0.0035f);
		const float Radius = 0.064f + Unit(Seed + 5) * 0.055f;
		const float Angle = Unit(Seed + 7) * Pi * 2.0f;
		if (Organ == 8 || (Variant == 1 && Organ == 2))
		{
			Painter.PaintPetal(Center, Vector2(Side * 0.5f, 0.8660254f), Radius * 0.70f, Radius * 0.38f, Seed);
		}
		else
		{
			Painter.PaintFlower(Center, Radius, Angle, Organ % 3 == 0 ? 0.52f : 0.94f, Seed);
		}
	}
}

Ref<ImageTexture> GetAtlas()
{
	if (ImageTexture* Cached = Object::cast_to<ImageTexture>(ObjectDB::get_instance(AtlasId)))
	{
		return Ref<ImageTexture>(Cached);
	}
	constexpr int32_t Width = TileSize * AtlasColumns;
	constexpr int32_t Height = TileSize * AtlasRows;
	PackedByteArray Pixels;
	Pixels.resize(Width * Height * 4);
	uint8_t* Bytes = Pixels.ptrw();
	for (int32_t Cell = 0; Cell < 8 * AtlasVariants; ++Cell)
	{
		const int32_t Shape = Cell / AtlasVariants;
		if (Shape == int32_t(ETreeFoliageShape::Blossom))
		{
			PaintBlossomTile(Bytes, Width, Cell);
			continue;
		}
		const std::vector<MaskLeaf> Leaves = MakeSpray(Shape, Cell % AtlasVariants);
		for (int32_t Y = 0; Y < TileSize; ++Y)
		{
			for (int32_t X = 0; X < TileSize; ++X)
			{
				const Vector2 Point((X + 0.5f) / TileSize, (Y + 0.5f) / TileSize);
				float Alpha = 0.0f;
				float Shade = 1.0f;
				for (const MaskLeaf& Leaf : Leaves)
				{
					float LeafShade = 1.0f;
					const float Coverage = LeafCoverage(Leaf, Point, LeafShade);
					if (Coverage > Alpha)
					{
						Alpha = Coverage;
						Shade = LeafShade;
					}
				}
				// The visible rachis meets the actual twig; no floating oval cloud.
				const float RachisEnd = Shape == int32_t(ETreeFoliageShape::Bamboo) ? 0.65f : 0.89f;
				if (Point.y > 0.06f && Point.y < (Shape == int32_t(ETreeFoliageShape::Ginkgo) ? 0.23f : RachisEnd))
				{
					Alpha = std::max(Alpha,
									 std::clamp(0.5f + (0.003f - std::abs(Point.x - 0.5f)) * TileSize, 0.0f, 1.0f));
				}
				const int32_t Offset =
					((Cell / AtlasColumns * TileSize + Y) * Width + Cell % AtlasColumns * TileSize + X) * 4;
				Bytes[Offset] = Bytes[Offset + 1] = Bytes[Offset + 2] = uint8_t(std::clamp(Shade, 0.0f, 1.0f) * 255.0f);
				Bytes[Offset + 3] = uint8_t(Alpha * 255.0f);
			}
		}
	}
	Ref<Image> Atlas = Image::create_from_data(Width, Height, false, Image::FORMAT_RGBA8, Pixels);
	Atlas->generate_mipmaps();
	// Preserve per-cell alpha-test coverage instead of erasing narrow needles in lower mips.
	PackedByteArray Mips = Atlas->get_data();
	uint8_t* MipBytes = Mips.ptrw();
	for (int32_t Shape = 0; Shape < 8 * AtlasVariants; ++Shape)
	{
		float TargetCoverage = 0.0f;
		for (int32_t Y = 0; Y < TileSize; ++Y)
		{
			for (int32_t X = 0; X < TileSize; ++X)
			{
				const int32_t Offset = ((Shape / 4 * TileSize + Y) * Width + Shape % 4 * TileSize + X) * 4;
				TargetCoverage += MipBytes[Offset + 3] >= 128 ? 1.0f : 0.0f;
			}
		}
		TargetCoverage /= float(TileSize * TileSize);
		for (int32_t Level = 1; (TileSize >> Level) >= 4; ++Level)
		{
			const int32_t Size = TileSize >> Level;
			const int32_t MipWidth = Width >> Level;
			const int64_t MipOffset = Atlas->get_mipmap_offset(Level);
			float Low = 0.0f;
			float High = 4.0f;
			for (int32_t Step = 0; Step < 10; ++Step)
			{
				const float Scale = (Low + High) * 0.5f;
				int32_t Covered = 0;
				for (int32_t Y = 0; Y < Size; ++Y)
				{
					for (int32_t X = 0; X < Size; ++X)
					{
						const int64_t Offset =
							MipOffset + ((Shape / 4 * Size + Y) * MipWidth + Shape % 4 * Size + X) * 4;
						Covered += MipBytes[Offset + 3] * Scale >= 127.5f ? 1 : 0;
					}
				}
				if (float(Covered) / float(Size * Size) < TargetCoverage)
				{
					Low = Scale;
				}
				else
				{
					High = Scale;
				}
			}
			for (int32_t Y = 0; Y < Size; ++Y)
			{
				for (int32_t X = 0; X < Size; ++X)
				{
					const int64_t Offset = MipOffset + ((Shape / 4 * Size + Y) * MipWidth + Shape % 4 * Size + X) * 4;
					MipBytes[Offset + 3] = uint8_t(std::min(255.0f, MipBytes[Offset + 3] * High));
				}
			}
		}
	}
	Atlas = Image::create_from_data(Width, Height, true, Image::FORMAT_RGBA8, Mips);
	Ref<ImageTexture> Texture = ImageTexture::create_from_image(Atlas);
	Texture->set_name("TreeGen botanical foliage atlas");
	AtlasId = Texture->get_instance_id();
	return Texture;
}
} // namespace

uint32_t SlowTreeFoliage::GetShootSeed(const Vector3& Position, int32_t Seed)
{
	return PositionSeed(Position, Seed);
}

void SlowTreeFoliage::ApplySpeciesRules(NodeGraph& Graph, int32_t Preset)
{
	for (const auto& Entry : Graph.nodes())
	{
		TreeNode* Node = Entry.second.get();
		if (Node->getType() == NodeType::Trunk)
		{
			TrunkParams& Params = static_cast<TrunkNode*>(Node)->params;
			if (Preset == 1)
			{
				Params.length = 6.2f;
				Params.endRadius = 0.025f;
				Params.noiseAmount = 18.0f;
				Params.gnarl = 5.0f;
			}
			if (Preset == 2 || Preset == 5)
			{
				Params.noiseAmount = 5.0f;
				Params.gnarl = 2.0f;
				Params.endRadius = 0.018f;
			}
			if (Preset == 3)
			{
				Params.length = 11.5f;
				Params.startRadius = 0.24f;
				Params.endRadius = 0.008f;
				Params.taperPow = 0.85f;
				Params.baseFlare = 1.3f;
				Params.noiseAmount = 9.0f;
				Params.gnarl = 4.0f;
				Params.material.albedo = Vector3(0.31f, 0.28f, 0.22f);
			}
		}
		if (Node->getType() == NodeType::Branch)
		{
			BranchParams& Params = static_cast<BranchNode*>(Node)->params;
			if (Preset == 1)
			{
				const bool bScaffold = Node->id == 2;
				Params.gravity = bScaffold ? 0.16f : 0.45f;
				Params.noiseAmount = bScaffold ? 14.0f : 18.0f;
				Params.spreadAngle = bScaffold ? 58.0f : 65.0f;
				Params.lengthRatio = bScaffold ? 0.75f : 0.48f;
				Params.radiusScale = bScaffold ? 0.58f : 0.38f;
				Params.lengthRatioVar = 0.065f;
				Params.spreadAngleVar = 10.0f;
				Params.sizeFalloff = bScaffold ? 0.22f : 0.15f;
				Params.branchCount = bScaffold ? 9 : 6;
				Params.baseFlare = 1.35f;
				Params.gnarl = 4.0f;
			}
			if ((Preset == 2 || Preset == 5) && Node->id == 2)
			{
				// A persistent leader and tapering lateral tiers define the crown envelope.
				Params.sizeFalloff = 0.85f;
				Params.lengthRatioVar = 0.025f;
				Params.spreadAngleVar = 6.0f;
			}
			if (Preset == 4 && Params.mode == BranchMode::Interval)
			{
				Params.branchesPerNode = 2;
			}
			if (Preset == 3)
			{
				const bool bScaffold = Node->id == 2;
				Params.lengthRatio = bScaffold ? 0.20f : 0.43f;
				Params.spreadAngle = bScaffold ? 56.0f : 48.0f;
				Params.sizeFalloff = bScaffold ? 0.68f : 0.12f;
				Params.gravity = 0.08f;
				Params.noiseAmount = 24.0f;
				Params.lengthRatioVar = bScaffold ? 0.035f : 0.12f;
				Params.spreadAngleVar = 15.0f;
				Params.radiusScale = bScaffold ? 0.48f : 0.30f;
				Params.radiusScaleVar = 0.06f;
				Params.endRatio = 0.055f;
				Params.baseFlare = 1.15f;
				Params.taperPow = 0.95f;
				Params.branchCount = bScaffold ? 24 : 9;
				Params.regionStart = bScaffold ? 0.10f : 0.20f;
				Params.regionEnd = 0.99f;
				Params.lengthSegs = bScaffold ? 9 : 5;
				Params.material.albedo = Vector3(0.30f, 0.27f, 0.21f);
			}
			if (Preset == 5)
			{
				Params.noiseAmount = 12.0f;
				Params.gravity = 0.20f;
				Params.radiusScale = Node->id == 2 ? 0.48f : 0.26f;
				if (Node->id == 2)
				{
					Params.mode = BranchMode::Interval;
					Params.branchesPerNode = 3;
					Params.intervalSpacing = 0.10f;
					Params.regionStart = 0.18f;
					Params.regionEnd = 0.99f;
				}
				else
				{
					Params.rotateOffset = 180.0f;
					Params.branchCount = 9;
				}
			}
		}
		if (Node->getType() == NodeType::Twig && Preset == 1)
		{
			TwigParams& Params = static_cast<TwigNode*>(Node)->params;
			Params.lengthRatio = 2.25f;
			Params.lengthRatioVar = 0.80f;
			Params.twigCount = 4;
			Params.gravity = 1.8f;
			Params.gravityVar = 0.2f;
			Params.radiusScale = 0.20f;
			Params.spreadAngle = 52.0f;
			Params.noiseAmount = 6.0f;
			Params.gnarl = 2.0f;
			Params.lengthSegs = 10;
			Params.sides = 3;
			Params.regionStart = 0.24f;
			Params.regionEnd = 1.0f;
		}
		if (Node->getType() == NodeType::LeafCluster && Preset == 1)
		{
			LeafClusterParams& Params = static_cast<LeafClusterNode*>(Node)->params;
			Params.leafCount = 32;
			Params.leafSize = 0.15f;
			Params.leafAspect = 0.12f;
			Params.material.albedo = Vector3(0.19f, 0.32f, 0.095f);
		}
		if (Node->getType() == NodeType::Twig && Preset == 3)
		{
			// Ginkgo bears clustered fan leaves on short shoots along persistent long shoots.
			TwigParams& Params = static_cast<TwigNode*>(Node)->params;
			Params.lengthRatio = 0.10f;
			Params.lengthRatioVar = 0.04f;
			Params.twigCount = 18;
			Params.radiusScale = 0.22f;
			Params.noiseAmount = 8.0f;
			Params.gravity = 0.05f;
			Params.baseFlare = 1.0f;
			Params.spreadAngle = 62.0f;
			Params.regionStart = 0.06f;
			Params.regionEnd = 1.0f;
		}
		if (Node->getType() == NodeType::LeafCluster && Preset == 3)
		{
			LeafClusterParams& Params = static_cast<LeafClusterNode*>(Node)->params;
			Params.leafCount = 6;
			Params.leafSize = 0.13f;
			Params.material.albedo = Vector3(0.20f, 0.34f, 0.075f);
		}
		if (Node->getType() == NodeType::Spine && Preset == 5)
		{
			SpineParams& Params = static_cast<SpineNode*>(Node)->params;
			Params.rotateOffset = 180.0f;
			Params.spineCount = 14;
			Params.gravity = 0.25f;
			Params.lengthRatio = 0.46f;
			Params.radiusScale = 0.10f;
		}
		if (Node->getType() == NodeType::Frond && Preset == 5)
		{
			static_cast<FrondNode*>(Node)->params.width = 0.12f;
			static_cast<FrondNode*>(Node)->params.material.albedo = Vector3(0.17f, 0.32f, 0.11f);
		}
		if (Node->getType() == NodeType::Twig && Preset == 4)
		{
			TwigParams& Params = static_cast<TwigNode*>(Node)->params;
			Params.radiusScale = 0.22f;
			Params.twigCount = 8;
		}
		if (Node->getType() == NodeType::LeafCluster && Preset == 4)
		{
			LeafClusterParams& Params = static_cast<LeafClusterNode*>(Node)->params;
			Params.leafCount = 12;
			Params.leafSize = 0.16f;
			Params.material.albedo = Vector3(0.11f, 0.26f, 0.070f);
		}
		if (Node->getType() == NodeType::LeafCluster && Preset == 6)
		{
			LeafClusterParams& Params = static_cast<LeafClusterNode*>(Node)->params;
			if (Params.material.albedo.y > Params.material.albedo.x)
			{
				Params.leafCount = 14;
				Params.leafSize = 0.15f;
				Params.material.albedo = Vector3(0.13f, 0.28f, 0.075f);
			}
			else
			{
				Params.material.albedo = Vector3(0.98f, 0.62f, 0.78f);
			}
		}
		if (Node->getType() == NodeType::LeafCluster && Preset == 0)
		{
			LeafClusterParams& Params = static_cast<LeafClusterNode*>(Node)->params;
			Params.leafSize = 0.16f;
			Params.material.albedo = Vector3(0.14f, 0.29f, 0.075f);
		}
		if (Node->getType() == NodeType::Frond && Preset == 2)
		{
			static_cast<FrondNode*>(Node)->params.width = 0.19f;
			static_cast<FrondNode*>(Node)->params.material.albedo = Vector3(0.11f, 0.23f, 0.105f);
		}
		if (Node->getType() == NodeType::Roots && (Preset == 1 || Preset == 3))
		{
			RootsParams& Params = static_cast<RootsNode*>(Node)->params;
			Params.length = Preset == 3 ? 0.65f : 0.95f;
			Params.radiusScale = 0.22f;
			Params.baseFlare = 1.2f;
		}
	}
	if (Preset == 1)
	{
		// Retain an intermediate woody tier between scaffold branches and pendant shoots.
		TreeNode* Twigs = Graph.getNode(4);
		if (Graph.getNode(3) && Twigs && !Twigs->inputPins.empty())
		{
			const NodeId SupportId = Graph.addChildNode(3, NodeType::Branch);
			BranchNode* Support = static_cast<BranchNode*>(Graph.getNode(SupportId));
			Support->params = static_cast<BranchNode*>(Graph.getNode(3))->params;
			Support->params.branchCount = 3;
			Support->params.lengthRatio = 0.52f;
			Support->params.lengthRatioVar = 0.18f;
			Support->params.radiusScale = 0.44f;
			Support->params.regionStart = 0.35f;
			Support->params.regionEnd = 0.98f;
			Support->params.spreadAngle = 48.0f;
			Support->params.gravity = 0.30f;
			Support->params.seed = 127;
			Graph.addLink(Support->outputPin.id, Twigs->inputPins.front().id);
		}
	}
}

void SlowTreeFoliage::ShapePendantShoot(std::vector<BranchRing>& Rings, float Length)
{
	if (Rings.size() < 2)
	{
		return;
	}
	const Vector3 Initial = Rings.front().up;
	// Local y=0 is the planting plane. A pendant shoot must not continue below the roots.
	const float AvailableLength = std::min(Length, std::max(0.05f, Rings.front().center.y - 0.35f));
	const float StepLength = AvailableLength / float(Rings.size() - 1);
	for (size_t Index = 1; Index < Rings.size(); ++Index)
	{
		const float T = float(Index) / float(Rings.size() - 1);
		const float Blend = std::clamp((T - 0.06f) / 0.58f, 0.0f, 1.0f);
		const float Smooth = Blend * Blend * (3.0f - 2.0f * Blend);
		Vector3 Direction = Initial.lerp(Vector3(0.08f * Initial.x, -1.0f, 0.08f * Initial.z), Smooth).normalized();
		Vector3 Right = Rings[Index - 1].right;
		Right = (Right - Direction * Right.dot(Direction)).normalized();
		Rings[Index].center = Rings[Index - 1].center + (Rings[Index - 1].up + Direction).normalized() * StepLength;
		Rings[Index].up = Direction;
		Rings[Index].right = Right;
	}
}

namespace
{
void CollectAttachedSprays(TreeMeshData& Data, const std::vector<BranchRing>& Rings,
						   const std::vector<float>& Distances, const MaterialParams& Material, ETreeFoliageShape Shape,
						   int32_t LeafCount, float LeafSize, int32_t Seed, float Phase,
						   const TreeGrowthSettings& Growth)
{
	const float Length = Distances.back();
	const bool bGinkgo = Shape == ETreeFoliageShape::Ginkgo;
	const bool bWillow = Shape == ETreeFoliageShape::Willow;
	const bool bPine = Shape == ETreeFoliageShape::Pine;
	const bool bFeather = Shape == ETreeFoliageShape::Metasequoia;
	const bool bBlossom = Shape == ETreeFoliageShape::Blossom;
	if (bBlossom && Growth.PeachBlossomDensity <= 0.0f)
	{
		return;
	}
	const float Spacing = LeafSize * (bWillow ? 2.3f : (bBlossom ? 2.65f : (bFeather ? 2.2f : 2.0f)));
	const float CountScale = bBlossom ? std::max(0.05f, LeafCount / 18.0f * Growth.PeachBlossomDensity) : 1.0f;
	const int32_t Count =
		bGinkgo ? 1
				: std::clamp(std::max(int32_t(std::ceil(Length / Spacing * CountScale)),
									  bFeather || bPine || bBlossom ? 1 : int32_t(std::ceil(LeafCount / 7.0f))),
							 1, 40);
	const uint32_t BaseSeed = PositionSeed(Rings.front().center, Seed);
	for (int32_t Index = 0; Index < Count; ++Index)
	{
		const uint32_t CardSeed = Hash(BaseSeed + uint32_t(Index) * 2654435761u);
		const float Distance = bGinkgo ? 0.0f : Length * float(Index) / Count;
		const size_t Upper =
			std::min(size_t(std::upper_bound(Distances.begin(), Distances.end(), Distance) - Distances.begin()),
					 Rings.size() - 1);
		const size_t Lower = Upper > 0 ? Upper - 1 : 0;
		const float Blend = (Distance - Distances[Lower]) / std::max(0.0001f, Distances[Upper] - Distances[Lower]);
		const Vector3 Base = Rings[Lower].center.lerp(Rings[Upper].center, Blend);
		TreeFoliageCard Card;
		Card.Up = Rings[Lower].up.lerp(Rings[Upper].up, Blend).normalized();
		const Vector3 Horizontal = Card.Up.cross(Vector3(0, 1, 0));
		Vector3 Right =
			bFeather && Horizontal.length_squared() > 0.0001f ? Horizontal.normalized() : Rings[Lower].right;
		Right = (Right - Card.Up * Right.dot(Card.Up)).normalized();
		Card.Right = Right.rotated(Card.Up, (Unit(CardSeed + 1) - 0.5f) * (bFeather ? 0.55f : 2.0f * Pi));
		Card.Length = bGinkgo
						  ? LeafSize * (2.3f + 0.4f * Unit(CardSeed + 6))
						  : std::clamp(Length / Count / 0.80f, LeafSize * (bBlossom ? 1.4f : 1.8f), LeafSize * 3.0f);
		Card.Width = LeafSize * (bPine ? 2.8f : (bFeather ? 1.5f : (bWillow ? 2.7f : 2.5f))) *
					 (0.84f + 0.30f * Unit(CardSeed + 2));
		if (bGinkgo)
		{
			Card.Width = Card.Length * (0.90f + 0.15f * Unit(CardSeed + 2));
		}
		if (bBlossom)
		{
			Card.Width = LeafSize * (2.55f + 0.45f * Unit(CardSeed + 2)) * Growth.PeachBlossomScale;
			Card.Length =
				std::clamp(Length / Count / 0.88f, LeafSize * 1.8f, LeafSize * 3.4f) * Growth.PeachBlossomScale;
		}
		// Align the mask's painted stem base with the real bearing axis, including its UV gutter.
		const float MaskRoot = ((bBlossom ? 0.035f : 0.06f) - 0.01f) / 0.98f;
		Card.Anchor = Base + Card.Up * (Card.Length * (0.5f - MaskRoot));
		Card.Color = Material.albedo * (0.78f + 0.30f * Unit(CardSeed + 3));
		Card.Phase = Phase + Unit(CardSeed + 4) * 6.0f;
		Card.Seed = CardSeed;
		Card.Shape = Shape;
		Data.FoliageCards.push_back(Card);
	}
}
} // namespace

void SlowTreeFoliage::CollectCards(TreeMeshData& Data, const std::vector<BranchRing>& Rings,
								   const MaterialParams& Material, const TreeFoliageOptions& Options, int32_t LeafCount,
								   float LeafSize, int32_t Seed, float Phase)
{
	if (LeafCount <= 0 || Options.Density <= 0.0f)
	{
		return;
	}
	std::vector<float> Distances(Rings.size(), 0.0f);
	for (size_t Index = 1; Index < Rings.size(); ++Index)
	{
		Distances[Index] = Distances[Index - 1] + Rings[Index].center.distance_to(Rings[Index - 1].center);
	}
	const float Length = Distances.back();
	if (Length < 0.001f)
	{
		return;
	}
	const ETreeFoliageShape Shape = GetShape(Options.Preset, Material);
	if (SlowTreeGrowth::IsEnabled(Options) && Shape != ETreeFoliageShape::Bamboo)
	{
		CollectAttachedSprays(Data, Rings, Distances, Material, Shape, LeafCount, LeafSize, Seed, Phase, Options.Growth);
		return;
	}
	// Keep the mask's physical proportions: a long twig receives more sprays, not stretched leaves.
	const float MaxSprayLength = LeafSize * (Shape == ETreeFoliageShape::Ginkgo ? 2.6f : 4.0f);
	const bool bBamboo = Shape == ETreeFoliageShape::Bamboo && SlowTreeGrowth::IsEnabled(Options);
	const int32_t Count = Shape == ETreeFoliageShape::Ginkgo || bBamboo
							  ? 1
							  : std::clamp(std::max(int32_t(std::ceil(LeafCount / 6.0f)),
													int32_t(std::ceil(Length / std::max(0.05f, MaxSprayLength)))),
										   1, 32);
	const uint32_t BaseSeed = PositionSeed(Rings.front().center, Seed);
	for (int32_t Index = 0; Index < Count; ++Index)
	{
		const uint32_t CardSeed = Hash(BaseSeed + uint32_t(Index) * 2654435761u);
		const float Along =
			Shape == ETreeFoliageShape::Ginkgo ? 0.90f : (Index + 0.3f + 0.4f * Unit(CardSeed + 7)) / Count;
		const float Distance = Length * Along;
		const size_t RingIndex =
			std::min(size_t(std::lower_bound(Distances.begin(), Distances.end(), Distance) - Distances.begin()),
					 Rings.size() - 1);
		const size_t Previous = RingIndex > 0 ? RingIndex - 1 : 0;
		const float Blend =
			(Distance - Distances[Previous]) / std::max(0.0001f, Distances[RingIndex] - Distances[Previous]);
		TreeFoliageCard Card;
		Card.Anchor = Rings[Previous].center.lerp(Rings[RingIndex].center, Blend);
		Card.Up = Rings[Previous].up.lerp(Rings[RingIndex].up, Blend).normalized();
		Vector3 Right = Rings[Previous].right;
		Right = (Right - Card.Up * Right.dot(Card.Up)).normalized();
		Card.Right = Right.rotated(Card.Up, Unit(CardSeed + 1) * 2.0f * Pi);
		if (SlowTreeGrowth::IsEnabled(Options) &&
			(Shape == ETreeFoliageShape::Metasequoia || Shape == ETreeFoliageShape::Bamboo))
		{
			const Vector3 PlanarRight = Card.Up.cross(Vector3(0, 1, 0));
			if (PlanarRight.length_squared() > 0.0001f)
			{
				Card.Right = PlanarRight.normalized().rotated(Card.Up, (Unit(CardSeed + 1) - 0.5f) * 0.35f);
			}
		}
		Card.Length = std::max(LeafSize * 1.7f, Length / Count * 1.24f);
		Card.Width = LeafSize * (Shape == ETreeFoliageShape::Ginkgo ? 2.9f : 2.2f);
		if (Shape == ETreeFoliageShape::Pine || Shape == ETreeFoliageShape::Metasequoia)
		{
			Card.Width = LeafSize * 2.0f;
		}
		if (Shape == ETreeFoliageShape::Pine && SlowTreeGrowth::IsEnabled(Options))
		{
			Card.Width = LeafSize * 3.1f;
			Card.Length = std::max(Card.Length, LeafSize * 2.8f);
		}
		if (SlowTreeGrowth::IsEnabled(Options) &&
			(Shape == ETreeFoliageShape::Broadleaf || Shape == ETreeFoliageShape::Metasequoia))
		{
			Card.Width = LeafSize * 3.0f;
			Card.Length = std::max(Card.Length, LeafSize * 2.8f);
		}
		Card.Width *= 0.85f + 0.3f * Unit(CardSeed + 2);
		if (Shape == ETreeFoliageShape::Ginkgo)
		{
			Card.Length = LeafSize * (2.5f + 1.1f * Unit(CardSeed + 6));
			Card.Width = Card.Length * (0.87f + 0.15f * Unit(CardSeed + 2));
			Card.Up = (Card.Up + Vector3(0.0f, 0.45f, 0.0f)).normalized();
			Card.Right = (Card.Right - Card.Up * Card.Right.dot(Card.Up)).normalized();
		}
		if (Shape == ETreeFoliageShape::Willow)
		{
			Card.Width *= 0.80f + 0.24f * Unit(CardSeed + 6);
		}
		if (bBamboo)
		{
			Card.Length = LeafSize * Options.Growth.BambooLeafScale * (1.85f + 0.45f * Unit(CardSeed + 6));
			Card.Width = Card.Length * (0.90f + 0.12f * Unit(CardSeed + 2));
			Card.Up = (Rings.back().center - Rings.front().center).normalized();
			const Vector3 Side = Card.Up.cross(Vector3(0, 1, 0));
			Card.Right = (Side.length_squared() > 0.0001f ? Side.normalized() : Right)
							 .rotated(Card.Up, (Unit(CardSeed + 1) - 0.5f) * 1.5f);
			// UV y=.06 is the petiole root. Both crossed planes start exactly on their supporting twig.
			Card.Anchor = Rings.front().center + Card.Up * (Card.Length * 0.44f);
		}
		Card.Color = Material.albedo * (0.90f + 0.20f * Unit(CardSeed + 3));
		Card.Phase = Phase + Unit(CardSeed + 4) * 6.0f;
		Card.Seed = CardSeed;
		Card.Shape = Shape;
		Data.FoliageCards.push_back(Card);
	}
}

void SlowTreeFoliage::AppendCards(TreeMeshData& Data, const TreeFoliageOptions& Options)
{
	if (Data.FoliageCards.empty())
	{
		return;
	}
	std::vector<size_t> Selected;
	Selected.reserve(Data.FoliageCards.size());
	for (size_t Index = 0; Index < Data.FoliageCards.size(); ++Index)
	{
		if (Unit(Data.FoliageCards[Index].Seed) < Options.Density)
		{
			Selected.push_back(Index);
		}
	}
	if (Selected.size() > size_t(Options.MaxCards))
	{
		// Hash rank thins the entire crown, never the traversal tail. Attachment stays stable.
		std::nth_element(Selected.begin(), Selected.begin() + Options.MaxCards, Selected.end(),
						 [&Data](size_t Left, size_t Right)
						 {
							 return Data.FoliageCards[Left].Seed < Data.FoliageCards[Right].Seed;
						 });
		Selected.resize(Options.MaxCards);
		std::sort(Selected.begin(), Selected.end());
		Data.bFoliageBudgetApplied = true;
	}
	MeshBatch Batch;
	Batch.isLeaf = true;
	Batch.bMaskedFoliage = true;
	Batch.vertices.reserve(Selected.size() * 8 * 16);
	Batch.indices.reserve(Selected.size() * 12);
	for (const size_t Index : Selected)
	{
		const TreeFoliageCard& Card = Data.FoliageCards[Index];
		const int32_t Shape = int32_t(Card.Shape);
		for (int32_t Plane = 0; Plane < 2; ++Plane)
		{
			const int32_t Cell = Shape * AtlasVariants + int32_t(Hash(Card.Seed + Plane * 71u) % AtlasVariants);
			const Vector3 Right = Plane == 0 ? Card.Right : Card.Up.cross(Card.Right).normalized();
			const Vector3 Normal = Right.cross(Card.Up).normalized();
			const uint32_t Base = uint32_t(Batch.vertices.size() / 16);
			for (int32_t Corner = 0; Corner < 4; ++Corner)
			{
				const float U = Corner == 1 || Corner == 2 ? 1.0f : 0.0f;
				const float V = Corner >= 2 ? 1.0f : 0.0f;
				const Vector3 Offset = Right * ((U - 0.5f) * Card.Width) + Card.Up * ((V - 0.5f) * Card.Length);
				const Vector3 Position = Card.Anchor + Offset;
				const Vector3 SoftNormal = (Normal * 0.4f + Offset.normalized() * 0.6f).normalized();
				// A transparent gutter in each tile prevents neighboring leaf types bleeding.
				const float AtlasU = (Cell % AtlasColumns + 0.01f + U * 0.98f) / float(AtlasColumns);
				const float AtlasV = (Cell / AtlasColumns + 0.01f + V * 0.98f) / float(AtlasRows);
				Batch.vertices.insert(Batch.vertices.end(),
									  {Position.x, Position.y, Position.z, SoftNormal.x, SoftNormal.y, SoftNormal.z,
									   AtlasU, AtlasV, Card.Color.x, Card.Color.y, Card.Color.z, 1.0f, Card.Phase,
									   Card.Anchor.x, Card.Anchor.y, Card.Anchor.z});
			}
			Batch.indices.insert(Batch.indices.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
		}
	}
	Data.FoliageCardCount = uint32_t(Selected.size());
	Data.batches.push_back(std::move(Batch));
	Data.FoliageCards.clear();
}

Ref<Material> SlowTreeFoliage::CreateMaterial(float Season)
{
	Ref<Shader> FoliageShader;
	if (Shader* Cached = Object::cast_to<Shader>(ObjectDB::get_instance(ShaderId)))
	{
		FoliageShader = Ref<Shader>(Cached);
	}
	else
	{
		FoliageShader.instantiate();
		FoliageShader->set_code(R"SHADER(shader_type spatial;
render_mode cull_disabled, diffuse_burley, specular_disabled, alpha_to_coverage;
uniform sampler2D foliage_atlas : source_color, filter_linear_mipmap, repeat_disable;
uniform float season : hint_range(0.0, 4.0) = 2.0;
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
    vec4 texel = texture(foliage_atlas, UV);
    int shape = (int(floor(UV.x * 4.0)) + 4 * int(floor(UV.y * 4.0))) / 2;
    bool evergreen = shape == 2 || shape == 4;
    bool blossom = shape == 7;
    float leaf_seed = fract(sin(phase * 17.31) * 43758.5453);
    float coverage = evergreen ? 1.0 : smoothstep(0.12, 1.2, min(season, 4.0 - season));
    if (blossom) { coverage = smoothstep(0.3, 0.8, season) * (1.0 - smoothstep(1.2, 1.8, season)); }
    if (shape == 6) { coverage *= smoothstep(1.28, 1.95, season); }
    float autumn = evergreen || blossom ? 0.0 : smoothstep(2.35, 3.3, season + leaf_seed * 0.18);
    vec3 autumn_color = shape == 3 ? mix(vec3(0.62, 0.40, 0.035), vec3(1.0, 0.76, 0.12), leaf_seed) : vec3(0.64, 0.29, 0.055);
    ALBEDO = mix(COLOR.rgb, autumn_color, autumn) * texel.rgb;
    ROUGHNESS = 0.86;
    BACKLIGHT = ALBEDO * (blossom ? 0.42 : 0.22);
    if (!FRONT_FACING) { NORMAL = -NORMAL; }
    ALPHA = texel.a * (coverage > leaf_seed ? 1.0 : 0.0);
    ALPHA_SCISSOR_THRESHOLD = 0.5;
    ALPHA_ANTIALIASING_EDGE = 0.15;
    ALPHA_TEXTURE_COORDINATE = UV * vec2(textureSize(foliage_atlas, 0));
}
)SHADER");
		FoliageShader->set_name("TreeGen masked foliage");
		ShaderId = FoliageShader->get_instance_id();
	}
	Ref<ShaderMaterial> Material;
	Material.instantiate();
	Material->set_shader(FoliageShader);
	Material->set_shader_parameter("foliage_atlas", GetAtlas());
	Material->set_shader_parameter("season", Season);
	Material->set_name("TreeGen crossed leaf clusters");
	return Material;
}

void SlowTreeFoliage::UpdateMaterial(const Ref<Material>& Material, float Season, float WindStrength, float WindTime)
{
	Ref<ShaderMaterial> ShaderMaterialRef = Material;
	if (ShaderMaterialRef.is_valid())
	{
		ShaderMaterialRef->set_shader_parameter("season", Season);
		ShaderMaterialRef->set_shader_parameter("wind_strength", WindStrength);
		ShaderMaterialRef->set_shader_parameter("wind_time", WindTime);
	}
}
