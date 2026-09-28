#pragma once

// 连体: several AncientBuilding wings assembled into one continuous building — 抱厦, 勾连搭,
// 十字脊, 工字, 曲尺 — without a general CSG.
//
// Every wing is generated on its own by BuildBuilding, then the junctions are resolved by one
// rule applied to triangles: a wing may not occupy another wing's *roof shell* (the slab between
// that roof's soffit and its surface) nor its *body* (inside its wall line, below its soffit).
// Where two roofs overlap that leaves whichever is higher — the visible roof is the upper
// envelope of the two — and the line where they are equal is the 窝角沟 (valley), which is traced
// and given a gutter. Under an eave a lower roof is kept, which is what lets a 廊 tuck under a
// hall's eave or a 抱厦's rafters run in to the hall's wall.
//
// Structure is exempt (MeshAccumulator::TAG_FIXED: 台基, 踏步, 柱): a junction never trims it,
// and the wings are laid out so that shared column lines are built once (NoColumnSides).
//
// Deviation from 05 契约 §3.2, recorded: the tile courses here are trimmed after the fact by the
// junction, not laid out by TileLayout against a valley boundary. The gutter covers the cut.

#include "AncientBuilding/BuildingBuilder.h"

#include <cstdint>
#include <vector>

namespace BuildingGen
{
	struct CompoundWing
	{
		BuildingSpec Spec;
		/** Wing origin (its local plan centre at ground level) in the compound frame. */
		Vector3 Offset;
		/** Quarter turns about +Y; 1 turns the wing's local +Z to the compound's +X. */
		int32_t Turns = 0;
	};

	struct CompoundReport
	{
		int32_t TrianglesIn = 0;
		int32_t TrianglesOut = 0;
		int32_t ValleyCount = 0;
		float ValleyLength = 0.0f;
	};

	/**
	 * Height of a wing's roof surface over the plan, sampled from its own boarding and ridge
	 * geometry — so it is exactly the roof that was built, for all nine types, corner flip and
	 * storeys included. Returns a very negative value where the wing has no roof.
	 */
	class RoofHeightField
	{
	public:
		static constexpr float NONE = -1e30f;

		void Build(const MeshAccumulator& Mesh, float MinY, float CellSize);
		float Sample(float X, float Z) const;
		bool IsEmpty() const { return Triangles.empty(); }

		float MinX = 0.0f;
		float MinZ = 0.0f;
		float MaxX = 0.0f;
		float MaxZ = 0.0f;

	private:
		struct FieldTriangle
		{
			Vector3 A;
			Vector3 B;
			Vector3 C;
		};

		std::vector<FieldTriangle> Triangles;
		std::vector<std::vector<int32_t>> Cells;
		int32_t Columns = 0;
		int32_t Rows = 0;
		float Cell = 1.0f;
	};

	/** Transform helpers shared with the node's layout code. */
	Vector3 RotateQuarterTurns(const Vector3& Point, int32_t Turns);

	/**
	 * Builds every wing, resolves the junctions and writes the whole compound to OutMesh.
	 * OutWingMeshes, if given, receives each wing's post-junction geometry separately (tests).
	 * OutFields, if given, receives each wing's roof height field in the compound frame.
	 */
	void BuildCompound(const std::vector<CompoundWing>& Wings, MeshAccumulator& OutMesh,
		std::vector<MeshAccumulator>* OutWingMeshes, std::vector<RoofHeightField>* OutFields,
		CompoundReport* OutReport);
} // namespace BuildingGen
