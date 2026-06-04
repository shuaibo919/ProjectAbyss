#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>

namespace godot {

class CameraViewpoint : public Node3D {
	GDCLASS(CameraViewpoint, Node3D)

private:
	Node3D* watch = nullptr;
	Node3D* follow = nullptr;
	bool has_transition = true;
	float speed_movement = 1.0f;
	float speed_rotation = 1.0f;

	Camera3D* _camera = nullptr;
	Transform3D _current_transform;
	bool _initialized = false;

protected:
	static void _bind_methods();

public:
	CameraViewpoint();
	~CameraViewpoint();

	void _enter_tree() override;
	void _ready() override;
	void _process(double p_delta) override;

	void set_watch(Node3D* p_node);
	Node3D* get_watch() const;

	void set_follow(Node3D* p_node);
	Node3D* get_follow() const;

	void set_has_transition(bool p_has);
	bool get_has_transition() const;

	void set_speed_movement(float p_speed);
	float get_speed_movement() const;

	void set_speed_rotation(float p_speed);
	float get_speed_rotation() const;

	bool is_current() const;

private:
	void _on_camera_switched(CameraViewpoint* p_node);
};

} // namespace godot
