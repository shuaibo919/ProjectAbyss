#include "AncientBuilding/AncientMasonry.h"

#include "AncientBuilding/Masonry.h"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>

using namespace godot;

namespace
{
	template <typename TPacked, typename TElement>
	TPacked ToPackedArray(const std::vector<TElement>& Source)
	{
		TPacked Result;
		if (!Source.empty())
		{
			Result.resize(int64_t(Source.size()));
			std::copy(Source.begin(), Source.end(), Result.ptrw());
		}
		return Result;
	}
} // namespace

#define ANCIENT_MASONRY_BIND_RANGE(VariantType, PropName, Member, Hint)                                \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &AncientMasonry::Set##Member);           \
	ClassDB::bind_method(D_METHOD("get_" PropName), &AncientMasonry::Get##Member);                    \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName, PROPERTY_HINT_RANGE, Hint), "set_" PropName, "get_" PropName);

#define ANCIENT_MASONRY_BIND(VariantType, PropName, Member)                                            \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &AncientMasonry::Set##Member);           \
	ClassDB::bind_method(D_METHOD("get_" PropName), &AncientMasonry::Get##Member);                    \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName), "set_" PropName, "get_" PropName);

void AncientMasonry::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("generate"), &AncientMasonry::Generate);
	ClassDB::bind_method(D_METHOD("bake_mesh"), &AncientMasonry::BakeMesh);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &AncientMasonry::GetTriangleCount);

	ADD_GROUP("Block", "");
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "length", Length, "0.5,400,0.01,or_greater")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "thickness", Thickness, "0.1,60,0.01,or_greater")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "height", Height, "0.1,40,0.01,or_greater")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "batter_ends", BatterEnds, "0,0.4,0.001")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "batter_faces", BatterFaces, "0,0.4,0.001")
	ClassDB::bind_method(D_METHOD("set_top_cap", "value"), &AncientMasonry::SetTopCap);
	ClassDB::bind_method(D_METHOD("has_top_cap"), &AncientMasonry::HasTopCap);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "top_cap"), "set_top_cap", "has_top_cap");
	ClassDB::bind_method(D_METHOD("set_parapet", "value"), &AncientMasonry::SetParapet);
	ClassDB::bind_method(D_METHOD("get_parapet"), &AncientMasonry::GetParapet);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "parapet", PROPERTY_HINT_ENUM,
		String::utf8("无 None,垛口 Crenel,宇墙 Plain")), "set_parapet", "get_parapet");
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "plinth_height", PlinthHeight, "0,5,0.01")

	ADD_GROUP("Arches", "arch_");
	ANCIENT_MASONRY_BIND_RANGE(Variant::INT, "arch_count", ArchCount, "0,24,1")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "arch_width", ArchWidth, "0,30,0.01")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "arch_height_ratio", ArchHeightRatio, "0.1,0.95,0.01")
	ClassDB::bind_method(D_METHOD("set_arch_profile", "value"), &AncientMasonry::SetArchProfile);
	ClassDB::bind_method(D_METHOD("get_arch_profile"), &AncientMasonry::GetArchProfile);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "arch_profile", PROPERTY_HINT_ENUM,
		String::utf8("半圆券 Semicircle,双心券 Pointed,平券 Flat")), "set_arch_profile", "get_arch_profile");
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "arch_sill", ArchSill, "0,30,0.01")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "arch_pitch", ArchPitch, "0,60,0.01")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "ring_thickness", RingThickness, "0,3,0.01")
	ANCIENT_MASONRY_BIND_RANGE(Variant::FLOAT, "ring_projection", RingProjection, "0,1,0.001")

	ADD_GROUP("Colors", "");
	ANCIENT_MASONRY_BIND(Variant::COLOR, "brick_color", BrickColor)
	ANCIENT_MASONRY_BIND(Variant::COLOR, "stone_color", StoneColor)
}

void AncientMasonry::_validate_property(PropertyInfo& Property) const
{
	// Derived from the parameters, like AncientBuilding's: never serialised.
	if (Property.name == StringName("mesh"))
	{
		Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
	}
}

void AncientMasonry::_ready()
{
	if (get_mesh().is_null())
	{
		Generate();
	}
}

