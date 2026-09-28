#include "AncientBuilding/AncientBuildingCompound.h"

#include "AncientBuilding/AncientBuilding.h"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>
#include <cmath>

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

	Ref<ArrayMesh> ToArrayMesh(const BuildingGen::MeshAccumulator& Mesh, const Ref<Material>& Material)
	{
		Ref<ArrayMesh> Result(memnew(ArrayMesh));
		if (Mesh.Indices.empty())
		{
			return Result;
		}
		Array Arrays;
		Arrays.resize(Mesh::ARRAY_MAX);
		Arrays[Mesh::ARRAY_VERTEX] = ToPackedArray<PackedVector3Array>(Mesh.Vertices);
		Arrays[Mesh::ARRAY_NORMAL] = ToPackedArray<PackedVector3Array>(Mesh.Normals);
		Arrays[Mesh::ARRAY_TEX_UV] = ToPackedArray<PackedVector2Array>(Mesh.UVs);
		Arrays[Mesh::ARRAY_COLOR] = ToPackedArray<PackedColorArray>(Mesh.Colors);
		Arrays[Mesh::ARRAY_INDEX] = ToPackedArray<PackedInt32Array>(Mesh.Indices);
		Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays);
		if (Material.is_valid())
		{
			Result->surface_set_material(0, Material);
		}
		return Result;
	}

	/** Side bits as BuildingSpec uses them: 1 +Z, 2 -Z, 4 +X, 8 -X. */
	uint32_t SideBit(int32_t Side)
	{
		switch (Side)
		{
			case AncientBuildingWing::ATTACH_FRONT: return 1u;
			case AncientBuildingWing::ATTACH_BACK: return 2u;
			case AncientBuildingWing::ATTACH_RIGHT: return 4u;
			case AncientBuildingWing::ATTACH_LEFT: return 8u;
			default: return 0u;
		}
	}

	/** Quarter turns that point a wing's local +Z out of the parent's side. */
	int32_t SideTurns(int32_t Side)
	{
		switch (Side)
		{
			case AncientBuildingWing::ATTACH_RIGHT: return 1;
			case AncientBuildingWing::ATTACH_BACK: return 2;
			case AncientBuildingWing::ATTACH_LEFT: return 3;
			default: return 0;
		}
	}

	float NearestLine(const std::vector<float>& Lines, float Value)
	{
		float Best = Lines.front();
		for (const float Line : Lines)
		{
			if (std::abs(Line - Value) < std::abs(Best - Value))
			{
				Best = Line;
			}
		}
		return Best;
	}

	Ref<AncientBuildingParameters> MakeParameters(float Width, float Depth, int32_t BaysX, int32_t BaysZ, int32_t Roof)
	{
		Ref<AncientBuildingParameters> P(memnew(AncientBuildingParameters));
		P->SetWidth(Width);
		P->SetDepth(Depth);
		P->SetBaysX(BaysX);
		P->SetBaysZ(BaysZ);
		P->SetRoofType(Roof);
		P->SetRidgeDetail(1);
		return P;
	}

	Ref<AncientBuildingWing> MakeWing(const Ref<AncientBuildingParameters>& P, int32_t AttachTo, int32_t Side)
	{
		Ref<AncientBuildingWing> Wing(memnew(AncientBuildingWing));
		Wing->SetParameters(P);
		Wing->SetAttachTo(AttachTo);
		Wing->SetAttachSide(Side);
		return Wing;
	}
} // namespace

// ==================== AncientBuildingWing ====================

