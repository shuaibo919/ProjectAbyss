# Stage 1 gate: ProceduralTree dual-backend integration smoke test.
# Weber-Penn default regression + SlowTree backend through the node layer
# (property setters, _ready path, apply_preset redirect, determinism).
#
# Usage:
#   Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --script res://tests/treegen_backend_smoke.gd
extends SceneTree

func _initialize() -> void:
	var failed := 0

	# 1) Weber-Penn 默认后端回归: 默认路径不受双后端改动影响。
	var weber := ProceduralTree.new()
	root.add_child(weber)
	weber.generate()
	if weber.get_mesh() == null or weber.get_vertex_count() <= 0:
		print("FAIL  weber_default")
		failed += 1
	else:
		print("PASS  weber_default (v=%d t=%d)" % [weber.get_vertex_count(), weber.get_triangle_count()])

	# 2) SlowTree 后端逐预设冒烟: 切后端/预设/种子 → 生成 + 统计。
	# (--script 下 _initialize 阶段不进帧, _ready 不会触发, 显式调用 generate。)
	var preset_count := SlowTreeGenerator.get_preset_count()
	for i in range(preset_count):
		var st := ProceduralTree.new()
		root.add_child(st)
		st.backend = ProceduralTree.BACKEND_SLOWTREE
		st.slowtree_preset = i
		st.seed = 100 + i
		st.generate()
		if st.get_mesh() == null or st.get_vertex_count() <= 0 or st.get_segment_count() <= 0:
			print("FAIL  slowtree_preset_%d" % i)
			failed += 1
		else:
			print("PASS  slowtree_preset_%d (%s: v=%d t=%d s=%d)" % [
				i,
				SlowTreeGenerator.get_preset_name(i),
				st.get_vertex_count(),
				st.get_triangle_count(),
				st.get_segment_count(),
			])

	# 3) 确定性经节点层: 同种子两次 generate 计数一致。
	var d1 := ProceduralTree.new()
	var d2 := ProceduralTree.new()
	root.add_child(d1)
	root.add_child(d2)
	for d in [d1, d2]:
		d.backend = ProceduralTree.BACKEND_SLOWTREE
		d.slowtree_preset = 0
		d.seed = 777
		d.generate()
	if d1.get_vertex_count() == d2.get_vertex_count() and d1.get_triangle_count() == d2.get_triangle_count():
		print("PASS  node_determinism (v=%d)" % d1.get_vertex_count())
	else:
		print("FAIL  node_determinism (%d vs %d)" % [d1.get_vertex_count(), d2.get_vertex_count()])
		failed += 1

	# 4) SlowTree 模式下 apply_preset 重定向到 SlowTree 预设 id。
	var st2 := ProceduralTree.new()
	root.add_child(st2)
	st2.backend = ProceduralTree.BACKEND_SLOWTREE
	st2.apply_preset(1)
	if st2.slowtree_preset == 1:
		print("PASS  apply_preset_redirect")
	else:
		print("FAIL  apply_preset_redirect (got %d)" % st2.slowtree_preset)
		failed += 1

	quit(0 if failed == 0 else 1)
