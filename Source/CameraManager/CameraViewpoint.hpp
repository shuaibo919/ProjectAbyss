#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>

namespace godot {

class CameraViewpoint : public Node3D {
	GDCLASS(CameraViewpoint, Node3D)

	Node3D* Watch = nullptr;
	Node3D* Follow = nullptr;
	bool HasTransition = true;
	float SpeedMovement = 1.0f;
	float SpeedRotation = 1.0f;

	Camera3D* ActiveCamera = nullptr;
	Transform3D CurrentTransform;
	bool Initialized = false;

protected:
	static void _bind_methods();

public:
	CameraViewpoint();
	~CameraViewpoint();

	void _enter_tree() override;
	void _ready() override;
	void _process(double p_delta) override;

	void SetWatch(Node3D* p_node);
	Node3D* GetWatch() const;

	void SetFollow(Node3D* p_node);
	Node3D* GetFollow() const;

	void SetHasTransition(bool p_has);
	bool GetHasTransition() const;

	void SetSpeedMovement(float p_speed);
	float GetSpeedMovement() const;

	void SetSpeedRotation(float p_speed);
	float GetSpeedRotation() const;

	bool IsCurrent() const;

private:
	void _OnCameraSwitched(CameraViewpoint* p_node);
};

} // namespace godot
