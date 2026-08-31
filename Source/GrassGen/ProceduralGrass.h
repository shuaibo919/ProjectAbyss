#pragma once

// Scene node for a procedural grass clump. Asset-free: geometry, colours and material
// are all generated, so it needs nothing on disk beyond this extension — the same
// contract as ProceduralRock/ProceduralTree/AncientBuilding.
//
// Generation is an original geometric design (no ported reference); see
// GrassGen/GrassMeshBuilder.h for the algorithm notes.

#include "GrassGen/GrassMeshBuilder.h"
#include "GrassGen/ProceduralGrassParameters.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot
{
	class ProceduralGrass : public MeshInstance3D
	{
		GDCLASS(ProceduralGrass, MeshInstance3D)

	private:
		Ref<ProceduralGrassParameters> Parameters;
		bool bAutoRegenerate = true;

		Ref<StandardMaterial3D> GrassMaterial;

		int32_t LastVertexCount = 0;
		int32_t LastTriangleCount = 0;

		void EnsureParameters();
		void EnsureMaterial();
		void OnParametersChanged();
		void RequestRegenerate();
		void CollectSpec(GrassGen::GrassSpec& OutSpec) const;

	protected:
		static void _bind_methods();

		/**
		 * Keeps `mesh` out of the scene file: it is derived from the parameters, and storing it
		 * would inline an ArrayMesh into every scene holding a grass clump. Use bake_mesh().
		 */
		void _validate_property(PropertyInfo& Property) const;

	public:
		void _ready() override;

		void Generate();
		Ref<ArrayMesh> BakeMesh();

		void SetParameters(const Ref<ProceduralGrassParameters>& Value);
		Ref<ProceduralGrassParameters> GetParameters() const { return Parameters; }

		void SetAutoRegenerate(bool bValue);
		bool ShouldAutoRegenerate() const { return bAutoRegenerate; }

		int32_t GetVertexCount() const { return LastVertexCount; }
		int32_t GetTriangleCount() const { return LastTriangleCount; }
	};
} // namespace godot
