#include "AncientBuilding/AncientBuilding.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>

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

	/** One vertex-coloured triangle surface, whether it holds a whole mesh or one slot of it. */
	Array MakeSurfaceArrays(
		const std::vector<Vector3>& Vertices,
		const std::vector<Vector3>& Normals,
		const std::vector<Vector2>& UVs,
		const std::vector<Color>& Colors,
		const std::vector<int32_t>& Indices)
	{
		Array Arrays;
		Arrays.resize(Mesh::ARRAY_MAX);
		Arrays[Mesh::ARRAY_VERTEX] = ToPacked<PackedVector3Array>(Vertices);
		Arrays[Mesh::ARRAY_NORMAL] = ToPacked<PackedVector3Array>(Normals);
		Arrays[Mesh::ARRAY_TEX_UV] = ToPacked<PackedVector2Array>(UVs);
		Arrays[Mesh::ARRAY_COLOR] = ToPacked<PackedColorArray>(Colors);
		Arrays[Mesh::ARRAY_INDEX] = ToPacked<PackedInt32Array>(Indices);

		return Arrays;
	}

	/** Slot names, so the enum's order is readable from GDScript without hard-coded numbers. */
	struct SlotConstant
	{
		const char* Name;
		BuildingGen::EMaterialSlot Slot;
	};

	const SlotConstant MATERIAL_SLOT_CONSTANTS[] = {
		{ "SLOT_TILE", BuildingGen::EMaterialSlot::Tile },
		{ "SLOT_TIMBER", BuildingGen::EMaterialSlot::Timber },
		{ "SLOT_STONE", BuildingGen::EMaterialSlot::Stone },
		{ "SLOT_WALL", BuildingGen::EMaterialSlot::Wall },
		{ "SLOT_RIDGE", BuildingGen::EMaterialSlot::Ridge },
		{ "SLOT_GABLE", BuildingGen::EMaterialSlot::Gable },
	};

	/** 脊饰 class names, for the property bindings and the enum constants below. */
	struct RidgeKindConstant
	{
		const char* Constant;
		const char* Property;
		BuildingGen::ERidgeOrnamentKind Kind;
	};

	const RidgeKindConstant RIDGE_KIND_CONSTANTS[] = {
		{ "RIDGE_ORNAMENT_FINIAL", "ridge_finial_mesh", BuildingGen::ERidgeOrnamentKind::Finial },
		{ "RIDGE_ORNAMENT_BEAST", "ridge_beast_mesh", BuildingGen::ERidgeOrnamentKind::Beast },
		{ "RIDGE_ORNAMENT_WALKER", "ridge_walker_mesh", BuildingGen::ERidgeOrnamentKind::Walker },
	};

	/** Packed array -> plain vector, for handing a Godot mesh to the geometry layer. */
	template <typename TElement>
	std::vector<TElement> ToVectors(const Variant& Source)
	{
		const PackedVector3Array Packed = Source;
		std::vector<TElement> Result(size_t(Packed.size()));
		for (int64_t Index = 0; Index < Packed.size(); ++Index)
		{
			Result[size_t(Index)] = Packed[Index];
		}

		return Result;
	}

	template <>
	std::vector<Vector2> ToVectors<Vector2>(const Variant& Source)
	{
		const PackedVector2Array Packed = Source;
		std::vector<Vector2> Result(size_t(Packed.size()));
		for (int64_t Index = 0; Index < Packed.size(); ++Index)
		{
			Result[size_t(Index)] = Packed[Index];
		}

		return Result;
	}

	template <>
	std::vector<int32_t> ToVectors<int32_t>(const Variant& Source)
	{
		const PackedInt32Array Packed = Source;
		std::vector<int32_t> Result(size_t(Packed.size()));
		for (int64_t Index = 0; Index < Packed.size(); ++Index)
		{
			Result[size_t(Index)] = Packed[Index];
		}

		return Result;
	}
} // namespace

