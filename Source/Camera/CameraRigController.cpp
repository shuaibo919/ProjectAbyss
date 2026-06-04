#include "CameraRigController.hpp"
#include "CameraViewpoint.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

CameraRigController::CameraRigController() {}
CameraRigController::~CameraRigController() {}

void CameraRigController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_player_path", "path"), &CameraRigController::set_player_path);
	ClassDB::bind_method(D_METHOD("get_player_path"), &CameraRigController::get_player_path);
	ClassDB::bind_method(D_METHOD("set_yaw_pivot_path", "path"), &CameraRigController::set_yaw_pivot_path);
	ClassDB::bind_method(D_METHOD("get_yaw_pivot_path"), &CameraRigController::get_yaw_pivot_path);
	ClassDB::bind_method(D_METHOD("set_pitch_pivot_path", "path"), &CameraRigController::set_pitch_pivot_path);
	ClassDB::bind_method(D_METHOD("get_pitch_pivot_path"), &CameraRigController::get_pitch_pivot_path);
	ClassDB::bind_method(D_METHOD("set_spring_arm_path", "path"), &CameraRigController::set_spring_arm_path);
	ClassDB::bind_method(D_METHOD("get_spring_arm_path"), &CameraRigController::get_spring_arm_path);
	ClassDB::bind_method(D_METHOD("set_camera_viewpoint_path", "path"), &CameraRigController::set_camera_viewpoint_path);
	ClassDB::bind_method(D_METHOD("get_camera_viewpoint_path"), &CameraRigController::get_camera_viewpoint_path);

	ClassDB::bind_method(D_METHOD("set_orbit_action", "name"), &CameraRigController::set_orbit_action);
	ClassDB::bind_method(D_METHOD("get_orbit_action"), &CameraRigController::get_orbit_action);
	ClassDB::bind_method(D_METHOD("set_zoom_in_action", "name"), &CameraRigController::set_zoom_in_action);
	ClassDB::bind_method(D_METHOD("get_zoom_in_action"), &CameraRigController::get_zoom_in_action);
	ClassDB::bind_method(D_METHOD("set_zoom_out_action", "name"), &CameraRigController::set_zoom_out_action);
	ClassDB::bind_method(D_METHOD("get_zoom_out_action"), &CameraRigController::get_zoom_out_action);

	ClassDB::bind_method(D_METHOD("set_default_yaw", "degrees"), &CameraRigController::set_default_yaw);
	ClassDB::bind_method(D_METHOD("get_default_yaw"), &CameraRigController::get_default_yaw);
	ClassDB::bind_method(D_METHOD("set_default_pitch", "degrees"), &CameraRigController::set_default_pitch);
	ClassDB::bind_method(D_METHOD("get_default_pitch"), &CameraRigController::get_default_pitch);
	ClassDB::bind_method(D_METHOD("set_yaw_range", "degrees"), &CameraRigController::set_yaw_range);
	ClassDB::bind_method(D_METHOD("get_yaw_range"), &CameraRigController::get_yaw_range);
	ClassDB::bind_method(D_METHOD("set_pitch_range", "degrees"), &CameraRigController::set_pitch_range);
	ClassDB::bind_method(D_METHOD("get_pitch_range"), &CameraRigController::get_pitch_range);
	ClassDB::bind_method(D_METHOD("set_zoom_default", "distance"), &CameraRigController::set_zoom_default);
	ClassDB::bind_method(D_METHOD("get_zoom_default"), &CameraRigController::get_zoom_default);
	ClassDB::bind_method(D_METHOD("set_zoom_min", "distance"), &CameraRigController::set_zoom_min);
	ClassDB::bind_method(D_METHOD("get_zoom_min"), &CameraRigController::get_zoom_min);
	ClassDB::bind_method(D_METHOD("set_zoom_max", "distance"), &CameraRigController::set_zoom_max);
	ClassDB::bind_method(D_METHOD("get_zoom_max"), &CameraRigController::get_zoom_max);
	ClassDB::bind_method(D_METHOD("set_zoom_step", "step"), &CameraRigController::set_zoom_step);
	ClassDB::bind_method(D_METHOD("get_zoom_step"), &CameraRigController::get_zoom_step);
	ClassDB::bind_method(D_METHOD("set_mouse_sensitivity", "deg_per_pixel"), &CameraRigController::set_mouse_sensitivity);
	ClassDB::bind_method(D_METHOD("get_mouse_sensitivity"), &CameraRigController::get_mouse_sensitivity);

	ClassDB::bind_method(D_METHOD("_resolve_node_refs"), &CameraRigController::_resolve_node_refs);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player_path"), "set_player_path", "get_player_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "yaw_pivot_path"), "set_yaw_pivot_path", "get_yaw_pivot_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "pitch_pivot_path"), "set_pitch_pivot_path", "get_pitch_pivot_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "spring_arm_path"), "set_spring_arm_path", "get_spring_arm_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "camera_viewpoint_path"), "set_camera_viewpoint_path", "get_camera_viewpoint_path");

	ADD_GROUP("Input Actions", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "orbit_action"), "set_orbit_action", "get_orbit_action");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "zoom_in_action"), "set_zoom_in_action", "get_zoom_in_action");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "zoom_out_action"), "set_zoom_out_action", "get_zoom_out_action");

	ADD_GROUP("Camera Defaults", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "default_yaw", PROPERTY_HINT_RANGE, "-180,180,0.5"), "set_default_yaw", "get_default_yaw");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "default_pitch", PROPERTY_HINT_RANGE, "-90,0,0.5"), "set_default_pitch", "get_default_pitch");

	ADD_GROUP("Camera Limits", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "yaw_range", PROPERTY_HINT_RANGE, "0,180,1"), "set_yaw_range", "get_yaw_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_range", PROPERTY_HINT_RANGE, "0,90,1"), "set_pitch_range", "get_pitch_range");

	ADD_GROUP("Zoom", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_default", PROPERTY_HINT_RANGE, "1,100,0.5"), "set_zoom_default", "get_zoom_default");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_min", PROPERTY_HINT_RANGE, "0.5,50,0.5"), "set_zoom_min", "get_zoom_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_max", PROPERTY_HINT_RANGE, "1,200,1"), "set_zoom_max", "get_zoom_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_step", PROPERTY_HINT_RANGE, "0.1,5,0.1"), "set_zoom_step", "get_zoom_step");

	ADD_GROUP("Mouse", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mouse_sensitivity", PROPERTY_HINT_RANGE, "0.05,1.0,0.05"), "set_mouse_sensitivity", "get_mouse_sensitivity");
}

