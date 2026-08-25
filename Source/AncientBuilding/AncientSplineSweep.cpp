#include "AncientBuilding/AncientSplineSweep.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/curve3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>

using namespace godot;

namespace
{
	const float SWEEP_TAU = 6.28318530717958647692f;

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

void AncientSplineSweep::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("generate"), &AncientSplineSweep::Generate);

	ClassDB::bind_method(D_METHOD("set_target_path", "value"), &AncientSplineSweep::SetTargetPath);
	ClassDB::bind_method(D_METHOD("get_target_path"), &AncientSplineSweep::GetTargetPath);
	ADD_PROPERTY(
		PropertyInfo(Variant::NODE_PATH, "target_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path3D"),
		"set_target_path", "get_target_path");

	ADD_GROUP("Profile", "");
	ClassDB::bind_method(D_METHOD("set_contour_preset", "value"), &AncientSplineSweep::SetContourPreset);
	ClassDB::bind_method(D_METHOD("get_contour_preset"), &AncientSplineSweep::GetContourPreset);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "contour_preset", PROPERTY_HINT_ENUM, "Custom,Square,Round,Ridge Tile,Step"),
		"set_contour_preset", "get_contour_preset");

	ClassDB::bind_method(D_METHOD("set_contour", "value"), &AncientSplineSweep::SetContour);
	ClassDB::bind_method(D_METHOD("get_contour"), &AncientSplineSweep::GetContour);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "contour"), "set_contour", "get_contour");

	ClassDB::bind_method(D_METHOD("set_contour_scale", "value"), &AncientSplineSweep::SetContourScale);
	ClassDB::bind_method(D_METHOD("get_contour_scale"), &AncientSplineSweep::GetContourScale);
	ADD_PROPERTY(
		PropertyInfo(Variant::FLOAT, "contour_scale", PROPERTY_HINT_RANGE, "0.01,10,0.001,or_greater"),
		"set_contour_scale", "get_contour_scale");

	ClassDB::bind_method(D_METHOD("set_closed_contour", "value"), &AncientSplineSweep::SetClosedContour);
	ClassDB::bind_method(D_METHOD("is_closed_contour"), &AncientSplineSweep::IsClosedContour);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "closed_contour"), "set_closed_contour", "is_closed_contour");

	ADD_GROUP("Sweep", "");
	ClassDB::bind_method(D_METHOD("set_mode", "value"), &AncientSplineSweep::SetMode);
	ClassDB::bind_method(D_METHOD("get_mode"), &AncientSplineSweep::GetMode);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Miter (paper),Frame (prior art)"),
		"set_mode", "get_mode");

	ClassDB::bind_method(D_METHOD("set_subdivisions_per_segment", "value"), &AncientSplineSweep::SetSubdivisionsPerSegment);
	ClassDB::bind_method(D_METHOD("get_subdivisions_per_segment"), &AncientSplineSweep::GetSubdivisionsPerSegment);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "subdivisions_per_segment", PROPERTY_HINT_RANGE, "0,32,1"),
		"set_subdivisions_per_segment", "get_subdivisions_per_segment");

	ClassDB::bind_method(D_METHOD("set_up_reference", "value"), &AncientSplineSweep::SetUpReference);
	ClassDB::bind_method(D_METHOD("get_up_reference"), &AncientSplineSweep::GetUpReference);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "up_reference"), "set_up_reference", "get_up_reference");

	ClassDB::bind_method(D_METHOD("set_generate_caps", "value"), &AncientSplineSweep::SetGenerateCaps);
	ClassDB::bind_method(D_METHOD("should_generate_caps"), &AncientSplineSweep::ShouldGenerateCaps);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "generate_caps"), "set_generate_caps", "should_generate_caps");

	ADD_GROUP("Displacement", "");
	ClassDB::bind_method(D_METHOD("set_displacement_curve", "value"), &AncientSplineSweep::SetDisplacementCurve);
	ClassDB::bind_method(D_METHOD("get_displacement_curve"), &AncientSplineSweep::GetDisplacementCurve);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "displacement_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"),
		"set_displacement_curve", "get_displacement_curve");

	ClassDB::bind_method(D_METHOD("set_displacement_scale", "value"), &AncientSplineSweep::SetDisplacementScale);
	ClassDB::bind_method(D_METHOD("get_displacement_scale"), &AncientSplineSweep::GetDisplacementScale);
	ADD_PROPERTY(
		PropertyInfo(Variant::FLOAT, "displacement_scale", PROPERTY_HINT_RANGE, "-10,10,0.001"),
		"set_displacement_scale", "get_displacement_scale");

	ClassDB::bind_method(D_METHOD("set_constraint_angle", "value"), &AncientSplineSweep::SetConstraintAngle);
	ClassDB::bind_method(D_METHOD("get_constraint_angle"), &AncientSplineSweep::GetConstraintAngle);
	ADD_PROPERTY(
		PropertyInfo(Variant::FLOAT, "constraint_angle", PROPERTY_HINT_RANGE, "0,180,0.1"),
		"set_constraint_angle", "get_constraint_angle");

	ADD_GROUP("Appearance", "");
	ClassDB::bind_method(D_METHOD("set_base_color", "value"), &AncientSplineSweep::SetBaseColor);
	ClassDB::bind_method(D_METHOD("get_base_color"), &AncientSplineSweep::GetBaseColor);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "base_color"), "set_base_color", "get_base_color");

	ClassDB::bind_method(D_METHOD("set_auto_regenerate", "value"), &AncientSplineSweep::SetAutoRegenerate);
	ClassDB::bind_method(D_METHOD("should_auto_regenerate"), &AncientSplineSweep::ShouldAutoRegenerate);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_regenerate"), "set_auto_regenerate", "should_auto_regenerate");

	ClassDB::bind_method(D_METHOD("get_degenerate_joint_count"), &AncientSplineSweep::GetDegenerateJointCount);
	ClassDB::bind_method(D_METHOD("get_max_miter_stretch"), &AncientSplineSweep::GetMaxMiterStretch);
	ClassDB::bind_method(D_METHOD("get_knot_count"), &AncientSplineSweep::GetKnotCount);

	BIND_ENUM_CONSTANT(CONTOUR_CUSTOM);
	BIND_ENUM_CONSTANT(CONTOUR_SQUARE);
	BIND_ENUM_CONSTANT(CONTOUR_ROUND);
	BIND_ENUM_CONSTANT(CONTOUR_RIDGE_TILE);
	BIND_ENUM_CONSTANT(CONTOUR_STEP);
	BIND_ENUM_CONSTANT(MODE_MITER);
	BIND_ENUM_CONSTANT(MODE_FRAME);
}

