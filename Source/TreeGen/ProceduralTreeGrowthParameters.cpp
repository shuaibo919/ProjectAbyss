#include "ProceduralTreeGrowthParameters.h"

#include <algorithm>
#include <cmath>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

using namespace godot;

void ProceduralTreeGrowthParameters::_bind_methods()
{
#define GROWTH_FLOAT(Name, Member, Range)                                                                              \
	ClassDB::bind_method(D_METHOD("set_" Name, "value"), &ProceduralTreeGrowthParameters::Set##Member);                \
	ClassDB::bind_method(D_METHOD("get_" Name), &ProceduralTreeGrowthParameters::Get##Member);                         \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, Name, PROPERTY_HINT_RANGE, Range), "set_" Name, "get_" Name);
	GROWTH_FLOAT("trunk_bend", TrunkBend, "0,3,0.01")
	GROWTH_FLOAT("branch_bend", BranchBend, "0,3,0.01")
	GROWTH_FLOAT("forking", Forking, "0,2,0.01")
	GROWTH_FLOAT("radius_power", RadiusPower, "1.8,3,0.01")
	GROWTH_FLOAT("junction_shape", JunctionShape, "0,2,0.01")
	ADD_GROUP("Bamboo", "bamboo_");
	GROWTH_FLOAT("bamboo_internode_length", BambooInternodeLength, "0.15,0.6,0.01,suffix:m")
	GROWTH_FLOAT("bamboo_node_definition", BambooNodeDefinition, "0,2,0.01")
	GROWTH_FLOAT("bamboo_leaf_scale", BambooLeafScale, "0.5,2,0.01")
	ADD_GROUP("Peach", "peach_");
	GROWTH_FLOAT("peach_twig_density", PeachTwigDensity, "0.25,2,0.01")
	GROWTH_FLOAT("peach_blossom_density", PeachBlossomDensity, "0,3,0.01")
	GROWTH_FLOAT("peach_blossom_scale", PeachBlossomScale, "0.5,1.8,0.01")
	ADD_GROUP("Growth Curves", "");
#undef GROWTH_FLOAT
#define GROWTH_CURVE(Name, Member)                                                                                     \
	ClassDB::bind_method(D_METHOD("set_" Name, "value"), &ProceduralTreeGrowthParameters::Set##Member);                \
	ClassDB::bind_method(D_METHOD("get_" Name), &ProceduralTreeGrowthParameters::Get##Member);                         \
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, Name, PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_" Name, "get_" Name);
	GROWTH_CURVE("length_by_height", LengthByHeight)
	GROWTH_CURVE("density_by_height", DensityByHeight)
	GROWTH_CURVE("bend_along_branch", BendAlongBranch)
	GROWTH_CURVE("radius_along_branch", RadiusAlongBranch)
#undef GROWTH_CURVE
}

#define GROWTH_SETTER(Member, Minimum, Maximum)                                                                        \
	void ProceduralTreeGrowthParameters::Set##Member(float Value)                                                      \
	{                                                                                                                  \
		if (!std::isfinite(Value))                                                                                     \
		{                                                                                                              \
			return;                                                                                                    \
		}                                                                                                              \
		Value = std::clamp(Value, Minimum, Maximum);                                                                   \
		if (Member != Value)                                                                                           \
		{                                                                                                              \
			Member = Value;                                                                                            \
			emit_changed();                                                                                            \
		}                                                                                                              \
	}
GROWTH_SETTER(TrunkBend, 0.0f, 3.0f)
GROWTH_SETTER(BranchBend, 0.0f, 3.0f)
GROWTH_SETTER(Forking, 0.0f, 2.0f)
GROWTH_SETTER(RadiusPower, 1.8f, 3.0f)
GROWTH_SETTER(JunctionShape, 0.0f, 2.0f)
GROWTH_SETTER(BambooInternodeLength, 0.15f, 0.60f)
GROWTH_SETTER(BambooNodeDefinition, 0.0f, 2.0f)
GROWTH_SETTER(BambooLeafScale, 0.5f, 2.0f)
GROWTH_SETTER(PeachTwigDensity, 0.25f, 2.0f)
GROWTH_SETTER(PeachBlossomDensity, 0.0f, 3.0f)
GROWTH_SETTER(PeachBlossomScale, 0.5f, 1.8f)
#undef GROWTH_SETTER

void ProceduralTreeGrowthParameters::OnCurveChanged()
{
	emit_changed();
}

void ProceduralTreeGrowthParameters::ChangeCurve(Ref<Curve>& Target, const Ref<Curve>& Value)
{
	// Reconnect distinct resources once, including curves shared by multiple controls.
	const Callable Changed = callable_mp(this, &ProceduralTreeGrowthParameters::OnCurveChanged);
	for (const Ref<Curve>& Item : {LengthByHeight, DensityByHeight, BendAlongBranch, RadiusAlongBranch})
	{
		if (Item.is_valid() && Item->is_connected("changed", Changed))
		{
			Item->disconnect("changed", Changed);
		}
	}
	Target = Value;
	for (const Ref<Curve>& Item : {LengthByHeight, DensityByHeight, BendAlongBranch, RadiusAlongBranch})
	{
		if (Item.is_valid() && !Item->is_connected("changed", Changed))
		{
			Item->connect("changed", Changed);
		}
	}
	emit_changed();
}

void ProceduralTreeGrowthParameters::SetLengthByHeight(const Ref<Curve>& Value)
{
	ChangeCurve(LengthByHeight, Value);
}
void ProceduralTreeGrowthParameters::SetDensityByHeight(const Ref<Curve>& Value)
{
	ChangeCurve(DensityByHeight, Value);
}
void ProceduralTreeGrowthParameters::SetBendAlongBranch(const Ref<Curve>& Value)
{
	ChangeCurve(BendAlongBranch, Value);
}
void ProceduralTreeGrowthParameters::SetRadiusAlongBranch(const Ref<Curve>& Value)
{
	ChangeCurve(RadiusAlongBranch, Value);
}

TreeGrowthSettings ProceduralTreeGrowthParameters::MakeSnapshot() const
{
	TreeGrowthSettings Result;
	Result.TrunkBend = TrunkBend;
	Result.BranchBend = BranchBend;
	Result.Forking = Forking;
	Result.RadiusPower = RadiusPower;
	Result.JunctionShape = JunctionShape;
	Result.BambooInternodeLength = BambooInternodeLength;
	Result.BambooNodeDefinition = BambooNodeDefinition;
	Result.BambooLeafScale = BambooLeafScale;
	Result.PeachTwigDensity = PeachTwigDensity;
	Result.PeachBlossomDensity = PeachBlossomDensity;
	Result.PeachBlossomScale = PeachBlossomScale;
	const auto Copy = [](const Ref<Curve>& CurveResource, TreeGrowthCurve& Target)
	{
		Target.bOverride = CurveResource.is_valid() && CurveResource->get_point_count() > 0;
		if (Target.bOverride)
		{
			for (size_t Index = 0; Index < Target.Samples.size(); ++Index)
			{
				const float Value = CurveResource->sample(float(Index) / float(Target.Samples.size() - 1));
				Target.Samples[Index] = std::isfinite(Value) ? std::clamp(Value, 0.0f, 3.0f) : 1.0f;
			}
		}
	};
	Copy(LengthByHeight, Result.LengthByHeight);
	Copy(DensityByHeight, Result.DensityByHeight);
	Copy(BendAlongBranch, Result.BendAlongBranch);
	Copy(RadiusAlongBranch, Result.RadiusAlongBranch);
	return Result;
}
