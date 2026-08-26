# Stage 1/2 gate: structural self-test over every SlowTree preset + the embedded
# HelloTree default template (mesh non-empty / no NaN / sane AABB / determinism /
# v1 node gating), plus Stage 2 GPU-vs-CPU comparison (index bit-identity,
# vertex eps=1e-4, GPU readback mesh). No byte-level golden —
# see Source/TreeGen/SlowTree/UPSTREAM_SYNC.md.
#
# Usage:
#   Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --script res://tests/treegen_slowtree_selftest.gd
extends SceneTree

func _initialize() -> void:
	var result := SlowTreeSelfTest.run_all()
	var lines: Array[String] = []
	lines.append("== SlowTree SelfTest ==")
	for item in result.get("results", []):
		var ok: bool = item.get("ok", false)
		var label := "PASS"
		if not ok:
			label = "FAIL"
		var name_or_preset := str(item.get("name", item.get("test", "?")))
		var detail := ""
		if item.get("test", "") == "gpu_vs_cpu":
			# Stage 2: GPU-vs-CPU 对拍(索引位级/顶点 ε/读回网格)。
			name_or_preset = "GPUvsCPU %s (seed %s)" % [item.get("name", "?"), item.get("seed", 0)]
			detail = " (v=%s t=%s s=%s bitdiff=%s/%s cpu_ms=%s gpu_ms=%s [%s])" % [
				item.get("vertex_count", 0),
				item.get("triangle_count", 0),
				item.get("surface_count", 0),
				item.get("bit_diff_floats", 0),
				item.get("total_floats", 0),
				item.get("cpu_ms", 0.0),
				item.get("gpu_total_ms", 0.0),
				item.get("gpu_phases", ""),
			]
		elif item.has("vertex_count"):
			detail = " (v=%s t=%s s=%s ms=%s)" % [
				item.get("vertex_count", 0),
				item.get("triangle_count", 0),
				item.get("surface_count", 0),
				item.get("generation_ms", 0.0),
			]
		lines.append("%s  %s%s" % [label, name_or_preset, detail])
		if not ok:
			lines.append("     error: %s" % item.get("error", ""))
	for line in lines:
		print(line)
	quit(0 if result.get("ok", false) else 1)
