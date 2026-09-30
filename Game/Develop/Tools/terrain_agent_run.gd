extends SceneTree

# Command-line front end for Terrain3DAgent: load (or create) a terrain, run a JSON command list,
# print/write the results, optionally save. This is how an agent edits terrain without the editor.
#
#   Engine/bin/godot.windows.editor.x86_64.console.exe --headless --path Game/ \
#       --script res://Develop/Tools/terrain_agent_run.gd -- <options>
#
# Terrain (one of):
#   --scene=res://Map/X.tscn     instance the scene and use its first Terrain3D
#   --data=res://Path/Dir        a bare Terrain3D on this data directory (created on save)
#   (neither)                    a bare in-memory Terrain3D
# Commands (one of):
#   --commands=path.json         a JSON array (or object) of {"op": ..., ...params}
#   --command='{"op": ...}'      inline JSON
# Output:
#   --out=path.json              also write the results there
#   --preview=path.png           save_preview of the final state (--preview-mode=height|texture|color|shade)
#   --save                       save terrain data (and the assets file, if it has one) at the end
#   --save-scene                 repack and save --scene (for property/asset changes stored in it)
#
# Results are printed as one line prefixed TERRAIN_AGENT_RESULT: so they are easy to find in the log.
# Exit code is 1 if any command failed. Command reference: Terrain3DAgent.get_command_names() and
# ProjectAbyssWiki/documentation/systems/TerrainAgent_Spec.md.

var options := {}


func parse_options() -> void:
	for arg in OS.get_cmdline_user_args():
		if not arg.begins_with("--"):
			continue
		var eq := arg.find("=")
		if eq < 0:
			options[arg.substr(2)] = true
		else:
			options[arg.substr(2, eq - 2)] = arg.substr(eq + 1)


func find_terrain(node: Node) -> Terrain3D:
	if node is Terrain3D:
		return node
	for child in node.get_children():
		var found := find_terrain(child)
		if found:
			return found
	return null


## Vectors, colours, NaN and packed arrays are not JSON; convert them to plain values.
func to_json_safe(value: Variant) -> Variant:
	match typeof(value):
		TYPE_FLOAT:
			return null if is_nan(value) or is_inf(value) else value
		TYPE_VECTOR2, TYPE_VECTOR2I:
			return [to_json_safe(float(value.x)), to_json_safe(float(value.y))]
		TYPE_VECTOR3, TYPE_VECTOR3I:
			return [to_json_safe(float(value.x)), to_json_safe(float(value.y)), to_json_safe(float(value.z))]
		TYPE_RECT2, TYPE_RECT2I:
			return [value.position.x, value.position.y, value.size.x, value.size.y]
		TYPE_AABB:
			return {"position": to_json_safe(value.position), "size": to_json_safe(value.size)}
		TYPE_COLOR:
			return "#" + value.to_html(value.a < 1.0)
		TYPE_TRANSFORM3D:
			return {"origin": to_json_safe(value.origin), "basis": [to_json_safe(value.basis.x), to_json_safe(value.basis.y), to_json_safe(value.basis.z)]}
		TYPE_OBJECT:
			if value == null:
				return null
			return value.resource_path if value is Resource and not value.resource_path.is_empty() else str(value)
		TYPE_DICTIONARY:
			var out := {}
			for key in value:
				out[str(key)] = to_json_safe(value[key])
			return out
		TYPE_ARRAY, TYPE_PACKED_FLOAT32_ARRAY, TYPE_PACKED_FLOAT64_ARRAY, TYPE_PACKED_INT32_ARRAY, \
		TYPE_PACKED_INT64_ARRAY, TYPE_PACKED_STRING_ARRAY, TYPE_PACKED_VECTOR2_ARRAY, TYPE_PACKED_VECTOR3_ARRAY, \
		TYPE_PACKED_COLOR_ARRAY:
			var out := []
			for item in value:
				out.append(to_json_safe(item))
			return out
	return value


func load_commands() -> Variant:
	if options.has("commands"):
		var path: String = options["commands"]
		if not FileAccess.file_exists(path):
			return {"ok": false, "error": "No such command file: " + path}
		return FileAccess.get_file_as_string(path)
	if options.has("command"):
		return options["command"]
	return []


func _initialize() -> void:
	parse_options()
	var scene_root: Node = null
	var terrain: Terrain3D = null
	if options.has("scene"):
		var packed: PackedScene = load(options["scene"])
		if packed == null:
			finish([{"ok": false, "error": "Cannot load scene " + str(options["scene"])}])
			return
		scene_root = packed.instantiate()
		root.add_child(scene_root)
		terrain = find_terrain(scene_root)
		if terrain == null:
			finish([{"ok": false, "error": "No Terrain3D in " + str(options["scene"])}])
			return
	else:
		terrain = Terrain3D.new()
		if options.has("data"):
			terrain.data_directory = options["data"]
		root.add_child(terrain)
	# Terrain3D builds its data on entering the tree, which happens after _initialize.
	await process_frame

	var agent := Terrain3DAgent.new()
	agent.set_terrain(terrain)
	var commands: Variant = load_commands()
	var results: Variant = commands if commands is Dictionary else agent.execute(commands)
	if results is Dictionary:
		results = [results]
	if options.has("preview"):
		results.append(agent.execute({"op": "save_preview", "path": options["preview"],
			"mode": options.get("preview-mode", "height")}))
	if options.has("save"):
		results.append(agent.execute({"op": "save"}))
	if options.has("save-scene") and scene_root:
		var repacked := PackedScene.new()
		var err := repacked.pack(scene_root)
		if err == OK:
			err = ResourceSaver.save(repacked, options["scene"])
		results.append({"op": "save_scene", "ok": err == OK, "error": "" if err == OK else error_string(err)})
	finish(results)


func finish(results: Array) -> void:
	var safe: Variant = to_json_safe(results)
	var text := JSON.stringify(safe)
	print("TERRAIN_AGENT_RESULT:", text)
	if options.has("out"):
		var file := FileAccess.open(options["out"], FileAccess.WRITE)
		if file:
			file.store_string(JSON.stringify(safe, "  "))
	var failed := 0
	for result in results:
		if result is Dictionary and not result.get("ok", false):
			failed += 1
	quit(1 if failed > 0 else 0)
