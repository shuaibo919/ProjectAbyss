#include "CameraViewpoint.hpp"
#include "CameraManager.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

CameraViewpoint::CameraViewpoint() {}
CameraViewpoint::~CameraViewpoint() {}

void CameraViewpoint::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_watch", "node"), &CameraViewpoint::set_watch);
	ClassDB::bind_method(D_METHOD("get_watch"), &CameraViewpoint::get_watch);
	ClassDB::bind_method(D_METHOD("set_follow", "node"), &CameraViewpoint::set_follow);
	ClassDB::bind_method(D_METHOD("get_follow"), &CameraViewpoint::get_follow);
	ClassDB::bind_method(D_METHOD("set_has_transition", "has"), &CameraViewpoint::set_has_transition);
	ClassDB::bind_method(D_METHOD("get_has_transition"), &CameraViewpoint::get_has_transition);
	ClassDB::bind_method(D_METHOD("set_speed_movement", "speed"), &CameraViewpoint::set_speed_movement);
	ClassDB::bind_method(D_METHOD("get_speed_movement"), &CameraViewpoint::get_speed_movement);
	ClassDB::bind_method(D_METHOD("set_speed_rotation", "speed"), &CameraViewpoint::set_speed_rotation);
	ClassDB::bind_method(D_METHOD("get_speed_rotation"), &CameraViewpoint::get_speed_rotation);
	ClassDB::bind_method(D_METHOD("is_current"), &CameraViewpoint::is_current);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "watch", PROPERTY_HINT_NODE_TYPE, "Node3D"), "set_watch", "get_watch");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "follow", PROPERTY_HINT_NODE_TYPE, "Node3D"), "set_follow", "get_follow");

	ADD_GROUP("Transition", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_transition"), "set_has_transition", "get_has_transition");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_movement"), "set_speed_movement", "get_speed_movement");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_rotation"), "set_speed_rotation", "get_speed_rotation");
}

void CameraViewpoint::_enter_tree() {
	add_to_group(CameraManager::CAMERA_GROUP, true);
}

void CameraViewpoint::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	Viewport* viewport = get_viewport();
	if (viewport != nullptr) {
		_camera = viewport->get_camera_3d();
	}

	_current_transform = get_global_transform();

	CameraManager* mgr = Object::cast_to<CameraManager>(
		get_tree()->get_first_node_in_group("camera_manager_singleton"));
	if (mgr != nullptr) {
		mgr->connect("camera_switched", callable_mp(this, &CameraViewpoint::_on_camera_switched));
	}

	if (is_current() && _camera != nullptr) {
		_camera->set_global_transform(get_global_transform());
	}

	_initialized = true;
}

void CameraViewpoint::_process(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (!_initialized) {
		return;
	}

	float delta = static_cast<float>(p_delta);

	if (follow != nullptr) {
		set_as_top_level(true);
	}

	Transform3D target_transform = (follow != nullptr)
		? follow->get_global_transform()
		: get_global_transform();

	_current_transform.set_origin(
		_current_transform.get_origin().lerp(target_transform.get_origin(), delta * speed_movement)
	);

	if (watch != nullptr) {
		Transform3D looking_at = _current_transform.looking_at(watch->get_global_position(), Vector3(0, 1, 0));
		_current_transform = _current_transform.interpolate_with(looking_at, delta * speed_rotation);
	}

	if (is_current() && _camera != nullptr) {
		_camera->set_global_transform(_current_transform);
	} else {
		set_global_transform(_current_transform);
	}
}

void CameraViewpoint::_on_camera_switched(CameraViewpoint* p_node) {
	if (this != p_node) {
		return;
	}
	if (has_transition && _camera != nullptr) {
		_current_transform = _camera->get_global_transform();
	}
}

bool CameraViewpoint::is_current() const {
	CameraManager* mgr = Object::cast_to<CameraManager>(
		get_tree()->get_first_node_in_group("camera_manager_singleton"));
	if (mgr == nullptr) {
		return false;
	}
	return mgr->get_current_camera() == this;
}

// -- property accessors --

void CameraViewpoint::set_watch(Node3D* p_node) { watch = p_node; }
Node3D* CameraViewpoint::get_watch() const { return watch; }

void CameraViewpoint::set_follow(Node3D* p_node) { follow = p_node; }
Node3D* CameraViewpoint::get_follow() const { return follow; }

void CameraViewpoint::set_has_transition(bool p_has) { has_transition = p_has; }
bool CameraViewpoint::get_has_transition() const { return has_transition; }

void CameraViewpoint::set_speed_movement(float p_speed) { speed_movement = p_speed; }
float CameraViewpoint::get_speed_movement() const { return speed_movement; }

void CameraViewpoint::set_speed_rotation(float p_speed) { speed_rotation = p_speed; }
float CameraViewpoint::get_speed_rotation() const { return speed_rotation; }

} // namespace godot
