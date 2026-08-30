#include "RockGen/ProceduralRockParameters.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

#define ROCK_BIND(VariantType, PropName, Member)                                                     \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &ProceduralRockParameters::Set##Member); \
	ClassDB::bind_method(D_METHOD("get_" PropName), &ProceduralRockParameters::Get##Member);          \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName), "set_" PropName, "get_" PropName);

#define ROCK_BIND_RANGE(VariantType, PropName, Member, Hint)                                          \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &ProceduralRockParameters::Set##Member); \
	ClassDB::bind_method(D_METHOD("get_" PropName), &ProceduralRockParameters::Get##Member);          \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName, PROPERTY_HINT_RANGE, Hint), "set_" PropName, "get_" PropName);

String ProceduralRockParameters::GetFormName(int32_t Form)
{
	switch (Form)
	{
		case FORM_ELLIPSOID: return "Pebble";
		case FORM_ROUNDED_BOX: return "Slab";
		default: return "Boulder";
	}
}

String ProceduralRockParameters::GetFormNameLocalized(int32_t Form)
{
	// Narrow literals reach Godot through string_new_with_utf8_chars, and godot-cpp compiles
	// MSVC with /utf-8, so these are safe as long as the source stays UTF-8 encoded.
	const char* Chinese = "";
	switch (Form)
	{
		case FORM_ELLIPSOID: Chinese = "鹅卵石"; break;
		case FORM_ROUNDED_BOX: Chinese = "岩块"; break;
		default: Chinese = "巨石"; break;
	}

	return vformat("%s (%s)", GetFormName(Form), String::utf8(Chinese));
}

void ProceduralRockParameters::_bind_methods()
{
	ADD_GROUP("Form", "");
	ClassDB::bind_method(D_METHOD("set_form", "value"), &ProceduralRockParameters::SetForm);
	ClassDB::bind_method(D_METHOD("get_form"), &ProceduralRockParameters::GetForm);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "form", PROPERTY_HINT_ENUM, "Boulder,Pebble,Slab"),
		"set_form", "get_form");

	ROCK_BIND_RANGE(Variant::INT, "resolution", Resolution, "16,128,1")
	ROCK_BIND_RANGE(Variant::FLOAT, "scale", Scale, "0.1,20,0.01,or_greater")
	ROCK_BIND_RANGE(Variant::INT, "steps", Steps, "8,72,1")
	ROCK_BIND_RANGE(Variant::FLOAT, "smoothness", Smoothness, "0.01,0.2,0.001")
	ROCK_BIND_RANGE(Variant::FLOAT, "seed", Seed, "0,1000000,1")

	ADD_GROUP("Surface", "");
	ROCK_BIND_RANGE(Variant::FLOAT, "displacement_scale", DisplacementScale, "0,1,0.001")
	ROCK_BIND_RANGE(Variant::FLOAT, "displacement_spread", DisplacementSpread, "1,10,0.01")
	ROCK_BIND_RANGE(Variant::FLOAT, "flatness", Flatness, "0.3,1.5,0.01")
	ROCK_BIND_RANGE(Variant::FLOAT, "roundness", Roundness, "0,1,0.01")

	ADD_GROUP("Ground", "");
	ClassDB::bind_method(D_METHOD("set_cut_ground", "value"), &ProceduralRockParameters::SetCutGround);
	ClassDB::bind_method(D_METHOD("should_cut_ground"), &ProceduralRockParameters::ShouldCutGround);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cut_ground"), "set_cut_ground", "should_cut_ground");
	ROCK_BIND_RANGE(Variant::FLOAT, "ground_cut", GroundCut, "-0.5,0.5,0.01")

	ADD_GROUP("Colors", "");
	ROCK_BIND(Variant::COLOR, "base_color", BaseColor)
	ROCK_BIND(Variant::COLOR, "crevice_color", CreviceColor)

	ClassDB::bind_static_method("ProceduralRockParameters",
		D_METHOD("get_form_name", "form"), &ProceduralRockParameters::GetFormName);
	ClassDB::bind_static_method("ProceduralRockParameters",
		D_METHOD("get_form_name_localized", "form"), &ProceduralRockParameters::GetFormNameLocalized);
}
