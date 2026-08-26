#include "TreeGen/ProceduralTree.h"

#include "TreeGen/TreeSkeleton.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

using namespace godot;

namespace
{
	/** Generates the setter/getter binding for one scalar property of ProceduralTree. */
#define TREE_BIND(VariantType, PropName, Member)                                             \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &ProceduralTree::Set##Member);   \
	ClassDB::bind_method(D_METHOD("get_" PropName), &ProceduralTree::Get##Member);            \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName), "set_" PropName, "get_" PropName);

#define TREE_BIND_RANGE(VariantType, PropName, Member, Hint)                                 \
	ClassDB::bind_method(D_METHOD("set_" PropName, "value"), &ProceduralTree::Set##Member);   \
	ClassDB::bind_method(D_METHOD("get_" PropName), &ProceduralTree::Get##Member);            \
	ADD_PROPERTY(PropertyInfo(VariantType, PropName, PROPERTY_HINT_RANGE, Hint), "set_" PropName, "get_" PropName);
} // namespace

ProceduralTree::ProceduralTree()
{
}

void ProceduralTree::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("generate"), &ProceduralTree::Generate);
	ClassDB::bind_method(D_METHOD("bake_mesh"), &ProceduralTree::BakeMesh);
	ClassDB::bind_method(D_METHOD("apply_preset", "preset"), &ProceduralTree::ApplyPreset);

	ClassDB::bind_method(D_METHOD("set_parameters", "value"), &ProceduralTree::SetParameters);
	ClassDB::bind_method(D_METHOD("get_parameters"), &ProceduralTree::GetParameters);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "parameters", PROPERTY_HINT_RESOURCE_TYPE, "ProceduralTreeParameters"),
		"set_parameters", "get_parameters");

	// 双后端选择 + SlowTree 预设 id。slowtree_preset 的枚举提示由预设表动态拼接。
	// (嵌套枚举无法走 BIND_ENUM_CONSTANT 的 GetTypeInfo 路径, 用显式常量绑定。)
	ClassDB::bind_integer_constant(
		get_class_static(), StringName("ProceduralTreeBackend"), StringName("BACKEND_WEBER_PENN"),
		int64_t(BACKEND_WEBER_PENN));
	ClassDB::bind_integer_constant(
		get_class_static(), StringName("ProceduralTreeBackend"), StringName("BACKEND_SLOWTREE"),
		int64_t(BACKEND_SLOWTREE));

	ClassDB::bind_method(D_METHOD("set_backend", "value"), &ProceduralTree::SetBackend);
	ClassDB::bind_method(D_METHOD("get_backend"), &ProceduralTree::GetBackend);
	ADD_PROPERTY(
		PropertyInfo(Variant::INT, "backend", PROPERTY_HINT_ENUM, "Weber-Penn (HPG 2025),SlowTree"),
		"set_backend", "get_backend");

	ClassDB::bind_method(D_METHOD("set_slowtree_preset", "value"), &ProceduralTree::SetSlowTreePreset);
	ClassDB::bind_method(D_METHOD("get_slowtree_preset"), &ProceduralTree::GetSlowTreePreset);
	{
		String PresetHint;
		for (int32_t i = 0; i < SlowTreeGenerator::GetPresetCount(); ++i)
		{
			if (i > 0)
			{
				PresetHint += ",";
			}
			PresetHint += SlowTreeGenerator::GetPresetName(i);
		}
		ADD_PROPERTY(
			PropertyInfo(Variant::INT, "slowtree_preset", PROPERTY_HINT_ENUM, PresetHint),
			"set_slowtree_preset", "get_slowtree_preset");
	}

	// Stage 2: SlowTree 细分走 GPU compute 管线(默认关; 仅 SlowTree 后端生效)。
	ClassDB::bind_method(D_METHOD("set_use_gpu_tessellation", "value"), &ProceduralTree::SetUseGpuTessellation);
	ClassDB::bind_method(D_METHOD("should_use_gpu_tessellation"), &ProceduralTree::ShouldUseGpuTessellation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_gpu_tessellation"),
		"set_use_gpu_tessellation", "should_use_gpu_tessellation");

	TREE_BIND_RANGE(Variant::INT, "seed", Seed, "0,65535,1,or_greater")
	TREE_BIND_RANGE(Variant::FLOAT, "season", Season, "0,4,0.01")
	TREE_BIND_RANGE(Variant::FLOAT, "wind_strength", WindStrength, "0,20,0.01")
	TREE_BIND_RANGE(Variant::FLOAT, "wind_time", WindTime, "0,120,0.01")
	TREE_BIND_RANGE(Variant::FLOAT, "leaf_density", LeafDensity, "0.001,1,0.001")

	ADD_GROUP("Tessellation", "");
	TREE_BIND_RANGE(Variant::INT, "radial_segments", RadialSegments, "3,32,1")
	TREE_BIND_RANGE(Variant::INT, "rings_per_segment", RingsPerSegment, "1,8,1")
	TREE_BIND_RANGE(Variant::INT, "leaf_arc_segments", LeafArcSegments, "1,8,1")
	TREE_BIND_RANGE(Variant::INT, "fruit_longitudes", FruitLongitudes, "4,32,1")
	TREE_BIND_RANGE(Variant::INT, "fruit_bands", FruitBands, "3,24,1")

	ClassDB::bind_method(D_METHOD("set_bark_detail", "value"), &ProceduralTree::SetBarkDetail);
	ClassDB::bind_method(D_METHOD("has_bark_detail"), &ProceduralTree::HasBarkDetail);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bark_detail"), "set_bark_detail", "has_bark_detail");

	ClassDB::bind_method(D_METHOD("set_generate_leaves", "value"), &ProceduralTree::SetGenerateLeaves);
	ClassDB::bind_method(D_METHOD("should_generate_leaves"), &ProceduralTree::ShouldGenerateLeaves);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "generate_leaves"), "set_generate_leaves", "should_generate_leaves");

	ClassDB::bind_method(D_METHOD("set_generate_fruit", "value"), &ProceduralTree::SetGenerateFruit);
	ClassDB::bind_method(D_METHOD("should_generate_fruit"), &ProceduralTree::ShouldGenerateFruit);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "generate_fruit"), "set_generate_fruit", "should_generate_fruit");

	ADD_GROUP("Limits", "");
	TREE_BIND_RANGE(Variant::INT, "max_segments", MaxSegments, "100,200000,1")
	TREE_BIND_RANGE(Variant::INT, "max_leaves", MaxLeaves, "100,400000,1")

	ClassDB::bind_method(D_METHOD("set_auto_regenerate", "value"), &ProceduralTree::SetAutoRegenerate);
	ClassDB::bind_method(D_METHOD("should_auto_regenerate"), &ProceduralTree::ShouldAutoRegenerate);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_regenerate"), "set_auto_regenerate", "should_auto_regenerate");

	ClassDB::bind_method(D_METHOD("get_vertex_count"), &ProceduralTree::GetVertexCount);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &ProceduralTree::GetTriangleCount);
	ClassDB::bind_method(D_METHOD("get_segment_count"), &ProceduralTree::GetSegmentCount);
	ClassDB::bind_method(D_METHOD("get_leaf_count"), &ProceduralTree::GetLeafCount);
	ClassDB::bind_method(D_METHOD("was_truncated"), &ProceduralTree::WasTruncated);
}

