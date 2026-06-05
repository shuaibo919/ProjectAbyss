#include "InputMapping.hpp"

#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

InputMapping::InputMapping() {}
InputMapping::~InputMapping() {}

void InputMapping::_bind_methods() {
	ClassDB::bind_static_method("InputMapping", D_METHOD("SetupDefaults"), &InputMapping::SetupDefaults);
	ClassDB::bind_static_method("InputMapping", D_METHOD("AddAction", "name", "events"), &InputMapping::AddAction);
	ClassDB::bind_static_method("InputMapping", D_METHOD("RemoveAction", "name"), &InputMapping::RemoveAction);
}

InputEvent* InputMapping::_MakeKey(Key p_key) {
	InputEventKey* ev = memnew(InputEventKey);
	ev->set_keycode(p_key);
	ev->set_pressed(true);
	return ev;
}

InputEvent* InputMapping::_MakeMouseButton(MouseButton p_button) {
	InputEventMouseButton* ev = memnew(InputEventMouseButton);
	ev->set_button_index(p_button);
	ev->set_pressed(true);
	return ev;
}

void InputMapping::AddAction(const StringName& p_name, const TypedArray<InputEvent>& p_events) {
	InputMap* im = InputMap::get_singleton();
	if (im == nullptr) return;

	if (!im->has_action(p_name)) {
		im->add_action(p_name);
	}
	for (int i = 0; i < p_events.size(); i++) {
		Ref<InputEvent> ev = p_events[i];
		if (ev.is_valid()) {
			im->action_add_event(p_name, ev);
		}
	}
}

void InputMapping::RemoveAction(const StringName& p_name) {
	InputMap* im = InputMap::get_singleton();
	if (im != nullptr && im->has_action(p_name)) {
		im->erase_action(p_name);
	}
}

void InputMapping::SetupDefaults() {
	// === Camera ===
	AddAction("camera_orbit",    { _MakeMouseButton(MOUSE_BUTTON_RIGHT) });
	AddAction("camera_zoom_in",  { _MakeMouseButton(MOUSE_BUTTON_WHEEL_UP) });
	AddAction("camera_zoom_out", { _MakeMouseButton(MOUSE_BUTTON_WHEEL_DOWN) });
	AddAction("camera_yaw_left",   { _MakeKey(KEY_Q) });
	AddAction("camera_yaw_right",  { _MakeKey(KEY_E) });
	AddAction("camera_pitch_up",   { _MakeKey(KEY_R) });
	AddAction("camera_pitch_down", { _MakeKey(KEY_F) });

	// === Player movement ===
	AddAction("move_forward", { _MakeKey(KEY_W)});
	AddAction("move_forward", { _MakeKey(KEY_UP) });
	AddAction("move_back",    { _MakeKey(KEY_S)});
	AddAction("move_back",    { _MakeKey(KEY_DOWN) });
	AddAction("move_left",    { _MakeKey(KEY_A)});
	AddAction("move_left",    { _MakeKey(KEY_LEFT) });
	AddAction("move_right",   { _MakeKey(KEY_D)});
	AddAction("move_right",   { _MakeKey(KEY_RIGHT) });
}

} // namespace godot