void AncientBuilding::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("generate"), &AncientBuilding::Generate);
	ClassDB::bind_method(D_METHOD("get_storey_frames"), &AncientBuilding::GetStoreyFrames);
	ClassDB::bind_method(D_METHOD("bake_mesh"), &AncientBuilding::BakeMesh);

	ClassDB::bind_method(D_METHOD("set_parameters", "value"), &AncientBuilding::SetParameters);
	ClassDB::bind_method(D_METHOD("get_parameters"), &AncientBuilding::GetParameters);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "parameters", PROPERTY_HINT_RESOURCE_TYPE, "AncientBuildingParameters"),
		"set_parameters", "get_parameters");

	ClassDB::bind_method(D_METHOD("set_auto_regenerate", "value"), &AncientBuilding::SetAutoRegenerate);
	ClassDB::bind_method(D_METHOD("should_auto_regenerate"), &AncientBuilding::ShouldAutoRegenerate);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_regenerate"), "set_auto_regenerate", "should_auto_regenerate");

	// 按类别的材质槽. The slot indices are BuildingGen::EMaterialSlot, in the order the constants
	// below name; a null entry means "keep the generated vertex-colour material".
	for (const SlotConstant& Constant : MATERIAL_SLOT_CONSTANTS)
	{
		ClassDB::bind_integer_constant(
			get_class_static(), StringName("MaterialSlot"), StringName(Constant.Name), int64_t(Constant.Slot));
	}

	ClassDB::bind_method(D_METHOD("set_slot_material", "slot", "material"), &AncientBuilding::SetSlotMaterial);
	ClassDB::bind_method(D_METHOD("get_slot_material", "slot"), &AncientBuilding::GetSlotMaterial);
	ClassDB::bind_method(D_METHOD("set_slot_materials", "materials"), &AncientBuilding::SetSlotMaterials);
	ClassDB::bind_method(D_METHOD("get_slot_materials"), &AncientBuilding::GetSlotMaterials);
	ADD_PROPERTY(
		PropertyInfo(Variant::ARRAY, "slot_materials", PROPERTY_HINT_ARRAY_TYPE, "Material"),
		"set_slot_materials", "get_slot_materials");

	ClassDB::bind_method(D_METHOD("get_slot_count"), &AncientBuilding::GetSlotCount);
	ClassDB::bind_method(D_METHOD("get_slot_triangle_count", "slot"), &AncientBuilding::GetSlotTriangleCount);

	// 脊饰 mesh slots: null keeps the generator's cube placeholder. The class constants are the
	// BuildingGen::ERidgeOrnamentKind indices, in the order RIDGE_KIND_CONSTANTS names them.
	ClassDB::bind_method(D_METHOD("set_ridge_ornament_mesh", "kind", "mesh"),
		&AncientBuilding::SetRidgeOrnamentMesh);
	ClassDB::bind_method(D_METHOD("get_ridge_ornament_mesh", "kind"),
		&AncientBuilding::GetRidgeOrnamentMesh);
	ClassDB::bind_method(D_METHOD("get_ridge_ornament_slot_count"), &AncientBuilding::GetRidgeOrnamentSlotCount);
	for (const RidgeKindConstant& Constant : RIDGE_KIND_CONSTANTS)
	{
		ClassDB::bind_integer_constant(
			get_class_static(), StringName("RidgeOrnamentKind"), StringName(Constant.Constant),
			int64_t(Constant.Kind));
	}

	ClassDB::bind_method(D_METHOD("set_ridge_finial_mesh", "mesh"), &AncientBuilding::SetRidgeFinialMesh);
	ClassDB::bind_method(D_METHOD("get_ridge_finial_mesh"), &AncientBuilding::GetRidgeFinialMesh);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "ridge_finial_mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
		"set_ridge_finial_mesh", "get_ridge_finial_mesh");
	ClassDB::bind_method(D_METHOD("set_ridge_beast_mesh", "mesh"), &AncientBuilding::SetRidgeBeastMesh);
	ClassDB::bind_method(D_METHOD("get_ridge_beast_mesh"), &AncientBuilding::GetRidgeBeastMesh);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "ridge_beast_mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
		"set_ridge_beast_mesh", "get_ridge_beast_mesh");
	ClassDB::bind_method(D_METHOD("set_ridge_walker_mesh", "mesh"), &AncientBuilding::SetRidgeWalkerMesh);
	ClassDB::bind_method(D_METHOD("get_ridge_walker_mesh"), &AncientBuilding::GetRidgeWalkerMesh);
	ADD_PROPERTY(
		PropertyInfo(Variant::OBJECT, "ridge_walker_mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
		"set_ridge_walker_mesh", "get_ridge_walker_mesh");

	ClassDB::bind_method(D_METHOD("get_vertex_count"), &AncientBuilding::GetVertexCount);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &AncientBuilding::GetTriangleCount);
}

