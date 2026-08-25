@tool
extends EditorPlugin

## Packages the AncientBuilding generator for the Data Flow (PCGODOT) graph system.
##
## The generator itself lives in the `abyss` GDExtension (Source/AncientBuilding/) — this
## plugin only registers the Flow node directory so the node shows up in the graph editor.
## It is the first consumer of FlowNodeRegistry.register_node_directory(), which existed but
## was unused, so if node lookup ever misbehaves that API is the first thing to suspect.
##
## RUNTIME CAVEAT: an EditorPlugin only runs in the editor, so nothing registers this
## directory in an exported game. A saved graph referencing the `ancient_building` template
## will fail to resolve it at runtime unless something registers the directory first. Do it
## once at startup, e.g. from an autoload:
##
##     FlowNodeRegistry.register_node_directory("res://addons/ancient_building/nodes")
##
## Baking the generated meshes to disk and using a plain Spawn Meshes node avoids the issue
## entirely, and is the better option for shipped content.

const NODE_DIRECTORY := "res://addons/ancient_building/nodes"


func _enter_tree() -> void:
	if not ClassDB.class_exists("AncientBuilding"):
		push_warning(
			"ancient_building: the `abyss` GDExtension is not loaded, so the AncientBuilding "
			+ "class is unavailable. Build it with `scons` from the repo root and restart the editor.")
		return

	FlowNodeRegistry.register_node_directory(NODE_DIRECTORY)


func _exit_tree() -> void:
	FlowNodeRegistry.unregister_node_directory(NODE_DIRECTORY)
