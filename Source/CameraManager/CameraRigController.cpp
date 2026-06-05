#include "CameraRigController.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

CameraRigController::CameraRigController() {}
CameraRigController::~CameraRigController() {}

void CameraRigController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("SetPlayerPath", "path"), &CameraRigController::SetPlayerPath);
	ClassDB::bind_method(D_METHOD("GetPlayerPath"), &CameraRigController::GetPlayerPath);
	ClassDB::bind_method(D_METHOD("SetYawPivotPath", "path"), &CameraRigController::SetYawPivotPath);
	ClassDB::bind_method(D_METHOD("GetYawPivotPath"), &CameraRigController::GetYawPivotPath);
	ClassDB::bind_method(D_METHOD("SetPitchPivotPath", "path"), &CameraRigController::SetPitchPivotPath);
	ClassDB::bind_method(D_METHOD("GetPitchPivotPath"), &CameraRigController::GetPitchPivotPath);
	ClassDB::bind_method(D_METHOD("SetSpringArmPath", "path"), &CameraRigController::SetSpringArmPath);
	ClassDB::bind_method(D_METHOD("GetSpringArmPath"), &CameraRigController::GetSpringArmPath);

	ClassDB::bind_method(D_METHOD("SetOrbitAction", "name"), &CameraRigController::SetOrbitAction);
	ClassDB::bind_method(D_METHOD("GetOrbitAction"), &CameraRigController::GetOrbitAction);
	ClassDB::bind_method(D_METHOD("SetZoomInAction", "name"), &CameraRigController::SetZoomInAction);
	ClassDB::bind_method(D_METHOD("GetZoomInAction"), &CameraRigController::GetZoomInAction);
	ClassDB::bind_method(D_METHOD("SetZoomOutAction", "name"), &CameraRigController::SetZoomOutAction);
	ClassDB::bind_method(D_METHOD("GetZoomOutAction"), &CameraRigController::GetZoomOutAction);
	ClassDB::bind_method(D_METHOD("SetYawLeftAction", "name"), &CameraRigController::SetYawLeftAction);
	ClassDB::bind_method(D_METHOD("GetYawLeftAction"), &CameraRigController::GetYawLeftAction);
	ClassDB::bind_method(D_METHOD("SetYawRightAction", "name"), &CameraRigController::SetYawRightAction);
	ClassDB::bind_method(D_METHOD("GetYawRightAction"), &CameraRigController::GetYawRightAction);
	ClassDB::bind_method(D_METHOD("SetPitchUpAction", "name"), &CameraRigController::SetPitchUpAction);
	ClassDB::bind_method(D_METHOD("GetPitchUpAction"), &CameraRigController::GetPitchUpAction);
	ClassDB::bind_method(D_METHOD("SetPitchDownAction", "name"), &CameraRigController::SetPitchDownAction);
	ClassDB::bind_method(D_METHOD("GetPitchDownAction"), &CameraRigController::GetPitchDownAction);

	ClassDB::bind_method(D_METHOD("SetDefaultYaw", "degrees"), &CameraRigController::SetDefaultYaw);
	ClassDB::bind_method(D_METHOD("GetDefaultYaw"), &CameraRigController::GetDefaultYaw);
	ClassDB::bind_method(D_METHOD("SetDefaultPitch", "degrees"), &CameraRigController::SetDefaultPitch);
	ClassDB::bind_method(D_METHOD("GetDefaultPitch"), &CameraRigController::GetDefaultPitch);
	ClassDB::bind_method(D_METHOD("SetYawRange", "degrees"), &CameraRigController::SetYawRange);
	ClassDB::bind_method(D_METHOD("GetYawRange"), &CameraRigController::GetYawRange);
	ClassDB::bind_method(D_METHOD("SetPitchRange", "degrees"), &CameraRigController::SetPitchRange);
	ClassDB::bind_method(D_METHOD("GetPitchRange"), &CameraRigController::GetPitchRange);
	ClassDB::bind_method(D_METHOD("SetZoomDefault", "distance"), &CameraRigController::SetZoomDefault);
	ClassDB::bind_method(D_METHOD("GetZoomDefault"), &CameraRigController::GetZoomDefault);
	ClassDB::bind_method(D_METHOD("SetZoomMin", "distance"), &CameraRigController::SetZoomMin);
	ClassDB::bind_method(D_METHOD("GetZoomMin"), &CameraRigController::GetZoomMin);
	ClassDB::bind_method(D_METHOD("SetZoomMax", "distance"), &CameraRigController::SetZoomMax);
	ClassDB::bind_method(D_METHOD("GetZoomMax"), &CameraRigController::GetZoomMax);
	ClassDB::bind_method(D_METHOD("SetZoomStep", "step"), &CameraRigController::SetZoomStep);
	ClassDB::bind_method(D_METHOD("GetZoomStep"), &CameraRigController::GetZoomStep);
	ClassDB::bind_method(D_METHOD("SetMouseSensitivity", "deg_per_pixel"), &CameraRigController::SetMouseSensitivity);
	ClassDB::bind_method(D_METHOD("GetMouseSensitivity"), &CameraRigController::GetMouseSensitivity);
	ClassDB::bind_method(D_METHOD("SetKeyboardSpeed", "deg_per_sec"), &CameraRigController::SetKeyboardSpeed);
	ClassDB::bind_method(D_METHOD("GetKeyboardSpeed"), &CameraRigController::GetKeyboardSpeed);
	ClassDB::bind_method(D_METHOD("SetOrbitButton", "button_index"), &CameraRigController::SetOrbitButton);
	ClassDB::bind_method(D_METHOD("GetOrbitButton"), &CameraRigController::GetOrbitButton);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player_path"), "SetPlayerPath", "GetPlayerPath");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "yaw_pivot_path"), "SetYawPivotPath", "GetYawPivotPath");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "pitch_pivot_path"), "SetPitchPivotPath", "GetPitchPivotPath");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "spring_arm_path"), "SetSpringArmPath", "GetSpringArmPath");

	ADD_GROUP("Input Actions", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "orbit_action"), "SetOrbitAction", "GetOrbitAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "zoom_in_action"), "SetZoomInAction", "GetZoomInAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "zoom_out_action"), "SetZoomOutAction", "GetZoomOutAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "yaw_left_action"), "SetYawLeftAction", "GetYawLeftAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "yaw_right_action"), "SetYawRightAction", "GetYawRightAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "pitch_up_action"), "SetPitchUpAction", "GetPitchUpAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "pitch_down_action"), "SetPitchDownAction", "GetPitchDownAction");

	ADD_GROUP("Camera Defaults", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "default_yaw", PROPERTY_HINT_RANGE, "-180,180,0.5"), "SetDefaultYaw", "GetDefaultYaw");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "default_pitch", PROPERTY_HINT_RANGE, "-90,0,0.5"), "SetDefaultPitch", "GetDefaultPitch");

	ADD_GROUP("Camera Limits", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "yaw_range", PROPERTY_HINT_RANGE, "0,180,1"), "SetYawRange", "GetYawRange");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_range", PROPERTY_HINT_RANGE, "0,90,1"), "SetPitchRange", "GetPitchRange");

	ADD_GROUP("Zoom", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_default", PROPERTY_HINT_RANGE, "1,100,0.5"), "SetZoomDefault", "GetZoomDefault");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_min", PROPERTY_HINT_RANGE, "0.5,50,0.5"), "SetZoomMin", "GetZoomMin");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_max", PROPERTY_HINT_RANGE, "1,200,1"), "SetZoomMax", "GetZoomMax");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_step", PROPERTY_HINT_RANGE, "0.1,5,0.1"), "SetZoomStep", "GetZoomStep");

	ADD_GROUP("Mouse", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orbit_button"), "SetOrbitButton", "GetOrbitButton");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mouse_sensitivity", PROPERTY_HINT_RANGE, "0.05,1.0,0.05"), "SetMouseSensitivity", "GetMouseSensitivity");

	ADD_GROUP("Keyboard Orbit", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "keyboard_speed", PROPERTY_HINT_RANGE, "10,360,5"), "SetKeyboardSpeed", "GetKeyboardSpeed");
}

