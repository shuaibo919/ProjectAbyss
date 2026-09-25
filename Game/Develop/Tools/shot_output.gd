extends RefCounted

# Absolute output path for dev screenshots/reports. Resolves under Reference/Shots
# (a sibling of Game/), not under Game/ itself -- PNGs and .import sidecars piling up
# under Develop slow down the editor's filesystem scan on every launch.
#
# Usage: const ShotOutput := preload("res://Develop/Tools/shot_output.gd")
#        ShotOutput.file("Ink", "overview.png")

const REL_ROOT := "../Reference/Shots"


static func dir(subdir: String) -> String:
	# SHOT_SUBDIR env redirects the whole batch (e.g. to a versioned dir while a
	# review agent still reads the previous set).
	if OS.has_environment("SHOT_SUBDIR"):
		subdir = OS.get_environment("SHOT_SUBDIR")
	var path := ProjectSettings.globalize_path("res://").path_join(REL_ROOT).path_join(subdir).simplify_path()
	DirAccess.make_dir_recursive_absolute(path)
	return path


static func file(subdir: String, filename: String) -> String:
	return dir(subdir).path_join(filename)
