@tool
class_name MeshExtractor
extends RefCounted


static func extract_meshes(res_path: String, extract_all: bool = false) -> Array[Mesh]:
	var results: Array[Mesh] = []
	if not ResourceLoader.exists(res_path):
		push_error("ResourceConverter: Resource not found: " + res_path)
		return results

	var resource: Resource = ResourceLoader.load(res_path, "", ResourceLoader.CACHE_MODE_REUSE)
	if resource == null:
		push_error("ResourceConverter: Failed to load: " + res_path)
		return results

	if resource is Mesh:
		var mesh := (resource as Mesh).duplicate() as Mesh
		strip_materials(mesh)
		results.append(mesh)
		return results

	if resource is PackedScene:
		var instance := (resource as PackedScene).instantiate()
		if instance == null:
			return results
		if extract_all:
			_find_all_meshes(instance, results)
		else:
			var mesh := _find_first_mesh(instance)
			if mesh != null:
				var dup := mesh.duplicate() as Mesh
				strip_materials(dup)
				results.append(dup)
		instance.free()

	return results


static func strip_materials(mesh: Mesh) -> void:
	if mesh is ArrayMesh:
		for i in range(mesh.get_surface_count()):
			mesh.surface_set_material(i, null)


static func save_mesh_as_res(mesh: Mesh, output_path: String) -> Error:
	return ResourceSaver.save(mesh, output_path)


static func _find_first_mesh(node: Node) -> Mesh:
	if node is MeshInstance3D and (node as MeshInstance3D).mesh != null:
		return (node as MeshInstance3D).mesh
	for child in node.get_children():
		var found := _find_first_mesh(child)
		if found != null:
			return found
	return null


static func _find_all_meshes(node: Node, results: Array[Mesh]) -> void:
	if node is MeshInstance3D and (node as MeshInstance3D).mesh != null:
		var dup := (node as MeshInstance3D).mesh.duplicate() as Mesh
		strip_materials(dup)
		results.append(dup)
	for child in node.get_children():
		_find_all_meshes(child, results)
