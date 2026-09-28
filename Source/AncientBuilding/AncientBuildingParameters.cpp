#include "AncientBuilding/AncientBuildingParameters.h"

#include "AncientBuilding/RoofCurve.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

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
	// godot-cpp's String(const char*) decodes as LATIN-1, never UTF-8 — every narrow
	// literal containing non-ASCII must go through String::utf8, or it garbles in the
	// editor/console. godot-cpp's /utf-8 flag only guarantees the literal's bytes are
	// UTF-8 in the binary.
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

String AncientBuildingParameters::GetStyleName(int32_t Value)
{
	switch (Value)
	{
		case STYLE_THATCHED: return "Thatched";
		case STYLE_EARTHEN: return "Earthen";
		default: return "Traditional";
	}
}

String AncientBuildingParameters::GetStyleNameLocalized(int32_t Value)
{
	const char* Chinese = "";
	switch (Value)
	{
		case STYLE_THATCHED: Chinese = "茅草"; break;
		case STYLE_EARTHEN: Chinese = "土木"; break;
		default: Chinese = "官式"; break;
	}

	return vformat("%s (%s)", GetStyleName(Value), String::utf8(Chinese));
}

float AncientBuildingParameters::GetStyleMottle(int32_t Value)
{
	switch (Value)
	{
		case STYLE_THATCHED: return 0.12f;
		case STYLE_EARTHEN: return 0.09f;
		default: return 0.05f;
	}
}

/**
 * Material palettes (vertex colours, no textures). Sampled off the thatched hut of
 * Reference/image.png: straw thatch reads as dry grey-yellow bundles, the walls as pale
 * plaster with a whitewash bloom, the timber as dark weathered pine.
 */
void AncientBuildingParameters::ApplyMaterialStyle(int32_t Value)
{
	switch (Value)
	{
		case STYLE_THATCHED:
			// Straw (roof), straw-dark (ridge cap), pale plaster, dark timber, wood braces, earth base.
			TileColor = Color(0.62f, 0.54f, 0.32f, 1.0f);
			RidgeColor = Color(0.42f, 0.35f, 0.21f, 1.0f);
			PlasterColor = Color(0.76f, 0.72f, 0.60f, 1.0f);
			TimberColor = Color(0.34f, 0.25f, 0.16f, 1.0f);
			BracketColor = Color(0.46f, 0.33f, 0.19f, 1.0f);
			StoneColor = Color(0.56f, 0.48f, 0.34f, 1.0f);
			break;

		case STYLE_EARTHEN:
			// Earth-toned tile, dark ridge, rammed earth walls, dark timber.
			TileColor = Color(0.33f, 0.31f, 0.27f, 1.0f);
			RidgeColor = Color(0.24f, 0.22f, 0.19f, 1.0f);
			PlasterColor = Color(0.68f, 0.57f, 0.38f, 1.0f);
			TimberColor = Color(0.36f, 0.26f, 0.17f, 1.0f);
			BracketColor = Color(0.47f, 0.35f, 0.21f, 1.0f);
			StoneColor = Color(0.52f, 0.46f, 0.35f, 1.0f);
			break;

		default:
			// STYLE_TRADITIONAL: keep whatever the user has already set.
			break;
	}
}

PackedVector2Array AncientBuildingParameters::SampleRoofCurve(
	float EaveRiseRatio, float RidgeRiseRatio, float RoofHeight, float HalfSpan,
	float ChordError, float MaxSegment)
{
	BuildingGen::BuildingSpec Spec;
	Spec.EaveRiseRatio = EaveRiseRatio;
	Spec.RidgeRiseRatio = RidgeRiseRatio;
	Spec.RoofChordError = ChordError;
	Spec.RoofMaxSegment = MaxSegment;

	const std::vector<BuildingGen::Vector2> Profile = BuildingGen::SampleRoofCurve(Spec, HalfSpan, RoofHeight);

	PackedVector2Array Result;
	Result.resize(int64_t(Profile.size()));
	for (size_t Index = 0; Index < Profile.size(); ++Index)
	{
		Result[int64_t(Index)] = Profile[Index];
	}

	return Result;
}

