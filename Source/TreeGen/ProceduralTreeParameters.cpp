#include "TreeGen/ProceduralTreeParameters.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

using namespace godot;

// ==================== Binding helpers ====================

#define TREEGEN_BIND(ClassName, VariantType, PropName, Member)                                          \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &ClassName::Set##Member);                   \
	ClassDB::bind_method(D_METHOD("get_" PropName), &ClassName::Get##Member);                            \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName), "set_" PropName, "get_" PropName);

#define TREEGEN_BIND_RANGE(ClassName, VariantType, PropName, Member, Hint)                               \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &ClassName::Set##Member);                   \
	ClassDB::bind_method(D_METHOD("get_" PropName), &ClassName::Get##Member);                            \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName, PROPERTY_HINT_RANGE, Hint), "set_" PropName, "get_" PropName);

// ==================== ProceduralTreeLeafParameters ====================

void ProceduralTreeLeafParameters::_bind_methods()
{
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::INT, "count", Count, "0,512,1,or_greater")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::INT, "scale_shape", ScaleShape, "0,7,1")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "scale", Scale, "0,2,0.001,or_greater")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "scale_x", ScaleX, "0,2,0.001,or_greater")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "stem_len", StemLen, "0,1,0.001,or_greater")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "bot_angle", BotAngle, "-180,180,0.1")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "mid_angle", MidAngle, "-180,180,0.1")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "top_angle", TopAngle, "-180,180,0.1")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "side_offset", SideOffset, "0,1,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::INT, "lobes", Lobes, "1,5,1")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "lobe_angle", LobeAngle, "-90,90,0.1")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "lobe_falloff", LobeFalloff, "0,1,0.001")
	TREEGEN_BIND(ProceduralTreeLeafParameters, Variant::COLOR, "color", LeafColor)
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "translucency", Translucency, "0,1,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "season_offset", SeasonOffset, "-2,2,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "curl", Curl, "0,2,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "color_jitter", ColorJitter, "0,1,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::FLOAT, "scale_jitter", ScaleJitter, "0,1,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeLeafParameters, Variant::INT, "needle_blades", NeedleBlades, "2,10,1")

	ClassDB::bind_method(D_METHOD("set_top_convex", "value"), &ProceduralTreeLeafParameters::SetTopConvex);
	ClassDB::bind_method(D_METHOD("is_top_convex"), &ProceduralTreeLeafParameters::IsTopConvex);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "top_convex"), "set_top_convex", "is_top_convex");

	ClassDB::bind_method(D_METHOD("set_is_needle", "value"), &ProceduralTreeLeafParameters::SetIsNeedle);
	ClassDB::bind_method(D_METHOD("is_needle"), &ProceduralTreeLeafParameters::IsNeedle);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_needle"), "set_is_needle", "is_needle");

	ClassDB::bind_method(D_METHOD("set_evergreen", "value"), &ProceduralTreeLeafParameters::SetEvergreen);
	ClassDB::bind_method(D_METHOD("is_evergreen"), &ProceduralTreeLeafParameters::IsEvergreen);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "evergreen"), "set_evergreen", "is_evergreen");
}

// ==================== ProceduralTreeFruitParameters ====================

void ProceduralTreeFruitParameters::_bind_methods()
{
	TREEGEN_BIND_RANGE(ProceduralTreeFruitParameters, Variant::FLOAT, "chance", Chance, "0,1,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeFruitParameters, Variant::FLOAT, "down_force", DownForce, "0,2,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeFruitParameters, Variant::FLOAT, "size", Size, "0,1,0.001,or_greater")
	TREEGEN_BIND(ProceduralTreeFruitParameters, Variant::VECTOR4, "shape", Shape)
	TREEGEN_BIND(ProceduralTreeFruitParameters, Variant::COLOR, "color", FruitColor)
}

// ==================== ProceduralTreeParameters ====================

ProceduralTreeParameters::ProceduralTreeParameters()
{
	// Sub-resources are deliberately NOT created here. ClassDB instantiates one object per
	// class to snapshot property defaults, and a live Ref in that snapshot would become the
	// class-level default ("Instantiated X used as default value" warnings). EnsureSubResources()
	// fills them in on first real use instead.
}

void ProceduralTreeParameters::EnsureSubResources()
{
	if (Leaf.is_null())
	{
		SetLeaf(Ref<ProceduralTreeLeafParameters>(memnew(ProceduralTreeLeafParameters)));
	}
	if (Blossom.is_null())
	{
		Ref<ProceduralTreeLeafParameters> DefaultBlossom(memnew(ProceduralTreeLeafParameters));
		DefaultBlossom->SetCount(0);
		DefaultBlossom->SetLobes(5);
		SetBlossom(DefaultBlossom);
	}
	if (Fruit.is_null())
	{
		SetFruit(Ref<ProceduralTreeFruitParameters>(memnew(ProceduralTreeFruitParameters)));
	}
}