void ProceduralTree::_validate_property(PropertyInfo& Property) const
{
	if (Property.name == StringName("mesh"))
	{
		Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
	}
}

void ProceduralTree::_ready()
{
	EnsureParameters();

	// A saved scene stores only the parameters, not the mesh, so rebuild on load.
	if (get_mesh().is_null())
	{
		Generate();
	}
}

void ProceduralTree::EnsureParameters()
{
	if (Parameters.is_valid())
	{
		// A resource loaded from disk may predate a sub-resource being added.
		Parameters->EnsureSubResources();
		return;
	}

	Ref<ProceduralTreeParameters> Defaults(memnew(ProceduralTreeParameters));
	Defaults->ApplyPreset(ProceduralTreeParameters::PRESET_APPLE);
	SetParameters(Defaults);
}

void ProceduralTree::EnsureMaterials()
{
	if (BarkMaterial.is_null())
	{
		BarkMaterial.instantiate();
		BarkMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		BarkMaterial->set_roughness(0.9f);
		BarkMaterial->set_metallic(0.0f);
	}

	if (FoliageMaterial.is_null())
	{
		FoliageMaterial.instantiate();
		FoliageMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		// Leaves are single-sided geometry, so both faces must render and light correctly.
		FoliageMaterial->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		FoliageMaterial->set_roughness(0.8f);
		FoliageMaterial->set_metallic(0.0f);
	}

	// Translucency lives on the leaf parameters, so refresh it every generation.
	if (Parameters.is_valid() && Parameters->GetLeaf().is_valid())
	{
		const float Translucency = Parameters->GetLeaf()->GetTranslucency();
		FoliageMaterial->set_feature(BaseMaterial3D::FEATURE_BACKLIGHT, Translucency > 0.0f);
		FoliageMaterial->set_backlight(Color(Translucency, Translucency, Translucency, 1.0f) * 0.5f);
	}

	if (FruitMaterial.is_null())
	{
		FruitMaterial.instantiate();
		FruitMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		FruitMaterial->set_roughness(0.6f);
		FruitMaterial->set_metallic(0.0f);
	}
}

