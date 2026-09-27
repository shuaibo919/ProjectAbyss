#pragma once

// Assembles a whole building from the Table 1 frame (ProjectAbyssWiki/documentation/systems/AncientBuilding_Spec.md §8).
//
// Where the paper's section 3.1 sweep earns its keep it is used: ridges, eave courses, tile
// courses and the stepped stair blocks all go through BuildSweep rather than being hand-built
// from boxes. Everything genuinely prismatic — platform, columns, walls — is a box, because
// dressing those up as sweeps would buy nothing.
//
// Every triangle carries the BuildingGen::EMaterialSlot it belongs to, so the output can be handed
// to Godot either as one vertex-coloured surface — the contract PcgVillageMeshes established for
// this project, no textures and nothing baked into the scene — or as one surface per category for
// the callers that want a material on the 瓦面, the 山花 or the 下碱 alone. See MeshAccumulator.

#include "AncientBuilding/SplineSweep.h"

#include <godot_cpp/variant/color.hpp>

#include <cstdint>
#include <vector>

namespace BuildingGen
{
	using godot::Color;

	enum ERoofKind
	{
		ROOF_FLUSH_GABLE = 0,
		ROOF_GABLE_AND_HIP = 1,
		ROOF_HIP = 2,
		ROOF_OVERHANGING_GABLE = 3,
		ROOF_ROUND_RIDGE = 4,
		ROOF_HOLLOW = 5,
		ROOF_PYRAMIDAL = 6,
		ROOF_ROUND = 7,
		ROOF_HELMET = 8,
	};

	/** Profile shape for the centralised family. */
	enum ECentralProfile
	{
		/** 攒尖 — the plain concave 举架 curve. */
		CENTRAL_STRAIGHT,
		/** 盔顶 — bulged low, concave high. */
		CENTRAL_HELMET,
	};

	/**
	 * 脊饰类 (50_脊饰 R1-R4).
	 *
	 * 套兽 (R5) is deliberately absent rather than unimplemented: it caps the 角梁 head, and this
	 * generator builds no 角梁, so there is nowhere to seat it. Registered as a gap, not a TODO.
	 */
	enum class ERidgeOrnamentKind : int32_t
	{
		/** 正吻: one at each end of a 正脊. */
		Finial = 0,
		/** 垂兽 / 戗兽: one at each 檐口 end of a 垂脊 / 戗脊. */
		Beast = 1,
		/** 走兽: the row walking up a 垂脊 / 戗脊 from its 垂兽. */
		Walker = 2,
		Count,
	};

	inline constexpr int32_t RIDGE_ORNAMENT_KIND_COUNT = int32_t(ERidgeOrnamentKind::Count);

	/** Bit per ERidgeOrnamentKind, for "does the caller supply its own mesh for this class". */
	inline constexpr uint32_t RidgeOrnamentBit(ERidgeOrnamentKind Kind)
	{
		return 1u << uint32_t(Kind);
	}

	/** How the hipped generator terminates its shell. */
	enum EHipTop
	{
		/** 歇山 — stop at the 收山 break and put a gabled tier above. */
		HIP_TOP_GABLED_TIER,
		/** 庑殿 — take the shell all the way in, so the top collapses to the ridge. */
		HIP_TOP_RIDGE,
		/** 盝顶 — stop early and cap with a flat platform ringed by a 围脊. */
		HIP_TOP_FLAT,
	};

	/**
	 * Per-component-category material slot.
	 *
	 * Every triangle the accumulator emits is stamped with the slot that was current when it was
	 * added, so the mesh can be handed to Godot as one surface per category and each category can
	 * carry its own material — a normal map on the 瓦面, a 山花 texture, a brick material on the
	 * 下碱 — while the vertex-coloured default stays exactly as it was: a slot with no material is
	 * given the building's own vertex-colour material, so nothing about the default look changes.
	 */
	enum class EMaterialSlot
	{
		/** 瓦: 瓦垄, 瓦当, 滴水, 泥背 / 板瓦坡面, and the 盝顶's flat deck. */
		Tile = 0,
		/** 木构: 柱, 枋, 梁, 檐椽飞, 望板, 连檐, 斗拱带, 门窗棂条. */
		Timber,
		/** 石作: 台基, 踏步, 柱础, 铺地, 栏杆. */
		Stone,
		/** 墙面: 抹灰面, 下碱砌块带, 槛墙, 分界带. */
		Wall,
		/** 脊: 正脊, 垂脊, 戗脊, 围脊, 宝顶连座. */
		Ridge,
		/** 山花 / 山墙幕墙: 歇山山花, 硬山悬山山墙, 卷棚山墙. */
		Gable,
		Count,
	};

