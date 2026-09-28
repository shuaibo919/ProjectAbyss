#pragma once

// Weber-Penn has nothing to do with this one: every dimension here descends from Table 1 of
// Hu & Qin 2020 (see ProjectAbyssWiki/documentation/systems/AncientBuilding_Spec.md §8), which is itself a simplification of
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

		/** 构筑原型, Hu & Qin Fig 2 (舫/轩 left out: 舫 is a boat hull, 轩 a walled 廊). */
		enum EArchetype
		{
			ARCHETYPE_TING = 0,
			ARCHETYPE_TAI = 1,
			ARCHETYPE_LOU = 2,
			ARCHETYPE_GE = 3,
			ARCHETYPE_XIE = 4,
			ARCHETYPE_LANG = 5,
			ARCHETYPE_COUNT = 6,
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
		 * The span Table 1's module D is derived from. **0 = the frontage (legacy).** A 廊 or a long
		 * 连房 is not a bigger building for being longer — its columns belong to its bay, not its
		 * length — so it sets this to one pavilion's worth of frontage instead.
		 */
		float ModuleSpan = 0.0f;
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
		/**
		 * 台基有无 (地基). **Default `true` = the historical output** (every building stands on a
		 * platform, base at y = PlatformHeight). Off: the platform block, its steps and the
		 * balustrade are skipped, and `PlatformHeight` is treated as 0 — since the column base,
		 * the wall base and the roof base all derive from it, the whole building shifts down to
		 * stand on the ground. That is the physically right result: no 台基 means the floor is at
		 * ground level. Consumers that read `get_platform_height()` still see the parameter's own
		 * value; only the geometry treats it as 0.
		 * Paving is unaffected: it is ground paving, not part of the platform.
		 */
		bool bGeneratePlatform = true;
		/**
		 * 台基顶面接缝网格 (60_台基地面 R6). Off = legacy: one seamless cap slab. On: the cap top
		 * is cut into a rectangular slab grid with real grooves instead of a colour pattern.
		 * [自定] Slab pitch and groove size have no source; sized to read at the M4 acceptance view.
		 */
		bool bPlatformTopJoints = false;
		/**
		 * 台基边缘沿口 (60_台基地面 R6). Off = legacy straight drop. On: the cap gets a
		 * protruding moulded band under its top, so the edge steps out instead of dropping.
		 * [自定] Lip height / projection have no source; sized to read at the M4 acceptance view.
		 */
		bool bPlatformEdgeLip = false;
		/** 方砖铺地 around the platform (60_台基地面 R9). Off = legacy: no paving at all. */
		bool bPaving = false;
		/**
		 * 铺地几何板缝 (60_台基地面 R9). Off = the legacy reading: one vertex-coloured tile quad
		 * each, so the joints are a colour difference only. On: the tiles are separated by real
		 * grooves over a recessed bed, which is the "grid + visible joints" the plate asks for.
		 */
		bool bPavingJointGeometry = false;
		/**
		 * 踏步两侧简单侧挡 (60_台基地面 R8). Off = legacy: a bare swept staircase block. On: a
		 * stepped cheek follows the run on both sides. [自定] Form has no source.
		 */
		bool bStepSideCheek = false;

		// ---- Masonry (砖石作) ----
		/**
		 * Base kind. **0 = 台基, the legacy platform.** 1 = 城台 / 墩台: a battered masonry terrace
		 * pierced by arched passages, with a parapet, and the building standing on its top. The
		 * building's own proportions do not change — it is lifted by the terrace (HeightPolicy
		 * PreserveColumnHeight, 05 契约 §6.4), so the 11D chain is measured from the terrace top.
		 */
		int32_t BaseKind = 0;
		/** 城台 height in metres. */
		float MasonryBaseHeight = 7.0f;
		/** How far the 城台 reaches past the building's column line on every side, in metres. [自定] */
		float MasonryBaseMargin = 4.0f;
		/** 收分: horizontal draw-in per metre of height. [自定] ~1:12 reads as a city wall. */
		float MasonryBatter = 0.08f;
		int32_t BaseArchCount = 1;
		/** Passage width in metres. 0 derives it from the terrace. */
		float BaseArchWidth = 0.0f;
		/** Top of the arch as a fraction of the terrace height. */
		float BaseArchHeightRatio = 0.62f;
		/** 0 半圆券, 1 双心券, 2 平券. */
		int32_t BaseArchProfile = 0;
		/** 0 = passages run front to back, 1 = side to side. */
		int32_t BaseArchAxis = 0;
		/** 0 none, 1 垛口, 2 宇墙. */
		int32_t BaseParapet = 1;
		/**
		 * Storeys, counted from the ground, built as brick walls with 券门 / 券窗 instead of a timber
		 * frame (箭楼, 砖砌下层, 无梁殿). 0 = legacy: every storey is timber.
		 */
		int32_t MasonryStoreys = 0;
		Color BrickColor = Color(0.55f, 0.53f, 0.50f, 1.0f);

		// ---- Open structures (亭 / 台 / 榭 / 廊) ----
		/** 0 none (legacy), 1 坐凳栏杆, 2 美人靠. Only where there is no wall. */
		int32_t RailingKind = 0;
		/** 倒挂楣子 under the architrave in every open bay. */
		bool bHangingFascia = false;
		/** 桩台: pile depth below ground / water, in metres. */
		float StiltDepth = 2.0f;
		/**
		 * 须弥座: the platform body as a moulded pedestal (圭脚 / 下枋 / 下枭 / 束腰 / 上枭 / 上枋)
		 * instead of a plain block. Off = legacy. [待定标] The band proportions have no source; the
		 * handbook lists them as missing (60_台基地面).
		 */
		bool bPlatformSumeru = false;

		// ---- Body ----
		bool bGenerateColumns = true;
		bool bGenerateWalls = true;
		/**
		 * 下碱带高 ÷ 墙身批高 (20_墙体 R15). 0 = legacy: no zoning, the wall is one plain panel.
		 * Above 0 the bottom of every wall bay becomes a masonry block band with real horizontal
		 * and staggered vertical joints, and the plaster above it stops being a bare plane. The
		 * band never grows past the solid 槛墙 below the window.
		 * [自定] The 下碱 proportion on the reference plate is a hand-drawn indication only, so
		 * this ratio and the block sizes derived from it are project choices, not measured values.
		 */
		float DadoHeightRatio = 0.0f;
		/**
		 * 分界带 between the plaster and the block band, as a multiple of the module D
		 * (20_墙体 R15). 0 = legacy: no trim band; only acts when DadoHeightRatio > 0.
		 * [自定] Form and size have no source; sized to read at the M1 acceptance view.
		 */
		float DadoTopTrim = 0.0f;
		/**
		 * 方形石础 (60_台基地面 R12). Off = legacy: the turned, moulded stone drum every existing
		 * resource was baked with. On: a square stone block with a base course under each column,
		 * which is what the dwelling elevation asks for. Height semantics do not change — the
		 * plinth still comes from ColumnBaseHeightScale, so 0 keeps both variants disabled.
		 */
		bool bColumnBaseSquare = false;
		/** Column radius as a fraction of the module D. */
		float ColumnRadiusScale = 0.42f;
		int32_t ColumnSides = 10;
		bool SmoothColumns = true;
		/** Optional stone plinth height in modules; zero preserves the original footprint. */
		float ColumnBaseHeightScale = 0.0f;
		/** Bracket band height as a multiple of D. Stands in for 斗拱 until phase 3. */
		float BracketHeightScale = 0.85f;

		// ---- Storeys (多层: 重檐 / 楼 / 阁) ----
		/**
		 * Storey count. **1 = the legacy single-storey building.** Above 1 every storey but the top
		 * one gets a 腰檐 (a hipped skirt opened at the upper storey's wall); the top storey carries
		 * roof_type. 重檐 = 2 storeys, setback 1, no balcony, short upper columns.
		 */
		int32_t StoreyCount = 1;
		/**
		 * Bay rings each storey steps in by. 0 = 叉柱造: the upper columns stand on the lower
		 * column lines. 1 = the upper storey stands on the inner ring (金柱 over a 周围廊); the
		 * lower storey's outer bays then become aisles of one 廊步 on every side.
		 */
		int32_t StoreySetbackBays = 0;
		/** Upper storeys' column height as a fraction of the ground storey's. [自定] */
		float UpperColumnHeightScale = 0.8f;
		/** 平座: a projecting floor with a 勾栏 under each upper storey (楼阁 on, 重檐 off). */
		bool bStoreyBalcony = false;
		/** 平座 projection past the upper storey's column line, as a multiple of D. [自定] */
		float BalconyProjectionScale = 1.6f;

		// ---- Roof ----
		/** Eave overhang as a multiple of D. */
		float EaveOverhangScale = 2.6f;
		/** 步架, the rafter courses per slope. Drives both the curve and equation 10. */
		int32_t RafterCourses = 5;
		/** 举架 rise ratios: shallow at the eave, steep at the ridge. */
		float EaveRiseRatio = 0.5f;
		float RidgeRiseRatio = 0.9f;
		float RoofHeightScale = 1.0f;
		/**
		 * Roof profile mode (v2 P1). 0 = Legacy: the old course polyline, kept bit-identical so
		 * existing resources bake to the same mesh. 1 = Continuous: the analytic integral of the
		 * rise-ratio ramp, adaptively sampled (see RoofCurve.h). Missing on old resources, which
		 * therefore default to Legacy.
		 */
		int32_t RoofCurveMode = 0;
		/** Continuous-mode sampling quality: maximum distance from the curve to a sample chord. */
		float RoofChordError = 0.005f;
		/** Continuous-mode sampling quality: longest allowed sample segment, in metres. */
		float RoofMaxSegment = 0.5f;
		/**
		 * 檐下椽飞 — the rafter heads under the eave (v2 P4). 0 = none, 1 = 檐椽头 row,
		 * 2 = 檐椽头 with the 飞椽 step on top (the default). The heads share one 檐口断面
		 * across every roof type, anchored to the eave line and the curve's eave tangent.
		 */
		int32_t EaveRafterStyle = 2;
		/** 瓦垄 spacing along the ridge. */
		float TileCourseWidth = 0.34f;
		/**
		 * 瓦作 detail level (30_瓦作 §2). 0 = legacy: world-equal courses, one displaced sheet, no
		 * longitudinal lap, plain eave tongues — every existing resource bakes to the same mesh.
		 * 1 = 排垄与叠压: courses laid by the patch's own metric (R1(d)) with an integer course
		 * count (R7/R8 甲) and a real longitudinal lap (R8 乙/R21). 2 = 1 + 檐口件与泥背: 瓦当 end
		 * disc and 13-point 如意 滴水 (R4/R5) plus the 泥背 layer (R17).
		 */
		int32_t TileDetail = 0;
		/** 泥背层厚 in metres (R17/card T3). 0 = the legacy skin sitting straight on the boarding. */
		float TileBeddingThickness = 0.0f;
		/**
		 * Distance tier (30_瓦作 §2 / ExecutionPlan ). A *tier*, not a capability: it decides how
		 * much of whatever TileDetail built survives into the baked mesh, so a resource can keep one
		 * semantic description and still bake near, mid and far meshes from it.
		 *
		 * 0 = 近景, full: byte for byte what the same resource baked before this parameter existed.
		 * 1 = 中景: the section keeps half its samples and the lap step collapses to one slant, which
		 *     is where the skin's triangles actually are — the lap adds four points per joint per
		 *     column, and those points are most of the roof.
		 * 2 = 远景: no lap at all (one layer), a two-facet section, a plain bar along the eave instead
		 *     of per-course 瓦当/滴水, and box ridges.
		 */
		int32_t LODLevel = 0;
		/** Table's Cr: fraction of the slope covered by tiles, measured from the ridge. */
		float TileCoverage = 1.0f;
		float RidgeScale = 1.0f;
		/**
		 * 脊断面 (50_脊饰 §3). 0 = legacy: the seven-point section every resource written so far
		 * baked. 1 = 分层: 当沟条 / 脊身 / 盖脊筒 as three bands with real ledges between them, so the
		 * ridge reads as a stack of members instead of one half-round bar. Same soffit and same crown
		 * height either way, so the ground plan and the 瓦-脊 高度链 are unaffected by the choice.
		 */
		int32_t RidgeDetail = 0;
		/**
		 * 脊饰 (50_脊饰 R1-R4). **Off = the legacy building, which has no 脊饰 at all** — and that is
		 * the right default for a dwelling: 普通建筑是没有脊饰的 (user, 2026-09-27).
		 *
		 * This switch is the manual stand-in for the 等级 gate the handbook asks for (R1: 鸱吻 for
		 * 宫殿 only); `BuildingTier` does not exist in this tree, so nothing decides it but the user.
		 */
		bool bRidgeOrnaments = false;
		/** 正吻 placeholder cube side, in modules D. [自定] No source gives a size. */
		float RidgeFinialSize = 0.70f;
		/** 垂兽 placeholder cube side, in modules D. [自定] */
		float RidgeBeastSize = 0.42f;
		/** 走兽 placeholder cube side, in modules D. [自定] */
		float RidgeWalkerSize = 0.26f;
		/** 走兽 per 垂脊 / 戗脊. [自定] The 会典 sequence count is [待定标] (50_脊饰 R4). */
		int32_t RidgeWalkerCount = 3;
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
		ANCIENT_ACCESSORS(bool, SmoothColumns)
		ANCIENT_ACCESSORS(float, ColumnBaseHeightScale)
		ANCIENT_ACCESSORS(float, Width)
		ANCIENT_ACCESSORS(float, Depth)
		ANCIENT_ACCESSORS(int32_t, BaysX)
		ANCIENT_ACCESSORS(int32_t, BaysZ)
		ANCIENT_ACCESSORS(float, ModuleSpan)
		ANCIENT_ACCESSORS(int32_t, Sides)
		ANCIENT_ACCESSORS(int32_t, RoofType)
		ANCIENT_ACCESSORS(float, PlatformMargin)
		ANCIENT_ACCESSORS(float, PlatformHeightScale)
		ANCIENT_ACCESSORS(float, DadoHeightRatio)
		ANCIENT_ACCESSORS(float, DadoTopTrim)
		ANCIENT_ACCESSORS(float, FenceHeight)
		ANCIENT_ACCESSORS(int32_t, FenceLambda)
		ANCIENT_ACCESSORS(float, FenceGapOverride)
		ANCIENT_ACCESSORS(int32_t, StepCount)
		ANCIENT_ACCESSORS(float, ColumnRadiusScale)
		ANCIENT_ACCESSORS(int32_t, ColumnSides)
		ANCIENT_ACCESSORS(float, BracketHeightScale)
		ANCIENT_ACCESSORS(int32_t, BaseKind)
		ANCIENT_ACCESSORS(float, MasonryBaseHeight)
		ANCIENT_ACCESSORS(float, MasonryBaseMargin)
		ANCIENT_ACCESSORS(float, MasonryBatter)
		ANCIENT_ACCESSORS(int32_t, BaseArchCount)
		ANCIENT_ACCESSORS(float, BaseArchWidth)
		ANCIENT_ACCESSORS(float, BaseArchHeightRatio)
		ANCIENT_ACCESSORS(int32_t, BaseArchProfile)
		ANCIENT_ACCESSORS(int32_t, BaseArchAxis)
		ANCIENT_ACCESSORS(int32_t, BaseParapet)
		ANCIENT_ACCESSORS(int32_t, MasonryStoreys)
		ANCIENT_ACCESSORS(Color, BrickColor)
		ANCIENT_ACCESSORS(int32_t, RailingKind)
		ANCIENT_ACCESSORS(float, StiltDepth)
		ANCIENT_ACCESSORS(int32_t, StoreyCount)
		ANCIENT_ACCESSORS(int32_t, StoreySetbackBays)
		ANCIENT_ACCESSORS(float, UpperColumnHeightScale)
		ANCIENT_ACCESSORS(float, BalconyProjectionScale)
		ANCIENT_ACCESSORS(float, EaveOverhangScale)
		ANCIENT_ACCESSORS(int32_t, RafterCourses)
		ANCIENT_ACCESSORS(float, EaveRiseRatio)
		ANCIENT_ACCESSORS(float, RidgeRiseRatio)
		ANCIENT_ACCESSORS(float, RoofHeightScale)
		ANCIENT_ACCESSORS(int32_t, RoofCurveMode)
		ANCIENT_ACCESSORS(int32_t, EaveRafterStyle)
		ANCIENT_ACCESSORS(float, RoofChordError)
		ANCIENT_ACCESSORS(float, RoofMaxSegment)
		ANCIENT_ACCESSORS(float, TileCourseWidth)
		ANCIENT_ACCESSORS(int32_t, TileDetail)
		ANCIENT_ACCESSORS(float, TileBeddingThickness)
		ANCIENT_ACCESSORS(int32_t, LODLevel)
		ANCIENT_ACCESSORS(float, TileCoverage)
		ANCIENT_ACCESSORS(float, RidgeScale)
		ANCIENT_ACCESSORS(int32_t, RidgeDetail)
		ANCIENT_ACCESSORS(float, RidgeFinialSize)
		ANCIENT_ACCESSORS(float, RidgeBeastSize)
		ANCIENT_ACCESSORS(float, RidgeWalkerSize)
		ANCIENT_ACCESSORS(int32_t, RidgeWalkerCount)
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

		/**
		 * Test/debug entry to the continuous roof curve (v2 P1.2): samples the analytic
		 * rise-ratio integral between (HalfSpan, 0) and (0, RoofHeight) with the given chord
		 * error and maximum segment length. Eave first, ridge last.
		 */
		static PackedVector2Array SampleRoofCurve(
			float EaveRiseRatio, float RidgeRiseRatio, float RoofHeight, float HalfSpan,
			float ChordError, float MaxSegment);

		void SetGenerateFence(bool bValue) { bGenerateFence = bValue; emit_changed(); }
		bool ShouldGenerateFence() const { return bGenerateFence; }

		void SetGenerateSteps(bool bValue) { bGenerateSteps = bValue; emit_changed(); }
		bool ShouldGenerateSteps() const { return bGenerateSteps; }

		void SetGenerateColumns(bool bValue) { bGenerateColumns = bValue; emit_changed(); }
		bool ShouldGenerateColumns() const { return bGenerateColumns; }

		void SetGenerateWalls(bool bValue) { bGenerateWalls = bValue; emit_changed(); }
		bool ShouldGenerateWalls() const { return bGenerateWalls; }

		void SetGeneratePlatform(bool bValue) { bGeneratePlatform = bValue; emit_changed(); }
		bool ShouldGeneratePlatform() const { return bGeneratePlatform; }

		void SetStoreyBalcony(bool bValue) { bStoreyBalcony = bValue; emit_changed(); }
		bool HasStoreyBalcony() const { return bStoreyBalcony; }

		void SetHangingFascia(bool bValue) { bHangingFascia = bValue; emit_changed(); }
		bool HasHangingFascia() const { return bHangingFascia; }

		void SetPlatformSumeru(bool bValue) { bPlatformSumeru = bValue; emit_changed(); }
		bool HasPlatformSumeru() const { return bPlatformSumeru; }

		/**
		 * 构筑原型 preset (Hu & Qin Fig 2): writes the plan / base / body / roof fields for one of
		 * 亭 台 楼 阁 榭 廊. Only writes parameters (G6 two-layer API) — nothing about the
		 * generator changes, and every field can be hand-tuned afterwards.
		 */
		void ApplyArchetype(int32_t Archetype);

		void SetPlatformTopJoints(bool bValue) { bPlatformTopJoints = bValue; emit_changed(); }
		bool ShouldGeneratePlatformTopJoints() const { return bPlatformTopJoints; }

		void SetPlatformEdgeLip(bool bValue) { bPlatformEdgeLip = bValue; emit_changed(); }
		bool ShouldGeneratePlatformEdgeLip() const { return bPlatformEdgeLip; }

		void SetPaving(bool bValue) { bPaving = bValue; emit_changed(); }
		bool ShouldGeneratePaving() const { return bPaving; }

		void SetPavingJointGeometry(bool bValue) { bPavingJointGeometry = bValue; emit_changed(); }
		bool ShouldGeneratePavingJointGeometry() const { return bPavingJointGeometry; }

		void SetStepSideCheek(bool bValue) { bStepSideCheek = bValue; emit_changed(); }
		bool ShouldGenerateStepSideCheek() const { return bStepSideCheek; }

		void SetColumnBaseSquare(bool bValue) { bColumnBaseSquare = bValue; emit_changed(); }
		bool ShouldGenerateColumnBaseSquare() const { return bColumnBaseSquare; }

		/** 脊饰 on/off (50_脊饰). Off is the legacy building: no 脊饰 geometry is generated. */
		void SetRidgeOrnaments(bool bValue) { bRidgeOrnaments = bValue; emit_changed(); }
		bool ShouldGenerateRidgeOrnaments() const { return bRidgeOrnaments; }

		// ==================== Table 1 derivation ====================

		/** The module D. Table 1: D = width x 0.8 x 1/11. */
		float GetModule() const
		{
			const float Basis = (ModuleSpan > 0.0f) ? ModuleSpan : Width;
			return std::fmax(Basis, 0.1f) * 0.8f / 11.0f;
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
