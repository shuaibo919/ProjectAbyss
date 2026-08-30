@tool
extends EditorPlugin

## Packages the ProceduralRock generator for the Data Flow (PCGODOT) graph system.
##
## The generator itself lives in the `abyss` GDExtension (Source/RockGen/) — this
## plugin only registers the Flow node directory so the node shows up in the graph
## editor, exactly as the ancient_building addon does for buildings.
##
## RUNTIME CAVEAT: an EditorPlugin only runs in the editor, so nothing registers this
## directory in an exported game. A saved graph referencing the `procedural_rock`
## template will fail to resolve it at runtime unless something registers the
## directory first. Do it once at startup, e.g. from an autoload:
##
##     FlowNodeRegistry.register_node_directory("res://addons/procedural_rock/nodes")
##
## Baking the generated meshes to disk and using a plain Spawn Meshes node avoids the
## issue entirely, and is the better option for shipped content.

const NODE_DIRECTORY := "res://addons/procedural_rock/nodes"


func _enter_tree() -> void:
	if not ClassDB.class_exists("ProceduralRock"):
		push_warning(
			"procedural_rock: the `abyss` GDExtension is not loaded, so the ProceduralRock "
			+ "class is unavailable. Build it with `scons` from the repo root and restart the editor.")
		return

	FlowNodeRegistry.register_node_directory(NODE_DIRECTORY)


func _exit_tree() -> void:
	FlowNodeRegistry.unregister_node_directory(NODE_DIRECTORY)
