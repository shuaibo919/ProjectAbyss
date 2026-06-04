#include "WhiteboxPlayer.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

WhiteboxPlayer::WhiteboxPlayer() {}
WhiteboxPlayer::~WhiteboxPlayer() {}

void WhiteboxPlayer::_bind_methods() {
	// Bind all methods FIRST, then register properties that reference them
	ClassDB::bind_method(D_METHOD("set_move_speed", "speed"), &WhiteboxPlayer::set_move_speed);
	ClassDB::bind_method(D_METHOD("get_move_speed"), &WhiteboxPlayer::get_move_speed);
	ClassDB::bind_method(D_METHOD("set_acceleration", "accel"), &WhiteboxPlayer::set_acceleration);
	ClassDB::bind_method(D_METHOD("get_acceleration"), &WhiteboxPlayer::get_acceleration);
	ClassDB::bind_method(D_METHOD("set_move_forward_action", "name"), &WhiteboxPlayer::set_move_forward_action);
	ClassDB::bind_method(D_METHOD("get_move_forward_action"), &WhiteboxPlayer::get_move_forward_action);
	ClassDB::bind_method(D_METHOD("set_move_back_action", "name"), &WhiteboxPlayer::set_move_back_action);
	ClassDB::bind_method(D_METHOD("get_move_back_action"), &WhiteboxPlayer::get_move_back_action);
	ClassDB::bind_method(D_METHOD("set_move_left_action", "name"), &WhiteboxPlayer::set_move_left_action);
	ClassDB::bind_method(D_METHOD("get_move_left_action"), &WhiteboxPlayer::get_move_left_action);
	ClassDB::bind_method(D_METHOD("set_move_right_action", "name"), &WhiteboxPlayer::set_move_right_action);
	ClassDB::bind_method(D_METHOD("get_move_right_action"), &WhiteboxPlayer::get_move_right_action);

	ADD_GROUP("Movement", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "move_speed", PROPERTY_HINT_RANGE, "0.5,20,0.5"), "set_move_speed", "get_move_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration", PROPERTY_HINT_RANGE, "1,100,1"), "set_acceleration", "get_acceleration");

	ADD_GROUP("Input Actions", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_forward_action"), "set_move_forward_action", "get_move_forward_action");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_back_action"), "set_move_back_action", "get_move_back_action");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_left_action"), "set_move_left_action", "get_move_left_action");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_right_action"), "set_move_right_action", "get_move_right_action");
}

void WhiteboxPlayer::_physics_process(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	Input* input = Input::get_singleton();
	if (input == nullptr) {
		return;
	}

	float x = input->get_axis(move_left_action, move_right_action);
	float z = input->get_axis(move_forward_action, move_back_action);
	Vector3 direction(x, 0, z);

	if (direction.length() > 0) {
		direction = direction.normalized();
	}

	float delta = static_cast<float>(p_delta);
	Vector3 target_velocity = direction * move_speed;
	Vector3 current_velocity = get_velocity();
	current_velocity.x = Math::move_toward(current_velocity.x, target_velocity.x, acceleration * delta);
	current_velocity.z = Math::move_toward(current_velocity.z, target_velocity.z, acceleration * delta);

	set_velocity(current_velocity);
	move_and_slide();
}

void WhiteboxPlayer::set_move_speed(float p_speed) { move_speed = p_speed; }
float WhiteboxPlayer::get_move_speed() const { return move_speed; }
void WhiteboxPlayer::set_acceleration(float p_accel) { acceleration = p_accel; }
float WhiteboxPlayer::get_acceleration() const { return acceleration; }

void WhiteboxPlayer::set_move_forward_action(const StringName& p_name) { move_forward_action = p_name; }
StringName WhiteboxPlayer::get_move_forward_action() const { return move_forward_action; }
void WhiteboxPlayer::set_move_back_action(const StringName& p_name) { move_back_action = p_name; }
StringName WhiteboxPlayer::get_move_back_action() const { return move_back_action; }
void WhiteboxPlayer::set_move_left_action(const StringName& p_name) { move_left_action = p_name; }
StringName WhiteboxPlayer::get_move_left_action() const { return move_left_action; }
void WhiteboxPlayer::set_move_right_action(const StringName& p_name) { move_right_action = p_name; }
StringName WhiteboxPlayer::get_move_right_action() const { return move_right_action; }

} // namespace godot
