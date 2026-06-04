extends Node

## Auto-registers Input Map actions for the camera system + whitebox player.
## Add this as an Autoload in Project Settings (name: InputActions).
##
## To customise bindings, edit the _default_actions dict below.
## C++ classes reference actions by name — no keycodes are hardcoded.

const _default_actions := {
	# --- camera orbit / zoom ---
	"camera_orbit":    [MOUSE_BUTTON_RIGHT],
	"camera_zoom_in":  [MOUSE_BUTTON_WHEEL_UP],
	"camera_zoom_out": [MOUSE_BUTTON_WHEEL_DOWN],

	# --- player movement ---
	"move_forward": [KEY_W, KEY_UP],
	"move_back":    [KEY_S, KEY_DOWN],
	"move_left":    [KEY_A, KEY_LEFT],
	"move_right":   [KEY_D, KEY_RIGHT],
}


func _ready() -> void:
	for action_name in _default_actions:
		if InputMap.has_action(action_name):
			continue
		InputMap.add_action(action_name)
		for raw_key in _default_actions[action_name]:
			var ev := InputEventKey.new() if raw_key is Key else null
			if ev == null:
				var mb := InputEventMouseButton.new()
				mb.button_index = raw_key
				InputMap.action_add_event(action_name, mb)
			else:
				ev.keycode = raw_key
				InputMap.action_add_event(action_name, ev)