void AncientBuildingWing::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_parameters", "value"), &AncientBuildingWing::SetParameters);
	ClassDB::bind_method(D_METHOD("get_parameters"), &AncientBuildingWing::GetParameters);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "parameters", PROPERTY_HINT_RESOURCE_TYPE, "AncientBuildingParameters"),
		"set_parameters", "get_parameters");

	ClassDB::bind_method(D_METHOD("set_attach_to", "value"), &AncientBuildingWing::SetAttachTo);
	ClassDB::bind_method(D_METHOD("get_attach_to"), &AncientBuildingWing::GetAttachTo);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "attach_to", PROPERTY_HINT_RANGE, "-1,31,1"), "set_attach_to", "get_attach_to");
	ClassDB::bind_method(D_METHOD("set_attach_side", "value"), &AncientBuildingWing::SetAttachSide);
	ClassDB::bind_method(D_METHOD("get_attach_side"), &AncientBuildingWing::GetAttachSide);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "attach_side", PROPERTY_HINT_ENUM,
		String::utf8("前 Front,后 Back,右 Right,左 Left,十字 Cross")), "set_attach_side", "get_attach_side");
	ClassDB::bind_method(D_METHOD("set_offset_along", "value"), &AncientBuildingWing::SetOffsetAlong);
	ClassDB::bind_method(D_METHOD("get_offset_along"), &AncientBuildingWing::GetOffsetAlong);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_along"), "set_offset_along", "get_offset_along");
	ClassDB::bind_method(D_METHOD("set_snap_to_columns", "value"), &AncientBuildingWing::SetSnapToColumns);
	ClassDB::bind_method(D_METHOD("should_snap_to_columns"), &AncientBuildingWing::ShouldSnapToColumns);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "snap_to_columns"), "set_snap_to_columns", "should_snap_to_columns");
	ClassDB::bind_method(D_METHOD("set_position", "value"), &AncientBuildingWing::SetPosition);
	ClassDB::bind_method(D_METHOD("get_position"), &AncientBuildingWing::GetPosition);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "position"), "set_position", "get_position");
	ClassDB::bind_method(D_METHOD("set_turns", "value"), &AncientBuildingWing::SetTurns);
	ClassDB::bind_method(D_METHOD("get_turns"), &AncientBuildingWing::GetTurns);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "turns", PROPERTY_HINT_RANGE, "0,3,1"), "set_turns", "get_turns");
	ClassDB::bind_method(D_METHOD("set_roof_only", "value"), &AncientBuildingWing::SetRoofOnly);
	ClassDB::bind_method(D_METHOD("is_roof_only"), &AncientBuildingWing::IsRoofOnly);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "roof_only"), "set_roof_only", "is_roof_only");
	ClassDB::bind_method(D_METHOD("set_end_on", "value"), &AncientBuildingWing::SetEndOn);
	ClassDB::bind_method(D_METHOD("is_end_on"), &AncientBuildingWing::IsEndOn);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "end_on"), "set_end_on", "is_end_on");

	ClassDB::bind_integer_constant(get_class_static(), StringName(), "ATTACH_FRONT", ATTACH_FRONT);
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "ATTACH_BACK", ATTACH_BACK);
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "ATTACH_RIGHT", ATTACH_RIGHT);
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "ATTACH_LEFT", ATTACH_LEFT);
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "ATTACH_CROSS", ATTACH_CROSS);
}

void AncientBuildingWing::SetParameters(const Ref<AncientBuildingParameters>& Value)
{
	const Callable Listener = callable_mp(this, &AncientBuildingWing::OnChanged);
	if (Parameters.is_valid() && Parameters->is_connected("changed", Listener))
	{
		Parameters->disconnect("changed", Listener);
	}
	Parameters = Value;
	if (Parameters.is_valid())
	{
		Parameters->connect("changed", Listener);
	}
	emit_changed();
}

void AncientBuildingWing::OnChanged()
{
	emit_changed();
}

// ==================== AncientBuildingCompound ====================