void AncientBuilding::_validate_property(PropertyInfo& Property) const
{
	if (Property.name == StringName("mesh"))
	{
		Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
	}
	else if (Property.name == StringName("slot_materials") && !HasAnySlotMaterial())
	{
		Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
	}
	else
	{
		// A 脊饰 slot that was never filled leaves no trace in the scene, exactly as a null
		// `slot_materials` entry does not.
		for (const RidgeKindConstant& Constant : RIDGE_KIND_CONSTANTS)
		{
			if (Property.name == StringName(Constant.Property)
				&& RidgeOrnamentMeshes[int32_t(Constant.Kind)].is_null())
			{
				Property.usage &= ~uint32_t(PROPERTY_USAGE_STORAGE);
				break;
			}
		}
	}
}

void AncientBuilding::_ready()
{
	EnsureParameters();

	if (get_mesh().is_null())
	{
		Generate();
	}
}

void AncientBuilding::EnsureParameters()
{
	if (Parameters.is_valid())
	{
		return;
	}

	SetParameters(Ref<AncientBuildingParameters>(memnew(AncientBuildingParameters)));
}

void AncientBuilding::EnsureMaterial()
{
	if (BuildingMaterial.is_null())
	{
		BuildingMaterial.instantiate();
		BuildingMaterial->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		BuildingMaterial->set_roughness(0.88f);
		BuildingMaterial->set_metallic(0.0f);
	}
}

void AncientBuilding::CollectSpec(BuildingGen::BuildingSpec& OutSpec) const
{
	CollectSpecFrom(Parameters, OutSpec);
	for (int32_t Kind = 0; Kind < BuildingGen::RIDGE_ORNAMENT_KIND_COUNT; ++Kind)
	{
		if (RidgeOrnamentMeshes[Kind].is_valid())
		{
			OutSpec.RidgeOrnamentMeshMask |= BuildingGen::RidgeOrnamentBit(
				BuildingGen::ERidgeOrnamentKind(Kind));
		}
	}
}

