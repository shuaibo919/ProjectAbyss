# Stage 0 gate: runs the SlowTree hello-compute probe against the fork's D3D12
# RenderingDevice and exits 0 only if the round trip verified bit-exact.
#
# Usage:
#   Engine/bin/godot.windows.editor.x86_64.exe --path Game --script res://tests/treegen_hello_compute.gd
extends SceneTree

func _initialize() -> void:
	var result := SlowTreeCompute.hello_compute_probe(1 << 20, true)
	quit(0 if result.get("ok", false) and result.get("verified", false) else 1)
