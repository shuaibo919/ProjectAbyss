extends SceneTree

# One-off check: print the enum hint strings of AncientBuildingParameters so the
# String::utf8 fix can be verified headlessly.
# Run: Engine/bin/godot.windows.editor.x86_64.console.exe --path Game/ -s res://Develop/Tools/check_enum_hints.gd

func _init() -> void:
	var params = ClassDB.instantiate("AncientBuildingParameters")
	for p in params.get_property_list():
		if p.get("hint") == PROPERTY_HINT_ENUM and p.get("name") in [
			"tile_detail", "lod_level", "ridge_detail", "material_style", "roof_type"]:
			print("%s => %s" % [p["name"], p["hint_string"]])
	quit()
