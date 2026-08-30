#pragma once

// Weber-Penn has nothing to do with this one: every dimension here descends from Table 1 of
// Hu & Qin 2020 (see Docs/AncientBuilding_Spec.md §8), which is itself a simplification of
// the 材份/斗口 module system of the Yingzao Fashi. Do not read the constants as
// authoritative joinery — they are a games-grade approximation that happens to produce
// convincing proportions from a single number.
//
// The whole table reduces to: eave height = 0.8 x building width, of which the platform
// takes 2/11 and the wall the remaining 9/11.

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/color.hpp>

#include <algorithm>
#include <cmath>

/** Getter/setter pair that notifies listeners, e.g. the owning AncientBuilding. */
#define ANCIENT_ACCESSORS(Type, Member)                     \
	void Set##Member(Type Value)                            \
	{                                                       \
		Member = Value;                                     \
		emit_changed();                                     \
	}                                                       \
	Type Get##Member() const                                \
	{                                                       \
		return Member;                                      \
	}

namespace godot
{
	class AncientBuildingParameters : public Resource
	{
		GDCLASS(AncientBuildingParameters, Resource)

	public:
		enum ERoofType
		{
			/** 硬山 — gables flush with the end walls, no overhang there. */
			ROOF_FLUSH_GABLE = 0,
			/** 歇山 — hipped skirt below, gabled tier above. */
			ROOF_GABLE_AND_HIP = 1,
			/** 庑殿 — hipped on all four sides, no gable at all. */
			ROOF_HIP = 2,
			/** 悬山 — like 硬山 but the roof overhangs past the end walls. */
			ROOF_OVERHANGING_GABLE = 3,
			/** 卷棚 — the two slopes roll over into each other, no sharp ridge. */
			ROOF_ROUND_RIDGE = 4,
			/** 盝顶 — hipped skirt around a flat top platform. */
			ROOF_HOLLOW = 5,
			/** 攒尖 — all slopes converge on a point. Needs a regular plan (Eq 8). */
			ROOF_PYRAMIDAL = 6,
			/** 圆攒尖 — the same, resolved finely enough to read as a cone. */
			ROOF_ROUND = 7,
			/** 盔顶 — 攒尖 with a bulged, helmet-like profile. */
			ROOF_HELMET = 8,
		};

		/**
		 * Material preset. The geometry stays the same 硬山 structure; only the vertex-colour
		 * palette (and the per-piece colour mottle) changes. Selecting a preset writes the
		 * palette into the six colour properties, so any colour can still be hand-tuned after.
		 */
		enum EMaterialStyle
		{
			/** 官式 — timber frame, grey tiles. The historical default. */
			STYLE_TRADITIONAL = 0,
			/** 茅草 — straw thatch roof, plaster walls, dark timber. (Reference/image.png) */
			STYLE_THATCHED = 1,
			/** 土木 — rammed-earth walls with earth-toned tiles. */
			STYLE_EARTHEN = 2,
		};

	private:
		// ---- Plan ----
		/** 通面阔, the frontage. Every other dimension is derived from this. */
		float Width = 9.0f;
		/** 通进深, the depth. */
		float Depth = 6.0f;
		/** 开间, bays across the frontage. Columns stand on the bay boundaries. */
		int32_t BaysX = 3;
		/** Bays through the depth. */
		int32_t BaysZ = 2;
		/**
		 * Plan sides. Equation 8 ties this to the aspect ratio: 4 means a rectangle built from
		 * width and depth, anything else a regular polygon whose apothem is width/2 — so depth
		 * is ignored, because a polygonal plan must be regular.
		 */
		int32_t Sides = 4;
		int32_t RoofType = ROOF_FLUSH_GABLE;

		// ---- Base ----
		float PlatformMargin = 0.7f;
		/** Scales Table 1's 2D platform height. */
		float PlatformHeightScale = 1.0f;
		bool bGenerateFence = true;
		float FenceHeight = 0.95f;
		/** Table 1's lambda: 2^lambda gaps in the balustrade, and that many step runs. */
		int32_t FenceLambda = 1;
		/** Overrides Table 1's omega. Zero lets GetFenceGapWidth() derive it. */
		float FenceGapOverride = 0.0f;
		bool bGenerateSteps = true;
		int32_t StepCount = 5;