void CameraRigController::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) return;
	current_yaw = default_yaw;
	current_pitch = default_pitch;
	current_zoom = zoom_default;
	call_deferred("_resolve_node_refs");
}

void CameraRigController::_resolve_node_refs() {
	if (!player_path.is_empty())
		player_node = Object::cast_to<Node3D>(get_node_or_null(player_path));
	if (!yaw_pivot_path.is_empty())
		yaw_pivot = Object::cast_to<Node3D>(get_node_or_null(yaw_pivot_path));
	if (!pitch_pivot_path.is_empty())
		pitch_pivot = Object::cast_to<Node3D>(get_node_or_null(pitch_pivot_path));
	if (!spring_arm_path.is_empty())
		spring_arm = Object::cast_to<SpringArm3D>(get_node_or_null(spring_arm_path));
	if (!camera_viewpoint_path.is_empty())
		camera_viewpoint = Object::cast_to<CameraViewpoint>(get_node_or_null(camera_viewpoint_path));

	_apply_camera_transform();
	_apply_zoom();
	_initialized = true;
}

void CameraRigController::_process(double p_delta) {
	if (!_initialized) return;

	Input* input = Input::get_singleton();
	if (input == nullptr) return;

	if (input->is_action_just_pressed(zoom_in_action)) {
		current_zoom = Math::max(current_zoom - zoom_step, zoom_min);
		_apply_zoom();
	}
	if (input->is_action_just_pressed(zoom_out_action)) {
		current_zoom = Math::min(current_zoom + zoom_step, zoom_max);
		_apply_zoom();
	}

}

