@tool
extends EditorPlugin

const _ContextMenu = preload("res://addons/ResourceConverter/context_menu/filesystem_context_menu.gd")
const _ConvertDialog = preload("res://addons/ResourceConverter/ui/convert_dialog.gd")
const _RenameDialog = preload("res://addons/ResourceConverter/ui/rename_dialog.gd")

var _context_menu: EditorContextMenuPlugin
var _convert_dialog: ConfirmationDialog
var _rename_dialog: ConfirmationDialog


func _enter_tree() -> void:
	_context_menu = _ContextMenu.new()
	_context_menu.convert_requested.connect(_on_convert_requested)
	_context_menu.rename_requested.connect(_on_rename_requested)
	add_context_menu_plugin(EditorContextMenuPlugin.CONTEXT_SLOT_FILESYSTEM, _context_menu)


func _exit_tree() -> void:
	remove_context_menu_plugin(_context_menu)
	_context_menu = null
	if is_instance_valid(_convert_dialog):
		_convert_dialog.queue_free()
	if is_instance_valid(_rename_dialog):
		_rename_dialog.queue_free()


func _on_convert_requested(paths: PackedStringArray) -> void:
	if is_instance_valid(_convert_dialog):
		_convert_dialog.queue_free()
	_convert_dialog = _ConvertDialog.new()
	_convert_dialog.setup(paths)
	EditorInterface.get_base_control().add_child(_convert_dialog)
	_convert_dialog.popup_centered(Vector2i(600, 450))


func _on_rename_requested(paths: PackedStringArray) -> void:
	if is_instance_valid(_rename_dialog):
		_rename_dialog.queue_free()
	_rename_dialog = _RenameDialog.new()
	_rename_dialog.setup(paths)
	EditorInterface.get_base_control().add_child(_rename_dialog)
	_rename_dialog.popup_centered(Vector2i(700, 500))
