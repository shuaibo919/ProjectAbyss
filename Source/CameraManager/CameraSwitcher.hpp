#pragma once

#include <godot_cpp/classes/node3d.hpp>

namespace godot {

class CameraViewpoint;

class CameraSwitcher : public Node3D {
	GDCLASS(CameraSwitcher, Node3D)

	int CurrentCameraIndex = 0;

protected:
	static void _bind_methods();

public:
	CameraSwitcher();
	~CameraSwitcher();

	void _ready() override;

	void SetCurrentCamera(int p_index);
	int GetCurrentCamera() const;

private:
	void _ApplyCamera();
};

} // namespace godot