		// ---- Body ----
		bool bGenerateColumns = true;
		bool bGenerateWalls = true;
		/** Column radius as a fraction of the module D. */
		float ColumnRadiusScale = 0.42f;
		int32_t ColumnSides = 10;
		/** Bracket band height as a multiple of D. Stands in for 斗拱 until phase 3. */
		float BracketHeightScale = 0.85f;

		// ---- Roof ----
		/** Eave overhang as a multiple of D. */
		float EaveOverhangScale = 2.6f;
		/** 步架, the rafter courses per slope. Drives both the curve and equation 10. */
		int32_t RafterCourses = 5;
		/** 举架 rise ratios: shallow at the eave, steep at the ridge. */
		float EaveRiseRatio = 0.5f;
		float RidgeRiseRatio = 0.9f;
		float RoofHeightScale = 1.0f;
		/** 瓦垄 spacing along the ridge. */
		float TileCourseWidth = 0.34f;
		/** Table's Cr: fraction of the slope covered by tiles, measured from the ridge. */
		float TileCoverage = 1.0f;
		float RidgeScale = 1.0f;
		/** 收山 — where the hip skirt ends and the gable tier begins, as a fraction of depth. */
		float GableRatio = 0.42f;
		/** 悬山 overhang past the end walls, as a multiple of D. */
		float GableOverhangScale = 1.9f;
		/** 卷棚 roll radius at the ridge, as a multiple of D. */
		float RollRadiusScale = 1.3f;
		/** 盝顶 flat top size, as a fraction of the eave footprint. */
		float FlatTopRatio = 0.45f;
		/** 宝顶 finial size at a centralised roof apex, as a multiple of D. */
		float FinialScale = 1.3f;
		/**
		 * 盔顶 bulge. 0 gives the plain concave 攒尖 curve; higher values push the lower slope
		 * outward into the helmet profile the paper singles out as impossible for the old method.
		 */
		float HelmetBulge = 0.55f;
		/**
		 * 起翘 — how far the eave corner lifts, as a multiple of D. Only hipped roofs have a
		 * corner to lift; 硬山 gables are flush walls, so this does nothing there.
		 */
		float CornerRiseScale = 1.6f;
		/** 出翘 — how far the corner pushes out diagonally, as a multiple of D. */
		float CornerExtendScale = 0.7f;
		/** How far back along the eave the lift reaches, as a fraction of the half-depth. */
		float CornerSpanRatio = 0.55f;

		// ---- Material preset ----
		int32_t MaterialStyle = STYLE_TRADITIONAL;

		// ---- Colours (vertex colours; no textures anywhere) ----
		Color StoneColor = Color(0.60f, 0.58f, 0.54f, 1.0f);
		Color TimberColor = Color(0.40f, 0.15f, 0.12f, 1.0f);
		Color PlasterColor = Color(0.74f, 0.70f, 0.63f, 1.0f);
		Color TileColor = Color(0.26f, 0.29f, 0.31f, 1.0f);
		Color RidgeColor = Color(0.19f, 0.21f, 0.23f, 1.0f);
		Color BracketColor = Color(0.46f, 0.21f, 0.16f, 1.0f);

	protected:
		static void _bind_methods();