	/** Slot count as an int — the Godot side stores one material per slot in a fixed array. */
	inline constexpr int32_t MATERIAL_SLOT_COUNT = int32_t(EMaterialSlot::Count);

	/** Plain snapshot of AncientBuildingParameters, with Table 1 already evaluated. */
	struct BuildingSpec
	{
		float Width = 9.0f;
		float Depth = 6.0f;
		int32_t BaysX = 3;
		int32_t BaysZ = 2;
		int32_t RoofType = ROOF_FLUSH_GABLE;

		float Module = 0.0f;

		// Base
		float PlatformHeight = 0.0f;
		float PlatformHalfWidth = 0.0f;
		float PlatformHalfDepth = 0.0f;
		/** 台基有无. false = skip the platform block, its steps and the balustrade, and treat
		 *  PlatformHeight as 0 so the whole building stands on the ground. CollectSpec zeroes
		 *  PlatformHeight in that case; every base below derives from it. */
		bool bGeneratePlatform = true;
		bool bGenerateFence = true;
		bool bGenerateSteps = true;
		float FenceHeight = 0.95f;
		float FenceGapWidth = 0.0f;
		int32_t StepRunCount = 2;
		int32_t StepCount = 5;
		float StepRunDepth = 0.0f;
		/** 台基顶面接缝网格, 60_台基地面 R6. Off keeps the legacy seamless cap. */
		bool bPlatformTopJoints = false;
		/** 台基边缘沿口, 60_台基地面 R6. Off keeps the legacy straight drop. */
		bool bPlatformEdgeLip = false;
		/** 方砖铺地, 60_台基地面 R9. Off = the legacy build has no paving at all. */
		bool bPaving = false;
		/** 铺地几何板缝, 60_台基地面 R9. Off = joints are a vertex-colour difference. */
		bool bPavingJointGeometry = false;
		/** 踏步两侧侧挡, 60_台基地面 R8. Off keeps the legacy bare staircase block. */
		bool bStepSideCheek = false;

		// Body
		bool bGenerateColumns = true;
		bool bGenerateWalls = true;
		/** 下碱带高 ÷ 墙高, 20_墙体 R15. 0 keeps the legacy unzoned wall. */
		float DadoHeightRatio = 0.0f;
		/** 上下区分界带高, in modules D, 20_墙体 R15. 0 keeps the legacy wall without one. */
		float DadoTopTrim = 0.0f;
		float ColumnRadius = 0.0f;
		/** 方形石础 instead of the turned drum, 60_台基地面 R12. Off = legacy. */
		bool bColumnBaseSquare = false;
		int32_t ColumnSides = 10;
		bool bSmoothColumns = true;
		float ColumnBaseHeight = 0.0f;
		float ColumnHeight = 0.0f;
		float EaveHeight = 0.0f;
		float BracketHeight = 0.0f;
		float RoofBase = 0.0f;