void AncientBuildingParameters::ApplyArchetype(int32_t Archetype)
{
	// [自定] Each preset is a starting point read off Fig 2 (and Fig 28 for the compositions), not
	// a measured type: it sets the parameters that make the silhouette, and nothing else.
	switch (Archetype)
	{
		case ARCHETYPE_TING:
			// 亭: an open hexagonal pavilion with 美人靠 and a 攒尖.
			Sides = 6;
			Width = 6.0f;
			Depth = 6.0f;
			RoofType = ROOF_PYRAMIDAL;
			bGenerateWalls = false;
			bGenerateFence = false;
			FenceLambda = 0;
			RailingKind = 2;
			bHangingFascia = true;
			StoreyCount = 1;
			BaseKind = 0;
			break;

		case ARCHETYPE_TAI:
			// 台: an open hall raised on a tall terrace with a balustrade and paired stairs.
			Sides = 4;
			Width = 11.0f;
			Depth = 8.0f;
			BaysX = 3;
			BaysZ = 2;
			RoofType = ROOF_GABLE_AND_HIP;
			BaseKind = 2;
			MasonryBaseHeight = 3.2f;
			bGenerateFence = true;
			FenceLambda = 1;
			bGenerateWalls = false;
			RailingKind = 1;
			bHangingFascia = true;
			bPlatformSumeru = true;
			StoreyCount = 1;
			break;

		case ARCHETYPE_LOU:
			// 楼: two walled storeys, 叉柱造, with a 平座 and a 歇山.
			Sides = 4;
			Width = 12.0f;
			Depth = 8.0f;
			BaysX = 3;
			BaysZ = 2;
			RoofType = ROOF_GABLE_AND_HIP;
			bGenerateWalls = true;
			StoreyCount = 2;
			StoreySetbackBays = 0;
			bStoreyBalcony = true;
			UpperColumnHeightScale = 0.8f;
			BaseKind = 0;
			break;

		case ARCHETYPE_GE:
			// 阁: three storeys stepping in, each with its 平座 for looking out, under a 攒尖.
			Sides = 4;
			Width = 12.0f;
			Depth = 12.0f;
			BaysX = 5;
			BaysZ = 5;
			RoofType = ROOF_PYRAMIDAL;
			bGenerateWalls = true;
			StoreyCount = 3;
			StoreySetbackBays = 1;
			bStoreyBalcony = true;
			UpperColumnHeightScale = 0.75f;
			BaseKind = 0;
			break;

		case ARCHETYPE_XIE:
			// 榭: a low open hall on piles over the water, with 美人靠 along the water sides.
			Sides = 4;
			Width = 10.0f;
			Depth = 7.0f;
			BaysX = 3;
			BaysZ = 2;
			RoofType = ROOF_ROUND_RIDGE;
			BaseKind = 3;
			bGenerateWalls = false;
			bGenerateFence = false;
			FenceLambda = 0;
			RailingKind = 2;
			bHangingFascia = true;
			StoreyCount = 1;
			break;

		case ARCHETYPE_LANG:
			// 廊: a long open covered way — many narrow bays under a 卷棚, benches along both sides.
			Sides = 4;
			Width = 24.0f;
			Depth = 3.0f;
			BaysX = 8;
			BaysZ = 1;
			// Module from one 3 m bay's worth of pavilion, not from the 24 m run.
			ModuleSpan = 4.0f;
			RoofType = ROOF_ROUND_RIDGE;
			bGenerateWalls = false;
			bGenerateFence = false;
			bGeneratePlatform = true;
			PlatformHeightScale = 0.5f;
			FenceLambda = 0;
			bGenerateSteps = false;
			RailingKind = 1;
			bHangingFascia = true;
			StoreyCount = 1;
			BaseKind = 0;
			break;

		default:
			return;
	}

	emit_changed();
}

