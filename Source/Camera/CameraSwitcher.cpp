#include "CameraSwitcher.hpp"
#include "CameraManager.hpp"
#include "CameraViewpoint.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

CameraSwitcher::CameraSwitcher() {}
CameraSwitcher::~CameraSwitcher() {}

void CameraSwitcher::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_current_camera", "index"), &CameraSwitcher::set_current_camera);
	ClassDB::bind_method(D_METHOD("get_current_camera"), &CameraSwitcher::get_current_camera);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "current_camera"), "set_current_camera", "get_current_camera");
}

void CameraSwitcher::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	_apply_camera();
}

void CameraSwitcher::set_current_camera(int p_index) {
	_current_camera = p_index;
}

int CameraSwitcher::get_current_camera() const {
	return _current_camera;
}

void CameraSwitcher::_apply_camera() {
	CameraManager* mgr = Object::cast_to<CameraManager>(
		get_tree()->get_first_node_in_group("camera_manager_singleton"));
	if (mgr == nullptr) {
		return;
	}

	TypedArray<CameraViewpoint> cameras = mgr->get_cameras();
	if (_current_camera >= 0 && _current_camera < cameras.size()) {
		CameraViewpoint* cam = Object::cast_to<CameraViewpoint>(cameras[_current_camera]);
		if (cam != nullptr) {
			mgr->set_current_camera(cam);
		}
	}
}

} // namespace godot
