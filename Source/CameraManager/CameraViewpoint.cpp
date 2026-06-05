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
	ClassDB::bind_method(D_METHOD("SetWatch", "node"), &CameraViewpoint::SetWatch);
	ClassDB::bind_method(D_METHOD("GetWatch"), &CameraViewpoint::GetWatch);
	ClassDB::bind_method(D_METHOD("SetFollow", "node"), &CameraViewpoint::SetFollow);
	ClassDB::bind_method(D_METHOD("GetFollow"), &CameraViewpoint::GetFollow);
	ClassDB::bind_method(D_METHOD("SetHasTransition", "has"), &CameraViewpoint::SetHasTransition);
	ClassDB::bind_method(D_METHOD("GetHasTransition"), &CameraViewpoint::GetHasTransition);
	ClassDB::bind_method(D_METHOD("SetSpeedMovement", "speed"), &CameraViewpoint::SetSpeedMovement);
	ClassDB::bind_method(D_METHOD("GetSpeedMovement"), &CameraViewpoint::GetSpeedMovement);
	ClassDB::bind_method(D_METHOD("SetSpeedRotation", "speed"), &CameraViewpoint::SetSpeedRotation);
	ClassDB::bind_method(D_METHOD("GetSpeedRotation"), &CameraViewpoint::GetSpeedRotation);
	ClassDB::bind_method(D_METHOD("IsCurrent"), &CameraViewpoint::IsCurrent);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "watch", PROPERTY_HINT_NODE_TYPE, "Node3D"), "SetWatch", "GetWatch");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "follow", PROPERTY_HINT_NODE_TYPE, "Node3D"), "SetFollow", "GetFollow");

	ADD_GROUP("Transition", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_transition"), "SetHasTransition", "GetHasTransition");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_movement"), "SetSpeedMovement", "GetSpeedMovement");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_rotation"), "SetSpeedRotation", "GetSpeedRotation");
}

void CameraViewpoint::_enter_tree() {
	add_to_group(CameraManager::CameraGroup, true);
}

void CameraViewpoint::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) return;

	Viewport* viewport = get_viewport();
	if (viewport != nullptr) {
		ActiveCamera = viewport->get_camera_3d();
	}

	CurrentTransform = get_global_transform();

	CameraManager* mgr = Object::cast_to<CameraManager>(
		get_tree()->get_first_node_in_group(CameraManager::SingletonGroup));
	if (mgr != nullptr) {
		mgr->connect("camera_switched", callable_mp(this, &CameraViewpoint::_OnCameraSwitched));
	}

	if (IsCurrent() && ActiveCamera != nullptr) {
		ActiveCamera->set_global_transform(get_global_transform());
	}

	Initialized = true;
}

void CameraViewpoint::_process(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) return;
	if (!Initialized) return;

	float delta = static_cast<float>(p_delta);

	if (Follow != nullptr) {
		set_as_top_level(true);
	}

	Transform3D targetTransform = (Follow != nullptr)
		? Follow->get_global_transform()
		: get_global_transform();

	CurrentTransform.set_origin(
		CurrentTransform.get_origin().lerp(targetTransform.get_origin(), delta * SpeedMovement)
	);

	if (Watch != nullptr) {
		Transform3D lookingAt = CurrentTransform.looking_at(Watch->get_global_position(), Vector3(0, 1, 0));
		CurrentTransform = CurrentTransform.interpolate_with(lookingAt, delta * SpeedRotation);
	}

	if (IsCurrent() && ActiveCamera != nullptr) {
		ActiveCamera->set_global_transform(CurrentTransform);
	} else {
		set_global_transform(CurrentTransform);
	}
}

void CameraViewpoint::_OnCameraSwitched(CameraViewpoint* p_node) {
	if (this != p_node) return;
	if (HasTransition && ActiveCamera != nullptr) {
		CurrentTransform = ActiveCamera->get_global_transform();
	}
}

bool CameraViewpoint::IsCurrent() const {
	CameraManager* mgr = Object::cast_to<CameraManager>(
		get_tree()->get_first_node_in_group(CameraManager::SingletonGroup));
	if (mgr == nullptr) return false;
	return mgr->GetCurrentCamera() == this;
}

void CameraViewpoint::SetWatch(Node3D* p_node) { Watch = p_node; }
Node3D* CameraViewpoint::GetWatch() const { return Watch; }
void CameraViewpoint::SetFollow(Node3D* p_node) { Follow = p_node; }
Node3D* CameraViewpoint::GetFollow() const { return Follow; }
void CameraViewpoint::SetHasTransition(bool p_has) { HasTransition = p_has; }
bool CameraViewpoint::GetHasTransition() const { return HasTransition; }
void CameraViewpoint::SetSpeedMovement(float p_speed) { SpeedMovement = p_speed; }
float CameraViewpoint::GetSpeedMovement() const { return SpeedMovement; }
void CameraViewpoint::SetSpeedRotation(float p_speed) { SpeedRotation = p_speed; }
float CameraViewpoint::GetSpeedRotation() const { return SpeedRotation; }

} // namespace godot