	public:
		ANCIENT_ACCESSORS(float, Width)
		ANCIENT_ACCESSORS(float, Depth)
		ANCIENT_ACCESSORS(int32_t, BaysX)
		ANCIENT_ACCESSORS(int32_t, BaysZ)
		ANCIENT_ACCESSORS(int32_t, Sides)
		ANCIENT_ACCESSORS(int32_t, RoofType)
		ANCIENT_ACCESSORS(float, PlatformMargin)
		ANCIENT_ACCESSORS(float, PlatformHeightScale)
		ANCIENT_ACCESSORS(float, FenceHeight)
		ANCIENT_ACCESSORS(int32_t, FenceLambda)
		ANCIENT_ACCESSORS(float, FenceGapOverride)
		ANCIENT_ACCESSORS(int32_t, StepCount)
		ANCIENT_ACCESSORS(float, ColumnRadiusScale)
		ANCIENT_ACCESSORS(int32_t, ColumnSides)
		ANCIENT_ACCESSORS(float, BracketHeightScale)
		ANCIENT_ACCESSORS(float, EaveOverhangScale)
		ANCIENT_ACCESSORS(int32_t, RafterCourses)
		ANCIENT_ACCESSORS(float, EaveRiseRatio)
		ANCIENT_ACCESSORS(float, RidgeRiseRatio)
		ANCIENT_ACCESSORS(float, RoofHeightScale)
		ANCIENT_ACCESSORS(float, TileCourseWidth)
		ANCIENT_ACCESSORS(float, TileCoverage)
		ANCIENT_ACCESSORS(float, RidgeScale)
		ANCIENT_ACCESSORS(float, GableRatio)
		ANCIENT_ACCESSORS(float, GableOverhangScale)
		ANCIENT_ACCESSORS(float, RollRadiusScale)
		ANCIENT_ACCESSORS(float, FlatTopRatio)
		ANCIENT_ACCESSORS(float, FinialScale)
		ANCIENT_ACCESSORS(float, HelmetBulge)
		ANCIENT_ACCESSORS(float, CornerRiseScale)
		ANCIENT_ACCESSORS(float, CornerExtendScale)
		ANCIENT_ACCESSORS(float, CornerSpanRatio)
		ANCIENT_ACCESSORS(Color, StoneColor)
		ANCIENT_ACCESSORS(Color, TimberColor)
		ANCIENT_ACCESSORS(Color, PlasterColor)
		ANCIENT_ACCESSORS(Color, TileColor)
		ANCIENT_ACCESSORS(Color, RidgeColor)
		ANCIENT_ACCESSORS(Color, BracketColor)

		/**
		 * Selects a material style and applies its palette to the six colours (notifying
		 * listeners once for the whole change). STYLE_TRADITIONAL keeps the current colours.
		 */
		void SetMaterialStyle(int32_t Value)
		{
			MaterialStyle = Value;
			ApplyMaterialStyle(Value);
			emit_changed();
		}

		int32_t GetMaterialStyle() const { return MaterialStyle; }

		/** Applies a style's palette to the six colours without touching MaterialStyle. */
		void ApplyMaterialStyle(int32_t Value);

		/** Display name for a style, e.g. "Thatched". */
		static String GetStyleName(int32_t Value);

		/** The same, with the Chinese term appended, e.g. "Thatched (茅草)". */
		static String GetStyleNameLocalized(int32_t Value);

		/**
		 * Per-piece colour mottle for a style: the 官式 build is clean (0.05), thatch and
		 * rammed earth are naturally weathered pieces (0.12 / 0.09).
		 */
		static float GetStyleMottle(int32_t Value);

		void SetGenerateFence(bool bValue) { bGenerateFence = bValue; emit_changed(); }
		bool ShouldGenerateFence() const { return bGenerateFence; }

		void SetGenerateSteps(bool bValue) { bGenerateSteps = bValue; emit_changed(); }
		bool ShouldGenerateSteps() const { return bGenerateSteps; }

		void SetGenerateColumns(bool bValue) { bGenerateColumns = bValue; emit_changed(); }
		bool ShouldGenerateColumns() const { return bGenerateColumns; }

		void SetGenerateWalls(bool bValue) { bGenerateWalls = bValue; emit_changed(); }
		bool ShouldGenerateWalls() const { return bGenerateWalls; }

		// ==================== Table 1 derivation ====================

		/** The module D. Table 1: D = width x 0.8 x 1/11. */
		float GetModule() const
		{
			return std::fmax(Width, 0.1f) * 0.8f / 11.0f;
		}

		/** Table 1: Platform.height = 2D. */
		float GetPlatformHeight() const
		{
			return 2.0f * GetModule() * std::fmax(PlatformHeightScale, 0.0f);
		}

		/** Table 1: Brackets sit at 11D, i.e. 0.8 x width. This is the top of the columns. */
		float GetEaveHeight() const
		{
			return 11.0f * GetModule();
		}

		float GetBracketHeight() const
		{
			return GetModule() * std::fmax(BracketHeightScale, 0.0f);
		}

