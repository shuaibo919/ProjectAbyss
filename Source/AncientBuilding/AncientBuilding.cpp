#include "AncientBuilding/AncientBuilding.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>

using namespace godot;

namespace
{
	template <typename TPacked, typename TElement>
	TPacked ToPacked(const std::vector<TElement>& Source)
	{
		TPacked Result;
		if (Source.empty())
		{
			return Result;
		}

		Result.resize(int64_t(Source.size()));
		std::copy(Source.begin(), Source.end(), Result.ptrw());

		return Result;
	}
} // namespace

void AncientBuilding::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("generate"), &AncientBuilding::Generate);
	ClassDB::bind_method(D_METHOD("bake_mesh"), &AncientBuilding::BakeMesh);

	ClassDB::bind_method(D_METHOD("set_parameters", "value"), &AncientBuilding::SetParameters);
	ClassDB::bind_method(D_METHOD("get_parameters"), &AncientBuilding::GetParameters);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "parameters", PROPERTY_HINT_RESOURCE_TYPE, "AncientBuildingParameters"),
		"set_parameters", "get_parameters");

	ClassDB::bind_method(D_METHOD("set_auto_regenerate", "value"), &AncientBuilding::SetAutoRegenerate);
	ClassDB::bind_method(D_METHOD("should_auto_regenerate"), &AncientBuilding::ShouldAutoRegenerate);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_regenerate"), "set_auto_regenerate", "should_auto_regenerate");

	ClassDB::bind_method(D_METHOD("get_vertex_count"), &AncientBuilding::GetVertexCount);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &AncientBuilding::GetTriangleCount);
}

void AncientBuilding::_validate_property(PropertyInfo& Property) const
{
	if (Property.name == StringName("mesh"))
	{
		Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
	}
}

void AncientBuilding::_ready()
{
	EnsureParameters();

	if (get_mesh().is_null())
	{
		Generate();
	}
}

void AncientBuilding::EnsureParameters()
{
	if (Parameters.is_valid())
	{
		return;
	}

	SetParameters(Ref<AncientBuildingParameters>(memnew(AncientBuildingParameters)));
}

void AncientBuilding::EnsureMaterial()
{
	if (BuildingMaterial.is_null())
	{
		BuildingMaterial.instantiate();
		BuildingMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		BuildingMaterial->set_roughness(0.88f);
		BuildingMaterial->set_metallic(0.0f);
	}
}

void AncientBuilding::CollectSpec(BuildingGen::BuildingSpec& OutSpec) const
{
	const Ref<AncientBuildingParameters>& P = Parameters;

	OutSpec.Width = P->GetWidth();
	OutSpec.Depth = P->GetDepth();
	OutSpec.BaysX = std::max(P->GetBaysX(), 1);
	OutSpec.BaysZ = std::max(P->GetBaysZ(), 1);
	OutSpec.RoofType = P->GetRoofType();

	OutSpec.Module = P->GetModule();

	OutSpec.PlatformHeight = P->GetPlatformHeight();
	OutSpec.PlatformHalfWidth = P->GetPlatformHalfWidth();
	OutSpec.PlatformHalfDepth = P->GetPlatformHalfDepth();
	OutSpec.bGenerateFence = P->ShouldGenerateFence();
	OutSpec.bGenerateSteps = P->ShouldGenerateSteps();
	OutSpec.FenceHeight = P->GetFenceHeight();
	OutSpec.FenceGapWidth = P->GetFenceGapWidth();
	OutSpec.StepRunCount = P->GetStepRunCount();
	OutSpec.StepCount = std::max(P->GetStepCount(), 1);
	OutSpec.StepRunDepth = P->GetStepRunDepth();

	OutSpec.bGenerateColumns = P->ShouldGenerateColumns();
	OutSpec.bGenerateWalls = P->ShouldGenerateWalls();
	OutSpec.ColumnRadius = P->GetColumnRadius();
	OutSpec.ColumnSides = std::max(P->GetColumnSides(), 3);
	OutSpec.ColumnHeight = P->GetColumnHeight();
	OutSpec.EaveHeight = P->GetEaveHeight();
	OutSpec.BracketHeight = P->GetBracketHeight();
	OutSpec.RoofBase = P->GetRoofBase();

	OutSpec.EaveOverhang = P->GetEaveOverhang();
	OutSpec.RoofHeight = P->GetRoofHeight();
	OutSpec.RafterCourses = std::max(P->GetRafterCourses(), 3);
	OutSpec.EaveRiseRatio = P->GetEaveRiseRatio();
	OutSpec.RidgeRiseRatio = P->GetRidgeRiseRatio();
	OutSpec.TileCourseWidth = P->GetTileCourseWidth();
	OutSpec.TileCoverage = P->GetTileCoverage();
	OutSpec.RidgeScale = P->GetRidgeScale();
	OutSpec.GableRatio = P->GetGableRatio();
	OutSpec.GableOverhang = P->GetGableOverhang();
	OutSpec.RollRadius = P->GetRollRadius();
	OutSpec.FlatTopRatio = P->GetFlatTopRatio();
	OutSpec.Sides = std::max(P->GetSides(), 3);
	OutSpec.FinialSize = P->GetFinialSize();
	OutSpec.HelmetBulge = P->GetHelmetBulge();
	OutSpec.PlanApothem = P->GetPlanApothem();
	OutSpec.CornerRise = P->GetCornerRise();
	OutSpec.CornerExtend = P->GetCornerExtend();
	OutSpec.CornerSpan = P->GetCornerSpan();

	OutSpec.StoneColor = P->GetStoneColor();
	OutSpec.TimberColor = P->GetTimberColor();
	OutSpec.PlasterColor = P->GetPlasterColor();
	OutSpec.TileColor = P->GetTileColor();
	OutSpec.RidgeColor = P->GetRidgeColor();
	OutSpec.BracketColor = P->GetBracketColor();
}

