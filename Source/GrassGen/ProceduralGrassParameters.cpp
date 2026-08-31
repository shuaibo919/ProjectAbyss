#include "GrassGen/ProceduralGrassParameters.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

#define GRASS_BIND(VariantType, PropName, Member)                                                     \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &ProceduralGrassParameters::Set##Member); \
	ClassDB::bind_method(D_METHOD("get_" PropName), &ProceduralGrassParameters::Get##Member);          \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName), "set_" PropName, "get_" PropName);

#define GRASS_BIND_RANGE(VariantType, PropName, Member, Hint)                                          \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &ProceduralGrassParameters::Set##Member); \
	ClassDB::bind_method(D_METHOD("get_" PropName), &ProceduralGrassParameters::Get##Member);          \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName, PROPERTY_HINT_RANGE, Hint), "set_" PropName, "get_" PropName);

String ProceduralGrassParameters::GetSpeciesName(int32_t Species)
{
	switch (Species)
	{
		case SPECIES_FOXTAIL: return "Foxtail";
		case SPECIES_SHORT: return "Short";
		case SPECIES_WEED: return "Weed";
		default: return "Thatch";
	}
}

String ProceduralGrassParameters::GetSpeciesNameLocalized(int32_t Species)
{
	// Narrow literals reach Godot through string_new_with_utf8_chars, and godot-cpp compiles
	// MSVC with /utf-8, so these are safe as long as the source stays UTF-8 encoded.
	const char* Chinese = "";
	switch (Species)
	{
		case SPECIES_FOXTAIL: Chinese = "狗尾巴草"; break;
		case SPECIES_SHORT: Chinese = "小草"; break;
		case SPECIES_WEED: Chinese = "杂草"; break;
		default: Chinese = "茅草"; break;
	}

	return vformat("%s (%s)", GetSpeciesName(Species), String::utf8(Chinese));
}

void ProceduralGrassParameters::_bind_methods()
{
	ADD_GROUP("Species", "");
	ClassDB::bind_method(D_METHOD("set_species", "value"), &ProceduralGrassParameters::SetSpecies);
	ClassDB::bind_method(D_METHOD("get_species"), &ProceduralGrassParameters::GetSpecies);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "species", PROPERTY_HINT_ENUM, "Thatch,Foxtail,Short,Weed"),
		"set_species", "get_species");
	GRASS_BIND_RANGE(Variant::FLOAT, "seed", Seed, "0,1000000,1")

	ADD_GROUP("Shape", "");
	GRASS_BIND_RANGE(Variant::FLOAT, "scale", Scale, "0.1,5,0.01,or_greater")
	GRASS_BIND_RANGE(Variant::FLOAT, "clump_radius", ClumpRadius, "0.02,1.0,0.01,or_greater")
	GRASS_BIND_RANGE(Variant::INT, "blade_count", BladeCount, "0,60,1")
	GRASS_BIND_RANGE(Variant::FLOAT, "curvature", Curvature, "0,3,0.01")
	GRASS_BIND_RANGE(Variant::FLOAT, "lean_angle", LeanAngle, "0,60,0.5")
	GRASS_BIND_RANGE(Variant::FLOAT, "lean_azimuth", LeanAzimuth, "0,360,1")

	ADD_GROUP("Color", "");
	GRASS_BIND_RANGE(Variant::FLOAT, "color_variance", ColorVariance, "0,1,0.01")
	ClassDB::bind_method(D_METHOD("set_use_species_colors", "value"), &ProceduralGrassParameters::SetUseSpeciesColors);
	ClassDB::bind_method(D_METHOD("should_use_species_colors"), &ProceduralGrassParameters::ShouldUseSpeciesColors);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_species_colors"), "set_use_species_colors", "should_use_species_colors");
	GRASS_BIND(Variant::COLOR, "base_color", BaseColor)
	GRASS_BIND(Variant::COLOR, "tip_color", TipColor)

	ClassDB::bind_static_method("ProceduralGrassParameters",
		D_METHOD("get_species_name", "species"), &ProceduralGrassParameters::GetSpeciesName);
	ClassDB::bind_static_method("ProceduralGrassParameters",
		D_METHOD("get_species_name_localized", "species"), &ProceduralGrassParameters::GetSpeciesNameLocalized);
}