void CameraRigController::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) return;
	CurrentYaw = DefaultYaw;
	CurrentPitch = DefaultPitch;
	CurrentZoom = ZoomDefault;
	_ResolveNodeRefs();
}

void CameraRigController::_ResolveNodeRefs() {
	if (!PlayerPath.is_empty())   PlayerNode = Object::cast_to<Node3D>(get_node_or_null(PlayerPath));
	if (!YawPivotPath.is_empty())   YawPivot = Object::cast_to<Node3D>(get_node_or_null(YawPivotPath));
	if (!PitchPivotPath.is_empty()) PitchPivot = Object::cast_to<Node3D>(get_node_or_null(PitchPivotPath));
	if (!SpringArmPath.is_empty())  SpringArm = Object::cast_to<SpringArm3D>(get_node_or_null(SpringArmPath));

	_ApplyCameraTransform();
	_ApplyZoom();
	Initialized = true;
}

void CameraRigController::_process(double p_delta) {
	if (!Initialized) return;
	Input* input = Input::get_singleton();
	if (input == nullptr) return;

	if (input->is_action_just_pressed(ZoomInAction))  { CurrentZoom = Math::max(CurrentZoom - ZoomStep, ZoomMin); _ApplyZoom(); }
	if (input->is_action_just_pressed(ZoomOutAction)) { CurrentZoom = Math::min(CurrentZoom + ZoomStep, ZoomMax); _ApplyZoom(); }

	float delta = static_cast<float>(p_delta);
	bool changed = false;

	if (input->is_action_pressed(YawLeftAction))  { CurrentYaw -= KeyboardSpeed * delta; changed = true; }
	if (input->is_action_pressed(YawRightAction)) { CurrentYaw += KeyboardSpeed * delta; changed = true; }
	CurrentYaw = Math::clamp(CurrentYaw, DefaultYaw - YawRange, DefaultYaw + YawRange);

	if (input->is_action_pressed(PitchUpAction))   { CurrentPitch -= KeyboardSpeed * delta; changed = true; }
	if (input->is_action_pressed(PitchDownAction)) { CurrentPitch += KeyboardSpeed * delta; changed = true; }
	float HalfRange = PitchRange * 0.5f;
	CurrentPitch = Math::clamp(CurrentPitch, DefaultPitch - HalfRange, DefaultPitch + HalfRange);

	if (changed) _ApplyCameraTransform();
}

