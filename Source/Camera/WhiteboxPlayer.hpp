#pragma once

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace godot {

class WhiteboxPlayer : public CharacterBody3D {
	GDCLASS(WhiteboxPlayer, CharacterBody3D)

private:
	float move_speed = 5.0f;
	float acceleration = 20.0f;

	// -- input actions (configure in Project Settings → Input Map) --
	StringName move_forward_action = "move_forward";
	StringName move_back_action = "move_back";
	StringName move_left_action = "move_left";
	StringName move_right_action = "move_right";

protected:
	static void _bind_methods();

public:
	WhiteboxPlayer();
	~WhiteboxPlayer();

	void _physics_process(double p_delta) override;

	void set_move_speed(float p_speed);
	float get_move_speed() const;

	void set_acceleration(float p_accel);
	float get_acceleration() const;

	void set_move_forward_action(const StringName& p_name);
	StringName get_move_forward_action() const;

	void set_move_back_action(const StringName& p_name);
	StringName get_move_back_action() const;

	void set_move_left_action(const StringName& p_name);
	StringName get_move_left_action() const;

	void set_move_right_action(const StringName& p_name);
	StringName get_move_right_action() const;
};

} // namespace godot