void AncientSplineSweep::_ready()
{
	if (get_mesh().is_null())
	{
		Generate();
	}
}

void AncientSplineSweep::EnsureMaterial()
{
	if (SweepMaterial.is_null())
	{
		SweepMaterial.instantiate();
		SweepMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		SweepMaterial->set_roughness(0.85f);
		SweepMaterial->set_metallic(0.0f);
	}
}

Path3D* AncientSplineSweep::ResolvePath() const
{
	if (!TargetPath.is_empty())
	{
		return Object::cast_to<Path3D>(get_node_or_null(TargetPath));
	}

	// Convenience: with no explicit target, sweep the first Path3D child.
	for (int32_t Index = 0; Index < get_child_count(); ++Index)
	{
		Path3D* Candidate = Object::cast_to<Path3D>(get_child(Index));
		if (Candidate != nullptr)
		{
			return Candidate;
		}
	}

	return nullptr;
}

PackedVector2Array AncientSplineSweep::BuildPresetContour() const
{
	PackedVector2Array Result;

	switch (ContourPreset)
	{
		case CONTOUR_SQUARE:
		{
			Result.push_back(Vector2(-0.5f, -0.5f));
			Result.push_back(Vector2(0.5f, -0.5f));
			Result.push_back(Vector2(0.5f, 0.5f));
			Result.push_back(Vector2(-0.5f, 0.5f));
			break;
		}

		case CONTOUR_ROUND:
		{
			const int32_t Sides = 12;
			for (int32_t Index = 0; Index < Sides; ++Index)
			{
				const float Angle = SWEEP_TAU * float(Index) / float(Sides);
				Result.push_back(Vector2(0.5f * std::cos(Angle), 0.5f * std::sin(Angle)));
			}
			break;
		}

		case CONTOUR_RIDGE_TILE:
		{
			// Flat soffit, shoulders, rounded crown — the section of a ridge tile course.
			Result.push_back(Vector2(-0.5f, 0.0f));
			Result.push_back(Vector2(0.5f, 0.0f));
			Result.push_back(Vector2(0.5f, 0.18f));
			Result.push_back(Vector2(0.34f, 0.38f));
			Result.push_back(Vector2(0.0f, 0.5f));
			Result.push_back(Vector2(-0.34f, 0.38f));
			Result.push_back(Vector2(-0.5f, 0.18f));
			break;
		}

		case CONTOUR_STEP:
		{
			// Open profile: a flat tread the direction constraint can lift on its own.
			Result.push_back(Vector2(-0.5f, 0.0f));
			Result.push_back(Vector2(-0.25f, 0.0f));
			Result.push_back(Vector2(0.0f, 0.0f));
			Result.push_back(Vector2(0.25f, 0.0f));
			Result.push_back(Vector2(0.5f, 0.0f));
			break;
		}

		default:
			break;
	}

	return Result;
}

