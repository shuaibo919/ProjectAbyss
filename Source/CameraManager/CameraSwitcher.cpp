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
	ClassDB::bind_method(D_METHOD("SetCurrentCamera", "index"), &CameraSwitcher::SetCurrentCamera);
	ClassDB::bind_method(D_METHOD("GetCurrentCamera"), &CameraSwitcher::GetCurrentCamera);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "current_camera"), "SetCurrentCamera", "GetCurrentCamera");
}

void CameraSwitcher::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) return;
	_ApplyCamera();
}

void CameraSwitcher::SetCurrentCamera(int p_index) { CurrentCameraIndex = p_index; }
int CameraSwitcher::GetCurrentCamera() const { return CurrentCameraIndex; }

void CameraSwitcher::_ApplyCamera() {
	CameraManager* mgr = Object::cast_to<CameraManager>(
		get_tree()->get_first_node_in_group(CameraManager::SingletonGroup));
	if (mgr == nullptr) return;

	TypedArray<CameraViewpoint> cameras = mgr->GetCameras();
	if (CurrentCameraIndex >= 0 && CurrentCameraIndex < cameras.size()) {
		CameraViewpoint* cam = Object::cast_to<CameraViewpoint>(cameras[CurrentCameraIndex]);
		if (cam != nullptr) {
			mgr->SetCurrentCamera(cam);
		}
	}
}

} // namespace godot
