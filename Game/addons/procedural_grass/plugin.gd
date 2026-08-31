@tool
extends EditorPlugin

## Packages the ProceduralGrass generator for the Data Flow (PCGODOT) graph system.
##
## The generator itself lives in the `abyss` GDExtension (Source/GrassGen/) — this
## plugin only registers the Flow node directory so the node shows up in the graph
## editor, exactly as the procedural_rock addon does for rocks.
##
## RUNTIME CAVEAT: an EditorPlugin only runs in the editor, so nothing registers this
## directory in an exported game. A saved graph referencing the `procedural_grass`
## template will fail to resolve it at runtime unless something registers the
## directory first. Do it once at startup, e.g. from an autoload:
##
##     FlowNodeRegistry.register_node_directory("res://addons/procedural_grass/nodes")
##
## Baking the generated meshes to disk and using a plain Spawn Meshes node avoids the
## issue entirely, and is the better option for shipped content.

const NODE_DIRECTORY := "res://addons/procedural_grass/nodes"


func _enter_tree() -> void:
	if not ClassDB.class_exists("ProceduralGrass"):
		push_warning(
			"procedural_grass: the `abyss` GDExtension is not loaded, so the ProceduralGrass "
			+ "class is unavailable. Build it with `scons` from the repo root and restart the editor.")
		return

	FlowNodeRegistry.register_node_directory(NODE_DIRECTORY)


func _exit_tree() -> void:
	FlowNodeRegistry.unregister_node_directory(NODE_DIRECTORY)