void AncientBuildingCompound::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("generate"), &AncientBuildingCompound::Generate);
	ClassDB::bind_method(D_METHOD("bake_mesh"), &AncientBuildingCompound::BakeMesh);
	ClassDB::bind_method(D_METHOD("apply_preset", "preset"), &AncientBuildingCompound::ApplyPreset);
	ClassDB::bind_method(D_METHOD("get_report"), &AncientBuildingCompound::GetReport);
	ClassDB::bind_method(D_METHOD("get_wing_mesh", "wing"), &AncientBuildingCompound::GetWingMesh);
	ClassDB::bind_method(D_METHOD("sample_wing_roof_height", "wing", "x", "z"),
		&AncientBuildingCompound::SampleWingRoofHeight);
	ClassDB::bind_method(D_METHOD("get_wing_layout", "wing"), &AncientBuildingCompound::GetWingLayout);

	ClassDB::bind_method(D_METHOD("set_wings", "value"), &AncientBuildingCompound::SetWings);
	ClassDB::bind_method(D_METHOD("get_wings"), &AncientBuildingCompound::GetWings);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "wings", PROPERTY_HINT_TYPE_STRING,
		vformat("%d/%d:%s", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "AncientBuildingWing")),
		"set_wings", "get_wings");
	ClassDB::bind_method(D_METHOD("set_auto_regenerate", "value"), &AncientBuildingCompound::SetAutoRegenerate);
	ClassDB::bind_method(D_METHOD("should_auto_regenerate"), &AncientBuildingCompound::ShouldAutoRegenerate);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_regenerate"), "set_auto_regenerate", "should_auto_regenerate");

	ClassDB::bind_integer_constant(get_class_static(), StringName(), "PRESET_BAOSHA", PRESET_BAOSHA);
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "PRESET_GOULIANDA", PRESET_GOULIANDA);
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "PRESET_SHIZIJI", PRESET_SHIZIJI);
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "PRESET_GONGZI", PRESET_GONGZI);
	ClassDB::bind_integer_constant(get_class_static(), StringName(), "PRESET_QUCHI", PRESET_QUCHI);
}

void AncientBuildingCompound::_validate_property(PropertyInfo& Property) const
{
	// Derived from the wings, never serialised — same contract as AncientBuilding.
	if (Property.name == StringName("mesh"))
	{
		Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
	}
}

void AncientBuildingCompound::_ready()
{
	if (Wings.is_empty())
	{
		ApplyPreset(PRESET_BAOSHA);
	}
	if (get_mesh().is_null())
	{
		Generate();
	}
}

void AncientBuildingCompound::SetWings(const TypedArray<AncientBuildingWing>& Value)
{
	Wings = Value;
	ConnectWings();
	OnWingChanged();
}

void AncientBuildingCompound::ConnectWings()
{
	const Callable Listener = callable_mp(this, &AncientBuildingCompound::OnWingChanged);
	for (int64_t Index = 0; Index < Wings.size(); ++Index)
	{
		const Ref<AncientBuildingWing> Wing = Wings[Index];
		if (Wing.is_valid() && !Wing->is_connected("changed", Listener))
		{
			Wing->connect("changed", Listener);
		}
	}
}

void AncientBuildingCompound::OnWingChanged()
{
	if (bAutoRegenerate && is_inside_tree())
	{
		Generate();
	}
}

