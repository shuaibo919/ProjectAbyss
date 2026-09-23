#include "CartoonOutlineBuilder.h"

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <vector>

using namespace godot;

namespace {

// One unique mesh edge (UnitySimpleCartoonLine's "degraded rectangle"): the two
// endpoints plus the third vertex of each adjacent triangle. Ids are canonical
// (welded) vertex ids used for edge identity; positions are what the shader needs.
struct OutlineEdge {
	Vector3 VertexA;
	Vector3 VertexB;
	Vector3 FaceA;
	Vector3 FaceB;
	int32_t FaceAId = -1;
	bool bHasFaceB = false;
};

struct EdgeAccumulator {
	// Positions double as weld keys: Godot duplicates vertices at UV/hard-normal
	// seams, and an edge split by a seam must still read as ONE edge with two
	// adjacent faces — otherwise every seam would draw as a spurious boundary line.
	// Exact float compare is enough: duplicates from import/baking are bit-identical.
	HashMap<Vector3, int32_t> IdsByPosition;
	HashMap<uint64_t, int32_t> EdgeByKey;
	std::vector<OutlineEdge> Edges;
	int32_t NextId = 0;
};

int32_t CanonicalId(EdgeAccumulator& p_Acc, const Vector3& p_Pos) {
	if (const int32_t* Found = p_Acc.IdsByPosition.getptr(p_Pos)) {
		return *Found;
	}
	const int32_t Id = p_Acc.NextId++;
	p_Acc.IdsByPosition.insert(p_Pos, Id);
	return Id;
}

void AddEdge(EdgeAccumulator& p_Acc, const Vector3& p_A, const Vector3& p_B, const Vector3& p_Opposite) {
	const int32_t IdA = CanonicalId(p_Acc, p_A);
	const int32_t IdB = CanonicalId(p_Acc, p_B);
	if (IdA == IdB) {
		return; // Degenerate triangle corner.
	}
	const int32_t IdOpposite = CanonicalId(p_Acc, p_Opposite);

	const uint32_t Lo = uint32_t(IdA < IdB ? IdA : IdB);
	const uint32_t Hi = uint32_t(IdA < IdB ? IdB : IdA);
	const uint64_t Key = (uint64_t(Lo) << 32) | uint64_t(Hi);

	if (const int32_t* Found = p_Acc.EdgeByKey.getptr(Key)) {
		OutlineEdge& Edge = p_Acc.Edges[*Found];
		// Third vertex of the second adjacent triangle. Extra faces on a
		// non-manifold edge are dropped — two faces are all the test can use.
		if (!Edge.bHasFaceB && IdOpposite != Edge.FaceAId) {
			Edge.FaceB = p_Opposite;
			Edge.bHasFaceB = true;
		}
		return;
	}

	OutlineEdge Edge;
	Edge.VertexA = p_A;
	Edge.VertexB = p_B;
	Edge.FaceA = p_Opposite;
	Edge.FaceAId = IdOpposite;
	p_Acc.EdgeByKey.insert(Key, int32_t(p_Acc.Edges.size()));
	p_Acc.Edges.push_back(Edge);
}

void AddTriangle(EdgeAccumulator& p_Acc, const Vector3& p_A, const Vector3& p_B, const Vector3& p_C) {
	AddEdge(p_Acc, p_A, p_B, p_C);
	AddEdge(p_Acc, p_B, p_C, p_A);
	AddEdge(p_Acc, p_C, p_A, p_B);
}

// RGBA_FLOAT custom attributes take a flat PackedFloat32Array (4 floats per
// vertex), not a PackedColorArray.
void EmitCorner(PackedVector3Array& p_Positions, PackedFloat32Array& p_Custom0, PackedFloat32Array& p_Custom1,
		PackedFloat32Array& p_Custom2, PackedFloat32Array& p_Custom3,
		const OutlineEdge& p_Edge, float p_EndT, float p_Side) {
	const Vector3& Own = p_EndT < 0.5f ? p_Edge.VertexA : p_Edge.VertexB;
	p_Positions.push_back(Own);
	p_Custom0.append_array({ p_Edge.VertexA.x, p_Edge.VertexA.y, p_Edge.VertexA.z, p_EndT });
	p_Custom1.append_array({ p_Edge.VertexB.x, p_Edge.VertexB.y, p_Edge.VertexB.z, p_Side });
	p_Custom2.append_array({ p_Edge.FaceA.x, p_Edge.FaceA.y, p_Edge.FaceA.z, p_Edge.bHasFaceB ? 1.0f : 0.0f });
	const Vector3& FaceB = p_Edge.bHasFaceB ? p_Edge.FaceB : p_Edge.FaceA;
	p_Custom3.append_array({ FaceB.x, FaceB.y, FaceB.z, 0.0f });
}

} // namespace