bool AncientSplineSweep::CollectKnots(std::vector<Vector3>& OutKnots) const
{
	OutKnots.clear();

	const Path3D* Path = ResolvePath();
	if (Path == nullptr)
	{
		return false;
	}

	const Ref<Curve3D> Curve = Path->get_curve();
	if (Curve.is_null() || Curve->get_point_count() < 2)
	{
		return false;
	}

	// Knots come from the curve's own points, so a "corner curve" stays a polyline and its
	// corners stay sharp — which is the case the miter exists to handle. Subdividing samples
	// the tessellated curve instead, for the Bezier ridges of section 7.
	const Transform3D ToLocal = get_global_transform().affine_inverse() * Path->get_global_transform();
	const int32_t PointCount = Curve->get_point_count();

	if (SubdivisionsPerSegment <= 0)
	{
		for (int32_t Index = 0; Index < PointCount; ++Index)
		{
			OutKnots.push_back(ToLocal.xform(Curve->get_point_position(Index)));
		}
	}
	else
	{
		const int32_t Steps = SubdivisionsPerSegment + 1;
		for (int32_t Segment = 0; Segment + 1 < PointCount; ++Segment)
		{
			for (int32_t Step = 0; Step < Steps; ++Step)
			{
				const float T = float(Step) / float(Steps);
				OutKnots.push_back(ToLocal.xform(Curve->sample(Segment, T)));
			}
		}
		OutKnots.push_back(ToLocal.xform(Curve->get_point_position(PointCount - 1)));
	}

	// Drop duplicates, which would otherwise produce a zero-length segment and a bad frame.
	std::vector<Vector3> Filtered;
	Filtered.reserve(OutKnots.size());
	for (const Vector3& Knot : OutKnots)
	{
		if (Filtered.empty() || Filtered.back().distance_to(Knot) > 1e-5f)
		{
			Filtered.push_back(Knot);
		}
	}
	OutKnots.swap(Filtered);

	return OutKnots.size() >= 2;
}

void AncientSplineSweep::Generate()
{
	EnsureMaterial();

	std::vector<Vector3> Knots;
	if (!CollectKnots(Knots))
	{
		set_mesh(Ref<ArrayMesh>());
		LastKnotCount = 0;
		LastDegenerateJointCount = 0;
		LastMaxMiterStretch = 1.0f;
		return;
	}

	const PackedVector2Array ActiveContour =
		(ContourPreset == CONTOUR_CUSTOM) ? Contour : BuildPresetContour();

	BuildingGen::SweepSettings Settings;
	Settings.Contour.reserve(size_t(ActiveContour.size()));
	for (int64_t Index = 0; Index < ActiveContour.size(); ++Index)
	{
		Settings.Contour.push_back(ActiveContour[Index] * ContourScale);
	}
	Settings.bClosedContour = bClosedContour;
	Settings.Mode = (Mode == MODE_FRAME) ? BuildingGen::ESweepMode::Frame : BuildingGen::ESweepMode::Miter;
	Settings.DisplacementScale = DisplacementScale;
	Settings.ConstraintAngleDegrees = ConstraintAngle;
	Settings.UpReference = UpReference;
	Settings.bGenerateCaps = bGenerateCaps;

	if (DisplacementCurve.is_valid() && DisplacementScale != 0.0f)
	{
		const int32_t SampleCount = std::max(2, DisplacementSampleCount);
		Settings.DisplacementSamples.reserve(size_t(SampleCount));
		for (int32_t Index = 0; Index < SampleCount; ++Index)
		{
			Settings.DisplacementSamples.push_back(
				DisplacementCurve->sample(float(Index) / float(SampleCount - 1)));
		}
	}

	BuildingGen::SweepResult Sweep;
	if (!BuildingGen::BuildSweep(Knots, Settings, Sweep) || Sweep.Indices.empty())
	{
		set_mesh(Ref<ArrayMesh>());
		LastKnotCount = int32_t(Knots.size());
		return;
	}

	std::vector<Color> Colors(Sweep.Vertices.size(), BaseColor);

	Array Arrays;
	Arrays.resize(Mesh::ARRAY_MAX);
	Arrays[Mesh::ARRAY_VERTEX] = ToPacked<PackedVector3Array>(Sweep.Vertices);
	Arrays[Mesh::ARRAY_NORMAL] = ToPacked<PackedVector3Array>(Sweep.Normals);
	Arrays[Mesh::ARRAY_TEX_UV] = ToPacked<PackedVector2Array>(Sweep.UVs);
	Arrays[Mesh::ARRAY_COLOR] = ToPacked<PackedColorArray>(Colors);
	Arrays[Mesh::ARRAY_INDEX] = ToPacked<PackedInt32Array>(Sweep.Indices);

	Ref<ArrayMesh> Result(memnew(ArrayMesh));
	Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays);
	Result->surface_set_material(0, SweepMaterial);
	set_mesh(Result);

	LastKnotCount = int32_t(Knots.size());
	LastDegenerateJointCount = Sweep.DegenerateJointCount;
	LastMaxMiterStretch = Sweep.MaxMiterStretch;
}