		// Roof
		float EaveOverhang = 0.0f;
		float RoofHeight = 0.0f;
		int32_t RafterCourses = 7;
		float EaveRiseRatio = 0.5f;
		float RidgeRiseRatio = 0.9f;
		/** 0 = legacy course polyline (bit-identical output), 1 = continuous curve (v2 P1). */
		int32_t RoofCurveMode = 0;
		/** Continuous-mode sampling quality; see RoofCurve.h. */
		float RoofChordError = 0.005f;
		float RoofMaxSegment = 0.5f;
	/** 檐下椽飞 style: 0 none, 1 檐椽头, 2 檐椽头 + 飞椽 step (v2 P4). */
		int32_t EaveRafterStyle = 2;
		float TileCourseWidth = 0.34f;
		/** 瓦作 detail level (30_瓦作 §2). 0 keeps the legacy skin bit-identical. */
		int32_t TileDetail = 0;
		/** 泥背层 thickneess, in metres (R17). 0 keeps the skin on the boarding. */
		float TileBeddingThickness = 0.0f;
		/** Distance tier (30_瓦作 §2). 0 = 近景 = the mesh every earlier resource baked. */
		int32_t LODLevel = 0;
		float TileCoverage = 1.0f;
		float RidgeScale = 1.0f;
		/**
		 * 脊断面 profile (50_脊饰 §3). 0 = legacy: the seven-point 当沟条-less section every earlier
		 * resource baked, byte for byte. 1 = 分层: 当沟条 / 脊身 / 盖脊筒 separated by real ledges, so
		 * the three tiers read in silhouette instead of one half-round bar.
		 *
		 * Both levels keep the same soffit (±0.50, -0.10). The main crown stays at 0.75;
		 * detailed verges use 70% of its positive heights. The ridge
		 * covers exactly the ground it covered, which is what the 瓦垄 band is cut against
		 * (RidgeFootHalfWidth) and what the 泥背 lift is measured from.
		 */
		int32_t RidgeDetail = 0;
		/**
		 * 脊饰 (50_脊饰 R1-R4). Off = legacy: no ornament geometry at all.
		 *
		 * This is the manual stand-in for the 等级 gate the handbook wants (R1: 鸱吻 宫殿 only) —
		 * `BuildingTier` does not exist in this tree, so the switch is what decides whether a
		 * building grows 脊饰. A plain dwelling leaves it off, which is the handbook's own answer for
		 * 民居 ("普通建筑是没有脊饰的").
		 */
		bool bRidgeOrnaments = false;
		/**
		 * 正吻 placeholder cube side, in modules D. [自定] No source gives a size (50_脊饰 缺口 2).
		 *
		 * Sized by what the seat rule costs: a piece straddles the crown, so it sits at the height
		 * where the section is as wide as the piece, and the crown above that plane sinks into it.
		 * Above about 0.55 D the 正吻's half width crosses the 盖脊筒's steep base ledge and the sink
		 * jumps from ~20% of the piece to ~45% — the cube would read as half-buried. These three
		 * stay below that, measured against the legacy / tiered sections.
		 */
		float RidgeFinialSize = 0.70f;
		/** 垂兽 placeholder cube side, in modules D. [自定] */
		float RidgeBeastSize = 0.42f;
		/** 走兽 placeholder cube side, in modules D. [自定] */
		float RidgeWalkerSize = 0.26f;
		/** 走兽 per 垂脊 / 戗脊. [自定] The 会典 sequence count is still [待定标] (50_脊饰 R4). */
		int32_t RidgeWalkerCount = 3;
		/** Bitset of ERidgeOrnamentKind: kinds the caller substitutes its own mesh for. */
		uint32_t RidgeOrnamentMeshMask = 0;
		float GableRatio = 0.42f;
		float GableOverhang = 0.0f;
		float RollRadius = 0.0f;
		float FlatTopRatio = 0.45f;
		int32_t Sides = 4;
		float FinialSize = 0.0f;
		float HelmetBulge = 0.55f;
		float PlanApothem = 0.0f;
		float CornerRise = 0.0f;
		float CornerExtend = 0.0f;
		float CornerSpan = 1.0f;

		Color StoneColor;
		Color TimberColor;
		Color PlasterColor;
		Color TileColor;
		Color RidgeColor;
		Color BracketColor;

		/** Per-piece colour variation amplitude. 0.05 官式, ~0.14 茅草, ~0.10 夯土. */
		float ColorMottle = 0.05f;
	};

	/**
	 * Raises a knot polyline along its own surface normals (30_瓦作 R17 高度链).
	 *
	 * For a member that traces a roof face — a 垂脊 down a gable, a 戗脊 down a hip — that is the
	 * direction the tiles below it moved, so the two keep their relative position exactly. Straight
	 * up would under-lift by 1 / cos(slope pitch), which on these roofs is about 1.6.
	 */
	void LiftAlongKnotNormals(std::vector<Vector3>& Knots, float Lift);

	/**
	 * Raises a knot polyline straight up.
	 *
	 * For a line where two faces meet — a 正脊, a 围脊, a 宝顶 base — there is no single face normal
	 * to follow, and the body's own soffit is horizontal anyway. Lifting vertically leaves the band
	 * over which the tiles tuck under it exactly as it was, which is the contact that has to hold.
	 */
	void LiftVertically(std::vector<Vector3>& Knots, float Lift);