Ref<ArrayMesh> CartoonOutlineBuilder::BuildOutlineMesh(const Ref<Mesh>& p_Source) {
	EdgeAccumulator Acc;

	if (p_Source.is_valid()) {
		const int32_t SurfaceCount = p_Source->get_surface_count();
		for (int32_t Surf = 0; Surf < SurfaceCount; ++Surf) {
			// No primitive-type check (Mesh doesn't expose one): every mesh we bake
			// outlines for is triangles, and a non-triangle surface would just read
			// its index buffer in triples.
			const Array Arrays = p_Source->surface_get_arrays(Surf);
			if (Arrays.size() <= Mesh::ARRAY_VERTEX || Arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY) {
				continue;
			}
			const PackedVector3Array Vertices = Arrays[Mesh::ARRAY_VERTEX];
			if (Vertices.size() < 3) {
				continue;
			}

			PackedInt32Array Indices;
			if (Arrays.size() > Mesh::ARRAY_INDEX && Arrays[Mesh::ARRAY_INDEX].get_type() == Variant::PACKED_INT32_ARRAY) {
				Indices = Arrays[Mesh::ARRAY_INDEX];
			}

			if (Indices.is_empty()) {
				for (int32_t i = 0; i + 2 < Vertices.size(); i += 3) {
					AddTriangle(Acc, Vertices[i], Vertices[i + 1], Vertices[i + 2]);
				}
			} else {
				const int32_t VertexCount = Vertices.size();
				for (int32_t i = 0; i + 2 < Indices.size(); i += 3) {
					const int32_t I0 = Indices[i];
					const int32_t I1 = Indices[i + 1];
					const int32_t I2 = Indices[i + 2];
					if (I0 < 0 || I0 >= VertexCount || I1 < 0 || I1 >= VertexCount || I2 < 0 || I2 >= VertexCount) {
						continue;
					}
					AddTriangle(Acc, Vertices[I0], Vertices[I1], Vertices[I2]);
				}
			}
		}
	}

	// Six vertices per edge: two triangles spanning the quad that vertex() later
	// expands in clip space. Order matches the Unity geometry shader's output
	// (v0,v3,v2 / v0,v1,v3); the shader runs cull_disabled so winding is cosmetic.
	PackedVector3Array Positions;
	PackedFloat32Array Custom0; // VertexA xyz, end_t
	PackedFloat32Array Custom1; // VertexB xyz, side
	PackedFloat32Array Custom2; // FaceA third vertex xyz, has_face_b
	PackedFloat32Array Custom3; // FaceB third vertex xyz, unused

	// EmitCorner appends; no reserve on godot-cpp packed arrays.

	for (const OutlineEdge& Edge : Acc.Edges) {
		EmitCorner(Positions, Custom0, Custom1, Custom2, Custom3, Edge, 0.0f, 1.0f);
		EmitCorner(Positions, Custom0, Custom1, Custom2, Custom3, Edge, 1.0f, -1.0f);
		EmitCorner(Positions, Custom0, Custom1, Custom2, Custom3, Edge, 1.0f, 1.0f);
		EmitCorner(Positions, Custom0, Custom1, Custom2, Custom3, Edge, 0.0f, 1.0f);
		EmitCorner(Positions, Custom0, Custom1, Custom2, Custom3, Edge, 0.0f, -1.0f);
		EmitCorner(Positions, Custom0, Custom1, Custom2, Custom3, Edge, 1.0f, -1.0f);
	}

	Ref<ArrayMesh> Result(memnew(ArrayMesh));
	if (Positions.is_empty()) {
		return Result;
	}

	Array Arrays;
	Arrays.resize(Mesh::ARRAY_MAX);
	Arrays[Mesh::ARRAY_VERTEX] = Positions;
	Arrays[Mesh::ARRAY_CUSTOM0] = Custom0;
	Arrays[Mesh::ARRAY_CUSTOM1] = Custom1;
	Arrays[Mesh::ARRAY_CUSTOM2] = Custom2;
	Arrays[Mesh::ARRAY_CUSTOM3] = Custom3;

	// Custom attributes default to RGBA8_UNORM (clamped 0..1); positions need full
	// float precision, so every channel is declared RGBA_FLOAT explicitly.
	int64_t FormatFlags = Mesh::ARRAY_FORMAT_CUSTOM0 | Mesh::ARRAY_FORMAT_CUSTOM1 |
			Mesh::ARRAY_FORMAT_CUSTOM2 | Mesh::ARRAY_FORMAT_CUSTOM3;
	FormatFlags |= int64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
	FormatFlags |= int64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT;
	FormatFlags |= int64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT;
	FormatFlags |= int64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM3_SHIFT;

	Result->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays, Array(), Dictionary(),
			BitField<Mesh::ArrayFormat>(FormatFlags));
	return Result;
}

void CartoonOutlineBuilder::_bind_methods() {
	ClassDB::bind_static_method("CartoonOutlineBuilder", D_METHOD("build_outline_mesh", "source"),
			&CartoonOutlineBuilder::BuildOutlineMesh);
}
