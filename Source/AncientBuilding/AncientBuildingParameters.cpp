#include "AncientBuilding/AncientBuildingParameters.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

#define ANCIENT_BIND(VariantType, PropName, Member)                                                 \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &AncientBuildingParameters::Set##Member); \
	ClassDB::bind_method(D_METHOD("get_" PropName), &AncientBuildingParameters::Get##Member);          \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName), "set_" PropName, "get_" PropName);

#define ANCIENT_BIND_RANGE(VariantType, PropName, Member, Hint)                                      \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &AncientBuildingParameters::Set##Member); \
	ClassDB::bind_method(D_METHOD("get_" PropName), &AncientBuildingParameters::Get##Member);          \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName, PROPERTY_HINT_RANGE, Hint), "set_" PropName, "get_" PropName);

#define ANCIENT_BIND_FLAG(PropName, SetterId, GetterId, GetterName)                              \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &AncientBuildingParameters::SetterId); \
	ClassDB::bind_method(D_METHOD(GetterName), &AncientBuildingParameters::GetterId);               \
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, PropName), "set_" PropName, GetterName);

#define ANCIENT_BIND_DERIVED(PropName, Getter) \
	ClassDB::bind_method(D_METHOD(PropName), &AncientBuildingParameters::Getter);

String AncientBuildingParameters::GetRoofTypeName(int32_t RoofType)
{
	switch (RoofType)
	{
		case ROOF_FLUSH_GABLE: return "Flush Gable";
		case ROOF_OVERHANGING_GABLE: return "Overhanging Gable";
		case ROOF_ROUND_RIDGE: return "Round Ridge";
		case ROOF_GABLE_AND_HIP: return "Gable and Hip";
		case ROOF_HIP: return "Hip";
		case ROOF_HOLLOW: return "Hollow";
		case ROOF_PYRAMIDAL: return "Pyramidal";
		case ROOF_ROUND: return "Round";
		case ROOF_HELMET: return "Helmet";
		default: return "Unknown";
	}
}

String AncientBuildingParameters::GetRoofTypeNameLocalized(int32_t RoofType)
{
	// Narrow literals reach Godot through string_new_with_utf8_chars, and godot-cpp compiles
	// MSVC with /utf-8, so these are safe as long as the source stays UTF-8 encoded.
	const char* Chinese = "";
	switch (RoofType)
	{
		case ROOF_FLUSH_GABLE: Chinese = "硬山"; break;
		case ROOF_OVERHANGING_GABLE: Chinese = "悬山"; break;
		case ROOF_ROUND_RIDGE: Chinese = "卷棚"; break;
		case ROOF_GABLE_AND_HIP: Chinese = "歇山"; break;
		case ROOF_HIP: Chinese = "庑殿"; break;
		case ROOF_HOLLOW: Chinese = "盝顶"; break;
		case ROOF_PYRAMIDAL: Chinese = "攒尖"; break;
		case ROOF_ROUND: Chinese = "圆攒尖"; break;
		case ROOF_HELMET: Chinese = "盔顶"; break;
		default: return GetRoofTypeName(RoofType);
	}

	return vformat("%s (%s)", GetRoofTypeName(RoofType), String::utf8(Chinese));
}

bool AncientBuildingParameters::IsCentralisedRoof(int32_t RoofType)
{
	return RoofType == ROOF_PYRAMIDAL || RoofType == ROOF_ROUND || RoofType == ROOF_HELMET;
}

