#include "CameraManager.hpp"
#include "CameraViewpoint.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

namespace godot {

CameraManager::CameraManager() {}
CameraManager::~CameraManager() {}

void CameraManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("SetCurrentCamera", "node"), &CameraManager::SetCurrentCamera);
	ClassDB::bind_method(D_METHOD("GetCurrentCamera"), &CameraManager::GetCurrentCamera);
	ClassDB::bind_method(D_METHOD("GetCameras"), &CameraManager::GetCameras);

	ADD_SIGNAL(MethodInfo("camera_switched", PropertyInfo(Variant::OBJECT, "node", PROPERTY_HINT_NONE, "CameraViewpoint")));
}

void CameraManager::_ready() {
	add_to_group(SingletonGroup, true);

	if (CurrentCameraId != 0) return;

	TypedArray<CameraViewpoint> cameras = GetCameras();
	if (cameras.size() > 0) {
		CameraViewpoint* first = Object::cast_to<CameraViewpoint>(cameras[0]);
		if (first != nullptr) {
			CurrentCameraId = first->get_instance_id();
		}
	}
}

void CameraManager::SetCurrentCamera(CameraViewpoint* p_node) {
	if (p_node == nullptr) return;
	CurrentCameraId = p_node->get_instance_id();
	emit_signal("camera_switched", p_node);
}

CameraViewpoint* CameraManager::GetCurrentCamera() const {
	if (CurrentCameraId == 0) return nullptr;
	return Object::cast_to<CameraViewpoint>(ObjectDB::get_instance(CurrentCameraId));
}

TypedArray<CameraViewpoint> CameraManager::GetCameras() const {
	TypedArray<CameraViewpoint> result;
	SceneTree* tree = get_tree();
	if (tree == nullptr) return result;
	TypedArray<Node> nodes = tree->get_nodes_in_group(CameraGroup);
	for (int i = 0; i < nodes.size(); i++) {
		CameraViewpoint* cam = Object::cast_to<CameraViewpoint>(nodes[i]);
		if (cam != nullptr) {
			result.append(cam);
		}
	}
	return result;
}

} // namespace godot