void AncientMasonry::Regenerate()
{
	if (is_inside_tree())
	{
		Generate();
	}
}

Ref<ArrayMesh> AncientMasonry::BakeMesh()
{
	Generate();

	return get_mesh();
}

void AncientMasonry::Generate()
{
	using namespace BuildingGen;

	ArchedSlabDesc Desc;
	Desc.HalfLength = std::fmax(Length, 0.1f) * 0.5f;
	Desc.HalfThickness = std::fmax(Thickness, 0.05f) * 0.5f;
	Desc.Height = std::fmax(Height, 0.05f);
	Desc.BatterEnds = std::fmax(BatterEnds, 0.0f);
	Desc.BatterFaces = std::fmax(BatterFaces, 0.0f);
	Desc.bTopCap = bTopCap;
	Desc.Parapet = EParapet(std::clamp(Parapet, 0, 2));
	Desc.RingThickness = std::fmax(RingThickness, 0.0f);
	Desc.RingProjection = std::fmax(RingProjection, 0.0f);
	Desc.PlinthHeight = std::fmax(PlinthHeight, 0.0f);
	Desc.Tint = BrickColor;
	Desc.RingTint = StoneColor * 1.08f;
	Desc.PlinthTint = StoneColor;

	const int32_t Count = std::max(ArchCount, 0);
	const float TopHalf = Desc.HalfLength - Desc.BatterEnds * Desc.Height;
	const float Width = (ArchWidth > 0.0f) ? ArchWidth
		: std::fmin(2.0f * TopHalf * 0.55f / float(std::max(Count, 1)), 12.0f);
	const EArchProfile Profile = EArchProfile(std::clamp(ArchProfile, 0, 2));
	const float Pitch = (ArchPitch > 0.0f) ? ArchPitch : 2.0f * TopHalf / float(std::max(Count, 1));
	const float Rise = (Profile == EArchProfile::Semicircle) ? Width * 0.5f
		: (Profile == EArchProfile::Pointed) ? Width * 0.62f
		: std::fmin(Width * 0.18f, 0.6f);
	const float Top = Desc.Height * std::clamp(ArchHeightRatio, 0.1f, 0.95f);
	for (int32_t Index = 0; Index < Count; ++Index)
	{
		ArchOpening Opening;
		Opening.Centre = (float(Index) - float(Count - 1) * 0.5f) * Pitch;
		Opening.Width = Width;
		Opening.Sill = std::fmax(ArchSill, 0.0f);
		Opening.Profile = Profile;
		Opening.Rise = Rise;
		Opening.Spring = std::fmax(Top - ((Profile == EArchProfile::Flat) ? 0.0f : Rise), Opening.Sill + 0.05f);
		Desc.Openings.push_back(Opening);
	}

	MeshAccumulator Accumulated;
	Accumulated.SetMottle(0.05f);
	AddArchedSlab(Accumulated, Desc);

	LastTriangleCount = Accumulated.GetTriangleCount();
	if (Accumulated.Indices.empty())
	{
		set_mesh(Ref<ArrayMesh>());
		return;
	}

	if (Material.is_null())
	{
		Material.instantiate();
		Material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		Material->set_roughness(0.92f);
	}

	Array Arrays;
	Arrays.resize(Mesh::ARRAY_MAX);
	Arrays[Mesh::ARRAY_VERTEX] = ToPackedArray<PackedVector3Array>(Accumulated.Vertices);
	Arrays[Mesh::ARRAY_NORMAL] = ToPackedArray<PackedVector3Array>(Accumulated.Normals);
	Arrays[Mesh::ARRAY_TEX_UV] = ToPackedArray<PackedVector2Array>(Accumulated.UVs);
	Arrays[Mesh::ARRAY_COLOR] = ToPackedArray<PackedColorArray>(Accumulated.Colors);
	Arrays[Mesh::ARRAY_INDEX] = ToPackedArray<PackedInt32Array>(Accumulated.Indices);

	Ref<ArrayMesh> Result(memnew(ArrayMesh));
	Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays);
	Result->surface_set_material(0, Material);
	set_mesh(Result);
}
