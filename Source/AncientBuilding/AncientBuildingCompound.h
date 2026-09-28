#pragma once

// 连体 building node: several AncientBuilding wings laid out against each other's column grids
// and joined by BuildingGen::BuildCompound (Compound.h) into one mesh.
//
// A wing is either free (placed by position / turns) or attached to an earlier wing on one of its
// sides. An attached wing's back stands on the parent's wall line; when it is narrower, its edges
// snap to the parent's column lines and it gives up its own columns on the shared line, so every
// column along a junction is built exactly once.

#include "AncientBuilding/AncientBuildingParameters.h"
#include "AncientBuilding/Compound.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot
{
	class AncientBuildingWing : public Resource
	{
		GDCLASS(AncientBuildingWing, Resource)

	public:
		enum EAttachSide
		{
			/** Out from the parent's front (+Z). */
			ATTACH_FRONT = 0,
			ATTACH_BACK = 1,
			/** Out from the parent's +X side. */
			ATTACH_RIGHT = 2,
			ATTACH_LEFT = 3,
			/** Centred on the parent, turned a quarter, roof only — the crossing gable of 十字脊. */
			ATTACH_CROSS = 4,
		};

	private:
		Ref<AncientBuildingParameters> Parameters;
		/** Index of an earlier wing to attach to; -1 places the wing freely. */
		int32_t AttachTo = -1;
		int32_t AttachSide = ATTACH_FRONT;
		/** Along the parent's side, from its centre line, in metres. */
		float OffsetAlong = 0.0f;
		/** Snap the wing's edges to the parent's column lines (when narrower than that side). */
		bool bSnapToColumns = true;
		/**
		 * Attach by the wing's end (its -X gable) instead of its back, so its ridge runs out from
		 * the parent — the arm of a 曲尺 / 丁字. Off: the ridge runs along the parent's side (抱厦).
		 */
		bool bEndOn = false;
		/** Free wings only. */
		Vector3 Position;
		int32_t Turns = 0;
		bool bRoofOnly = false;

		void OnChanged();

	protected:
		static void _bind_methods();

	public:
		void SetParameters(const Ref<AncientBuildingParameters>& Value);
		Ref<AncientBuildingParameters> GetParameters() const { return Parameters; }

#define ANCIENT_WING_ACCESSORS(Type, Member) \
		void Set##Member(Type Value) { Member = Value; emit_changed(); } \
		Type Get##Member() const { return Member; }

		ANCIENT_WING_ACCESSORS(int32_t, AttachTo)
		ANCIENT_WING_ACCESSORS(int32_t, AttachSide)
		ANCIENT_WING_ACCESSORS(float, OffsetAlong)
		ANCIENT_WING_ACCESSORS(Vector3, Position)
		ANCIENT_WING_ACCESSORS(int32_t, Turns)

#undef ANCIENT_WING_ACCESSORS

		void SetSnapToColumns(bool bValue) { bSnapToColumns = bValue; emit_changed(); }
		bool ShouldSnapToColumns() const { return bSnapToColumns; }
		void SetRoofOnly(bool bValue) { bRoofOnly = bValue; emit_changed(); }
		bool IsRoofOnly() const { return bRoofOnly; }
		void SetEndOn(bool bValue) { bEndOn = bValue; emit_changed(); }
		bool IsEndOn() const { return bEndOn; }
	};

	class AncientBuildingCompound : public MeshInstance3D
	{
		GDCLASS(AncientBuildingCompound, MeshInstance3D)

	public:
		enum EPreset
		{
			/** 抱厦: a hall with a smaller porch hall projecting from its front. */
			PRESET_BAOSHA = 0,
			/** 勾连搭: two gabled halls joined front to back under one valley. */
			PRESET_GOULIANDA = 1,
			/** 十字脊: a square pavilion under two crossing gables. */
			PRESET_SHIZIJI = 2,
			/** 工字殿: front and back halls joined by a 穿堂. */
			PRESET_GONGZI = 3,
			/** 曲尺: an L of two halls. */
			PRESET_QUCHI = 4,
			PRESET_COUNT = 5,
		};

	private:
		TypedArray<AncientBuildingWing> Wings;
		bool bAutoRegenerate = true;

		Ref<StandardMaterial3D> Material;
		BuildingGen::CompoundReport LastReport;
		std::vector<BuildingGen::MeshAccumulator> LastWingMeshes;
		std::vector<BuildingGen::RoofHeightField> LastFields;
		std::vector<BuildingGen::CompoundWing> LastLayout;

		void OnWingChanged();
		void ConnectWings();
		/** Resolves attachments into placed specs, parents first. False (with a warning) on error. */
		bool Layout(std::vector<BuildingGen::CompoundWing>& Out) const;

	protected:
		static void _bind_methods();
		void _validate_property(PropertyInfo& Property) const;

	public:
		void _ready() override;

		void Generate();
		Ref<ArrayMesh> BakeMesh();

		void SetWings(const TypedArray<AncientBuildingWing>& Value);
		TypedArray<AncientBuildingWing> GetWings() const { return Wings; }

		void SetAutoRegenerate(bool bValue) { bAutoRegenerate = bValue; }
		bool ShouldAutoRegenerate() const { return bAutoRegenerate; }

		/** Replaces the wings with one of the EPreset layouts. */
		void ApplyPreset(int32_t Preset);

		/** triangles_in / triangles_out / valley_count / valley_length of the last generate(). */
		Dictionary GetReport() const;
		/** One wing's geometry after the junctions were resolved, from the last generate(). */
		Ref<ArrayMesh> GetWingMesh(int32_t Wing) const;
		/** A wing's roof surface height at a plan point (compound frame); -1e30 where it has none. */
		float SampleWingRoofHeight(int32_t Wing, float X, float Z) const;
		/** Placed wing: offset, turns, width, depth, eave_height, roof_base, module. */
		Dictionary GetWingLayout(int32_t Wing) const;
	};
} // namespace godot
