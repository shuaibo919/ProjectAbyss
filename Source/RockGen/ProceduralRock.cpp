#include "RockGen/ProceduralRock.h"

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

void ProceduralRock::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("generate"), &ProceduralRock::Generate);
	ClassDB::bind_method(D_METHOD("bake_mesh"), &ProceduralRock::BakeMesh);

	ClassDB::bind_method(D_METHOD("set_parameters", "value"), &ProceduralRock::SetParameters);
	ClassDB::bind_method(D_METHOD("get_parameters"), &ProceduralRock::GetParameters);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "parameters", PROPERTY_HINT_RESOURCE_TYPE, "ProceduralRockParameters"),
		"set_parameters", "get_parameters");

	ClassDB::bind_method(D_METHOD("set_auto_regenerate", "value"), &ProceduralRock::SetAutoRegenerate);
	ClassDB::bind_method(D_METHOD("should_auto_regenerate"), &ProceduralRock::ShouldAutoRegenerate);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_regenerate"), "set_auto_regenerate", "should_auto_regenerate");

	ClassDB::bind_method(D_METHOD("get_vertex_count"), &ProceduralRock::GetVertexCount);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &ProceduralRock::GetTriangleCount);
}

void ProceduralRock::_validate_property(PropertyInfo& Property) const
{
	if (Property.name == StringName("mesh"))
	{
		Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
	}
}

void ProceduralRock::_ready()
{
	EnsureParameters();

	if (get_mesh().is_null())
	{
		Generate();
	}
}

void ProceduralRock::EnsureParameters()
{
	if (Parameters.is_valid())
	{
		return;
	}

	SetParameters(Ref<ProceduralRockParameters>(memnew(ProceduralRockParameters)));
}

void ProceduralRock::EnsureMaterial()
{
	if (RockMaterial.is_null())
	{
		RockMaterial.instantiate();
		RockMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		RockMaterial->set_roughness(0.9f);
		RockMaterial->set_metallic(0.0f);
		// The surface is a marching-cubes shell of a displacement field: deep pits can
		// self-intersect, and the resulting tiny "reversed" facets must not show as
		// see-through holes (they were visible as the rock's inside from above). Two
		// shading passes cover them invisibly; the cost is negligible for rocks.
		RockMaterial->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	}
}

void ProceduralRock::CollectSpec(RockGen::RockSpec& OutSpec) const
{
	const Ref<ProceduralRockParameters>& P = Parameters;

	OutSpec.Form = RockGen::ERockForm(P->GetForm());
	OutSpec.Resolution = std::clamp(P->GetResolution(), 8, 128);
	OutSpec.Scale = std::max(P->GetScale(), 0.01f);
	OutSpec.Steps = std::clamp(P->GetSteps(), 8, 72);
	OutSpec.Smoothness = std::clamp(P->GetSmoothness(), 0.01f, 0.2f);
	OutSpec.Seed = P->GetSeed();
	OutSpec.DisplacementScale = std::clamp(P->GetDisplacementScale(), 0.0f, 1.0f);
	OutSpec.DisplacementSpread = std::clamp(P->GetDisplacementSpread(), 1.0f, 10.0f);
	OutSpec.Flatness = std::clamp(P->GetFlatness(), 0.3f, 1.5f);
	OutSpec.Roundness = std::clamp(P->GetRoundness(), 0.0f, 1.0f);
	OutSpec.bCutGround = P->ShouldCutGround();
	OutSpec.GroundCut = std::clamp(P->GetGroundCut(), -0.5f, 0.5f);
	OutSpec.BaseColor = P->GetBaseColor();
	OutSpec.CreviceColor = P->GetCreviceColor();
}

void ProceduralRock::Generate()
{
	EnsureParameters();
	EnsureMaterial();

	RockGen::RockSpec Spec;
	CollectSpec(Spec);

	RockGen::MeshAccumulator Accumulated;
	RockGen::BuildRock(Spec, Accumulated);

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
	Result->surface_set_material(0, RockMaterial);
	set_mesh(Result);

	LastVertexCount = int32_t(Accumulated.Vertices.size());
	LastTriangleCount = Accumulated.GetTriangleCount();
}

Ref<ArrayMesh> ProceduralRock::BakeMesh()
{
	Generate();

	return get_mesh();
}

void ProceduralRock::RequestRegenerate()
{
	// In-editor edits regenerate immediately; at runtime the caller decides when to pay for it.
	if (!bAutoRegenerate || !is_inside_tree())
	{
		return;
	}

	Generate();
}

void ProceduralRock::OnParametersChanged()
{
	RequestRegenerate();
}

void ProceduralRock::SetParameters(const Ref<ProceduralRockParameters>& Value)
{
	const Callable OnChanged = callable_mp(this, &ProceduralRock::OnParametersChanged);

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

void ProceduralRock::SetAutoRegenerate(bool bValue)
{
	bAutoRegenerate = bValue;
}
