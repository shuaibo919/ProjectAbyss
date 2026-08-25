#pragma once

// Turns a TreeSkeleton into a renderable ArrayMesh.
//
// The GPU sample rasterises stems as camera-facing, single-sided strips and carves leaf
// silhouettes out of triangles with pixel-shader discards. Neither survives baking, so
// here stems become closed tubes and leaf outlines are tessellated from the same
// quadratic Bezier arcs the original fed to its discard test. Bark cracks, lichen and
// snow are evaluated per vertex and baked into vertex colours, which is what keeps the
// result texture-free.

#include "TreeGen/TreeSkeleton.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <algorithm>

namespace TreeGen
{
	/** Tessellation density. Every field trades triangle count for silhouette fidelity. */
	struct MeshQuality
	{
		/** Vertices around the trunk's circumference. Deeper levels get proportionally fewer. */
		int32_t RadialSegments = 12;
		/** Extra rings inserted within each skeleton segment, for smoother curves. */
		int32_t RingsPerSegment = 1;
		/** Samples per leaf outline arc; a leaf outline is four arcs per side. */
		int32_t LeafArcSegments = 2;
		int32_t FruitLongitudes = 12;
		int32_t FruitBands = 6;
		/** Displace bark by the procedural crack/cloud noise and bake its tint. */
		bool bBarkDetail = true;
		bool bGenerateLeaves = true;
		bool bGenerateFruit = true;

		/**
		 * Twigs are thin and always seen small, so halving the radial count per level tracks
		 * their on-screen size closely enough while cutting most of the bark triangle budget.
		 */
		int32_t GetRadialSegmentsForLevel(int32_t Level) const
		{
			return std::max(3, RadialSegments >> Level);
		}
	};

	class TreeMeshBuilder
	{
	public:
		void Build(
			const TreeSkeleton& Skeleton,
			const TreeParams& InParams,
			const GenerationContext& InContext,
			const MeshQuality& InQuality);

		/**
		 * Surfaces are always added in the order bark, foliage, fruit, and empty ones are
		 * skipped. Use GetSurfaceKind() to find out what a given surface index holds.
		 */
		godot::Ref<godot::ArrayMesh> CreateMesh() const;

		enum ESurfaceKind
		{
			SURFACE_BARK,
			SURFACE_FOLIAGE,
			SURFACE_FRUIT,
		};

		/** Surface kinds in the order CreateMesh() emitted them. */
		const std::vector<ESurfaceKind>& GetSurfaceKinds() const { return SurfaceKinds; }

		int32_t GetVertexCount() const;
		int32_t GetTriangleCount() const;

	private:
		struct Surface
		{
			std::vector<Vector3> Vertices;
			std::vector<Vector3> Normals;
			std::vector<godot::Vector2> UVs;
			std::vector<Color> Colors;
			std::vector<int32_t> Indices;

			void Clear();
			bool IsEmpty() const { return Indices.empty(); }
			void Reserve(size_t VertexCount, size_t IndexCount);
		};

		/** Evaluated bark surface point plus the shading inputs derived along the way. */
		struct BarkSample
		{
			Vector3 Position;
			Vector3 RadialDirection;
			float Bump = 0.0f;
			float Cloud = 0.0f;
			godot::Vector2 NoiseUV;
		};

		BarkSample SampleBark(const StemSegment& Segment, float Theta, float V) const;
		Color ShadeBark(const StemSegment& Segment, const BarkSample& Sample, float V) const;

		void BuildBark(const std::vector<StemSegment>& Segments);
		void AddStemBaseCap(const StemSegment& Segment, int32_t Radial);
		void BuildFoliage(const std::vector<LeafInstance>& Leaves);
		void BuildLeaf(const LeafInstance& Instance, const LeafParams& LeafP);
		void BuildNeedleLeaf(const LeafInstance& Instance, const LeafParams& LeafP);
		void BuildFruit(const std::vector<LeafInstance>& Fruits);

		godot::Array ToArrays(const Surface& InSurface) const;

		TreeParams Params;
		GenerationContext Context;
		MeshQuality Quality;

		Surface Bark;
		Surface Foliage;
		Surface Fruit;

		std::vector<ESurfaceKind> SurfaceKinds;
	};
} // namespace TreeGen
