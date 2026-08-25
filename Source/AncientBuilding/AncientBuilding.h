#pragma once

// Scene node for a complete ancient Chinese building. Asset-free: geometry, colours and
// material are all generated, so it needs nothing on disk beyond this extension.

#include "AncientBuilding/AncientBuildingParameters.h"
#include "AncientBuilding/BuildingBuilder.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot
{
	class AncientBuilding : public MeshInstance3D
	{
		GDCLASS(AncientBuilding, MeshInstance3D)

	private:
		Ref<AncientBuildingParameters> Parameters;
		bool bAutoRegenerate = true;

		Ref<StandardMaterial3D> BuildingMaterial;

		int32_t LastVertexCount = 0;
		int32_t LastTriangleCount = 0;

		void EnsureParameters();
		void EnsureMaterial();
		void OnParametersChanged();
		void RequestRegenerate();
		void CollectSpec(BuildingGen::BuildingSpec& OutSpec) const;

	protected:
		static void _bind_methods();

		/**
		 * Keeps `mesh` out of the scene file: it is derived from the parameters, and storing it
		 * would inline a large ArrayMesh into every scene holding a building. Use bake_mesh().
		 */
		void _validate_property(PropertyInfo& Property) const;

	public:
		void _ready() override;

		void Generate();
		Ref<ArrayMesh> BakeMesh();

		void SetParameters(const Ref<AncientBuildingParameters>& Value);
		Ref<AncientBuildingParameters> GetParameters() const { return Parameters; }

		void SetAutoRegenerate(bool bValue);
		bool ShouldAutoRegenerate() const { return bAutoRegenerate; }

		int32_t GetVertexCount() const { return LastVertexCount; }
		int32_t GetTriangleCount() const { return LastTriangleCount; }
	};
} // namespace godot
