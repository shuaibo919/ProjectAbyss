#include "RegisterTypes.h"

#include "Camera/CameraManager.hpp"
#include "Camera/CameraViewpoint.hpp"
#include "Camera/CameraSwitcher.hpp"
#include "Camera/CameraRigController.hpp"
#include "Camera/WhiteboxPlayer.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_abyss_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(CameraManager);
	GDREGISTER_CLASS(CameraViewpoint);
	GDREGISTER_CLASS(CameraSwitcher);
	GDREGISTER_CLASS(CameraRigController);
	GDREGISTER_CLASS(WhiteboxPlayer);
}

void uninitialize_abyss_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT InitAbyssLibrary(
	GDExtensionInterfaceGetProcAddress p_get_proc_address,
	const GDExtensionClassLibraryPtr p_library,
	GDExtensionInitialization* r_initialization) {

	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_abyss_module);
	init_obj.register_terminator(uninitialize_abyss_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