void CameraRigController::_input(const Ref<InputEvent>& p_event) {
	if (!Initialized) return;

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == OrbitButton) {
		RightMouseDown = mb->is_pressed();
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && RightMouseDown) {
		Vector2 delta = mm->get_relative();
		CurrentYaw -= delta.x * MouseSensitivity;
		CurrentYaw = Math::clamp(CurrentYaw, DefaultYaw - YawRange, DefaultYaw + YawRange);
		CurrentPitch -= delta.y * MouseSensitivity;
		float HalfRange = PitchRange * 0.5f;
		CurrentPitch = Math::clamp(CurrentPitch, DefaultPitch - HalfRange, DefaultPitch + HalfRange);
		_ApplyCameraTransform();
	}
}

void CameraRigController::_ApplyCameraTransform() {
	if (YawPivot == nullptr || PitchPivot == nullptr) return;
	YawPivot->set_rotation(Vector3(0, Math::deg_to_rad(CurrentYaw), 0));
	PitchPivot->set_rotation(Vector3(Math::deg_to_rad(CurrentPitch), 0, 0));
}

void CameraRigController::_ApplyZoom() {
	if (SpringArm != nullptr) SpringArm->set_length(CurrentZoom);
}

void CameraRigController::SetPlayerPath(const NodePath& p) { PlayerPath = p; }
NodePath CameraRigController::GetPlayerPath() const { return PlayerPath; }
void CameraRigController::SetYawPivotPath(const NodePath& p) { YawPivotPath = p; }
NodePath CameraRigController::GetYawPivotPath() const { return YawPivotPath; }
void CameraRigController::SetPitchPivotPath(const NodePath& p) { PitchPivotPath = p; }
NodePath CameraRigController::GetPitchPivotPath() const { return PitchPivotPath; }
void CameraRigController::SetSpringArmPath(const NodePath& p) { SpringArmPath = p; }
NodePath CameraRigController::GetSpringArmPath() const { return SpringArmPath; }