void ProceduralTreeParameters::_bind_methods()
{
	ADD_GROUP("Skeleton", "");
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::INT, "levels", Levels, "1,4,1")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "scale", Scale, "0.1,60,0.001,or_greater")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "scale_v", ScaleV, "0,20,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "ratio", Ratio, "0.001,0.2,0.0001")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "ratio_power", RatioPower, "0.1,4,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "attraction_up", AttractionUp, "-4,4,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "flare", Flare, "0,4,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::INT, "lobes", Lobes, "0,12,1")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "lobe_depth", LobeDepth, "0,1,0.001")

	ADD_GROUP("Per Level", "");
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "base_size", BaseSize)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4I, "shape", Shape)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4I, "branches", Branches)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4I, "curve_res", CurveRes)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "length", Length)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "length_v", LengthV)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "taper", Taper)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "curve", Curve)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "curve_v", CurveV)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "curve_back", CurveBack)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "rotate", Rotate)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "rotate_v", RotateV)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "down_angle", DownAngle)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "down_angle_v", DownAngleV)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4I, "base_splits", BaseSplits)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "seg_splits", SegSplits)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "seg_split_base_offset", SegSplitBaseOffset)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "split_angle", SplitAngle)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::VECTOR4, "split_angle_v", SplitAngleV)

	ADD_GROUP("Foliage", "");
	ClassDB::bind_method(D_METHOD("set_leaf", "value"), &ProceduralTreeParameters::SetLeaf);
	ClassDB::bind_method(D_METHOD("get_leaf"), &ProceduralTreeParameters::GetLeaf);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "leaf", PROPERTY_HINT_RESOURCE_TYPE, "ProceduralTreeLeafParameters"), "set_leaf", "get_leaf");

	ClassDB::bind_method(D_METHOD("set_blossom", "value"), &ProceduralTreeParameters::SetBlossom);
	ClassDB::bind_method(D_METHOD("get_blossom"), &ProceduralTreeParameters::GetBlossom);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "blossom", PROPERTY_HINT_RESOURCE_TYPE, "ProceduralTreeLeafParameters"), "set_blossom", "get_blossom");

	ClassDB::bind_method(D_METHOD("set_fruit", "value"), &ProceduralTreeParameters::SetFruit);
	ClassDB::bind_method(D_METHOD("get_fruit"), &ProceduralTreeParameters::GetFruit);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "fruit", PROPERTY_HINT_RESOURCE_TYPE, "ProceduralTreeFruitParameters"), "set_fruit", "get_fruit");

	ADD_GROUP("Bark", "stem_");
	ClassDB::bind_method(D_METHOD("set_stem_birch_texture", "value"), &ProceduralTreeParameters::SetStemBirchTexture);
	ClassDB::bind_method(D_METHOD("has_stem_birch_texture"), &ProceduralTreeParameters::HasStemBirchTexture);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stem_birch_texture"), "set_stem_birch_texture", "has_stem_birch_texture");

	TREEGEN_BIND(ProceduralTreeParameters, Variant::COLOR, "stem_small_color", StemSmallColor)
	TREEGEN_BIND(ProceduralTreeParameters, Variant::COLOR, "stem_big_color", StemBigColor)
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "stem_bump_strength", StemBumpStrength, "0,4,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "stem_bump_gap_size", StemBumpGapSize, "0,1,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "stem_bump_voronoi_weight", StemBumpVoronoiWeight, "0,1,0.001")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "stem_lichen_frequency", StemLichenFrequency, "0,32,0.01")
	TREEGEN_BIND_RANGE(ProceduralTreeParameters, Variant::FLOAT, "stem_lichen_size", StemLichenSize, "0,1,0.001")

	ClassDB::bind_method(D_METHOD("apply_preset", "preset"), &ProceduralTreeParameters::ApplyPreset);
	ClassDB::bind_static_method("ProceduralTreeParameters", D_METHOD("get_preset_name", "preset"), &ProceduralTreeParameters::GetPresetName);

	BIND_ENUM_CONSTANT(PRESET_DEFAULT);
	BIND_ENUM_CONSTANT(PRESET_APPLE);
	BIND_ENUM_CONSTANT(PRESET_SASSAFRAS);
	BIND_ENUM_CONSTANT(PRESET_PALM);
	BIND_ENUM_CONSTANT(PRESET_TAMARACK);
	BIND_ENUM_CONSTANT(PRESET_GINKGO);
	BIND_ENUM_CONSTANT(PRESET_PEACH);
	BIND_ENUM_CONSTANT(PRESET_CAMPHOR);
	BIND_ENUM_CONSTANT(PRESET_PINE);
	BIND_ENUM_CONSTANT(PRESET_CHINESE_FIR);
	BIND_ENUM_CONSTANT(PRESET_WILLOW);
	BIND_ENUM_CONSTANT(PRESET_COUNT);
}

void ProceduralTreeParameters::ForwardSubResourceChanged()
{
	emit_changed();
}

void ProceduralTreeParameters::AdoptSubResource(const Ref<Resource>& Previous, const Ref<Resource>& Next)
{
	const Callable Forward = callable_mp(this, &ProceduralTreeParameters::ForwardSubResourceChanged);

	if (Previous.is_valid() && Previous->is_connected("changed", Forward))
	{
		Previous->disconnect("changed", Forward);
	}
	if (Next.is_valid() && !Next->is_connected("changed", Forward))
	{
		Next->connect("changed", Forward);
	}
}

void ProceduralTreeParameters::SetLeaf(const Ref<ProceduralTreeLeafParameters>& Value)
{
	AdoptSubResource(Leaf, Value);
	Leaf = Value;
	emit_changed();
}

void ProceduralTreeParameters::SetBlossom(const Ref<ProceduralTreeLeafParameters>& Value)
{
	AdoptSubResource(Blossom, Value);
	Blossom = Value;
	emit_changed();
}

void ProceduralTreeParameters::SetFruit(const Ref<ProceduralTreeFruitParameters>& Value)
{
	AdoptSubResource(Fruit, Value);
	Fruit = Value;
	emit_changed();
}
