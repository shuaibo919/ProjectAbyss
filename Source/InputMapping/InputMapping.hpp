#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/array.hpp>

namespace godot {

class InputEvent;

class InputMapping : public Node {
	GDCLASS(InputMapping, Node)

protected:
	static void _bind_methods();

public:
	InputMapping();
	~InputMapping();

	static void SetupDefaults();
	static void AddAction(const StringName& p_name, const TypedArray<InputEvent>& p_events);
	static void RemoveAction(const StringName& p_name);

private:
	static InputEvent* _MakeKey(Key p_key);
	static InputEvent* _MakeMouseButton(MouseButton p_button);
};

} // namespace godot
