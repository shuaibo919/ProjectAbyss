#pragma once

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace godot {

class WhiteboxPlayer : public CharacterBody3D {
	GDCLASS(WhiteboxPlayer, CharacterBody3D)

	float MoveSpeed = 10.0f;
	float Acceleration = 60.0f;
	float Gravity = 28.0f;
	float JumpVelocity = 12.0f;

	StringName MoveForwardAction = "move_forward";
	StringName MoveBackAction = "move_back";
	StringName MoveLeftAction = "move_left";
	StringName MoveRightAction = "move_right";
	StringName JumpAction = "jump";

protected:
	static void _bind_methods();

public:
	WhiteboxPlayer();
	~WhiteboxPlayer();

	void _physics_process(double p_delta) override;

	void SetMoveSpeed(float p_speed);
	float GetMoveSpeed() const;
	void SetAcceleration(float p_accel);
	float GetAcceleration() const;
	void SetGravity(float p_gravity);
	float GetGravity() const;
	void SetJumpVelocity(float p_velocity);
	float GetJumpVelocity() const;

	void SetMoveForwardAction(const StringName& p_name);
	StringName GetMoveForwardAction() const;
	void SetMoveBackAction(const StringName& p_name);
	StringName GetMoveBackAction() const;
	void SetMoveLeftAction(const StringName& p_name);
	StringName GetMoveLeftAction() const;
	void SetMoveRightAction(const StringName& p_name);
	StringName GetMoveRightAction() const;
	void SetJumpAction(const StringName& p_name);
	StringName GetJumpAction() const;
};

} // namespace godot
