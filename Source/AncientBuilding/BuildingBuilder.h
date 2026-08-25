#pragma once

// Assembles a whole building from the Table 1 frame (Docs/AncientBuilding_Spec.md §8).
//
// Where the paper's section 3.1 sweep earns its keep it is used: ridges, eave courses, tile
// courses and the stepped stair blocks all go through BuildSweep rather than being hand-built
// from boxes. Everything genuinely prismatic — platform, columns, walls — is a box, because
// dressing those up as sweeps would buy nothing.
//
// Output is a single vertex-coloured surface, matching the contract PcgVillageMeshes already
// established for this project: no textures, one material, nothing baked into the scene.

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
		bool bGenerateFence = true;
		bool bGenerateSteps = true;
		float FenceHeight = 0.95f;
		float FenceGapWidth = 0.0f;
		int32_t StepRunCount = 2;
		int32_t StepCount = 5;
		float StepRunDepth = 0.0f;

		// Body
		bool bGenerateColumns = true;
		bool bGenerateWalls = true;
		float ColumnRadius = 0.0f;
		int32_t ColumnSides = 10;
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
		float TileCourseWidth = 0.34f;
		float TileCoverage = 1.0f;
		float RidgeScale = 1.0f;
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
	};

	/** Growable mesh, one surface, vertex colours. */
	struct MeshAccumulator
	{
		std::vector<Vector3> Vertices;
		std::vector<Vector3> Normals;
		std::vector<Vector2> UVs;
		std::vector<Color> Colors;
		std::vector<int32_t> Indices;

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
		/** Tapered prism about a vertical axis; used for columns. */
		void AddColumn(const Vector3& Base, float Height, float BottomRadius, float TopRadius, int32_t Sides, const Color& Tint);

		int32_t GetTriangleCount() const { return int32_t(Indices.size() / 3); }
	};

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
	 */
	void AddRoofPanel(
		MeshAccumulator& Mesh,
		const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
		const Vector3& Normal,
		float Thickness,
		ERoofPanelEdges OpenEdges,
		const Color& Tint,
		const Color& SoffitTint);

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
