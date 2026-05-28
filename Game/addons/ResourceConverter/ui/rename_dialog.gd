@tool
extends ConfirmationDialog

var _source_paths: PackedStringArray
var _manual_overrides: Dictionary = {}  # index -> String

# Rule controls
var _prefix_edit: LineEdit
var _suffix_edit: LineEdit
var _find_edit: LineEdit
var _replace_edit: LineEdit
var _seq_enabled: CheckBox
var _seq_start: SpinBox
var _seq_padding: SpinBox

# Preview
var _tree: Tree
var _status_label: Label


func setup(paths: PackedStringArray) -> void:
	_source_paths = paths
	title = "Batch Rename"
	ok_button_text = "Rename"
	min_size = Vector2i(700, 500)
	confirmed.connect(_on_confirmed)

	var vbox := VBoxContainer.new()
	vbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(vbox)

	# --- Rules ---
	var rule_label := Label.new()
	rule_label.text = "Rename Rules:"
	vbox.add_child(rule_label)

	var grid := GridContainer.new()
	grid.columns = 4
	vbox.add_child(grid)

	grid.add_child(_make_label("Prefix:"))
	_prefix_edit = LineEdit.new()
	_prefix_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_prefix_edit.text_changed.connect(func(_t): _refresh_preview())
	grid.add_child(_prefix_edit)

	grid.add_child(_make_label("Suffix:"))
	_suffix_edit = LineEdit.new()
	_suffix_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_suffix_edit.text_changed.connect(func(_t): _refresh_preview())
	grid.add_child(_suffix_edit)

	grid.add_child(_make_label("Find:"))
	_find_edit = LineEdit.new()
	_find_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_find_edit.text_changed.connect(func(_t): _refresh_preview())
	grid.add_child(_find_edit)

	grid.add_child(_make_label("Replace:"))
	_replace_edit = LineEdit.new()
	_replace_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_replace_edit.text_changed.connect(func(_t): _refresh_preview())
	grid.add_child(_replace_edit)

	# Sequential numbering
	var seq_hbox := HBoxContainer.new()
	vbox.add_child(seq_hbox)

	_seq_enabled = CheckBox.new()
	_seq_enabled.text = "Sequential #"
	_seq_enabled.toggled.connect(func(_v): _refresh_preview())
	seq_hbox.add_child(_seq_enabled)

	seq_hbox.add_child(_make_label("  Start:"))
	_seq_start = SpinBox.new()
	_seq_start.min_value = 0
	_seq_start.max_value = 9999
	_seq_start.value = 1
	_seq_start.value_changed.connect(func(_v): _refresh_preview())
	seq_hbox.add_child(_seq_start)

	seq_hbox.add_child(_make_label("  Digits:"))
	_seq_padding = SpinBox.new()
	_seq_padding.min_value = 1
	_seq_padding.max_value = 6
	_seq_padding.value = 2
	_seq_padding.value_changed.connect(func(_v): _refresh_preview())
	seq_hbox.add_child(_seq_padding)

	vbox.add_child(HSeparator.new())

	# --- Preview ---
	var preview_label := Label.new()
	preview_label.text = "Preview (double-click New Name to edit):"
	vbox.add_child(preview_label)

	_tree = Tree.new()
	_tree.columns = 2
	_tree.set_column_title(0, "Original")
	_tree.set_column_title(1, "New Name")
	_tree.set_column_titles_visible(true)
	_tree.set_column_expand(0, true)
	_tree.set_column_expand(1, true)
	_tree.custom_minimum_size = Vector2(0, 150)
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.item_edited.connect(_on_item_edited)
	vbox.add_child(_tree)

	_status_label = Label.new()
	vbox.add_child(_status_label)

	_refresh_preview()


func _make_label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	return l


func _compute_new_name(index: int) -> String:
	if _manual_overrides.has(index):
		return _manual_overrides[index]

	var original: String = _source_paths[index].get_file()
	var base: String = original.get_basename()
	var ext: String = original.get_extension()

	var find_str: String = _find_edit.text
	if not find_str.is_empty():
		base = base.replace(find_str, _replace_edit.text)

	base = _prefix_edit.text + base + _suffix_edit.text

	if _seq_enabled.button_pressed:
		var num: int = int(_seq_start.value) + index
		var padding: int = int(_seq_padding.value)
		var num_str: String = str(num)
		while num_str.length() < padding:
			num_str = "0" + num_str
		base = base + "_" + num_str

	if ext.is_empty():
		return base
	return base + "." + ext


func _refresh_preview() -> void:
	_tree.clear()
	var root := _tree.create_item()
	var new_names: PackedStringArray = []

	for i in range(_source_paths.size()):
		new_names.append(_compute_new_name(i))

	var name_count: Dictionary = {}
	for n in new_names:
		name_count[n] = name_count.get(n, 0) + 1

	for i in range(_source_paths.size()):
		var item := _tree.create_item(root)
		item.set_text(0, _source_paths[i].get_file())
		item.set_text(1, new_names[i])
		item.set_editable(1, true)
		item.set_metadata(0, i)

		if name_count.get(new_names[i], 0) > 1:
			item.set_custom_color(1, Color.RED)

	var ext_conflicts := 0
	for i in range(_source_paths.size()):
		var new_path: String = _source_paths[i].get_base_dir().path_join(new_names[i])
		if new_path != _source_paths[i] and FileAccess.file_exists(ProjectSettings.globalize_path(new_path)):
			ext_conflicts += 1

	if ext_conflicts > 0:
		_status_label.text = "%d file(s) conflict with existing files" % ext_conflicts
	else:
		_status_label.text = "%d file(s) to rename" % _source_paths.size()


func _on_item_edited() -> void:
	var edited := _tree.get_edited()
	if edited == null:
		return
	var index: int = edited.get_metadata(0)
	var new_text: String = edited.get_text(1)

	if new_text.strip_edges().is_empty():
		_manual_overrides.erase(index)
	else:
		_manual_overrides[index] = new_text
	_refresh_preview()


func _on_confirmed() -> void:
	var renamed := 0
	var skipped := 0

	for i in range(_source_paths.size()):
		var old_path: String = _source_paths[i]
		var new_name: String = _compute_new_name(i)
		var new_path: String = old_path.get_base_dir().path_join(new_name)

		if old_path == new_path:
			continue

		var abs_old := ProjectSettings.globalize_path(old_path)
		var abs_new := ProjectSettings.globalize_path(new_path)

		if FileAccess.file_exists(abs_new):
			push_warning("ResourceConverter: Rename skipped (exists): " + new_path)
			skipped += 1
			continue

		var err := DirAccess.rename_absolute(abs_old, abs_new)
		if err != OK:
			push_error("ResourceConverter: Rename failed: " + old_path + " -> " + new_path)
			skipped += 1
			continue

		var import_old := abs_old + ".import"
		var import_new := abs_new + ".import"
		if FileAccess.file_exists(import_old):
			DirAccess.rename_absolute(import_old, import_new)

		renamed += 1

	if renamed > 0:
		EditorInterface.get_resource_filesystem().scan_sources()

	print("ResourceConverter: Renamed %d, skipped %d" % [renamed, skipped])