void AncientSplineSweep::RequestRegenerate()
{
	if (!bAutoRegenerate || !is_inside_tree())
	{
		return;
	}

	Generate();
}

void AncientSplineSweep::OnCurveChanged()
{
	RequestRegenerate();
}

void AncientSplineSweep::SetTargetPath(const NodePath& Value)
{
	TargetPath = Value;
	RequestRegenerate();
}

void AncientSplineSweep::SetDisplacementCurve(const Ref<Curve>& Value)
{
	const Callable OnChanged = callable_mp(this, &AncientSplineSweep::OnCurveChanged);

	if (DisplacementCurve.is_valid() && DisplacementCurve->is_connected("changed", OnChanged))
	{
		DisplacementCurve->disconnect("changed", OnChanged);
	}

	DisplacementCurve = Value;

	if (DisplacementCurve.is_valid() && !DisplacementCurve->is_connected("changed", OnChanged))
	{
		DisplacementCurve->connect("changed", OnChanged);
	}

	RequestRegenerate();
}

void AncientSplineSweep::SetContour(const PackedVector2Array& Value)
{
	Contour = Value;
	RequestRegenerate();
}

void AncientSplineSweep::SetUpReference(const Vector3& Value)
{
	UpReference = (Value.length_squared() > 1e-8f) ? Value.normalized() : Vector3(0, 1, 0);
	RequestRegenerate();
}

void AncientSplineSweep::SetBaseColor(const Color& Value)
{
	BaseColor = Value;
	RequestRegenerate();
}

#define SWEEP_DEFINE_SETTER(Type, Name, Member, Transform) \
	void AncientSplineSweep::Set##Name(Type Value)         \
	{                                                      \
		Member = Transform;                                \
		RequestRegenerate();                               \
	}

SWEEP_DEFINE_SETTER(int32_t, ContourPreset, ContourPreset, std::max(0, Value))
SWEEP_DEFINE_SETTER(float, ContourScale, ContourScale, std::fmax(0.001f, Value))
SWEEP_DEFINE_SETTER(bool, ClosedContour, bClosedContour, Value)
SWEEP_DEFINE_SETTER(int32_t, Mode, Mode, std::max(0, Value))
SWEEP_DEFINE_SETTER(float, DisplacementScale, DisplacementScale, Value)
SWEEP_DEFINE_SETTER(float, ConstraintAngle, ConstraintAngle, std::fmin(std::fmax(Value, 0.0f), 180.0f))
SWEEP_DEFINE_SETTER(bool, GenerateCaps, bGenerateCaps, Value)
SWEEP_DEFINE_SETTER(int32_t, SubdivisionsPerSegment, SubdivisionsPerSegment, std::max(0, Value))

#undef SWEEP_DEFINE_SETTER

void AncientSplineSweep::SetAutoRegenerate(bool bValue)
{
	bAutoRegenerate = bValue;
}
