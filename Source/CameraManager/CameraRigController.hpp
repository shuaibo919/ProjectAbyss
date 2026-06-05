#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/spring_arm3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace godot {

class CameraViewpoint;

class CameraRigController : public Node {
	GDCLASS(CameraRigController, Node)

	NodePath PlayerPath;
	NodePath YawPivotPath;
	NodePath PitchPivotPath;
	NodePath SpringArmPath;

	Node3D* PlayerNode = nullptr;
	Node3D* YawPivot = nullptr;
	Node3D* PitchPivot = nullptr;
	SpringArm3D* SpringArm = nullptr;

	StringName OrbitAction = "camera_orbit";
	StringName ZoomInAction = "camera_zoom_in";
	StringName ZoomOutAction = "camera_zoom_out";
	StringName YawLeftAction = "camera_yaw_left";
	StringName YawRightAction = "camera_yaw_right";
	StringName PitchUpAction = "camera_pitch_up";
	StringName PitchDownAction = "camera_pitch_down";

	float KeyboardSpeed = 90.0f;
	int OrbitButton = MOUSE_BUTTON_RIGHT;

	float DefaultYaw = 0.0f;
	float DefaultPitch = -55.0f;
	float YawRange = 30.0f;
	float PitchRange = 15.0f;
	float ZoomDefault = 12.0f;
	float ZoomMin = 4.0f;
	float ZoomMax = 30.0f;
	float ZoomStep = 0.5f;
	float MouseSensitivity = 0.2f;

	float CurrentYaw = 0.0f;
	float CurrentPitch = -55.0f;
	float CurrentZoom = 12.0f;
	bool RightMouseDown = false;
	bool Initialized = false;

protected:
	static void _bind_methods();

public:
	CameraRigController();
	~CameraRigController();

	void _ready() override;
	void _process(double p_delta) override;
	void _input(const Ref<InputEvent>& p_event) override;

	void SetPlayerPath(const NodePath& p_path);
	NodePath GetPlayerPath() const;
	void SetYawPivotPath(const NodePath& p_path);
	NodePath GetYawPivotPath() const;
	void SetPitchPivotPath(const NodePath& p_path);
	NodePath GetPitchPivotPath() const;
	void SetSpringArmPath(const NodePath& p_path);
	NodePath GetSpringArmPath() const;

	void SetOrbitAction(const StringName& p_name);
	StringName GetOrbitAction() const;
	void SetZoomInAction(const StringName& p_name);
	StringName GetZoomInAction() const;
	void SetZoomOutAction(const StringName& p_name);
	StringName GetZoomOutAction() const;
	void SetYawLeftAction(const StringName& p_name);
	StringName GetYawLeftAction() const;
	void SetYawRightAction(const StringName& p_name);
	StringName GetYawRightAction() const;
	void SetPitchUpAction(const StringName& p_name);
	StringName GetPitchUpAction() const;
	void SetPitchDownAction(const StringName& p_name);
	StringName GetPitchDownAction() const;

	void SetDefaultYaw(float p_val);
	float GetDefaultYaw() const;
	void SetDefaultPitch(float p_val);
	float GetDefaultPitch() const;
	void SetYawRange(float p_val);
	float GetYawRange() const;
	void SetPitchRange(float p_val);
	float GetPitchRange() const;
	void SetZoomDefault(float p_val);
	float GetZoomDefault() const;
	void SetZoomMin(float p_val);
	float GetZoomMin() const;
	void SetZoomMax(float p_val);
	float GetZoomMax() const;
	void SetZoomStep(float p_val);
	float GetZoomStep() const;
	void SetMouseSensitivity(float p_val);
	float GetMouseSensitivity() const;
	void SetKeyboardSpeed(float p_val);
	float GetKeyboardSpeed() const;
	void SetOrbitButton(int p_val);
	int GetOrbitButton() const;

private:
	void _ResolveNodeRefs();
	void _ApplyCameraTransform();
	void _ApplyZoom();
};

} // namespace godot
