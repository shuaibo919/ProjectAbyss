#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/spring_arm3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace godot {

class CameraViewpoint;

class CameraRigController : public Node {
	GDCLASS(CameraRigController, Node)

private:
	NodePath player_path;
	NodePath yaw_pivot_path;
	NodePath pitch_pivot_path;
	NodePath spring_arm_path;
	NodePath camera_viewpoint_path;

	Node3D* player_node = nullptr;
	Node3D* yaw_pivot = nullptr;
	Node3D* pitch_pivot = nullptr;
	SpringArm3D* spring_arm = nullptr;
	CameraViewpoint* camera_viewpoint = nullptr;

	StringName orbit_action = "camera_orbit";
	StringName zoom_in_action = "camera_zoom_in";
	StringName zoom_out_action = "camera_zoom_out";

	float default_yaw = 0.0f;
	float default_pitch = -55.0f;
	float yaw_range = 30.0f;
	float pitch_range = 15.0f;
	float zoom_default = 12.0f;
	float zoom_min = 4.0f;
	float zoom_max = 30.0f;
	float zoom_step = 0.5f;
	float mouse_sensitivity = 0.2f;

	float current_yaw = 0.0f;
	float current_pitch = -55.0f;
	float current_zoom = 12.0f;

	bool _initialized = false;

protected:
	static void _bind_methods();

public:
	CameraRigController();
	~CameraRigController();

	void _ready() override;
	void _process(double p_delta) override;
	void _input(const Ref<InputEvent>& p_event) override;

	void set_player_path(const NodePath& p_path);
	NodePath get_player_path() const;
	void set_yaw_pivot_path(const NodePath& p_path);
	NodePath get_yaw_pivot_path() const;
	void set_pitch_pivot_path(const NodePath& p_path);
	NodePath get_pitch_pivot_path() const;
	void set_spring_arm_path(const NodePath& p_path);
	NodePath get_spring_arm_path() const;
	void set_camera_viewpoint_path(const NodePath& p_path);
	NodePath get_camera_viewpoint_path() const;

	void set_orbit_action(const StringName& p_name);
	StringName get_orbit_action() const;
	void set_zoom_in_action(const StringName& p_name);
	StringName get_zoom_in_action() const;
	void set_zoom_out_action(const StringName& p_name);
	StringName get_zoom_out_action() const;

	void set_default_yaw(float p_val);
	float get_default_yaw() const;
	void set_default_pitch(float p_val);
	float get_default_pitch() const;
	void set_yaw_range(float p_val);
	float get_yaw_range() const;
	void set_pitch_range(float p_val);
	float get_pitch_range() const;
	void set_zoom_default(float p_val);
	float get_zoom_default() const;
	void set_zoom_min(float p_val);
	float get_zoom_min() const;
	void set_zoom_max(float p_val);
	float get_zoom_max() const;
	void set_zoom_step(float p_val);
	float get_zoom_step() const;
	void set_mouse_sensitivity(float p_val);
	float get_mouse_sensitivity() const;

private:
	void _resolve_node_refs();
	void _apply_camera_transform();
	void _apply_zoom();
};

} // namespace godot
