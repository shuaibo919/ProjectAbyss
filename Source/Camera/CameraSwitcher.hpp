#pragma once

#include <godot_cpp/classes/node3d.hpp>

namespace godot {

class CameraViewpoint;

class CameraSwitcher : public Node3D {
	GDCLASS(CameraSwitcher, Node3D)

private:
	int _current_camera = 0;

protected:
	static void _bind_methods();

public:
	CameraSwitcher();
	~CameraSwitcher();

	void _ready() override;

	void set_current_camera(int p_index);
	int get_current_camera() const;

private:
	void _apply_camera();
};

} // namespace godot
