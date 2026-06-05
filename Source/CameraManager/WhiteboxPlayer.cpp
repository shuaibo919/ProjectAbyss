#include "WhiteboxPlayer.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

WhiteboxPlayer::WhiteboxPlayer() {}
WhiteboxPlayer::~WhiteboxPlayer() {}

void WhiteboxPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("SetMoveSpeed", "speed"), &WhiteboxPlayer::SetMoveSpeed);
	ClassDB::bind_method(D_METHOD("GetMoveSpeed"), &WhiteboxPlayer::GetMoveSpeed);
	ClassDB::bind_method(D_METHOD("SetAcceleration", "accel"), &WhiteboxPlayer::SetAcceleration);
	ClassDB::bind_method(D_METHOD("GetAcceleration"), &WhiteboxPlayer::GetAcceleration);
	ClassDB::bind_method(D_METHOD("SetMoveForwardAction", "name"), &WhiteboxPlayer::SetMoveForwardAction);
	ClassDB::bind_method(D_METHOD("GetMoveForwardAction"), &WhiteboxPlayer::GetMoveForwardAction);
	ClassDB::bind_method(D_METHOD("SetMoveBackAction", "name"), &WhiteboxPlayer::SetMoveBackAction);
	ClassDB::bind_method(D_METHOD("GetMoveBackAction"), &WhiteboxPlayer::GetMoveBackAction);
	ClassDB::bind_method(D_METHOD("SetMoveLeftAction", "name"), &WhiteboxPlayer::SetMoveLeftAction);
	ClassDB::bind_method(D_METHOD("GetMoveLeftAction"), &WhiteboxPlayer::GetMoveLeftAction);
	ClassDB::bind_method(D_METHOD("SetMoveRightAction", "name"), &WhiteboxPlayer::SetMoveRightAction);
	ClassDB::bind_method(D_METHOD("GetMoveRightAction"), &WhiteboxPlayer::GetMoveRightAction);

	ADD_GROUP("Movement", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "move_speed", PROPERTY_HINT_RANGE, "0.5,20,0.5"), "SetMoveSpeed", "GetMoveSpeed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration", PROPERTY_HINT_RANGE, "1,100,1"), "SetAcceleration", "GetAcceleration");

	ADD_GROUP("Input Actions", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_forward_action"), "SetMoveForwardAction", "GetMoveForwardAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_back_action"), "SetMoveBackAction", "GetMoveBackAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_left_action"), "SetMoveLeftAction", "GetMoveLeftAction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_right_action"), "SetMoveRightAction", "GetMoveRightAction");
}

void WhiteboxPlayer::_physics_process(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) return;

	Input* input = Input::get_singleton();
	if (input == nullptr) return;

	float x = input->get_axis(MoveLeftAction, MoveRightAction);
	float z = input->get_axis(MoveForwardAction, MoveBackAction);
	Vector3 direction(x, 0, z);
	if (direction.length() > 0) direction = direction.normalized();

	float delta = static_cast<float>(p_delta);
	Vector3 TargetVelocity = direction * MoveSpeed;
	Vector3 CurrentVelocity = get_velocity();
	CurrentVelocity.x = Math::move_toward(CurrentVelocity.x, TargetVelocity.x, Acceleration * delta);
	CurrentVelocity.z = Math::move_toward(CurrentVelocity.z, TargetVelocity.z, Acceleration * delta);

	set_velocity(CurrentVelocity);
	move_and_slide();
}

void WhiteboxPlayer::SetMoveSpeed(float v) { MoveSpeed = v; }
float WhiteboxPlayer::GetMoveSpeed() const { return MoveSpeed; }
void WhiteboxPlayer::SetAcceleration(float v) { Acceleration = v; }
float WhiteboxPlayer::GetAcceleration() const { return Acceleration; }
void WhiteboxPlayer::SetMoveForwardAction(const StringName& n) { MoveForwardAction = n; }
StringName WhiteboxPlayer::GetMoveForwardAction() const { return MoveForwardAction; }
void WhiteboxPlayer::SetMoveBackAction(const StringName& n) { MoveBackAction = n; }
StringName WhiteboxPlayer::GetMoveBackAction() const { return MoveBackAction; }
void WhiteboxPlayer::SetMoveLeftAction(const StringName& n) { MoveLeftAction = n; }
StringName WhiteboxPlayer::GetMoveLeftAction() const { return MoveLeftAction; }
void WhiteboxPlayer::SetMoveRightAction(const StringName& n) { MoveRightAction = n; }
StringName WhiteboxPlayer::GetMoveRightAction() const { return MoveRightAction; }

} // namespace godot
