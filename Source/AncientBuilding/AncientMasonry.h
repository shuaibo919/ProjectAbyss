#pragma once

// Standalone arched masonry — city wall runs, 水门, 墩台, arch bridges — built from the same
// primitive as AncientBuilding's 城台 (Masonry.h), so a town's walls and gates share one
// construction with the buildings standing on them. Asset-free: vertex colours only.

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/color.hpp>

namespace godot
{
	class AncientMasonry : public MeshInstance3D
	{
		GDCLASS(AncientMasonry, MeshInstance3D)

	private:
		/** Along the local X axis; the passages run through the thickness (local Z). */
		float Length = 12.0f;
		float Thickness = 6.0f;
		float Height = 7.0f;
		float BatterEnds = 0.0f;
		float BatterFaces = 0.08f;
		int32_t ArchCount = 1;
		/** 0 derives the width from the length. */
		float ArchWidth = 0.0f;
		/** Top of the arch as a fraction of the height. */
		float ArchHeightRatio = 0.62f;
		/** 0 半圆券, 1 双心券, 2 平券. */
		int32_t ArchProfile = 0;
		/** Bottom of the openings (0 = passages; above 0 = windows or a bridge's arches over water). */
		float ArchSill = 0.0f;
		/** Centre-to-centre spacing. 0 sets the openings out evenly. */
		float ArchPitch = 0.0f;
		/** 0 none, 1 垛口, 2 宇墙. */
		int32_t Parapet = 0;
		float RingThickness = 0.4f;
		float RingProjection = 0.08f;
		float PlinthHeight = 0.8f;
		bool bTopCap = true;
		Color BrickColor = Color(0.55f, 0.53f, 0.50f, 1.0f);
		Color StoneColor = Color(0.60f, 0.58f, 0.54f, 1.0f);

		Ref<StandardMaterial3D> Material;
		int32_t LastTriangleCount = 0;

		void Regenerate();

	protected:
		static void _bind_methods();
		void _validate_property(PropertyInfo& Property) const;

	public:
		void _ready() override;

		void Generate();
		Ref<ArrayMesh> BakeMesh();
		int32_t GetTriangleCount() const { return LastTriangleCount; }

#define ANCIENT_MASONRY_ACCESSORS(Type, Member) \
		void Set##Member(Type Value) { Member = Value; Regenerate(); } \
		Type Get##Member() const { return Member; }

		ANCIENT_MASONRY_ACCESSORS(float, Length)
		ANCIENT_MASONRY_ACCESSORS(float, Thickness)
		ANCIENT_MASONRY_ACCESSORS(float, Height)
		ANCIENT_MASONRY_ACCESSORS(float, BatterEnds)
		ANCIENT_MASONRY_ACCESSORS(float, BatterFaces)
		ANCIENT_MASONRY_ACCESSORS(int32_t, ArchCount)
		ANCIENT_MASONRY_ACCESSORS(float, ArchWidth)
		ANCIENT_MASONRY_ACCESSORS(float, ArchHeightRatio)
		ANCIENT_MASONRY_ACCESSORS(int32_t, ArchProfile)
		ANCIENT_MASONRY_ACCESSORS(float, ArchSill)
		ANCIENT_MASONRY_ACCESSORS(float, ArchPitch)
		ANCIENT_MASONRY_ACCESSORS(int32_t, Parapet)
		ANCIENT_MASONRY_ACCESSORS(float, RingThickness)
		ANCIENT_MASONRY_ACCESSORS(float, RingProjection)
		ANCIENT_MASONRY_ACCESSORS(float, PlinthHeight)
		ANCIENT_MASONRY_ACCESSORS(Color, BrickColor)
		ANCIENT_MASONRY_ACCESSORS(Color, StoneColor)

#undef ANCIENT_MASONRY_ACCESSORS

		void SetTopCap(bool bValue) { bTopCap = bValue; Regenerate(); }
		bool HasTopCap() const { return bTopCap; }
	};
} // namespace godot