bool AncientBuildingCompound::Layout(std::vector<BuildingGen::CompoundWing>& Out) const
{
	Out.clear();
	for (int64_t Index = 0; Index < Wings.size(); ++Index)
	{
		const Ref<AncientBuildingWing> Wing = Wings[Index];
		if (Wing.is_null())
		{
			WARN_PRINT(vformat("AncientBuildingCompound: wing %d is empty.", Index));
			return false;
		}
		Ref<AncientBuildingParameters> Params = Wing->GetParameters();
		if (Params.is_null())
		{
			Params.instantiate();
		}

		BuildingGen::CompoundWing Placed;
		const int32_t Parent = Wing->GetAttachTo();
		if (Parent < 0)
		{
			AncientBuilding::CollectSpecFrom(Params, Placed.Spec);
			Placed.Offset = Wing->GetPosition();
			Placed.Turns = Wing->GetTurns();
			Placed.Spec.bRoofOnly = Wing->IsRoofOnly();
			Out.push_back(Placed);
			continue;
		}
		if (Parent >= int32_t(Index))
		{
			// Unsupported layouts are reported, not guessed at (05 契约 §2.7).
			WARN_PRINT(vformat("AncientBuildingCompound: wing %d attaches to wing %d, which is not an earlier wing.",
				Index, Parent));
			return false;
		}

		BuildingGen::CompoundWing& ParentWing = Out[size_t(Parent)];
		const BuildingGen::BuildingSpec& ParentSpec = ParentWing.Spec;
		const int32_t Side = Wing->GetAttachSide();

		if (Side == AncientBuildingWing::ATTACH_CROSS)
		{
			AncientBuilding::CollectSpecFrom(Params, Placed.Spec);
			Placed.Offset = ParentWing.Offset;
			Placed.Turns = ParentWing.Turns + 1;
			Placed.Spec.bRoofOnly = true;
			Out.push_back(Placed);
			continue;
		}
		if (ParentSpec.Sides != 4)
		{
			WARN_PRINT(vformat("AncientBuildingCompound: wing %d attaches to a polygonal wing; only CROSS or free wings can.",
				Index));
			return false;
		}

		const bool bAlongX = Side == AncientBuildingWing::ATTACH_FRONT || Side == AncientBuildingWing::ATTACH_BACK;
		std::vector<BuildingGen::StoreyFrame> Frames;
		BuildingGen::DescribeStoreys(ParentSpec, Frames);
		const std::vector<float> Lines = bAlongX ? Frames[0].ColumnLinesX : Frames[0].ColumnLinesZ;
		const float SideSpan = bAlongX ? ParentSpec.Width : ParentSpec.Depth;

		// Snap the wing's edges to the parent's column lines, so the junction has one set of columns.
		// End-on, the wing meets the parent with its depth; otherwise with its width.
		const bool bEndOn = Wing->IsEndOn();
		float Centre = Wing->GetOffsetAlong();
		const float Original = bEndOn ? Params->GetDepth() : Params->GetWidth();
		float Width = Original;
		const bool bNarrower = Width <= SideSpan + 0.01f;
		if (Wing->ShouldSnapToColumns() && bNarrower && Lines.size() >= 2)
		{
			float Low = NearestLine(Lines, Centre - Width * 0.5f);
			float High = NearestLine(Lines, Centre + Width * 0.5f);
			if (High - Low < 0.01f)
			{
				// Both edges snapped to one line: take the bay on the side the centre is on.
				for (const float Line : Lines)
				{
					if (Line > High + 0.01f)
					{
						High = Line;
						break;
					}
				}
			}
			Width = High - Low;
			Centre = (Low + High) * 0.5f;
		}
		if (std::abs(Width - Original) > 1e-4f)
		{
			// A snapped wing keeps its proportions: its module stays the one its own width gave it.
			Ref<AncientBuildingParameters> Snapped = Params->duplicate();
			if (Snapped->GetModuleSpan() <= 0.0f)
			{
				Snapped->SetModuleSpan(Params->GetWidth());
			}
			if (bEndOn)
			{
				Snapped->SetDepth(Width);
			}
			else
			{
				Snapped->SetWidth(Width);
			}
			Params = Snapped;
		}
		AncientBuilding::CollectSpecFrom(Params, Placed.Spec);

		const Vector3 Outward = bAlongX
			? Vector3(0.0f, 0.0f, Side == AncientBuildingWing::ATTACH_FRONT ? 1.0f : -1.0f)
			: Vector3(Side == AncientBuildingWing::ATTACH_RIGHT ? 1.0f : -1.0f, 0.0f, 0.0f);
		const Vector3 Along = bAlongX ? Vector3(1, 0, 0) : Vector3(0, 0, 1);
		const float ParentHalf = bAlongX ? ParentSpec.Depth * 0.5f : ParentSpec.Width * 0.5f;
		const float Reach = bEndOn ? Placed.Spec.Width : Placed.Spec.Depth;
		const Vector3 LocalCentre = Outward * (ParentHalf + Reach * 0.5f) + Along * Centre;
		Placed.Offset = ParentWing.Offset + BuildingGen::RotateQuarterTurns(LocalCentre, ParentWing.Turns);
		// End-on turns the wing's local +X, not +Z, out of the parent's side.
		Placed.Turns = ParentWing.Turns + SideTurns(Side) + (bEndOn ? 3 : 0);
		Placed.Spec.bRoofOnly = Wing->IsRoofOnly();

		// The narrower wing gives up its columns on the shared line; neither puts a stair there.
		const uint32_t ChildShared = bEndOn ? 8u : 2u;
		const float SharedSpan = bEndOn ? Placed.Spec.Depth : Placed.Spec.Width;
		Placed.Spec.NoStepSides |= ChildShared;
		ParentWing.Spec.NoStepSides |= SideBit(Side);
		if (SharedSpan <= SideSpan + 0.01f)
		{
			Placed.Spec.NoColumnSides |= ChildShared;
		}
		else
		{
			ParentWing.Spec.NoColumnSides |= SideBit(Side);
		}
		Out.push_back(Placed);
	}
	return !Out.empty();
}