void AncientBuilding::CollectSpecFrom(const Ref<AncientBuildingParameters>& P, BuildingGen::BuildingSpec& OutSpec)
{
	OutSpec.Width = P->GetWidth();
	OutSpec.Depth = P->GetDepth();
	OutSpec.BaysX = std::max(P->GetBaysX(), 1);
	OutSpec.BaysZ = std::max(P->GetBaysZ(), 1);
	OutSpec.RoofType = P->GetRoofType();

	OutSpec.Module = P->GetModule();

	OutSpec.bGeneratePlatform = P->ShouldGeneratePlatform();
	// No platform ⇒ the building stands on the ground. `PlatformHeight` drives the column base
	// and the wall base directly, and `EaveHeight` / `RoofBase` (below) carry it too — so every
	// quantity that includes it comes down by the same amount and the whole building drops onto
	// the ground plane instead of floating or leaving a gap under the eave.
	const float FullPlatformHeight = P->GetPlatformHeight();
	// 城台 / 高台: the building stands on the terrace top instead, with its own proportions
	// unchanged (PreserveColumnHeight). The shift below then *lifts* everything by the difference.
	const int32_t BaseKind = std::clamp(P->GetBaseKind(), 0, 3);
	const bool bTerrace = BaseKind == 1 || BaseKind == 2;
	const float Floor = bTerrace
		? std::fmax(P->GetMasonryBaseHeight(), 0.0f)
		: (OutSpec.bGeneratePlatform ? FullPlatformHeight : 0.0f);
	const float HeightShift = FullPlatformHeight - Floor;
	OutSpec.PlatformHeight = Floor;
	// 高台 is a tall 台基 — the platform generator, not the masonry one — so it keeps kind 0.
	OutSpec.BaseKind = (BaseKind == 2) ? 0 : BaseKind;
	OutSpec.bGeneratePlatform = OutSpec.bGeneratePlatform || bTerrace;
	OutSpec.StiltDepth = std::fmax(P->GetStiltDepth(), 0.0f);
	OutSpec.RailingKind = std::clamp(P->GetRailingKind(), 0, 2);
	OutSpec.bHangingFascia = P->HasHangingFascia();
	OutSpec.bPlatformSumeru = P->HasPlatformSumeru();
	OutSpec.MasonryHalfWidth = P->GetWidth() * 0.5f + std::fmax(P->GetMasonryBaseMargin(), 0.0f);
	OutSpec.MasonryHalfDepth = (P->IsPolygonal() ? P->GetWidth() : P->GetDepth()) * 0.5f
		+ std::fmax(P->GetMasonryBaseMargin(), 0.0f);
	OutSpec.MasonryBatter = std::fmax(P->GetMasonryBatter(), 0.0f);
	OutSpec.BaseArchCount = std::max(P->GetBaseArchCount(), 0);
	OutSpec.BaseArchWidth = std::fmax(P->GetBaseArchWidth(), 0.0f);
	OutSpec.BaseArchHeightRatio = P->GetBaseArchHeightRatio();
	OutSpec.BaseArchProfile = std::clamp(P->GetBaseArchProfile(), 0, 2);
	OutSpec.BaseArchAxis = std::clamp(P->GetBaseArchAxis(), 0, 1);
	OutSpec.BaseParapet = std::clamp(P->GetBaseParapet(), 0, 2);
	OutSpec.MasonryStoreys = std::max(P->GetMasonryStoreys(), 0);
	OutSpec.BrickColor = P->GetBrickColor();
	OutSpec.PlatformHalfWidth = P->GetPlatformHalfWidth();
	OutSpec.PlatformHalfDepth = P->GetPlatformHalfDepth();
	OutSpec.bGenerateFence = P->ShouldGenerateFence();
	OutSpec.bGenerateSteps = P->ShouldGenerateSteps();
	OutSpec.FenceHeight = P->GetFenceHeight();
	OutSpec.FenceGapWidth = P->GetFenceGapWidth();
	OutSpec.StepRunCount = P->GetStepRunCount();
	OutSpec.StepCount = std::max(P->GetStepCount(), 1);
	OutSpec.StepRunDepth = P->GetStepRunDepth();
	if (BaseKind == 2)
	{
		// A 高台 is climbed, not stepped onto: keep the riser near 0.16 m and the flight at about
		// 30 degrees, so a tall terrace grows a long stair instead of a ladder. [自定] Riser and
		// pitch are ordinary stair practice, not a 则例 value.
		OutSpec.StepCount = std::max(OutSpec.StepCount, int32_t(std::ceil(Floor / 0.16f)));
		OutSpec.StepRunDepth = std::fmax(OutSpec.StepRunDepth, Floor * 1.7f);
	}
	OutSpec.bPlatformTopJoints = P->ShouldGeneratePlatformTopJoints();
	OutSpec.bPlatformEdgeLip = P->ShouldGeneratePlatformEdgeLip();
	OutSpec.bPaving = P->ShouldGeneratePaving();
	OutSpec.bPavingJointGeometry = P->ShouldGeneratePavingJointGeometry();
	OutSpec.bStepSideCheek = P->ShouldGenerateStepSideCheek();

	OutSpec.bGenerateColumns = P->ShouldGenerateColumns();
	OutSpec.bGenerateWalls = P->ShouldGenerateWalls();
	OutSpec.DadoHeightRatio = std::fmax(P->GetDadoHeightRatio(), 0.0f);
	OutSpec.DadoTopTrim = std::fmax(P->GetDadoTopTrim(), 0.0f);
	OutSpec.ColumnRadius = P->GetColumnRadius();
	OutSpec.bColumnBaseSquare = P->ShouldGenerateColumnBaseSquare();
	OutSpec.ColumnSides = std::max(P->GetColumnSides(), 3);
	OutSpec.bSmoothColumns = P->GetSmoothColumns();
	OutSpec.ColumnBaseHeight = std::max(P->GetColumnBaseHeightScale(), 0.0f) * OutSpec.Module;
	OutSpec.ColumnHeight = P->GetColumnHeight();
	// `GetEaveHeight()` / `GetRoofBase()` are derived on the parameter side and both include the
	// full platform height (檐高 = 台基 + 柱 + 斗拱). When the platform is switched off we zeroed
	// `OutSpec.PlatformHeight`, so these two must come down by the same amount — otherwise the
	// body drops to the ground while the roof stays up, leaving a gap between wall top and eave.
	OutSpec.EaveHeight = P->GetEaveHeight() - HeightShift;
	OutSpec.BracketHeight = P->GetBracketHeight();
	OutSpec.RoofBase = P->GetRoofBase() - HeightShift;

	OutSpec.StoreyCount = std::max(P->GetStoreyCount(), 1);
	OutSpec.StoreySetbackBays = std::max(P->GetStoreySetbackBays(), 0);
	OutSpec.UpperColumnHeightScale = P->GetUpperColumnHeightScale();
	OutSpec.bStoreyBalcony = P->HasStoreyBalcony();
	OutSpec.BalconyProjection = OutSpec.Module * std::fmax(P->GetBalconyProjectionScale(), 0.0f);

	OutSpec.EaveOverhang = P->GetEaveOverhang();
	OutSpec.RoofHeight = P->GetRoofHeight();
	OutSpec.RafterCourses = std::max(P->GetRafterCourses(), 3);
	OutSpec.EaveRiseRatio = P->GetEaveRiseRatio();
	OutSpec.RidgeRiseRatio = P->GetRidgeRiseRatio();
	OutSpec.RoofCurveMode = P->GetRoofCurveMode();
	OutSpec.RoofChordError = P->GetRoofChordError();
	OutSpec.RoofMaxSegment = P->GetRoofMaxSegment();
	OutSpec.EaveRafterStyle = P->GetEaveRafterStyle();
	OutSpec.TileCourseWidth = P->GetTileCourseWidth();
	OutSpec.TileDetail = P->GetTileDetail();
	OutSpec.TileBeddingThickness = P->GetTileBeddingThickness();
	OutSpec.LODLevel = std::max(P->GetLODLevel(), 0);
	OutSpec.TileCoverage = P->GetTileCoverage();
	OutSpec.RidgeScale = P->GetRidgeScale();
	OutSpec.RidgeDetail = std::max(P->GetRidgeDetail(), 0);
	OutSpec.bRidgeOrnaments = P->ShouldGenerateRidgeOrnaments();
	// Sizes stay in modules D here; the generator is what knows the module.
	OutSpec.RidgeFinialSize = std::fmax(P->GetRidgeFinialSize(), 0.0f);
	OutSpec.RidgeBeastSize = std::fmax(P->GetRidgeBeastSize(), 0.0f);
	OutSpec.RidgeWalkerSize = std::fmax(P->GetRidgeWalkerSize(), 0.0f);
	OutSpec.RidgeWalkerCount = std::max(P->GetRidgeWalkerCount(), 0);
	OutSpec.GableRatio = P->GetGableRatio();
	OutSpec.GableOverhang = P->GetGableOverhang();
	OutSpec.RollRadius = P->GetRollRadius();
	OutSpec.FlatTopRatio = P->GetFlatTopRatio();
	OutSpec.Sides = std::max(P->GetSides(), 3);
	OutSpec.FinialSize = P->GetFinialSize();
	OutSpec.HelmetBulge = P->GetHelmetBulge();
	OutSpec.PlanApothem = P->GetPlanApothem();
	OutSpec.CornerRise = P->GetCornerRise();
	OutSpec.CornerExtend = P->GetCornerExtend();
	OutSpec.CornerSpan = P->GetCornerSpan();

	OutSpec.StoneColor = P->GetStoneColor();
	OutSpec.TimberColor = P->GetTimberColor();
	OutSpec.PlasterColor = P->GetPlasterColor();
	OutSpec.TileColor = P->GetTileColor();
	OutSpec.RidgeColor = P->GetRidgeColor();
	OutSpec.BracketColor = P->GetBracketColor();
	OutSpec.ColorMottle = AncientBuildingParameters::GetStyleMottle(P->GetMaterialStyle());
}

