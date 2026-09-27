#pragma once

// Scene node for a complete ancient Chinese building. Asset-free: geometry, colours and
// material are all generated, so it needs nothing on disk beyond this extension.
//
// The mesh can be generated as one surface per building category (BuildingGen::EMaterialSlot), so a
// category can be given its own material — a normal map on the 瓦面, a texture on the 山花 — from
// `slot_materials` or set_slot_material(). A slot left unset changes nothing: its surface gets the
// same vertex-colour material the single-surface build always used.
//
// The split only happens once at least one slot material is assigned. While every slot is null the
// node emits the single vertex-coloured surface it has always emitted — byte for byte, which is
// also what the geometry regression tests read (they take surface 0 to be the whole building, as
// get_vertex_count()/get_triangle_count() report the whole mesh either way).

#include "AncientBuilding/AncientBuildingParameters.h"
#include "AncientBuilding/BuildingBuilder.h"

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace godot
{
	class AncientBuilding : public MeshInstance3D
	{
		GDCLASS(AncientBuilding, MeshInstance3D)

	public:
		/** One material slot per BuildingGen::EMaterialSlot entry. */
		static constexpr int32_t MATERIAL_SLOT_COUNT = BuildingGen::MATERIAL_SLOT_COUNT;

	private:
		Ref<AncientBuildingParameters> Parameters;
		bool bAutoRegenerate = true;

		Ref<StandardMaterial3D> BuildingMaterial;

		/** Null = the slot keeps the generated vertex-colour material. */
		Ref<Material> SlotMaterials[MATERIAL_SLOT_COUNT];

		/**
		 * 脊饰 mesh slots, one per BuildingGen::ERidgeOrnamentKind (正吻 / 垂兽 / 走兽).
		 *
		 * Null = the generator's own cube placeholder, which is the whole point of the switch: a
		 * dwelling grows no 脊饰 at all, and a building that does wants blocks until the real meshes
		 * exist. A mesh set here replaces the cube for its class and is placed on the record the
		 * generator left behind, so it lands on the ridge exactly where the cube would have.
		 * 套兽 has no slot: it belongs to the 翼角梁, which this generator does not build.
		 */
		Ref<Mesh> RidgeOrnamentMeshes[BuildingGen::RIDGE_ORNAMENT_KIND_COUNT];

		int32_t LastVertexCount = 0;
		int32_t LastTriangleCount = 0;
		/** Whole-mesh triangle count per slot, whatever surface layout was emitted. */
		int32_t LastSlotTriangleCounts[MATERIAL_SLOT_COUNT] = {};

		void EnsureParameters();
		void EnsureMaterial();
		/** Fills in the 脊饰 classes the caller supplied a mesh for, at the generator's own records. */
		void PlaceRidgeOrnamentMeshes(BuildingGen::MeshAccumulator& Accumulated, const Color& Tint) const;
		void OnParametersChanged();
		void RequestRegenerate();
		void CollectSpec(BuildingGen::BuildingSpec& OutSpec) const;

		bool HasAnySlotMaterial() const;

	protected:
		static void _bind_methods();

		/**
		 * Keeps `mesh` out of the scene file: it is derived from the parameters, and storing it
		 * would inline a large ArrayMesh into every scene holding a building. Use bake_mesh().
		 * `slot_materials` is dropped the same way while every entry is null, so a building that
		 * uses no slot material leaves no trace in the scene.
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

		/** Slot index is a BuildingGen::EMaterialSlot; out-of-range values are ignored. */
		void SetSlotMaterial(int32_t Slot, const Ref<Material>& Value);
		Ref<Material> GetSlotMaterial(int32_t Slot) const;

		/** Always MATERIAL_SLOT_COUNT long, padded with nulls, so indices stay fixed. */
		void SetSlotMaterials(const Array& Values);
		Array GetSlotMaterials() const;

		/**
		 * Kind is a BuildingGen::ERidgeOrnamentKind; out-of-range values are ignored, and a null mesh
		 * puts the class back on its cube placeholder.
		 */
		void SetRidgeOrnamentMesh(int32_t Kind, const Ref<Mesh>& Value);
		Ref<Mesh> GetRidgeOrnamentMesh(int32_t Kind) const;

		// The same, per class — a bound property setter has to take exactly one argument, so the
		// three slots get named accessors rather than one method's default argument.
		void SetRidgeFinialMesh(const Ref<Mesh>& Value)
		{
			SetRidgeOrnamentMesh(int32_t(BuildingGen::ERidgeOrnamentKind::Finial), Value);
		}
		Ref<Mesh> GetRidgeFinialMesh() const
		{
			return GetRidgeOrnamentMesh(int32_t(BuildingGen::ERidgeOrnamentKind::Finial));
		}
		void SetRidgeBeastMesh(const Ref<Mesh>& Value)
		{
			SetRidgeOrnamentMesh(int32_t(BuildingGen::ERidgeOrnamentKind::Beast), Value);
		}
		Ref<Mesh> GetRidgeBeastMesh() const
		{
			return GetRidgeOrnamentMesh(int32_t(BuildingGen::ERidgeOrnamentKind::Beast));
		}
		void SetRidgeWalkerMesh(const Ref<Mesh>& Value)
		{
			SetRidgeOrnamentMesh(int32_t(BuildingGen::ERidgeOrnamentKind::Walker), Value);
		}
		Ref<Mesh> GetRidgeWalkerMesh() const
		{
			return GetRidgeOrnamentMesh(int32_t(BuildingGen::ERidgeOrnamentKind::Walker));
		}
		/** Slot count for the 脊饰 classes, so GDScript can iterate them by name. */
		int32_t GetRidgeOrnamentSlotCount() const { return BuildingGen::RIDGE_ORNAMENT_KIND_COUNT; }

		int32_t GetSlotCount() const { return MATERIAL_SLOT_COUNT; }
		/** 0 for an out-of-range slot, and for a slot the current building emits nothing into. */
		int32_t GetSlotTriangleCount(int32_t Slot) const;

		int32_t GetVertexCount() const { return LastVertexCount; }
		int32_t GetTriangleCount() const { return LastTriangleCount; }
	};
} // namespace godot