void ProceduralTree::CollectTreeParams(TreeGen::TreeParams& OutParams) const
{
	// Vector4 components map onto per-level array slots 0..3.
#define TREE_COPY_VECTOR4(Target, Source)          \
	{                                              \
		const Vector4 Value = Source;              \
		Target[0] = float(Value.x);                \
		Target[1] = float(Value.y);                \
		Target[2] = float(Value.z);                \
		Target[3] = float(Value.w);                \
	}

#define TREE_COPY_VECTOR4I(Target, Source)         \
	{                                              \
		const Vector4i Value = Source;             \
		Target[0] = int32_t(Value.x);              \
		Target[1] = int32_t(Value.y);              \
		Target[2] = int32_t(Value.z);              \
		Target[3] = int32_t(Value.w);              \
	}

	OutParams.Levels = Parameters->GetLevels();
	OutParams.AttractionUp = Parameters->GetAttractionUp();
	OutParams.Flare = Parameters->GetFlare();
	OutParams.Lobes = Parameters->GetLobes();
	OutParams.LobeDepth = Parameters->GetLobeDepth();
	OutParams.Scale = Parameters->GetScale();
	OutParams.ScaleV = Parameters->GetScaleV();
	OutParams.Ratio = Parameters->GetRatio();
	OutParams.RatioPower = Parameters->GetRatioPower();

	TREE_COPY_VECTOR4(OutParams.BaseSize, Parameters->GetBaseSize())
	TREE_COPY_VECTOR4(OutParams.SegSplits, Parameters->GetSegSplits())
	TREE_COPY_VECTOR4(OutParams.SegSplitBaseOffset, Parameters->GetSegSplitBaseOffset())
	TREE_COPY_VECTOR4(OutParams.SplitAngle, Parameters->GetSplitAngle())
	TREE_COPY_VECTOR4(OutParams.SplitAngleV, Parameters->GetSplitAngleV())
	TREE_COPY_VECTOR4(OutParams.Length, Parameters->GetLength())
	TREE_COPY_VECTOR4(OutParams.LengthV, Parameters->GetLengthV())
	TREE_COPY_VECTOR4(OutParams.Curve, Parameters->GetCurve())
	TREE_COPY_VECTOR4(OutParams.CurveV, Parameters->GetCurveV())
	TREE_COPY_VECTOR4(OutParams.CurveBack, Parameters->GetCurveBack())
	TREE_COPY_VECTOR4(OutParams.Rotate, Parameters->GetRotate())
	TREE_COPY_VECTOR4(OutParams.RotateV, Parameters->GetRotateV())
	TREE_COPY_VECTOR4(OutParams.DownAngle, Parameters->GetDownAngle())
	TREE_COPY_VECTOR4(OutParams.DownAngleV, Parameters->GetDownAngleV())
	TREE_COPY_VECTOR4(OutParams.Taper, Parameters->GetTaper())

	TREE_COPY_VECTOR4I(OutParams.Shape, Parameters->GetShape())
	TREE_COPY_VECTOR4I(OutParams.BaseSplits, Parameters->GetBaseSplits())
	TREE_COPY_VECTOR4I(OutParams.Branches, Parameters->GetBranches())
	TREE_COPY_VECTOR4I(OutParams.CurveRes, Parameters->GetCurveRes())

	OutParams.bStemBirchTexture = Parameters->HasStemBirchTexture();
	OutParams.StemSmallColor = Parameters->GetStemSmallColor();
	OutParams.StemBigColor = Parameters->GetStemBigColor();
	OutParams.StemBumpStrength = Parameters->GetStemBumpStrength();
	OutParams.StemBumpGapSize = Parameters->GetStemBumpGapSize();
	OutParams.StemBumpVoronoiWeight = Parameters->GetStemBumpVoronoiWeight();
	OutParams.StemLichenFrequency = Parameters->GetStemLichenFrequency();
	OutParams.StemLichenSize = Parameters->GetStemLichenSize();

	const auto CopyLeaf = [](const Ref<ProceduralTreeLeafParameters>& Source, TreeGen::LeafParams& Target)
	{
		if (Source.is_null())
		{
			Target.Count = 0;
			return;
		}

		Target.Count = Source->GetCount();
		Target.ScaleShape = Source->GetScaleShape();
		Target.Scale = Source->GetScale();
		Target.ScaleX = Source->GetScaleX();
		Target.StemLen = Source->GetStemLen();
		Target.BotAngle = Source->GetBotAngle();
		Target.MidAngle = Source->GetMidAngle();
		Target.TopAngle = Source->GetTopAngle();
		Target.SideOffset = Source->GetSideOffset();
		Target.Lobes = Source->GetLobes();
		Target.LobeAngle = Source->GetLobeAngle();
		Target.LobeFalloff = Source->GetLobeFalloff();
		Target.LeafColor = Source->GetLeafColor();
		Target.Translucency = Source->GetTranslucency();
		Target.SeasonOffset = Source->GetSeasonOffset();
		Target.Curl = Source->GetCurl();
		Target.ColorJitter = Source->GetColorJitter();
		Target.ScaleJitter = Source->GetScaleJitter();
		Target.NeedleBlades = Source->GetNeedleBlades();
		Target.bTopConvex = Source->IsTopConvex();
		Target.bIsNeedle = Source->IsNeedle();
		Target.bEvergreen = Source->IsEvergreen();
	};

	CopyLeaf(Parameters->GetLeaf(), OutParams.Leaf);
	CopyLeaf(Parameters->GetBlossom(), OutParams.Blossom);

	const Ref<ProceduralTreeFruitParameters> FruitParams = Parameters->GetFruit();
	if (FruitParams.is_valid())
	{
		OutParams.Fruit.Chance = FruitParams->GetChance();
		OutParams.Fruit.DownForce = FruitParams->GetDownForce();
		OutParams.Fruit.Size = FruitParams->GetSize();
		TREE_COPY_VECTOR4(OutParams.Fruit.Shape, FruitParams->GetShape())
		OutParams.Fruit.FruitColor = FruitParams->GetFruitColor();
	}
	else
	{
		OutParams.Fruit.Chance = 0.0f;
	}

#undef TREE_COPY_VECTOR4
#undef TREE_COPY_VECTOR4I
}