Array AncientBuilding::GetStoreyFrames()
{
	EnsureParameters();

	BuildingGen::BuildingSpec Spec;
	CollectSpec(Spec);

	std::vector<BuildingGen::StoreyFrame> Frames;
	BuildingGen::DescribeStoreys(Spec, Frames);

	Array Result;
	for (const BuildingGen::StoreyFrame& Frame : Frames)
	{
		Dictionary Entry;
		Entry["floor"] = Frame.Floor;
		Entry["column_foot"] = Frame.ColumnFoot;
		Entry["column_top"] = Frame.ColumnTop;
		Entry["roof_base"] = Frame.RoofBase;
		Entry["break_top"] = Frame.BreakTop;
		Entry["width"] = Frame.Width;
		Entry["depth"] = Frame.Depth;
		Entry["column_lines_x"] = ToPacked<PackedFloat32Array>(Frame.ColumnLinesX);
		Entry["column_lines_z"] = ToPacked<PackedFloat32Array>(Frame.ColumnLinesZ);
		Result.push_back(Entry);
	}

	return Result;
}

void AncientBuilding::Generate()
{
	EnsureParameters();
	EnsureMaterial();

	BuildingGen::BuildingSpec Spec;
	CollectSpec(Spec);

	BuildingGen::MeshAccumulator Accumulated;
	Accumulated.SetMottle(Spec.ColorMottle);
	BuildingGen::BuildBuilding(Spec, Accumulated);

	// 脊饰 with a mesh of their own: the generator left the placeholder out for those classes and
	// recorded where they go, so the swap happens here and the geometry layer stays mesh-free.
	PlaceRidgeOrnamentMeshes(Accumulated, BuildingGen::RidgeOrnamentColor(Spec));

	// The slot partition is reported whether or not the mesh gets split by it, so a caller can
	// always ask what the building is made of.
	for (int32_t Slot = 0; Slot < MATERIAL_SLOT_COUNT; ++Slot)
	{
		LastSlotTriangleCounts[Slot] =
			Accumulated.GetSlotTriangleCount(BuildingGen::EMaterialSlot(Slot));
	}

	if (Accumulated.Indices.empty())
	{
		set_mesh(Ref<ArrayMesh>());
		LastVertexCount = 0;
		LastTriangleCount = 0;
		return;
	}

	Ref<ArrayMesh> Result(memnew(ArrayMesh));

	if (HasAnySlotMaterial())
	{
		// One surface per category, each carrying its own material. Vertices are re-indexed inside
		// their surface, so a slot is free to be textured on its own.
		std::vector<BuildingGen::SurfaceData> Surfaces;
		Accumulated.BuildSurfaces(Surfaces);

		for (const BuildingGen::SurfaceData& Surface : Surfaces)
		{
			const Array Arrays = MakeSurfaceArrays(
				Surface.Vertices, Surface.Normals, Surface.UVs, Surface.Colors, Surface.Indices);
			Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays);

			// A slot with no material of its own keeps the generated vertex-colour material, so
			// asking for a 山花 texture does not strip the colour off the 瓦面 beside it.
			const Ref<Material>& SlotMaterial = SlotMaterials[int32_t(Surface.Slot)];
			Result->surface_set_material(
				Result->get_surface_count() - 1,
				SlotMaterial.is_valid() ? SlotMaterial : Ref<Material>(BuildingMaterial));
		}
	}
	else
	{
		// No slot material anywhere: emit the mesh exactly as it has always been emitted, one
		// surface over the whole vertex table. The geometry regression tests read surface 0 as the
		// entire building, and this keeps that true — and keeps the default bytes untouched.
		const Array Arrays = MakeSurfaceArrays(
			Accumulated.Vertices, Accumulated.Normals, Accumulated.UVs, Accumulated.Colors,
			Accumulated.Indices);
		Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays);
		Result->surface_set_material(0, BuildingMaterial);
	}

	set_mesh(Result);

	LastVertexCount = int32_t(Accumulated.Vertices.size());
	LastTriangleCount = Accumulated.GetTriangleCount();
}