void AncientBuildingCompound::Generate()
{
	std::vector<BuildingGen::CompoundWing> Placed;
	LastWingMeshes.clear();
	LastFields.clear();
	LastLayout.clear();
	LastReport = BuildingGen::CompoundReport();
	if (!Layout(Placed))
	{
		set_mesh(Ref<ArrayMesh>());
		return;
	}

	BuildingGen::MeshAccumulator Accumulated;
	BuildingGen::BuildCompound(Placed, Accumulated, &LastWingMeshes, &LastFields, &LastReport);
	LastLayout = Placed;

	if (Material.is_null())
	{
		Material.instantiate();
		Material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		Material->set_roughness(0.88f);
		Material->set_metallic(0.0f);
	}
	set_mesh(Accumulated.Indices.empty() ? Ref<ArrayMesh>() : ToArrayMesh(Accumulated, Material));
}

Ref<ArrayMesh> AncientBuildingCompound::BakeMesh()
{
	if (Wings.is_empty())
	{
		ApplyPreset(PRESET_BAOSHA);
	}
	Generate();
	return get_mesh();
}

void AncientBuildingCompound::ApplyPreset(int32_t Preset)
{
	TypedArray<AncientBuildingWing> Result;
	switch (Preset)
	{
		case PRESET_GOULIANDA:
		{
			// Two equal 悬山 halls, the back one standing on the front one's back column line.
			Result.push_back(MakeWing(MakeParameters(12.0f, 7.0f, 3, 2, 3), -1, 0));
			Result.push_back(MakeWing(MakeParameters(12.0f, 7.0f, 3, 2, 3), 0, AncientBuildingWing::ATTACH_BACK));
			break;
		}
		case PRESET_SHIZIJI:
		{
			// A square pavilion under two crossing 悬山 gables: the second is roof only.
			Ref<AncientBuildingParameters> Hall = MakeParameters(9.0f, 9.0f, 3, 3, 3);
			Hall->SetFenceLambda(2);
			Result.push_back(MakeWing(Hall, -1, 0));
			Result.push_back(MakeWing(MakeParameters(9.0f, 9.0f, 3, 3, 3), 0, AncientBuildingWing::ATTACH_CROSS));
			break;
		}
		case PRESET_GONGZI:
		{
			// 工字殿: front hall, a narrower 穿堂 behind it, and a back hall on the 穿堂's far end.
			Result.push_back(MakeWing(MakeParameters(14.0f, 8.0f, 5, 2, 1), -1, 0));
			Ref<AncientBuildingParameters> Passage = MakeParameters(7.0f, 7.0f, 1, 2, 3);
			Passage->SetGenerateFence(false);
			Result.push_back(MakeWing(Passage, 0, AncientBuildingWing::ATTACH_BACK));
			Result.push_back(MakeWing(MakeParameters(14.0f, 8.0f, 5, 2, 1), 1, AncientBuildingWing::ATTACH_FRONT));
			break;
		}
		case PRESET_QUCHI:
		{
			// 曲尺: an arm running forward from the hall's right end, gable end-on to the hall, its
			// ridge meeting the hall's front slope in two valleys. Same module as the hall, so the
			// two roofs are one family.
			Result.push_back(MakeWing(MakeParameters(14.0f, 7.0f, 5, 2, 3), -1, 0));
			Ref<AncientBuildingParameters> Arm = MakeParameters(10.0f, 5.6f, 3, 2, 3);
			Arm->SetModuleSpan(14.0f);
			Arm->SetFenceLambda(0);
			Ref<AncientBuildingWing> Wing = MakeWing(Arm, 0, AncientBuildingWing::ATTACH_FRONT);
			Wing->SetEndOn(true);
			Wing->SetOffsetAlong(7.0f - 2.8f);
			Result.push_back(Wing);
			break;
		}
		default:
		{
			// 抱厦: a 歇山 porch hall in front of a 歇山 main hall, snapped to its middle bays.
			Result.push_back(MakeWing(MakeParameters(15.0f, 10.0f, 5, 3, 1), -1, 0));
			Ref<AncientBuildingParameters> Porch = MakeParameters(8.0f, 6.0f, 3, 1, 1);
			Porch->SetFenceLambda(0);
			Result.push_back(MakeWing(Porch, 0, AncientBuildingWing::ATTACH_FRONT));
			break;
		}
	}
	SetWings(Result);
}