void ProceduralTree::Generate()
{
	if (Backend == BACKEND_SLOWTREE)
	{
		GenerateSlowTree();
		return;
	}

	EnsureParameters();
	EnsureMaterials();

	TreeGen::TreeParams TreeParams;
	CollectTreeParams(TreeParams);

	TreeGen::GenerationContext Context;
	Context.Season = Season;
	Context.WindStrength = WindStrength;
	Context.WindTime = WindTime;
	Context.LeafDensity = LeafDensity;
	Context.MaxSegments = MaxSegments;

	TreeGen::TreeSkeleton Skeleton;
	Skeleton.Generate(TreeParams, Context, uint32_t(Seed));

	// A species like Sassafras wants six figures of leaves. Cutting off at the budget would
	// strip the crown and leave the lower branches fully leafed, so rerun with the density
	// scaled instead and let the model's own thinning spread the loss evenly.
	if (Skeleton.GetFoliageCount() > MaxLeaves)
	{
		Context.LeafDensity = LeafDensity * (float(MaxLeaves) / float(Skeleton.GetFoliageCount()));
		Skeleton.Generate(TreeParams, Context, uint32_t(Seed));
	}

	TreeGen::MeshQuality Quality;
	Quality.RadialSegments = RadialSegments;
	Quality.RingsPerSegment = RingsPerSegment;
	Quality.LeafArcSegments = LeafArcSegments;
	Quality.FruitLongitudes = FruitLongitudes;
	Quality.FruitBands = FruitBands;
	Quality.bBarkDetail = bBarkDetail;
	Quality.bGenerateLeaves = bGenerateLeaves;
	Quality.bGenerateFruit = bGenerateFruit;

	TreeGen::TreeMeshBuilder Builder;
	Builder.Build(Skeleton, TreeParams, Context, Quality);

	const Ref<ArrayMesh> Mesh = Builder.CreateMesh();

	const std::vector<TreeGen::TreeMeshBuilder::ESurfaceKind>& Kinds = Builder.GetSurfaceKinds();
	for (size_t SurfaceIndex = 0; SurfaceIndex < Kinds.size(); ++SurfaceIndex)
	{
		switch (Kinds[SurfaceIndex])
		{
			case TreeGen::TreeMeshBuilder::SURFACE_BARK:
				Mesh->surface_set_material(int32_t(SurfaceIndex), BarkMaterial);
				break;
			case TreeGen::TreeMeshBuilder::SURFACE_FOLIAGE:
				Mesh->surface_set_material(int32_t(SurfaceIndex), FoliageMaterial);
				break;
			case TreeGen::TreeMeshBuilder::SURFACE_FRUIT:
				Mesh->surface_set_material(int32_t(SurfaceIndex), FruitMaterial);
				break;
		}
	}

	set_mesh(Mesh);

	LastVertexCount = Builder.GetVertexCount();
	LastTriangleCount = Builder.GetTriangleCount();
	LastSegmentCount = int32_t(Skeleton.GetSegments().size());
	LastLeafCount = int32_t(Skeleton.GetLeaves().size() + Skeleton.GetFruits().size());
	bLastResultTruncated = Skeleton.WereSegmentsTruncated() || Skeleton.WasFoliageTruncated();

	if (Skeleton.WereSegmentsTruncated())
	{
		UtilityFunctions::push_warning(
			"ProceduralTree '", get_name(), "' hit the segment cap at ", LastSegmentCount,
			" segments, so part of the crown is missing. Reduce the branch counts or levels, "
			"or raise max_segments.");
	}
	if (Skeleton.WasFoliageTruncated())
	{
		UtilityFunctions::push_warning(
			"ProceduralTree '", get_name(), "' produced more than ", Skeleton.GetFoliageCount(),
			" leaves and hit the internal backstop, so the crown is uneven. Reduce leaf counts "
			"or levels.");
	}
}

