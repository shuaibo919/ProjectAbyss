#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

class CameraViewpoint;

class CameraManager : public Node {
	GDCLASS(CameraManager, Node)

public:
	static constexpr const char* CAMERA_GROUP = "camera_dynamic";

private:
	uint64_t current_camera_id = 0;

protected:
	static void _bind_methods();

public:
	CameraManager();
	~CameraManager();

	void _ready() override;

	void set_current_camera(CameraViewpoint* p_node);
	CameraViewpoint* get_current_camera() const;
	TypedArray<CameraViewpoint> get_cameras() const;
};

} // namespace godot
