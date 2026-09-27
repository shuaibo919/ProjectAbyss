extends SceneTree

# Load the graph's IO entry before its leaf node (the editor does this as well).
func _initialize() -> void:
	load("res://addons/flow_nodes_editor/flow_nodes_io.gd")
	var script = load("res://addons/ancient_building/nodes/ancient_building.gd")
	if not script.can_instantiate():
		quit(1)
		return
	var node = script.new()
	node.settings = load("res://addons/ancient_building/nodes/ancient_building_settings.gd").new()
	var p = ClassDB.instantiate("AncientBuildingParameters")
	node.settings.ridge_detail = 1
	node._apply_tile_detail(p, {})
	var ok: bool = p.ridge_detail == 1
	node._apply_tile_detail(p, {"ab_ridge_detail": 0})
	ok = ok and p.ridge_detail == 0
	var ov := {"ab_width": 9.0, "ab_depth": 6.0, "ab_roof_type": 0, "ab_material_style": 0, "ab_ridge_detail": 0}
	var old_key: String = node._combo_key(ov, 0.25)
	ov.ab_ridge_detail = 1
	ok = ok and old_key != node._combo_key(ov, 0.25)
	print("ridge PCG settings and point override: ", "PASS" if ok else "FAIL")
	node.free()
	quit(0 if ok else 1)
