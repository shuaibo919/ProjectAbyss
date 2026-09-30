extends "res://Develop/TreeGenBranchValidate.gd"

const JOINT_OUTPUT := "E:/ProjectAbyss/Reference/TreeGenPeachJunctions"

func export_joints(mesh: Mesh, path: String):
	var arrays := mesh.surface_get_arrays(0)
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var points: Array = []
	var triangles: Array = []
	var welded := {}
	for triangle in range(0, indices.size(), 3):
		var a := positions[indices[triangle]]
		var b := positions[indices[triangle + 1]]
		var c := positions[indices[triangle + 2]]
		if minf(a.y, minf(b.y, c.y)) > 1.0 or maxf(a.y, maxf(b.y, c.y)) < 0.15:
			continue
		var face := []
		for point in [a, b, c]:
			if not welded.has(point):
				welded[point] = points.size()
				points.append([point.x, point.y, point.z])
			face.append(welded[point])
		triangles.append(face)
	FileAccess.open(path, FileAccess.WRITE).store_string(JSON.stringify({"vertices": points, "triangles": triangles}))

func run():
	for seed_value in [0, 17, 42]:
		var result := SlowTreeGenerator.generate(6, seed_value, false, 1.0,
			{"radial_segments": 12, "crossed_cards": true, "species_rules": true, "growth_debug": true})
		check(result.error.is_empty() and not result.truncated, "Peach junction generation failed")
		verify_skeleton(result, "joint/%d" % seed_value)
		verify_surface(result.mesh, "joint/%d" % seed_value)
		check(result.growth_stats.omitted_junctions == 0, "A joint was dropped")
		var baseline_path := JOINT_OUTPUT + "/before_%d.res" % seed_value
		if FileAccess.file_exists(baseline_path):
			var baseline: Mesh = load(baseline_path)
			check(var_to_bytes(baseline.surface_get_arrays(1)) == var_to_bytes(result.mesh.surface_get_arrays(1)), "Junction repair moved the approved crown")
			var before_wood: PackedInt32Array = baseline.surface_get_arrays(0)[Mesh.ARRAY_INDEX]
			var after_wood: PackedInt32Array = result.mesh.surface_get_arrays(0)[Mesh.ARRAY_INDEX]
			check(after_wood.size() > before_wood.size(), "Peach junction refinement was bypassed")
			check(after_wood.size() <= before_wood.size() * 1.15, "Junction refinement exceeded its local geometry budget")
			if "--export-joints" in OS.get_cmdline_user_args():
				export_joints(baseline, JOINT_OUTPUT + "/before_%d.json" % seed_value)
				export_joints(result.mesh, JOINT_OUTPUT + "/after_%d.json" % seed_value)
		else:
			print("JOINT_BASELINE_NOT_AVAILABLE ", baseline_path)
	print("PEACH_JUNCTION_VALIDATE ", "PASS" if failures.is_empty() else "FAIL", " ", failures)
	quit(0 if failures.is_empty() else 1)