Dictionary AncientBuildingCompound::GetReport() const
{
	Dictionary Result;
	Result["triangles_in"] = LastReport.TrianglesIn;
	Result["triangles_out"] = LastReport.TrianglesOut;
	Result["valley_count"] = LastReport.ValleyCount;
	Result["valley_length"] = LastReport.ValleyLength;
	return Result;
}

Ref<ArrayMesh> AncientBuildingCompound::GetWingMesh(int32_t Wing) const
{
	if (Wing < 0 || Wing >= int32_t(LastWingMeshes.size()))
	{
		return Ref<ArrayMesh>();
	}
	return ToArrayMesh(LastWingMeshes[size_t(Wing)], Material);
}

float AncientBuildingCompound::SampleWingRoofHeight(int32_t Wing, float X, float Z) const
{
	if (Wing < 0 || Wing >= int32_t(LastFields.size()))
	{
		return BuildingGen::RoofHeightField::NONE;
	}
	return LastFields[size_t(Wing)].Sample(X, Z);
}

Dictionary AncientBuildingCompound::GetWingLayout(int32_t Wing) const
{
	Dictionary Result;
	if (Wing < 0 || Wing >= int32_t(LastLayout.size()))
	{
		return Result;
	}
	const BuildingGen::CompoundWing& Placed = LastLayout[size_t(Wing)];
	Result["offset"] = Placed.Offset;
	Result["turns"] = Placed.Turns;
	Result["width"] = Placed.Spec.Width;
	Result["depth"] = Placed.Spec.Depth;
	Result["eave_height"] = Placed.Spec.EaveHeight;
	Result["roof_base"] = Placed.Spec.RoofBase;
	Result["module"] = Placed.Spec.Module;
	Result["roof_only"] = Placed.Spec.bRoofOnly;
	Result["no_column_sides"] = int32_t(Placed.Spec.NoColumnSides);
	return Result;
}