	/**
	 * One material slot's geometry: the triangles stamped with that slot, re-indexed against a
	 * vertex table of their own so each surface can be given its own material.
	 *
	 * Vertices are shared within the slot and duplicated across slots — a vertex used by both the
	 * 望板 and the 瓦面 belongs to both surfaces. That is the point of the split: neither surface
	 * depends on the other's vertex table.
	 */
	struct SurfaceData
	{
		EMaterialSlot Slot = EMaterialSlot::Timber;

		std::vector<Vector3> Vertices;
		std::vector<Vector3> Normals;
		std::vector<Vector2> UVs;
		std::vector<Color> Colors;
		std::vector<int32_t> Indices;

		int32_t GetTriangleCount() const { return int32_t(Indices.size() / 3); }
	};

	/**
	 * One 脊饰 the roof layer seated, in world space.
	 *
	 * The geometry layer knows nothing about 鸱吻; it knows a 脊饰 is a block that sits on a ridge
	 * (卡片 R3: 雕件与脊条是分离的支撑关系). So it records where every one of them goes and, unless the
	 * caller said it is supplying its own mesh for that class (Spec.RidgeOrnamentMeshMask), it emits
	 * its own cube as the placeholder. A caller with a real mesh reads the record and places it —
	 * the record is emitted either way, so the cube is a default, not a special case.
	 */
	struct RidgeOrnamentPlacement
	{
		ERidgeOrnamentKind Kind = ERidgeOrnamentKind::Finial;
		/** Centre of the bottom face — the point that rests on the ridge's crown. */
		Vector3 Origin;
		/** Orthonormal frame: +Y is the ridge's up, +Z points out along it, downhill. */
		Vector3 AxisX;
		Vector3 AxisY;
		Vector3 AxisZ;
		/** Side of the placeholder cube, in metres. A caller's mesh is scaled by the same number. */
		float Size = 0.0f;
	};

	/** Growable mesh, vertex colours, one material slot stamped per triangle. */
	struct MeshAccumulator
	{
		std::vector<Vector3> Vertices;
		std::vector<Vector3> Normals;
		std::vector<Vector2> UVs;
		std::vector<Color> Colors;
		std::vector<int32_t> Indices;

		/** Every 脊饰 the roof layer seated, in the order it seated them. */
		std::vector<RidgeOrnamentPlacement> RidgeOrnaments;

		/** Which slot every subsequent Add* call stamps its triangles with. */
		void SetSlot(EMaterialSlot Slot) { CurrentSlot = Slot; }
		EMaterialSlot GetSlot() const { return CurrentSlot; }

		/**
		 * Groups the triangles by slot, in slot order, skipping slots that got none.
		 *
		 * The tables above stay the whole-mesh tables: the geometry regression tests pin
		 * `surface_get_arrays(0)` as the entire mesh and `get_vertex_count()` as its vertex count,
		 * so the single-surface build writes them out verbatim. This is the other view of the same
		 * triangles, for the build that hands each category its own surface.
		 */
		void BuildSurfaces(std::vector<SurfaceData>& Out) const;

		/** How many triangles landed in a slot. Sums to GetTriangleCount() across the enum. */
		int32_t GetSlotTriangleCount(EMaterialSlot Slot) const;

