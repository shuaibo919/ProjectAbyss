extends "res://Develop/AncientBuildingDwelling.gd"

# Fixed cameras for the ridge junction; inherits the dwelling lighting and presets.
# --capture --tag=before|after --tile-detail=2 --bedding=0.03 --ridge-detail=1
func capture() -> void:
	var tag := _arg_str("tag", "after")
	DirAccess.make_dir_recursive_absolute(OUT_DIR)
	if _arg_bool("hip", false):
		var subject := {"key": "gable_hip", "x": 27.0, "roof_type": 1, "gable_decoration": 0, "cam3": "flush", "note": "歇山接头"}
		subjects.append(subject)
		buildings.append(_make_building(subject, _make_parameters(subject)))
	for i in subjects.size():
		var subject: Dictionary = subjects[i]
		show_only([i])
		await _shot(subject, {"pos": Vector3(15.5, 13.0, 0.8), "look": Vector3(4.5, 10.5, 0.0)},
			"junction_%s_%s" % [subject["key"], tag])
		await _shot(subject, {"pos": Vector3(9.0, 13.0, 3.2), "look": Vector3(5.3, 11.65, 0.0)},
			"junction_close_%s_%s" % [subject["key"], tag])
		await _shot(subject, CAM_M10_VERGE_END, "verge_end_%s_%s" % [subject["key"], tag])
	print("ridge review complete -> %s" % OUT_DIR)
	quit()