		/** 9D at the default platform scale. */
		float GetColumnHeight() const
		{
			return std::fmax(GetEaveHeight() - GetPlatformHeight(), 0.1f);
		}

		/** Table 1: Roof.position = 11D + bracket.height. */
		float GetRoofBase() const
		{
			return GetEaveHeight() + GetBracketHeight();
		}

		/** Equation 10, as a default rather than a law: taller for fewer rafter courses. */
		float GetRoofHeight() const
		{
			const float Courses = float(std::max(RafterCourses, 3));

			return 1.3f * std::fmax(Depth, 0.1f) / ((Courses - 1.0f) * 0.5f) * std::fmax(RoofHeightScale, 0.01f);
		}

		float GetEaveOverhang() const
		{
			return GetModule() * std::fmax(EaveOverhangScale, 0.0f);
		}

		float GetColumnRadius() const
		{
			return GetModule() * std::fmax(ColumnRadiusScale, 0.01f);
		}

		/** Half-extents of the platform footprint. */
		float GetPlatformHalfWidth() const
		{
			return Width * 0.5f + std::fmax(PlatformMargin, 0.0f);
		}

		float GetPlatformHalfDepth() const
		{
			return Depth * 0.5f + std::fmax(PlatformMargin, 0.0f);
		}

		/** Table 1's omega. Defaults to twice the balustrade bay width along the frontage. */
		float GetFenceGapWidth() const
		{
			if (FenceGapOverride > 0.0f)
			{
				return FenceGapOverride;
			}

			// Table 1: omega = fence.width x 2, where fence.width is one balustrade unit.
			// Posts sit every 2.4D, so a unit is 1.6D and omega lands at 3.2D. Clamped so a
			// small pavilion cannot end up with a stair wider than itself.
			const float Derived = GetModule() * 3.2f;

			return std::fmin(Derived, GetPlatformHalfWidth() * 0.9f);
		}

		/** Table 1: Steps.number = 2^Fence.lambda, and their directions follow from lambda. */
		int32_t GetStepRunCount() const
		{
			return 1 << std::max(std::min(FenceLambda, 3), 0);
		}

		/** Table 1: Steps.depth = Steps.width x 1.1. */
		float GetStepRunDepth() const
		{
			return GetFenceGapWidth() * 1.1f;
		}

		/** Display name for a roof type, e.g. "Gable and Hip". */
		static String GetRoofTypeName(int32_t RoofType);

		/** The same, with the Chinese term appended, e.g. "Gable and Hip (歇山)". */
		static String GetRoofTypeNameLocalized(int32_t RoofType);

		/** True for the centralised family, which equation 8 requires a regular plan for. */
		static bool IsCentralisedRoof(int32_t RoofType);

		/** True when the plan is a regular polygon rather than a rectangle. */
		bool IsPolygonal() const
		{
			return Sides != 4;
		}

		/** Apothem of the body outline. Equation 8: a polygonal plan is regular, so depth is unused. */
		float GetPlanApothem() const
		{
			return Width * 0.5f;
		}

		float GetFinialSize() const
		{
			return GetModule() * std::fmax(FinialScale, 0.0f);
		}

		float GetGableOverhang() const
		{
			return GetModule() * std::fmax(GableOverhangScale, 0.0f);
		}

		float GetRollRadius() const
		{
			return GetModule() * std::fmax(RollRadiusScale, 0.01f);
		}

		float GetCornerRise() const
		{
			return GetModule() * std::fmax(CornerRiseScale, 0.0f);
		}

		float GetCornerExtend() const
		{
			return GetModule() * std::fmax(CornerExtendScale, 0.0f);
		}

		/** Plan distance back from a corner over which the lift decays to zero. */
		float GetCornerSpan() const
		{
			return (Depth * 0.5f + GetEaveOverhang()) * std::fmax(CornerSpanRatio, 0.01f);
		}

		/** Total height from the ground to the top of the main ridge. */
		float GetTotalHeight() const
		{
			return GetRoofBase() + GetRoofHeight();
		}
	};
} // namespace godot

VARIANT_ENUM_CAST(AncientBuildingParameters::ERoofType);
VARIANT_ENUM_CAST(AncientBuildingParameters::EMaterialStyle);
