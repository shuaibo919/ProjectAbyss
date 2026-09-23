#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

// Builds the baked line mesh for the mesh-edge cartoon outline (a port of
// UnitySimpleCartoonLine's "degraded rectangle" pipeline — see
// Reference/UnitySimpleCartoonLine). The Unity original feeds edge adjacency to a
// geometry shader via DrawProcedural; Godot has no geometry shaders, so the edge
// data is baked instead: six vertices (one quad) per unique mesh edge, with the
// edge endpoints and both adjacent-face third vertices packed into CUSTOM0-3.
// ink_mesh_edge_outline.gdshader does the silhouette/crease classification and the
// clip-space quad expansion in vertex().
class CartoonOutlineBuilder : public Object {
	GDCLASS(CartoonOutlineBuilder, Object);

protected:
	static void _bind_methods();

public:
	// Returns one surface, six non-indexed vertices per edge, POSITION set to the
	// corner's own endpoint so the AABB matches the source mesh. Empty mesh if the
	// source has no triangle surfaces.
	static Ref<ArrayMesh> BuildOutlineMesh(const Ref<Mesh>& p_Source);
};

} // namespace godot
