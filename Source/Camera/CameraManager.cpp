#include "CameraManager.hpp"
#include "CameraViewpoint.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

namespace godot {

CameraManager::CameraManager() {}
CameraManager::~CameraManager() {}

void CameraManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_current_camera", "node"), &CameraManager::set_current_camera);
	ClassDB::bind_method(D_METHOD("get_current_camera"), &CameraManager::get_current_camera);
	ClassDB::bind_method(D_METHOD("get_cameras"), &CameraManager::get_cameras);

	ADD_SIGNAL(MethodInfo("camera_switched", PropertyInfo(Variant::OBJECT, "node", PROPERTY_HINT_NONE, "CameraViewpoint")));
}

void CameraManager::_ready() {
	add_to_group("camera_manager_singleton", true);

	if (current_camera_id != 0) {
		return;
	}
	TypedArray<CameraViewpoint> cameras = get_cameras();
	if (cameras.size() > 0) {
		CameraViewpoint* first = Object::cast_to<CameraViewpoint>(cameras[0]);
		if (first != nullptr) {
			current_camera_id = first->get_instance_id();
		}
	}
}

void CameraManager::set_current_camera(CameraViewpoint* p_node) {
	if (p_node == nullptr) {
		return;
	}
	current_camera_id = p_node->get_instance_id();
	emit_signal("camera_switched", p_node);
}

CameraViewpoint* CameraManager::get_current_camera() const {
	if (current_camera_id == 0) {
		return nullptr;
	}
	return Object::cast_to<CameraViewpoint>(ObjectDB::get_instance(current_camera_id));
}

TypedArray<CameraViewpoint> CameraManager::get_cameras() const {
	TypedArray<CameraViewpoint> result;
	SceneTree* tree = get_tree();
	if (tree == nullptr) {
		return result;
	}
	TypedArray<Node> nodes = tree->get_nodes_in_group(CAMERA_GROUP);
	for (int i = 0; i < nodes.size(); i++) {
		CameraViewpoint* cam = Object::cast_to<CameraViewpoint>(nodes[i]);
		if (cam != nullptr) {
			result.append(cam);
		}
	}
	return result;
}

} // namespace godot