void CameraRigController::SetOrbitAction(const StringName& n) { OrbitAction = n; }
StringName CameraRigController::GetOrbitAction() const { return OrbitAction; }
void CameraRigController::SetZoomInAction(const StringName& n) { ZoomInAction = n; }
StringName CameraRigController::GetZoomInAction() const { return ZoomInAction; }
void CameraRigController::SetZoomOutAction(const StringName& n) { ZoomOutAction = n; }
StringName CameraRigController::GetZoomOutAction() const { return ZoomOutAction; }
void CameraRigController::SetYawLeftAction(const StringName& n) { YawLeftAction = n; }
StringName CameraRigController::GetYawLeftAction() const { return YawLeftAction; }
void CameraRigController::SetYawRightAction(const StringName& n) { YawRightAction = n; }
StringName CameraRigController::GetYawRightAction() const { return YawRightAction; }
void CameraRigController::SetPitchUpAction(const StringName& n) { PitchUpAction = n; }
StringName CameraRigController::GetPitchUpAction() const { return PitchUpAction; }
void CameraRigController::SetPitchDownAction(const StringName& n) { PitchDownAction = n; }
StringName CameraRigController::GetPitchDownAction() const { return PitchDownAction; }

void CameraRigController::SetDefaultYaw(float v) { DefaultYaw = v; }
float CameraRigController::GetDefaultYaw() const { return DefaultYaw; }
void CameraRigController::SetDefaultPitch(float v) { DefaultPitch = v; }
float CameraRigController::GetDefaultPitch() const { return DefaultPitch; }
void CameraRigController::SetYawRange(float v) { YawRange = v; }
float CameraRigController::GetYawRange() const { return YawRange; }
void CameraRigController::SetPitchRange(float v) { PitchRange = v; }
float CameraRigController::GetPitchRange() const { return PitchRange; }
void CameraRigController::SetZoomDefault(float v) { ZoomDefault = v; }
float CameraRigController::GetZoomDefault() const { return ZoomDefault; }
void CameraRigController::SetZoomMin(float v) { ZoomMin = v; }
float CameraRigController::GetZoomMin() const { return ZoomMin; }
void CameraRigController::SetZoomMax(float v) { ZoomMax = v; }
float CameraRigController::GetZoomMax() const { return ZoomMax; }
void CameraRigController::SetZoomStep(float v) { ZoomStep = v; }
float CameraRigController::GetZoomStep() const { return ZoomStep; }
void CameraRigController::SetMouseSensitivity(float v) { MouseSensitivity = v; }
float CameraRigController::GetMouseSensitivity() const { return MouseSensitivity; }
void CameraRigController::SetKeyboardSpeed(float v) { KeyboardSpeed = v; }
float CameraRigController::GetKeyboardSpeed() const { return KeyboardSpeed; }
void CameraRigController::SetOrbitButton(int v) { OrbitButton = v; }
int CameraRigController::GetOrbitButton() const { return OrbitButton; }

} // namespace godot