		void AddTriangle(const Vector3& A, const Vector3& B, const Vector3& C, const Color& Tint);
		/** Corners in order; wound so the side facing the computed normal is the front face. */
		void AddQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, const Color& Tint);
		void AddBox(const Vector3& Centre, const Vector3& HalfExtents, const Color& Tint);
		/**
		 * Polygon, fan-triangulated, with an explicit normal.
		 *
		 * Each fan triangle is wound against that normal on its own, so a mildly non-convex outline
		 * such as 山花 — whose 举架 sides cave in — does not lose the half of itself that lies past
		 * the fan's turning point. The outline must still be star-shaped about Points[0].
		 */
		void AddPolygon(const std::vector<Vector3>& Points, const Vector3& Normal, const Color& Tint);
		/** Quad wound so its front face points along DesiredNormal. */
		void AddQuadOriented(
			const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
			const Vector3& DesiredNormal, const Color& Tint);
		/**
		 * Quad with a normal supplied per corner, for surfaces that must shade smoothly across
		 * their facets — the tile skin's 筒瓦 barrels, which are three quads pretending to be a
		 * cylinder. Degenerate quads are dropped, which is what makes the skin's crease pairs free.
		 */
		void AddQuadSmooth(
			const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
			const Vector3& NormalA, const Vector3& NormalB, const Vector3& NormalC, const Vector3& NormalD,
			const Color& Tint);
		void AddSweep(const SweepResult& Sweep, const Color& Tint);
		/**
		 * Box standing on an arbitrary frame, so a piece seated on a sloping ridge is not a box
		 * lying on the world axes. Origin is the centre of the bottom face; the box rises along
		 * AxisY. Same convention as AddPlacedTriangles, which seats a caller's mesh the same way.
		 */
		void AddOrientedBox(const Vector3& Origin, const Vector3& AxisX, const Vector3& AxisY,
			const Vector3& AxisZ, const Vector3& HalfExtents, const Color& Tint);
		/**
		 * External triangle soup placed by an orthonormal frame and uniformly scaled, into the
		 * current slot.
		 *
		 * The soup is seated by its own bounding box: its bottom-face centre is what lands on
		 * Origin, so a caller's mesh stands on the ridge whatever its pivot happens to be.
		 *
		 * UVs may be empty (they are then dropped); the source's own vertex colours are replaced by
		 * Tint, so a caller-supplied 脊饰 takes the same 脊色 the placeholder cube took and no
		 * per-mesh palette leaks into a vertex-coloured build. Normals are re-oriented by the frame.
		 */
		void AddPlacedTriangles(
			const std::vector<Vector3>& SourceVertices, const std::vector<Vector3>& SourceNormals,
			const std::vector<Vector2>& SourceUVs, const std::vector<int32_t>& SourceIndices,
			const Vector3& Origin, const Vector3& AxisX, const Vector3& AxisY, const Vector3& AxisZ,
			float Scale, const Color& Tint);
		/** Tapered shaft; smooth mode has analytic normals, metre-scale UVs and hard end caps. */
		void AddColumn(const Vector3& Base, float Height, float BottomRadius, float TopRadius, int32_t Sides,
			const Color& Tint, bool bSmooth = true, uint32_t ComponentId = 0);
		/** Radius/height profile, smooth around each ring, hard across profile corners. Closed ends. */
		void AddRevolvedProfile(const Vector3& Base, const std::vector<Vector2>& Profile,
			int32_t Sides, const Color& Tint);
		/** Stable for a semantic component, independent of tessellation and emission order. */
		Color ComponentColor(const Color& Tint, uint32_t ComponentId) const;

		/** Per-piece colour variation amplitude. 0 disables it. */
		void SetMottle(float Amount) { MottleAmount = Amount; }

		/**
		 * Deterministic per-component brightness variation, so a material palette (thatched
		 * straw, rammed earth, weathered timber) reads as natural instead of uniform. The hash
		 * seeds on a running piece counter, so building the same spec twice yields the same
		 * mesh. Every component colour lands here, because all Add* paths funnel through the
		 * three colour stores (AddTriangle / AddQuad / AddQuadSmooth).
		 */
		Color MottleColor(const Color& Tint);

		int32_t GetTriangleCount() const { return int32_t(Indices.size() / 3); }

	private:
		/** One stamp per emitted triangle, parallel to Indices / 3. */
		std::vector<uint8_t> TriangleSlots;

		EMaterialSlot CurrentSlot = EMaterialSlot::Timber;

		/**
		 * The only place indices are appended, so the stamps cannot fall out of step with the
		 * index list — one stamp per triangle, always.
		 */
		void PushTriangle(int32_t A, int32_t B, int32_t C);