void AncientBuildingParameters::_bind_methods()
{
	ADD_GROUP("Plan", "");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "width", Width, "1,60,0.01,or_greater")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "depth", Depth, "1,60,0.01,or_greater")
	ANCIENT_BIND_RANGE(Variant::INT, "bays_x", BaysX, "1,12,1")
	ANCIENT_BIND_RANGE(Variant::INT, "bays_z", BaysZ, "1,12,1")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "module_span", ModuleSpan, "0,60,0.01")
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
	// 60_台基地面 R6/R8/R9. All four default to the legacy output.
	ANCIENT_BIND_FLAG("generate_platform", SetGeneratePlatform, ShouldGeneratePlatform,
		"should_generate_platform")
	ANCIENT_BIND_FLAG("platform_top_joints", SetPlatformTopJoints, ShouldGeneratePlatformTopJoints,
		"should_generate_platform_top_joints")
	ANCIENT_BIND_FLAG("platform_edge_lip", SetPlatformEdgeLip, ShouldGeneratePlatformEdgeLip,
		"should_generate_platform_edge_lip")
	ANCIENT_BIND_FLAG("paving", SetPaving, ShouldGeneratePaving, "should_generate_paving")
	ANCIENT_BIND_FLAG("paving_joint_geometry", SetPavingJointGeometry, ShouldGeneratePavingJointGeometry,
		"should_generate_paving_joint_geometry")
	ANCIENT_BIND_FLAG("step_side_cheek", SetStepSideCheek, ShouldGenerateStepSideCheek,
		"should_generate_step_side_cheek")

	// 砖石作. base_kind 0 (台基) and masonry_storeys 0 are the legacy building.
	ADD_GROUP("Masonry", "");
	ClassDB::bind_method(D_METHOD("set_base_kind", "value"), &AncientBuildingParameters::SetBaseKind);
	ClassDB::bind_method(D_METHOD("get_base_kind"), &AncientBuildingParameters::GetBaseKind);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "base_kind", PROPERTY_HINT_ENUM,
		String::utf8("台基 Platform,城台 Masonry Terrace,高台 High Terrace,桩台 Stilts")), "set_base_kind", "get_base_kind");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "masonry_base_height", MasonryBaseHeight, "0.5,30,0.01")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "masonry_base_margin", MasonryBaseMargin, "0,30,0.01")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "masonry_batter", MasonryBatter, "0,0.3,0.001")
	ANCIENT_BIND_RANGE(Variant::INT, "base_arch_count", BaseArchCount, "0,7,1")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "base_arch_width", BaseArchWidth, "0,12,0.01")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "base_arch_height_ratio", BaseArchHeightRatio, "0.2,0.9,0.01")
	ClassDB::bind_method(D_METHOD("set_base_arch_profile", "value"), &AncientBuildingParameters::SetBaseArchProfile);
	ClassDB::bind_method(D_METHOD("get_base_arch_profile"), &AncientBuildingParameters::GetBaseArchProfile);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "base_arch_profile", PROPERTY_HINT_ENUM,
		String::utf8("半圆券 Semicircle,双心券 Pointed,平券 Flat")), "set_base_arch_profile", "get_base_arch_profile");
	ClassDB::bind_method(D_METHOD("set_base_arch_axis", "value"), &AncientBuildingParameters::SetBaseArchAxis);
	ClassDB::bind_method(D_METHOD("get_base_arch_axis"), &AncientBuildingParameters::GetBaseArchAxis);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "base_arch_axis", PROPERTY_HINT_ENUM, "Front to Back,Side to Side"),
		"set_base_arch_axis", "get_base_arch_axis");
	ClassDB::bind_method(D_METHOD("set_base_parapet", "value"), &AncientBuildingParameters::SetBaseParapet);
	ClassDB::bind_method(D_METHOD("get_base_parapet"), &AncientBuildingParameters::GetBaseParapet);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "base_parapet", PROPERTY_HINT_ENUM,
		String::utf8("无 None,垛口 Crenel,宇墙 Plain")), "set_base_parapet", "get_base_parapet");
	ANCIENT_BIND_RANGE(Variant::INT, "masonry_storeys", MasonryStoreys, "0,5,1")
	ANCIENT_BIND(Variant::COLOR, "brick_color", BrickColor)

	// 亭台榭廊. All off / legacy by default.
	ADD_GROUP("Open Structures", "");
	ClassDB::bind_method(D_METHOD("set_railing_kind", "value"), &AncientBuildingParameters::SetRailingKind);
	ClassDB::bind_method(D_METHOD("get_railing_kind"), &AncientBuildingParameters::GetRailingKind);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "railing_kind", PROPERTY_HINT_ENUM,
		String::utf8("无 None,坐凳栏杆 Bench,美人靠 Leaning Bench")), "set_railing_kind", "get_railing_kind");
	ANCIENT_BIND_FLAG("hanging_fascia", SetHangingFascia, HasHangingFascia, "has_hanging_fascia")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "stilt_depth", StiltDepth, "0,20,0.01")
	ANCIENT_BIND_FLAG("platform_sumeru", SetPlatformSumeru, HasPlatformSumeru, "has_platform_sumeru")
	ClassDB::bind_method(D_METHOD("apply_archetype", "archetype"), &AncientBuildingParameters::ApplyArchetype);
	// Plain integer constants: a nested enum cannot go through BIND_ENUM_CONSTANT here.
	const char* const ArchetypeNames[] = { "ARCHETYPE_TING", "ARCHETYPE_TAI", "ARCHETYPE_LOU",
		"ARCHETYPE_GE", "ARCHETYPE_XIE", "ARCHETYPE_LANG" };
	for (int32_t Index = 0; Index < ARCHETYPE_COUNT; ++Index)
	{
		ClassDB::bind_integer_constant(get_class_static(), StringName(), ArchetypeNames[Index], Index);
	}

	ADD_GROUP("Body", "");
	ANCIENT_BIND_FLAG("generate_columns", SetGenerateColumns, ShouldGenerateColumns, "should_generate_columns")
	ANCIENT_BIND_FLAG("generate_walls", SetGenerateWalls, ShouldGenerateWalls, "should_generate_walls")
	// 20_墙体 R15. Both default to the legacy output.
	ANCIENT_BIND_RANGE(Variant::FLOAT, "dado_height_ratio", DadoHeightRatio, "0,1,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "dado_top_trim", DadoTopTrim, "0,2,0.01")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "column_radius_scale", ColumnRadiusScale, "0.05,2,0.001")
	ANCIENT_BIND_RANGE(Variant::INT, "column_sides", ColumnSides, "3,24,1")
	ANCIENT_BIND(Variant::BOOL, "smooth_columns", SmoothColumns)
	ANCIENT_BIND_RANGE(Variant::FLOAT, "column_base_height_scale", ColumnBaseHeightScale, "0,2,0.01")
	// 60_台基地面 R12: the square 石础, off so the turned drum stays the legacy output.
	ANCIENT_BIND_FLAG("column_base_square", SetColumnBaseSquare, ShouldGenerateColumnBaseSquare,
		"should_generate_column_base_square")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "bracket_height_scale", BracketHeightScale, "0,4,0.01")

	// 多层. storey_count 1 is the legacy building, so every earlier resource bakes unchanged.
	ADD_GROUP("Storeys", "");
	ANCIENT_BIND_RANGE(Variant::INT, "storey_count", StoreyCount, "1,5,1")
	ANCIENT_BIND_RANGE(Variant::INT, "storey_setback_bays", StoreySetbackBays, "0,2,1")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "upper_column_height_scale", UpperColumnHeightScale, "0.05,1.5,0.01")
	ANCIENT_BIND_FLAG("storey_balcony", SetStoreyBalcony, HasStoreyBalcony, "has_storey_balcony")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "balcony_projection_scale", BalconyProjectionScale, "0,6,0.01")

	ADD_GROUP("Roof", "");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "eave_overhang_scale", EaveOverhangScale, "0,8,0.01")
	ANCIENT_BIND_RANGE(Variant::INT, "rafter_courses", RafterCourses, "3,15,1")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "eave_rise_ratio", EaveRiseRatio, "0.1,2,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "ridge_rise_ratio", RidgeRiseRatio, "0.1,2,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "roof_height_scale", RoofHeightScale, "0.1,4,0.001")

	ClassDB::bind_method(D_METHOD("set_roof_curve_mode", "value"), &AncientBuildingParameters::SetRoofCurveMode);
	ClassDB::bind_method(D_METHOD("get_roof_curve_mode"), &AncientBuildingParameters::GetRoofCurveMode);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "roof_curve_mode", PROPERTY_HINT_ENUM, "Legacy,Continuous"),
		"set_roof_curve_mode", "get_roof_curve_mode");
	ClassDB::bind_method(D_METHOD("set_eave_rafter_style", "value"), &AncientBuildingParameters::SetEaveRafterStyle);
	ClassDB::bind_method(D_METHOD("get_eave_rafter_style"), &AncientBuildingParameters::GetEaveRafterStyle);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "eave_rafter_style", PROPERTY_HINT_ENUM, "None,Rafter Heads,Rafters + Flying Rafters"),
		"set_eave_rafter_style", "get_eave_rafter_style");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "roof_chord_error", RoofChordError, "0.001,0.05,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "roof_max_segment", RoofMaxSegment, "0.05,2,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "tile_course_width", TileCourseWidth, "0.05,2,0.001")
	// 瓦作 (30_瓦作 §3): 0 = legacy, so every resource written before reads back unchanged.
	ClassDB::bind_method(D_METHOD("set_tile_detail", "value"), &AncientBuildingParameters::SetTileDetail);
	ClassDB::bind_method(D_METHOD("get_tile_detail"), &AncientBuildingParameters::GetTileDetail);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tile_detail", PROPERTY_HINT_ENUM,
		String::utf8("Legacy,排垄与叠压,檐口件与泥背")), "set_tile_detail", "get_tile_detail");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "tile_bedding_thickness", TileBeddingThickness, "0,0.2,0.001")
	// 距离档 : 0 = 近景 full = byte-compatible with every resource written before it existed.
	ClassDB::bind_method(D_METHOD("set_lod_level", "value"), &AncientBuildingParameters::SetLODLevel);
	ClassDB::bind_method(D_METHOD("get_lod_level"), &AncientBuildingParameters::GetLODLevel);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lod_level", PROPERTY_HINT_ENUM,
		String::utf8("近景,中景,远景")), "set_lod_level", "get_lod_level");
	ANCIENT_BIND_RANGE(Variant::FLOAT, "tile_coverage", TileCoverage, "0,1,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "ridge_scale", RidgeScale, "0.1,4,0.001")
	// 50_脊饰 §3. 0 = the legacy seven-point ridge, so every resource written before this existed
	// bakes to exactly the mesh it baked to then.
	ClassDB::bind_method(D_METHOD("set_ridge_detail", "value"), &AncientBuildingParameters::SetRidgeDetail);
	ClassDB::bind_method(D_METHOD("get_ridge_detail"), &AncientBuildingParameters::GetRidgeDetail);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ridge_detail", PROPERTY_HINT_ENUM,
		String::utf8("Legacy 7-point,分层 当沟条+脊身+盖脊筒")), "set_ridge_detail", "get_ridge_detail");
	ANCIENT_BIND_FLAG("ridge_ornaments", SetRidgeOrnaments, ShouldGenerateRidgeOrnaments,
		"should_generate_ridge_ornaments")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "ridge_finial_size", RidgeFinialSize, "0,4,0.01")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "ridge_beast_size", RidgeBeastSize, "0,4,0.01")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "ridge_walker_size", RidgeWalkerSize, "0,4,0.01")
	ANCIENT_BIND_RANGE(Variant::INT, "ridge_walker_count", RidgeWalkerCount, "0,12,1")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "gable_ratio", GableRatio, "0.05,0.9,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "gable_overhang_scale", GableOverhangScale, "0,8,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "roll_radius_scale", RollRadiusScale, "0.05,6,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "flat_top_ratio", FlatTopRatio, "0.05,0.9,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "finial_scale", FinialScale, "0,6,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "helmet_bulge", HelmetBulge, "0,2,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "corner_rise_scale", CornerRiseScale, "0,6,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "corner_extend_scale", CornerExtendScale, "0,4,0.001")
	ANCIENT_BIND_RANGE(Variant::FLOAT, "corner_span_ratio", CornerSpanRatio, "0.05,1.5,0.001")

	ADD_GROUP("Material Style", "");
	ClassDB::bind_method(D_METHOD("set_material_style", "value"), &AncientBuildingParameters::SetMaterialStyle);
	ClassDB::bind_method(D_METHOD("get_material_style"), &AncientBuildingParameters::GetMaterialStyle);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "material_style", PROPERTY_HINT_ENUM, String::utf8("Traditional (官式),Thatched (茅草),Earthen (土木)")),
		"set_material_style", "get_material_style");
	ClassDB::bind_method(D_METHOD("apply_material_style", "style"), &AncientBuildingParameters::ApplyMaterialStyle);

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
	ClassDB::bind_static_method("AncientBuildingParameters",
		D_METHOD("get_style_name", "style"), &AncientBuildingParameters::GetStyleName);
	ClassDB::bind_static_method("AncientBuildingParameters",
		D_METHOD("get_style_name_localized", "style"),
		&AncientBuildingParameters::GetStyleNameLocalized);
	ClassDB::bind_static_method("AncientBuildingParameters",
		D_METHOD("get_style_mottle", "style"), &AncientBuildingParameters::GetStyleMottle);
	ClassDB::bind_static_method("AncientBuildingParameters",
		D_METHOD("sample_roof_curve", "eave_rise_ratio", "ridge_rise_ratio", "roof_height", "half_span", "chord_error", "max_segment"),
		&AncientBuildingParameters::SampleRoofCurve);

	BIND_ENUM_CONSTANT(ROOF_FLUSH_GABLE);
	BIND_ENUM_CONSTANT(ROOF_GABLE_AND_HIP);
	BIND_ENUM_CONSTANT(ROOF_HIP);
	BIND_ENUM_CONSTANT(ROOF_OVERHANGING_GABLE);
	BIND_ENUM_CONSTANT(ROOF_ROUND_RIDGE);
	BIND_ENUM_CONSTANT(ROOF_HOLLOW);
	BIND_ENUM_CONSTANT(ROOF_PYRAMIDAL);
	BIND_ENUM_CONSTANT(ROOF_ROUND);
	BIND_ENUM_CONSTANT(ROOF_HELMET);
	BIND_ENUM_CONSTANT(STYLE_TRADITIONAL);
	BIND_ENUM_CONSTANT(STYLE_THATCHED);
	BIND_ENUM_CONSTANT(STYLE_EARTHEN);
}
