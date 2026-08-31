#include "RegisterTypes.h"

#include "InputMapping/InputMapping.hpp"
// CameraManager
#include "CameraManager/CameraManager.hpp"
#include "CameraManager/CameraViewpoint.hpp"
#include "CameraManager/CameraSwitcher.hpp"
#include "CameraManager/CameraRigController.hpp"
#include "CameraManager/WhiteboxPlayer.hpp"
// Terrain3D
#include "Terrain/terrain_3d.h"
#include "Terrain/terrain_3d_editor.h"
// Procedural trees (port of "Real-Time GPU Tree Generation", Kuth et al., HPG 2025)
#include "TreeGen/ProceduralTree.h"
#include "TreeGen/ProceduralTreeEditorPlugin.h"
#include "TreeGen/ProceduralTreeParameters.h"
// SlowTree integration (SpeedTree-style generator + compute-shader pipeline)
#include "TreeGen/SlowTree/SlowTreeCompute.h"
#include "TreeGen/SlowTree/SlowTreeGenerator.h"
#include "TreeGen/SlowTree/SlowTreeSelfTest.h"
// Ancient Chinese architecture (port of Hu & Qin 2020 — see Docs/AncientBuilding_Spec.md)
#include "AncientBuilding/AncientBuilding.h"
#include "AncientBuilding/AncientBuildingEditorPlugin.h"
#include "AncientBuilding/AncientBuildingParameters.h"
#include "AncientBuilding/AncientSplineSweep.h"
// Procedural rocks (CPU port of "Unity Procedural Rock Generation", marching cubes)
#include "RockGen/ProceduralRock.h"
#include "RockGen/ProceduralRockEditorPlugin.h"
#include "RockGen/ProceduralRockParameters.h"
// Procedural grass clumps (original design — see Docs/ProceduralGrass_Spec.md)
#include "GrassGen/ProceduralGrass.h"
#include "GrassGen/ProceduralGrassEditorPlugin.h"
#include "GrassGen/ProceduralGrassParameters.h"


#include <gdextension_interface.h>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_abyss_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		// Always-on editor plugin: no plugin.cfg and nothing under addons/ required.
		GDREGISTER_INTERNAL_CLASS(ProceduralTreeEditorPlugin);
		EditorPlugins::add_by_type<ProceduralTreeEditorPlugin>();

		GDREGISTER_INTERNAL_CLASS(AncientBuildingEditorPlugin);
		EditorPlugins::add_by_type<AncientBuildingEditorPlugin>();

		GDREGISTER_INTERNAL_CLASS(ProceduralRockEditorPlugin);
		EditorPlugins::add_by_type<ProceduralRockEditorPlugin>();

		GDREGISTER_INTERNAL_CLASS(ProceduralGrassEditorPlugin);
		EditorPlugins::add_by_type<ProceduralGrassEditorPlugin>();
		return;
	}

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(InputMapping);
	InputMapping::SetupDefaults();

	GDREGISTER_CLASS(CameraManager);
	GDREGISTER_CLASS(CameraViewpoint);
	GDREGISTER_CLASS(CameraSwitcher);
	GDREGISTER_CLASS(CameraRigController);
	GDREGISTER_CLASS(WhiteboxPlayer);

	GDREGISTER_CLASS(Terrain3D);
	GDREGISTER_CLASS(Terrain3DAssets);
	GDREGISTER_CLASS(Terrain3DData);
	GDREGISTER_CLASS(Terrain3DEditor);
	GDREGISTER_CLASS(Terrain3DCollision);
	GDREGISTER_CLASS(Terrain3DInstancer);
	GDREGISTER_CLASS(Terrain3DMaterial);
	GDREGISTER_CLASS(Terrain3DMeshAsset);
	GDREGISTER_CLASS(Terrain3DRegion);
	GDREGISTER_CLASS(Terrain3DTextureAsset);
	GDREGISTER_CLASS(Terrain3DUtil);

	GDREGISTER_CLASS(ProceduralTreeLeafParameters);
	GDREGISTER_CLASS(ProceduralTreeFruitParameters);
	GDREGISTER_CLASS(ProceduralTreeParameters);
	GDREGISTER_CLASS(ProceduralTree);

	GDREGISTER_CLASS(SlowTreeCompute);
	GDREGISTER_CLASS(SlowTreeGenerator);
	GDREGISTER_CLASS(SlowTreeSelfTest);

	GDREGISTER_CLASS(AncientSplineSweep);
	GDREGISTER_CLASS(AncientBuildingParameters);
	GDREGISTER_CLASS(AncientBuilding);

	GDREGISTER_CLASS(ProceduralRockParameters);
	GDREGISTER_CLASS(ProceduralRock);

	GDREGISTER_CLASS(ProceduralGrassParameters);
	GDREGISTER_CLASS(ProceduralGrass);
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