void AncientBuilding::PlaceRidgeOrnamentMeshes(
	BuildingGen::MeshAccumulator& Accumulated, const Color& Tint) const
{
	if (Accumulated.RidgeOrnaments.empty())
	{
		return;
	}

	// The 脊饰 ride on the 脊 material slot, exactly as their placeholder cubes did.
	Accumulated.SetSlot(BuildingGen::EMaterialSlot::Ridge);

	for (const BuildingGen::RidgeOrnamentPlacement& Placement : Accumulated.RidgeOrnaments)
	{
		const Ref<Mesh>& Source = RidgeOrnamentMeshes[int32_t(Placement.Kind)];
		if (Source.is_null())
		{
			continue;
		}

		for (int32_t Surface = 0; Surface < Source->get_surface_count(); ++Surface)
		{
			const Array Arrays = Source->surface_get_arrays(Surface);
			const std::vector<Vector3> Vertices = ToVectors<Vector3>(Arrays[Mesh::ARRAY_VERTEX]);
			const std::vector<Vector3> Normals = ToVectors<Vector3>(Arrays[Mesh::ARRAY_NORMAL]);
			const std::vector<Vector2> UVs = ToVectors<Vector2>(Arrays[Mesh::ARRAY_TEX_UV]);
			std::vector<int32_t> Indices = ToVectors<int32_t>(Arrays[Mesh::ARRAY_INDEX]);
			if (Indices.empty())
			{
				// A surface authored without an index buffer is still a triangle list.
				Indices.resize(Vertices.size());
				for (size_t Index = 0; Index < Indices.size(); ++Index)
				{
					Indices[Index] = int32_t(Index);
				}
			}

			Accumulated.AddPlacedTriangles(
				Vertices, Normals, UVs, Indices,
				Placement.Origin, Placement.AxisX, Placement.AxisY, Placement.AxisZ,
				Placement.Size, Tint);
		}
	}
}