void AncientBuilding::Generate()
{
	EnsureParameters();
	EnsureMaterial();

	BuildingGen::BuildingSpec Spec;
	CollectSpec(Spec);

	BuildingGen::MeshAccumulator Accumulated;
	BuildingGen::BuildBuilding(Spec, Accumulated);

	if (Accumulated.Indices.empty())
	{
		set_mesh(Ref<ArrayMesh>());
		LastVertexCount = 0;
		LastTriangleCount = 0;
		return;
	}

	Array Arrays;
	Arrays.resize(Mesh::ARRAY_MAX);
	Arrays[Mesh::ARRAY_VERTEX] = ToPacked<PackedVector3Array>(Accumulated.Vertices);
	Arrays[Mesh::ARRAY_NORMAL] = ToPacked<PackedVector3Array>(Accumulated.Normals);
	Arrays[Mesh::ARRAY_TEX_UV] = ToPacked<PackedVector2Array>(Accumulated.UVs);
	Arrays[Mesh::ARRAY_COLOR] = ToPacked<PackedColorArray>(Accumulated.Colors);
	Arrays[Mesh::ARRAY_INDEX] = ToPacked<PackedInt32Array>(Accumulated.Indices);

	Ref<ArrayMesh> Result(memnew(ArrayMesh));
	Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays);
	Result->surface_set_material(0, BuildingMaterial);
	set_mesh(Result);

	LastVertexCount = int32_t(Accumulated.Vertices.size());
	LastTriangleCount = Accumulated.GetTriangleCount();
}

Ref<ArrayMesh> AncientBuilding::BakeMesh()
{
	Generate();

	return get_mesh();
}

void AncientBuilding::RequestRegenerate()
{
	if (!bAutoRegenerate || !is_inside_tree())
	{
		return;
	}

	Generate();
}

void AncientBuilding::OnParametersChanged()
{
	RequestRegenerate();
}

void AncientBuilding::SetParameters(const Ref<AncientBuildingParameters>& Value)
{
	const Callable OnChanged = callable_mp(this, &AncientBuilding::OnParametersChanged);

	if (Parameters.is_valid() && Parameters->is_connected("changed", OnChanged))
	{
		Parameters->disconnect("changed", OnChanged);
	}

	Parameters = Value;

	if (Parameters.is_valid() && !Parameters->is_connected("changed", OnChanged))
	{
		Parameters->connect("changed", OnChanged);
	}

	RequestRegenerate();
}

void AncientBuilding::SetAutoRegenerate(bool bValue)
{
	bAutoRegenerate = bValue;
}
