extends Node

func _ready() -> void:
	setup_defaults()

func setup_defaults() -> void:
	_add_key_action("camera_yaw_left", [KEY_Q])
	_add_key_action("camera_yaw_right", [KEY_E])
	_add_key_action("camera_pitch_up", [KEY_R])
	_add_key_action("camera_pitch_down", [KEY_F])

	_add_key_action("move_forward", [KEY_W, KEY_UP])
	_add_key_action("move_back", [KEY_S, KEY_DOWN])
	_add_key_action("move_left", [KEY_A, KEY_LEFT])
	_add_key_action("move_right", [KEY_D, KEY_RIGHT])

	_add_key_action("jump", [KEY_SPACE])

	_add_mouse_action("camera_orbit", [MOUSE_BUTTON_RIGHT])
	_add_mouse_action("camera_zoom_in", [MOUSE_BUTTON_WHEEL_UP])
	_add_mouse_action("camera_zoom_out", [MOUSE_BUTTON_WHEEL_DOWN])

func _add_key_action(action_name: StringName, keycodes: Array) -> void:
	_reset_action(action_name)
	for keycode in keycodes:
		var event = InputEventKey.new()
		event.keycode = keycode
		InputMap.action_add_event(action_name, event)

func _add_mouse_action(action_name: StringName, buttons: Array) -> void:
	_reset_action(action_name)
	for button in buttons:
		var event = InputEventMouseButton.new()
		event.button_index = button
		InputMap.action_add_event(action_name, event)

func _reset_action(action_name: StringName) -> void:
	if InputMap.has_action(action_name):
		InputMap.erase_action(action_name)
	InputMap.add_action(action_name, 0.5)
