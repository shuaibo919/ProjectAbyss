@tool
extends EditorContextMenuPlugin

signal convert_requested(paths: PackedStringArray)
signal rename_requested(paths: PackedStringArray)

const MESH_EXTENSIONS: PackedStringArray = ["glb", "fbx", "gltf", "obj"]


func _popup_menu(paths: PackedStringArray) -> void:
	var has_mesh := false
	for path in paths:
		if path.get_extension().to_lower() in MESH_EXTENSIONS:
			has_mesh = true
			break

	if has_mesh:
		add_context_menu_item("Convert to .res (mesh only)", _on_convert)

	if paths.size() >= 1:
		add_context_menu_item("Batch Rename...", _on_rename)


func _on_convert(paths: PackedStringArray) -> void:
	# Filter to mesh files only
	var mesh_paths: PackedStringArray = []
	for path in paths:
		if path.get_extension().to_lower() in MESH_EXTENSIONS:
			mesh_paths.append(path)
	convert_requested.emit(mesh_paths)


func _on_rename(paths: PackedStringArray) -> void:
	rename_requested.emit(paths)