void ProceduralTree::GenerateSlowTree()
{
	const int32_t PresetCount = SlowTreeGenerator::GetPresetCount();
	const int32_t Preset = std::clamp(SlowTreePreset, 0, std::max(0, PresetCount - 1));

	const Dictionary Result = SlowTreeGenerator::Generate(Preset, int64_t(Seed), bUseGpuTessellation);
	const String Error = Result["error"];
	if (!Error.is_empty())
	{
		UtilityFunctions::push_warning(
			"ProceduralTree '", get_name(), "' SlowTree generation failed: ", Error);
		return;
	}

	const Ref<ArrayMesh> Mesh = Result["mesh"];
	set_mesh(Mesh);

	// 统计映射: SlowTree 没有段/叶独立计数, surface 数最有意义; 叶数为 0(Stage 2/3 可补)。
	LastVertexCount = int32_t(Result["vertex_count"]);
	LastTriangleCount = int32_t(Result["triangle_count"]);
	LastSegmentCount = int32_t(Result["surface_count"]);
	LastLeafCount = 0;
	bLastResultTruncated = bool(Result["truncated"]);

	if (bLastResultTruncated)
	{
		UtilityFunctions::push_warning(
			"ProceduralTree '", get_name(), "' SlowTree generation hit the vertex budget "
			"and was truncated. Reduce the preset's leaf/spine counts.");
	}
}