void CameraRigController::_input(const Ref<InputEvent>& p_event) {
	if (!_initialized) return;

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && Input::get_singleton()->is_action_pressed(orbit_action)) {
		Vector2 delta = mm->get_relative();
		current_yaw -= delta.x * mouse_sensitivity;
		current_yaw = Math::clamp(current_yaw, default_yaw - yaw_range, default_yaw + yaw_range);
		current_pitch -= delta.y * mouse_sensitivity;
		float half_range = pitch_range * 0.5f;
		current_pitch = Math::clamp(current_pitch, default_pitch - half_range, default_pitch + half_range);
		_apply_camera_transform();
	}
}

void CameraRigController::_apply_camera_transform() {
	if (yaw_pivot == nullptr || pitch_pivot == nullptr) return;
	yaw_pivot->set_rotation(Vector3(0, Math::deg_to_rad(current_yaw), 0));
	pitch_pivot->set_rotation(Vector3(Math::deg_to_rad(current_pitch), 0, 0));
}

void CameraRigController::_apply_zoom() {
	if (spring_arm != nullptr) {
		spring_arm->set_length(current_zoom);
	}
}

// -- accessors --

void CameraRigController::set_player_path(const NodePath& p) { player_path = p; }
NodePath CameraRigController::get_player_path() const { return player_path; }
void CameraRigController::set_yaw_pivot_path(const NodePath& p) { yaw_pivot_path = p; }
NodePath CameraRigController::get_yaw_pivot_path() const { return yaw_pivot_path; }
void CameraRigController::set_pitch_pivot_path(const NodePath& p) { pitch_pivot_path = p; }
NodePath CameraRigController::get_pitch_pivot_path() const { return pitch_pivot_path; }
void CameraRigController::set_spring_arm_path(const NodePath& p) { spring_arm_path = p; }
NodePath CameraRigController::get_spring_arm_path() const { return spring_arm_path; }
void CameraRigController::set_camera_viewpoint_path(const NodePath& p) { camera_viewpoint_path = p; }
NodePath CameraRigController::get_camera_viewpoint_path() const { return camera_viewpoint_path; }

void CameraRigController::set_orbit_action(const StringName& n) { orbit_action = n; }
StringName CameraRigController::get_orbit_action() const { return orbit_action; }
void CameraRigController::set_zoom_in_action(const StringName& n) { zoom_in_action = n; }
StringName CameraRigController::get_zoom_in_action() const { return zoom_in_action; }
void CameraRigController::set_zoom_out_action(const StringName& n) { zoom_out_action = n; }
StringName CameraRigController::get_zoom_out_action() const { return zoom_out_action; }

void CameraRigController::set_default_yaw(float v) { default_yaw = v; }
float CameraRigController::get_default_yaw() const { return default_yaw; }
void CameraRigController::set_default_pitch(float v) { default_pitch = v; }
float CameraRigController::get_default_pitch() const { return default_pitch; }
void CameraRigController::set_yaw_range(float v) { yaw_range = v; }
float CameraRigController::get_yaw_range() const { return yaw_range; }
void CameraRigController::set_pitch_range(float v) { pitch_range = v; }
float CameraRigController::get_pitch_range() const { return pitch_range; }
void CameraRigController::set_zoom_default(float v) { zoom_default = v; }
float CameraRigController::get_zoom_default() const { return zoom_default; }
void CameraRigController::set_zoom_min(float v) { zoom_min = v; }
float CameraRigController::get_zoom_min() const { return zoom_min; }
void CameraRigController::set_zoom_max(float v) { zoom_max = v; }
float CameraRigController::get_zoom_max() const { return zoom_max; }
void CameraRigController::set_zoom_step(float v) { zoom_step = v; }
float CameraRigController::get_zoom_step() const { return zoom_step; }
void CameraRigController::set_mouse_sensitivity(float v) { mouse_sensitivity = v; }
float CameraRigController::get_mouse_sensitivity() const { return mouse_sensitivity; }

} // namespace godot
