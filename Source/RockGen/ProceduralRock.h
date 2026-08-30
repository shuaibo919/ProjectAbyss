#pragma once

// Scene node for a procedural rock. Asset-free: geometry, colours and material are all
// generated, so it needs nothing on disk beyond this extension — the same contract as
// AncientBuilding and ProceduralTree.
//
// Generation is a CPU port of "Unity Procedural Rock Generation" (see
// RockGen/RockMeshBuilder.h for the algorithm notes).

#include "RockGen/ProceduralRockParameters.h"
#include "RockGen/RockMeshBuilder.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot
{
	class ProceduralRock : public MeshInstance3D
	{
		GDCLASS(ProceduralRock, MeshInstance3D)

	private:
		Ref<ProceduralRockParameters> Parameters;
		bool bAutoRegenerate = true;

		Ref<StandardMaterial3D> RockMaterial;

		int32_t LastVertexCount = 0;
		int32_t LastTriangleCount = 0;

		void EnsureParameters();
		void EnsureMaterial();
		void OnParametersChanged();
		void RequestRegenerate();
		void CollectSpec(RockGen::RockSpec& OutSpec) const;

	protected:
		static void _bind_methods();

		/**
		 * Keeps `mesh` out of the scene file: it is derived from the parameters, and storing it
		 * would inline a large ArrayMesh into every scene holding a rock. Use bake_mesh().
		 */
		void _validate_property(PropertyInfo& Property) const;

	public:
		void _ready() override;

		void Generate();
		Ref<ArrayMesh> BakeMesh();

		void SetParameters(const Ref<ProceduralRockParameters>& Value);
		Ref<ProceduralRockParameters> GetParameters() const { return Parameters; }

		void SetAutoRegenerate(bool bValue);
		bool ShouldAutoRegenerate() const { return bAutoRegenerate; }

		int32_t GetVertexCount() const { return LastVertexCount; }
		int32_t GetTriangleCount() const { return LastTriangleCount; }
	};
} // namespace godot