void AncientBuildingParameters::_bind_methods()
{
	ADD_GROUP("Plan", "");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "width", Width, "1,60,0.01,or_greater")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "depth", Depth, "1,60,0.01,or_greater")
	ANCIENT_BIND_RANGE(Variant::INT, "bays_x", BaysX, "1,12,1")
	ANCIENT_BIND_RANGE(Variant::INT, "bays_z", BaysZ, "1,12,1")
	ANCIENT_BIND_RANGE(Variant::INT, "sides", Sides, "3,24,1")

	ClassDB::bind_method(D_METHOD("set_roof_type", "value"), &AncientBuildingParameters::SetRoofType);
	ClassDB::bind_method(D_METHOD("get_roof_type"), &AncientBuildingParameters::GetRoofType);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "roof_type", PROPERTY_HINT_ENUM, "Flush Gable,Gable and Hip,Hip,Overhanging Gable,Round Ridge,Hollow,Pyramidal,Round,Helmet"),
		"set_roof_type", "get_roof_type");

	ADD_GROUP("Base", "");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "platform_margin", PlatformMargin, "0,8,0.01")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "platform_height_scale", PlatformHeightScale, "0,6,0.01")
	ANCIENT_BIND_FLAG("generate_fence", SetGenerateFence, ShouldGenerateFence, "should_generate_fence")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "fence_height", FenceHeight, "0.1,3,0.01")
	ANCIENT_BIND_RANGE(Variant::INT, "fence_lambda", FenceLambda, "0,3,1")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "fence_gap_override", FenceGapOverride, "0,20,0.01")
	ANCIENT_BIND_FLAG("generate_steps", SetGenerateSteps, ShouldGenerateSteps, "should_generate_steps")
	ANCIENT_BIND_RANGE(Variant::INT, "step_count", StepCount, "1,24,1")

	ADD_GROUP("Body", "");
	ANCIENT_BIND_FLAG("generate_columns", SetGenerateColumns, ShouldGenerateColumns, "should_generate_columns")
	ANCIENT_BIND_FLAG("generate_walls", SetGenerateWalls, ShouldGenerateWalls, "should_generate_walls")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "column_radius_scale", ColumnRadiusScale, "0.05,2,0.001")
	ANCIENT_BIND_RANGE(Variant::INT, "column_sides", ColumnSides, "3,24,1")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "bracket_height_scale", BracketHeightScale, "0,4,0.01")

	ADD_GROUP("Roof", "");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "eave_overhang_scale", EaveOverhangScale, "0,8,0.01")
	ANCIENT_BIND_RANGE(Variant::INT, "rafter_courses", RafterCourses, "3,15,1")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "eave_rise_ratio", EaveRiseRatio, "0.1,2,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "ridge_rise_ratio", RidgeRiseRatio, "0.1,2,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "roof_height_scale", RoofHeightScale, "0.1,4,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "tile_course_width", TileCourseWidth, "0.05,2,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "tile_coverage", TileCoverage, "0,1,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "ridge_scale", RidgeScale, "0.1,4,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "gable_ratio", GableRatio, "0.05,0.9,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "gable_overhang_scale", GableOverhangScale, "0,8,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "roll_radius_scale", RollRadiusScale, "0.05,6,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "flat_top_ratio", FlatTopRatio, "0.05,0.9,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "finial_scale", FinialScale, "0,6,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "helmet_bulge", HelmetBulge, "0,2,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "corner_rise_scale", CornerRiseScale, "0,6,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "corner_extend_scale", CornerExtendScale, "0,4,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "corner_span_ratio", CornerSpanRatio, "0.05,1.5,0.001")

	ADD_GROUP("Colors", "");
	ANCIENT_BIND(Variant::COLOR, "stone_color", StoneColor)
	ANCIENT_BIND(Variant::COLOR, "timber_color", TimberColor)
	ANCIENT_BIND(Variant::COLOR, "plaster_color", PlasterColor)
	ANCIENT_BIND(Variant::COLOR, "tile_color", TileColor)
	ANCIENT_BIND(Variant::COLOR, "ridge_color", RidgeColor)
	ANCIENT_BIND(Variant::COLOR, "bracket_color", BracketColor)

	// Read-only, so tools and tests can assert the Table 1 proportions.
	ANCIENT_BIND_DERIVED("get_module", GetModule)
	ANCIENT_BIND_DERIVED("get_platform_height", GetPlatformHeight)
	ANCIENT_BIND_DERIVED("get_eave_height", GetEaveHeight)
	ANCIENT_BIND_DERIVED("get_bracket_height", GetBracketHeight)
	ANCIENT_BIND_DERIVED("get_column_height", GetColumnHeight)
	ANCIENT_BIND_DERIVED("get_roof_base", GetRoofBase)
	ANCIENT_BIND_DERIVED("get_roof_height", GetRoofHeight)
	ANCIENT_BIND_DERIVED("get_eave_overhang", GetEaveOverhang)
	ANCIENT_BIND_DERIVED("get_fence_gap_width", GetFenceGapWidth)
	ANCIENT_BIND_DERIVED("get_step_run_count", GetStepRunCount)
	ANCIENT_BIND_DERIVED("is_polygonal", IsPolygonal)
	ANCIENT_BIND_DERIVED("get_plan_apothem", GetPlanApothem)
	ANCIENT_BIND_DERIVED("get_finial_size", GetFinialSize)
	ANCIENT_BIND_DERIVED("get_gable_overhang", GetGableOverhang)
	ANCIENT_BIND_DERIVED("get_roll_radius", GetRollRadius)
	ANCIENT_BIND_DERIVED("get_corner_rise", GetCornerRise)
	ANCIENT_BIND_DERIVED("get_corner_extend", GetCornerExtend)
	ANCIENT_BIND_DERIVED("get_corner_span", GetCornerSpan)
	ANCIENT_BIND_DERIVED("get_total_height", GetTotalHeight)

	ClassDB::bind_static_method("AncientBuildingParameters",
		D_METHOD("get_roof_type_name", "roof_type"), &AncientBuildingParameters::GetRoofTypeName);
	ClassDB::bind_static_method("AncientBuildingParameters",
		D_METHOD("get_roof_type_name_localized", "roof_type"),
		&AncientBuildingParameters::GetRoofTypeNameLocalized);
	ClassDB::bind_static_method("AncientBuildingParameters",
		D_METHOD("is_centralised_roof", "roof_type"), &AncientBuildingParameters::IsCentralisedRoof);

	BIND_ENUM_CONSTANT(ROOF_FLUSH_GABLE);
	BIND_ENUM_CONSTANT(ROOF_GABLE_AND_HIP);
	BIND_ENUM_CONSTANT(ROOF_HIP);
	BIND_ENUM_CONSTANT(ROOF_OVERHANGING_GABLE);
	BIND_ENUM_CONSTANT(ROOF_ROUND_RIDGE);
	BIND_ENUM_CONSTANT(ROOF_HOLLOW);
	BIND_ENUM_CONSTANT(ROOF_PYRAMIDAL);
	BIND_ENUM_CONSTANT(ROOF_ROUND);
	BIND_ENUM_CONSTANT(ROOF_HELMET);
}
