#pragma once

// Scene-facing wrapper around the Hu & Qin 2020 section 3.1 spline mesh generation.
//
// Reads a Path3D's curve, sweeps a section profile along it and writes the result into
// itself. Asset-free like the rest of this project: the profile is either a built-in preset
// or a hand-authored PackedVector2Array, and colour rides on vertex colours.

#include "AncientBuilding/SplineSweep.h"

#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/path3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

namespace godot
{
	class AncientSplineSweep : public MeshInstance3D
	{
		GDCLASS(AncientSplineSweep, MeshInstance3D)

	public:
		enum EContourPreset
		{
			/** Whatever is in the `contour` property. */
			CONTOUR_CUSTOM = 0,
			CONTOUR_SQUARE = 1,
			CONTOUR_ROUND = 2,
			/** Ridge tile: a flat soffit under a rounded crown. */
			CONTOUR_RIDGE_TILE = 3,
			/** Open profile, for use with a delta at or below 90 degrees. */
			CONTOUR_STEP = 4,
		};

		enum EMode
		{
			/** Paper equation 1. */
			MODE_MITER = 0,
			/** The prior art, kept for comparison. */
			MODE_FRAME = 1,
		};

	private:
		NodePath TargetPath;

		int32_t ContourPreset = CONTOUR_RIDGE_TILE;
		PackedVector2Array Contour;
		float ContourScale = 1.0f;
		bool bClosedContour = true;

		int32_t Mode = MODE_MITER;

		Ref<Curve> DisplacementCurve;
		float DisplacementScale = 0.0f;
		float ConstraintAngle = 180.0f;
		int32_t DisplacementSampleCount = 32;

		Vector3 UpReference = Vector3(0, 1, 0);
		bool bGenerateCaps = true;
		Color BaseColor = Color(0.30f, 0.31f, 0.33f, 1.0f);

		/** Extra knots inserted between the curve's own points. 0 uses the points as-is. */
		int32_t SubdivisionsPerSegment = 0;

		bool bAutoRegenerate = true;

		Ref<StandardMaterial3D> SweepMaterial;

		int32_t LastDegenerateJointCount = 0;
		float LastMaxMiterStretch = 1.0f;
		int32_t LastKnotCount = 0;

		void EnsureMaterial();
		void RequestRegenerate();
		void OnCurveChanged();

		/** Resolves TargetPath, falling back to the first Path3D child. */
		Path3D* ResolvePath() const;

		/** Preset profiles, in profile-local units before ContourScale. */
		PackedVector2Array BuildPresetContour() const;

		/** Samples the target curve into the knot list the kernel consumes. */
		bool CollectKnots(std::vector<Vector3>& OutKnots) const;

	protected:
		static void _bind_methods();

	public:
		void _ready() override;

		/** Rebuilds the swept mesh from the target Path3D. */
		void Generate();

		void SetTargetPath(const NodePath& Value);
		NodePath GetTargetPath() const { return TargetPath; }

		void SetContourPreset(int32_t Value);
		int32_t GetContourPreset() const { return ContourPreset; }

		void SetContour(const PackedVector2Array& Value);
		PackedVector2Array GetContour() const { return Contour; }

		void SetContourScale(float Value);
		float GetContourScale() const { return ContourScale; }

		void SetClosedContour(bool bValue);
		bool IsClosedContour() const { return bClosedContour; }

		void SetMode(int32_t Value);
		int32_t GetMode() const { return Mode; }

		void SetDisplacementCurve(const Ref<Curve>& Value);
		Ref<Curve> GetDisplacementCurve() const { return DisplacementCurve; }

		void SetDisplacementScale(float Value);
		float GetDisplacementScale() const { return DisplacementScale; }

		void SetConstraintAngle(float Value);
		float GetConstraintAngle() const { return ConstraintAngle; }

		void SetUpReference(const Vector3& Value);
		Vector3 GetUpReference() const { return UpReference; }

		void SetGenerateCaps(bool bValue);
		bool ShouldGenerateCaps() const { return bGenerateCaps; }

		void SetBaseColor(const Color& Value);
		Color GetBaseColor() const { return BaseColor; }

		void SetSubdivisionsPerSegment(int32_t Value);
		int32_t GetSubdivisionsPerSegment() const { return SubdivisionsPerSegment; }

		void SetAutoRegenerate(bool bValue);
		bool ShouldAutoRegenerate() const { return bAutoRegenerate; }

		/** Joints where the miter plane had to be clamped; non-zero means subdivide the curve. */
		int32_t GetDegenerateJointCount() const { return LastDegenerateJointCount; }
		/** Worst corner widening. 1 is a straight run. */
		float GetMaxMiterStretch() const { return LastMaxMiterStretch; }
		int32_t GetKnotCount() const { return LastKnotCount; }
	};
} // namespace godot

VARIANT_ENUM_CAST(AncientSplineSweep::EContourPreset);
VARIANT_ENUM_CAST(AncientSplineSweep::EMode);
