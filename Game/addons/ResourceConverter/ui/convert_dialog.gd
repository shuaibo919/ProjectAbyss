@tool
extends ConfirmationDialog

const _MeshExtractor = preload("res://addons/ResourceConverter/converter/mesh_extractor.gd")

var _source_paths: PackedStringArray
var _file_list: ItemList
var _output_edit: LineEdit
var _extract_all_check: CheckBox
var _progress_bar: ProgressBar
var _status_label: Label
var _file_dialog: EditorFileDialog


func setup(paths: PackedStringArray) -> void:
	_source_paths = paths
	title = "Convert to .res"
	ok_button_text = "Convert"
	min_size = Vector2i(600, 450)
	confirmed.connect(_on_confirmed)

	var vbox := VBoxContainer.new()
	vbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(vbox)

	# --- File list ---
	var file_label := Label.new()
	file_label.text = "Files to convert (%d):" % _source_paths.size()
	vbox.add_child(file_label)

	_file_list = ItemList.new()
	_file_list.custom_minimum_size = Vector2(0, 150)
	_file_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(_file_list)

	for path in _source_paths:
		_file_list.add_item(path.get_file())

	# --- Output directory ---
	var dir_hbox := HBoxContainer.new()
	vbox.add_child(dir_hbox)

	var dir_label := Label.new()
	dir_label.text = "Output:"
	dir_hbox.add_child(dir_label)

	_output_edit = LineEdit.new()
	_output_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_output_edit.placeholder_text = "Select output directory..."
	if _source_paths.size() > 0:
		_output_edit.text = _source_paths[0].get_base_dir()
	dir_hbox.add_child(_output_edit)

	var browse_btn := Button.new()
	browse_btn.text = "Browse..."
	browse_btn.pressed.connect(_on_browse_pressed)
	dir_hbox.add_child(browse_btn)

	# --- Options ---
	_extract_all_check = CheckBox.new()
	_extract_all_check.text = "Extract all meshes (one .res per MeshInstance3D)"
	vbox.add_child(_extract_all_check)

	vbox.add_child(HSeparator.new())

	# --- Progress ---
	_progress_bar = ProgressBar.new()
	_progress_bar.visible = false
	vbox.add_child(_progress_bar)

	_status_label = Label.new()
	vbox.add_child(_status_label)


func _on_browse_pressed() -> void:
	_file_dialog = EditorFileDialog.new()
	_file_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_DIR
	_file_dialog.access = EditorFileDialog.ACCESS_RESOURCES
	_file_dialog.title = "Select Output Directory"
	_file_dialog.dir_selected.connect(_on_dir_selected)
	add_child(_file_dialog)
	_file_dialog.popup_centered(Vector2i(600, 400))


func _on_dir_selected(dir: String) -> void:
	_output_edit.text = dir
	_file_dialog.queue_free()


func _on_confirmed() -> void:
	var output_dir: String = _output_edit.text.strip_edges()
	if output_dir.is_empty():
		_status_label.text = "Error: no output directory specified"
		return

	var abs_dir := ProjectSettings.globalize_path(output_dir)
	if not DirAccess.dir_exists_absolute(abs_dir):
		DirAccess.make_dir_recursive_absolute(abs_dir)

	var extract_all: bool = _extract_all_check.button_pressed
	var total := _source_paths.size()
	var converted := 0
	var skipped := 0
	var failed := 0

	_progress_bar.visible = true
	_progress_bar.max_value = total
	_progress_bar.value = 0

	for i in range(total):
		var src_path: String = _source_paths[i]
		var base_name: String = src_path.get_file().get_basename()

		var meshes: Array[Mesh] = _MeshExtractor.extract_meshes(src_path, extract_all)
		if meshes.is_empty():
			push_warning("ResourceConverter: No mesh found in: " + src_path)
			failed += 1
			_progress_bar.value = i + 1
			continue

		for j in range(meshes.size()):
			var mesh: Mesh = meshes[j]
			var file_name: String
			if meshes.size() == 1:
				file_name = base_name + ".res"
			else:
				file_name = base_name + "_%d.res" % j

			var out_path: String = output_dir.path_join(file_name)

			if ResourceLoader.exists(out_path):
				push_warning("ResourceConverter: Skipped (exists): " + out_path)
				skipped += 1
				continue

			var err := _MeshExtractor.save_mesh_as_res(mesh, out_path)
			if err == OK:
				converted += 1
			else:
				push_error("ResourceConverter: Failed to save: " + out_path + " error=" + str(err))
				failed += 1

		_progress_bar.value = i + 1

	EditorInterface.get_resource_filesystem().scan_sources()
	_status_label.text = "Done: %d converted, %d skipped, %d failed" % [converted, skipped, failed]