bool AncientBuilding::HasAnySlotMaterial() const
{
	for (const Ref<Material>& SlotMaterial : SlotMaterials)
	{
		if (SlotMaterial.is_valid())
		{
			return true;
		}
	}

	return false;
}

void AncientBuilding::SetSlotMaterial(int32_t Slot, const Ref<Material>& Value)
{
	if (Slot < 0 || Slot >= MATERIAL_SLOT_COUNT || SlotMaterials[Slot] == Value)
	{
		return;
	}

	SlotMaterials[Slot] = Value;
	RequestRegenerate();
}

Ref<Material> AncientBuilding::GetSlotMaterial(int32_t Slot) const
{
	if (Slot < 0 || Slot >= MATERIAL_SLOT_COUNT)
	{
		return Ref<Material>();
	}

	return SlotMaterials[Slot];
}

void AncientBuilding::SetSlotMaterials(const Array& Values)
{
	bool bChanged = false;
	for (int32_t Slot = 0; Slot < MATERIAL_SLOT_COUNT; ++Slot)
	{
		const Ref<Material> Value =
			(Slot < Values.size()) ? Ref<Material>(Values[Slot]) : Ref<Material>();
		if (SlotMaterials[Slot] != Value)
		{
			SlotMaterials[Slot] = Value;
			bChanged = true;
		}
	}

	if (bChanged)
	{
		RequestRegenerate();
	}
}

Array AncientBuilding::GetSlotMaterials() const
{
	Array Result;
	Result.resize(MATERIAL_SLOT_COUNT);
	for (int32_t Slot = 0; Slot < MATERIAL_SLOT_COUNT; ++Slot)
	{
		Result[Slot] = SlotMaterials[Slot];
	}

	return Result;
}

void AncientBuilding::SetRidgeOrnamentMesh(int32_t Kind, const Ref<Mesh>& Value)
{
	if (Kind < 0 || Kind >= BuildingGen::RIDGE_ORNAMENT_KIND_COUNT
		|| RidgeOrnamentMeshes[Kind] == Value)
	{
		return;
	}

	RidgeOrnamentMeshes[Kind] = Value;
	RequestRegenerate();
}

Ref<Mesh> AncientBuilding::GetRidgeOrnamentMesh(int32_t Kind) const
{
	if (Kind < 0 || Kind >= BuildingGen::RIDGE_ORNAMENT_KIND_COUNT)
	{
		return Ref<Mesh>();
	}

	return RidgeOrnamentMeshes[Kind];
}

int32_t AncientBuilding::GetSlotTriangleCount(int32_t Slot) const
{
	if (Slot < 0 || Slot >= MATERIAL_SLOT_COUNT)
	{
		return 0;
	}

	return LastSlotTriangleCounts[Slot];
}

Ref<ArrayMesh> AncientBuilding::BakeMesh()
{
	Generate();

	return get_mesh();
}

void AncientBuilding::RequestRegenerate()
{
	if (!bAutoRegenerate || !is_inside_tree())
	{
		return;
	}

	Generate();
}

void AncientBuilding::OnParametersChanged()
{
	RequestRegenerate();
}

void AncientBuilding::SetParameters(const Ref<AncientBuildingParameters>& Value)
{
	const Callable OnChanged = callable_mp(this, &AncientBuilding::OnParametersChanged);

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

void AncientBuilding::SetAutoRegenerate(bool bValue)
{
	bAutoRegenerate = bValue;
}