		float MottleAmount = 0.05f;
		uint32_t PieceCounter = 0;
	};

	// ==================== 脊断面 / 脊饰 (50_脊饰) ====================

	/**
	 * The ridge member's own section at this spec's tier, already multiplied by Scale.
	 *
	 * Every tier of a given RidgeDetail keeps the same soffit (±0.50, -0.10) and the same crown
	 * height (main 0.75, detailed verge 0.525). Coarser tiers preserve the crown seat, so a
	 * tier only ever drops detail: the ridge's ground plan — which the 瓦垄 band is cut against
	 * (RidgeFootHalfWidth) — is identical at every tier, and nothing seated on the crown ever ends up
	 * hovering (SeatRidgeFinials / SeatRidgeBeasts).
	 */
	std::vector<Vector2> RidgeContourFor(const BuildingSpec& Spec, float Scale, bool bVerge = false);
	void ConfigureRidgeSweep(SweepSettings& Settings, const BuildingSpec& Spec, float Scale, bool bVerge = false);


	/**
	 * Colour of a 脊饰 piece: the 脊 colour, lifted, so the piece reads as a separate member sitting
	 * on the ridge rather than as a lump of it. [自定] No source gives a 脊饰 / 脊 colour relation.
	 * A caller replacing the placeholder uses this too, so the swap does not change the palette.
	 */
	Color RidgeOrnamentColor(const BuildingSpec& Spec);

	/**
	 * 正吻 at both ends of a 正脊 (50_脊饰 R1/R2, 卡片 R2).
	 *
	 * The heights are not re-derived here: the caller passes the same knots it just swept, so the
	 * ornament sits on the ridge that was actually built, after every lift the 高度链 applied.
	 */
	void SeatRidgeFinials(MeshAccumulator& Mesh, const BuildingSpec& Spec,
		const std::vector<Vector3>& Knots, float RidgeScale);

	/**
	 * 垂兽 / 戗兽 at the 檐口 ends of a 垂脊 / 戗脊, with its 走兽 row (50_脊饰 R3/R4, J2).
	 *
	 * EaveEnds is a bitset: bit 0 = the first knot is an eave end, bit 1 = the last one. A 卷棚's
	 * verge runs eave to eave over the roll, so it passes both; a ridge that dies into another ridge
	 * (the 歇山's upper 垂脊 into the 戗脊) passes the end it dies at.
	 */
	void SeatRidgeBeasts(MeshAccumulator& Mesh, const BuildingSpec& Spec,
		const std::vector<Vector3>& Knots, float RidgeScale, int32_t EaveEnds);

	/** Shared rectangular/polygonal support. The plinth replaces the bottom of the shaft. */
	void AddBuildingColumn(const BuildingSpec& Spec, MeshAccumulator& Mesh, const Vector3& Base, uint32_t ComponentId);

	/**
	 * The 举架 down-slope profile, from the eave up to the ridge. Each entry is
	 * (horizontal distance from the ridge centreline, height above the roof base). Rise
	 * ratios lerp from shallow at the eave to steep at the ridge, then the whole run is
	 * normalised to the spec's roof height — which is what gives the concave Chinese slope.
	 */
	std::vector<Vector2> BuildRoofProfile(const BuildingSpec& Spec, float HalfSpan);
	/** As BuildRoofProfile, but normalised to an explicit rise instead of Spec.RoofHeight. */
	std::vector<Vector2> BuildRoofProfileScaled(const BuildingSpec& Spec, float HalfSpan, float TargetRise);

	/**
	 * Thickness given to the roof boarding, in world units.
	 *
	 * The boarding started life as a zero-thickness single-sided surface, which meant the roof was
	 * see-through from underneath — you could watch the sky between the rafters, and at grazing
	 * angles see straight through the near slope into the back of the far one. It also left the
	 * eave a knife edge. A real roof is a 望板 deck on rafters, so the fix is to give it depth
	 * rather than to disable backface culling, which would only paper over it and would fight the
	 * planned NPR shading.
	 */
	float GetBoardThickness(const BuildingSpec& Spec);

	/**
	 * Which open edges of a roof panel need closing between the weather face and the soffit.
	 *
	 * Most slopes only have their eave open, but 卷棚's profile runs eave to eave over the roll, so
	 * its last segment's *upper* edge is an eave too.
	 */
	enum class ERoofPanelEdges
	{
		None = 0,
		/** The A-B edge, which is down-slope. */
		Lower = 1,
		/** The D-C edge, which is up-slope. */
		Upper = 2,
	};

	inline ERoofPanelEdges operator|(ERoofPanelEdges Left, ERoofPanelEdges Right)
	{
		return ERoofPanelEdges(int32_t(Left) | int32_t(Right));
	}

	inline bool HasEdge(ERoofPanelEdges Set, ERoofPanelEdges Edge)
	{
		return (int32_t(Set) & int32_t(Edge)) != 0;
	}

	/**
	 * Emits a boarding quad together with its 望板 soffit and the rims that close any open edges.
	 *
	 * The soffit is the same quad pushed back along the surface normal and wound to face the other
	 * way. Corners are passed in the same order as AddQuadOriented: A-B along the lower edge,
	 * D-C along the upper.
	 *
	 * When VertexNormals is non-null it carries one shading normal per corner (A, B, C, D), and the
	 * panel is smooth-shaded with those instead of the facet normal — callers sample the roof curve
	 * at the corner span coordinates so adjacent bands share their boundary values and the 望板
	 * reads as one continuous surface. The single Normal is still used for the winding of the rim
	 * faces and as the soffit normal when VertexNormals is null.
	 */
	void AddRoofPanel(
		MeshAccumulator& Mesh,
		const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
		const Vector3& Normal,
		float Thickness,
		ERoofPanelEdges OpenEdges,
		const Color& Tint,
		const Color& SoffitTint,
		const Vector3* VertexNormals = nullptr);

	struct CornerFlip;

	/**
	 * 檐下椽飞: one shared 檐口断面 under an eave line — a row of rafter heads, each a single
	 * sweep along the eave tangent with a stepped section (lower wide step = 檐椽头, upper
	 * narrow step = 飞椽头; style 1 drops the step to a plain square). Every eave-bearing
	 * builder passes points on its own eave line, so all roofs share the same section,
	 * spacing and hang depth. Points are pre-flip eave positions at RoofBase height, each
	 * paired with a horizontal unit inward direction (the rafter's plan direction); every
	 * knot passes through Flip, so the 翼角起翘 lifts the corner heads with the eave.
	 * Heads hang with the section top kissing the 望板 soffit and pass behind the 连檐
	 * at the eave edge. TangentSlope is the profile's rise per unit plan run at the eave.
	 * The section stands square to the rafter axis (a real 檐椽头 end is cut perpendicular),
	 * so its upper outer corner leans out over the eave line by H * sin(theta).
	 */
	void AddEaveRafterHeads(
		MeshAccumulator& Mesh,
		const BuildingSpec& Spec,
		const std::vector<Vector3>& Points,
		const std::vector<Vector2>& Inward,
		float TangentSlope,
		const CornerFlip* Flip,
		const Color& Tint);

	/**
	 * 翼角起翘. Structurally the corner rafter is longer and tilts up, dragging the eave with
	 * it, so this is modelled as a deformation applied to *every* roof vertex rather than as
	 * special-case corner geometry — boarding, tiles, ridges and the drip course all pass
	 * through it and stay consistent.
	 */
	struct CornerFlip
	{
		float Rise = 0.0f;
		float Extend = 0.0f;
		float Span = 1.0f;
		float HalfWidth = 0.0f;
		float HalfDepth = 0.0f;

		/**
		 * Polygonal plans have their corners at vertex angles rather than at the rectangle
		 * extents, so the weight has to be measured as arc length around the eave instead.
		 */
		bool bPolygonal = false;
		int32_t Sides = 4;
		float Radius = 1.0f;

		/** 1 at the plan corner, falling to 0 Span away along the eave. */
		float Weight(float X, float Z) const;
		Vector3 Apply(const Vector3& Point) const;
	};

	/**
	 * Regular polygon outline. Vertices sit at 2*pi*k/N + pi/N so edges face the axes, which
	 * makes N = 4 an axis-aligned square of half-width Apothem rather than a diamond.
	 */
	std::vector<Vector2> PlanPolygon(float Apothem, int32_t Sides);

	/** Base, body and centralised roof for a polygonal plan. See PolygonalBuilding.cpp. */
	void BuildPolygonalBuilding(const BuildingSpec& Spec, ECentralProfile Profile, MeshAccumulator& OutMesh);

	/** Centralised roof only, so a rectangular plan can also carry a 攒尖. */
	void BuildCentralisedRoof(const BuildingSpec& Spec, ECentralProfile Profile, MeshAccumulator& OutMesh);

	void BuildBuilding(const BuildingSpec& Spec, MeshAccumulator& OutMesh);
} // namespace BuildingGen