Ref<ArrayMesh> ProceduralTree::BakeMesh()
{
	Generate();

	return get_mesh();
}

void ProceduralTree::RequestRegenerate()
{
	// In-editor edits regenerate immediately; at runtime the caller decides when to pay for it.
	if (!bAutoRegenerate)
	{
		return;
	}
	if (!is_inside_tree())
	{
		return;
	}

	Generate();
}

void ProceduralTree::OnParametersChanged()
{
	RequestRegenerate();
}

void ProceduralTree::SetParameters(const Ref<ProceduralTreeParameters>& Value)
{
	const Callable OnChanged = callable_mp(this, &ProceduralTree::OnParametersChanged);

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

void ProceduralTree::ApplyPreset(int32_t Preset)
{
	// SlowTree 后端下 apply_preset 落在 SlowTree 预设 id 上(Weber 参数面板此时无效)。
	if (Backend == BACKEND_SLOWTREE)
	{
		SetSlowTreePreset(Preset);
		return;
	}

	EnsureParameters();
	// ApplyPreset emits `changed`, which regenerates through OnParametersChanged().
	Parameters->ApplyPreset(Preset);
}

#define TREE_DEFINE_SETTER(Type, Name, Member, Transform) \
	void ProceduralTree::Set##Name(Type Value)            \
	{                                                     \
		Member = Transform;                               \
		RequestRegenerate();                              \
	}

TREE_DEFINE_SETTER(int32_t, Seed, Seed, Value)
TREE_DEFINE_SETTER(int32_t, Backend, Backend, TreeGen::ClampInt(Value, 0, 1))
TREE_DEFINE_SETTER(int32_t, SlowTreePreset, SlowTreePreset, TreeGen::ClampInt(Value, 0, std::max(0, SlowTreeGenerator::GetPresetCount() - 1)))
TREE_DEFINE_SETTER(bool, UseGpuTessellation, bUseGpuTessellation, Value)
TREE_DEFINE_SETTER(float, Season, Season, TreeGen::Clamp(Value, 0.0f, 4.0f))
TREE_DEFINE_SETTER(float, WindStrength, WindStrength, std::fmax(0.0f, Value))
TREE_DEFINE_SETTER(float, WindTime, WindTime, Value)
TREE_DEFINE_SETTER(float, LeafDensity, LeafDensity, TreeGen::Clamp(Value, 0.001f, 1.0f))
TREE_DEFINE_SETTER(int32_t, RadialSegments, RadialSegments, TreeGen::ClampInt(Value, 3, 64))
TREE_DEFINE_SETTER(int32_t, RingsPerSegment, RingsPerSegment, TreeGen::ClampInt(Value, 1, 16))
TREE_DEFINE_SETTER(int32_t, LeafArcSegments, LeafArcSegments, TreeGen::ClampInt(Value, 1, 8))
TREE_DEFINE_SETTER(int32_t, FruitLongitudes, FruitLongitudes, TreeGen::ClampInt(Value, 4, 32))
TREE_DEFINE_SETTER(int32_t, FruitBands, FruitBands, TreeGen::ClampInt(Value, 3, 24))
TREE_DEFINE_SETTER(bool, BarkDetail, bBarkDetail, Value)
TREE_DEFINE_SETTER(bool, GenerateLeaves, bGenerateLeaves, Value)
TREE_DEFINE_SETTER(bool, GenerateFruit, bGenerateFruit, Value)
TREE_DEFINE_SETTER(int32_t, MaxSegments, MaxSegments, std::max(1, Value))
TREE_DEFINE_SETTER(int32_t, MaxLeaves, MaxLeaves, std::max(1, Value))

#undef TREE_DEFINE_SETTER

void ProceduralTree::SetAutoRegenerate(bool bValue)
{
	bAutoRegenerate = bValue;
}
