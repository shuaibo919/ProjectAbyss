#include "GrassGen/ProceduralGrass.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>
#include <vector>

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

void ProceduralGrass::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("generate"), &ProceduralGrass::Generate);
	ClassDB::bind_method(D_METHOD("bake_mesh"), &ProceduralGrass::BakeMesh);

	ClassDB::bind_method(D_METHOD("set_parameters", "value"), &ProceduralGrass::SetParameters);
	ClassDB::bind_method(D_METHOD("get_parameters"), &ProceduralGrass::GetParameters);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "parameters", PROPERTY_HINT_RESOURCE_TYPE, "ProceduralGrassParameters"),
		"set_parameters", "get_parameters");

	ClassDB::bind_method(D_METHOD("set_auto_regenerate", "value"), &ProceduralGrass::SetAutoRegenerate);
	ClassDB::bind_method(D_METHOD("should_auto_regenerate"), &ProceduralGrass::ShouldAutoRegenerate);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_regenerate"), "set_auto_regenerate", "should_auto_regenerate");

	ClassDB::bind_method(D_METHOD("get_vertex_count"), &ProceduralGrass::GetVertexCount);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &ProceduralGrass::GetTriangleCount);
}

void ProceduralGrass::_validate_property(PropertyInfo& Property) const
{
	if (Property.name == StringName("mesh"))
	{
		Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
	}
}

void ProceduralGrass::_ready()
{
	EnsureParameters();

	if (get_mesh().is_null())
	{
		Generate();
	}
}

void ProceduralGrass::EnsureParameters()
{
	if (Parameters.is_valid())
	{
		return;
	}

	SetParameters(Ref<ProceduralGrassParameters>(memnew(ProceduralGrassParameters)));
}

void ProceduralGrass::EnsureMaterial()
{
	if (GrassMaterial.is_null())
	{
		GrassMaterial.instantiate();
		GrassMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		GrassMaterial->set_roughness(0.95f);
		GrassMaterial->set_metallic(0.0f);
		// Blades are two flat panels folded into a shallow V, not a closed volume — back
		// faces are a real, expected viewing angle (the far side of the fold), not a bug
		// to cull away.
		GrassMaterial->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	}
}

void ProceduralGrass::CollectSpec(GrassGen::GrassSpec& OutSpec) const
{
	const Ref<ProceduralGrassParameters>& P = Parameters;

	OutSpec.Species = GrassGen::EGrassSpecies(P->GetSpecies());
	OutSpec.Seed = P->GetSeed();
	OutSpec.Scale = std::clamp(P->GetScale(), 0.05f, 10.0f);
	OutSpec.ClumpRadius = std::clamp(P->GetClumpRadius(), 0.01f, 2.0f);
	OutSpec.BladeCount = std::clamp(P->GetBladeCount(), 0, 60);
	OutSpec.Curvature = std::clamp(P->GetCurvature(), 0.0f, 3.0f);
	OutSpec.LeanAngle = std::clamp(P->GetLeanAngle(), 0.0f, 60.0f);
	OutSpec.LeanAzimuth = P->GetLeanAzimuth();
	OutSpec.ColorVariance = std::clamp(P->GetColorVariance(), 0.0f, 1.0f);
	OutSpec.bUseSpeciesColors = P->ShouldUseSpeciesColors();
	OutSpec.BaseColor = P->GetBaseColor();
	OutSpec.TipColor = P->GetTipColor();
}

void ProceduralGrass::Generate()
{
	EnsureParameters();
	EnsureMaterial();

	GrassGen::GrassSpec Spec;
	CollectSpec(Spec);

	GrassGen::MeshAccumulator Accumulated;
	GrassGen::BuildGrass(Spec, Accumulated);

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
	Result->surface_set_material(0, GrassMaterial);
	set_mesh(Result);

	LastVertexCount = int32_t(Accumulated.Vertices.size());
	LastTriangleCount = Accumulated.GetTriangleCount();
}

Ref<ArrayMesh> ProceduralGrass::BakeMesh()
{
	Generate();

	return get_mesh();
}

void ProceduralGrass::RequestRegenerate()
{
	// In-editor edits regenerate immediately; at runtime the caller decides when to pay for it.
	if (!bAutoRegenerate || !is_inside_tree())
	{
		return;
	}

	Generate();
}

void ProceduralGrass::OnParametersChanged()
{
	RequestRegenerate();
}

void ProceduralGrass::SetParameters(const Ref<ProceduralGrassParameters>& Value)
{
	const Callable OnChanged = callable_mp(this, &ProceduralGrass::OnParametersChanged);

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

void ProceduralGrass::SetAutoRegenerate(bool bValue)
{
	bAutoRegenerate = bValue;
}
