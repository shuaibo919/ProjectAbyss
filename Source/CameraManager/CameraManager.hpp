#pragma once

#include <godot_cpp/classes/node.hpp>

namespace godot {

class CameraViewpoint;

class CameraManager : public Node {
	GDCLASS(CameraManager, Node)

	uint64_t CurrentCameraId = 0;

protected:
	static void _bind_methods();

public:
	static constexpr const char* CameraGroup = "camera_dynamic";
	static constexpr const char* SingletonGroup = "camera_manager_singleton";

	CameraManager();
	~CameraManager();

	void _ready() override;

	void SetCurrentCamera(CameraViewpoint* p_node);
	CameraViewpoint* GetCurrentCamera() const;
	TypedArray<CameraViewpoint> GetCameras() const;
};

} // namespace godot
