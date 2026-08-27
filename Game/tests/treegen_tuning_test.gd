# Stage 2.5: 4 个形变旋钮冒烟测试。
# 验证: 粗细旋钮改变几何但顶点数不变; 密度旋钮改变顶点数。
# 语义(哪个部位变粗)由树重拍截图判定; 这里只证机制连通。
# Usage:
#   Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --script res://tests/treegen_tuning_test.gd
extends SceneTree

func _max_disp(a: Dictionary, b: Dictionary) -> float:
	# 两棵同名预设/同种子的网格顶点一一对应(索引位级一致的前提), 取逐顶点最大位移。
	var pa: PackedVector3Array = (a["mesh"] as ArrayMesh).surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
	var pb: PackedVector3Array = (b["mesh"] as ArrayMesh).surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
	if pa.size() != pb.size():
		return INF
	var m := 0.0
	for i in pa.size():
		m = maxf(m, (pa[i] - pb[i]).length())
	return m

func _initialize() -> void:
	var base := SlowTreeGenerator.generate(3, 0, false, 2.0)
	var thick := SlowTreeGenerator.generate(3, 0, false, 2.0, {"trunk_thickness": 2.0})
	var thin := SlowTreeGenerator.generate(3, 0, false, 2.0, {"trunk_thickness": 0.5})
	var dense := SlowTreeGenerator.generate(3, 0, false, 2.0, {"branch_density": 1.5})
	var sparse := SlowTreeGenerator.generate(3, 0, false, 2.0, {"branch_density": 0.5})
	var rooty := SlowTreeGenerator.generate(3, 0, false, 2.0, {"root_thickness": 2.0})
	var branchy := SlowTreeGenerator.generate(3, 0, false, 2.0, {"branch_thickness": 2.0})

	var ok := true
	var v_base: int = base["vertex_count"]

	# 1) 主干粗细: 顶点数不变, 几何位移显著(半径量级 ×2)。
	var d_t := _max_disp(base, thick)
	var d_n := _max_disp(base, thin)
	print("trunk x2: verts %d/%d, max_disp %.3f (expect >0.15); x0.5: max_disp %.3f"
			% [v_base, thick["vertex_count"], d_t, d_n])
	if thick["vertex_count"] != v_base or not (d_t > 0.15 and d_n > 0.01):
		print("  FAIL trunk thickness")
		ok = false

	# 2) 枝杈密度: 顶点数随 1.5x/0.5x 变化。
	var v_d: int = dense["vertex_count"]
	var v_s: int = sparse["vertex_count"]
	print("density: verts %d -> %d (x1.5), -> %d (x0.5)" % [v_base, v_d, v_s])
	if not (v_d > v_base * 1.2 and v_s < v_base * 0.8):
		print("  FAIL density")
		ok = false

	# 3) 根部粗细: 顶点数不变, 根部几何位移显著。
	var d_r := _max_disp(base, rooty)
	print("root x2: verts %d/%d, max_disp %.3f (expect >0.05)" % [v_base, rooty["vertex_count"], d_r])
	if rooty["vertex_count"] != v_base or d_r < 0.05:
		print("  FAIL root thickness")
		ok = false

	# 4) 枝杈粗细: 顶点数不变, 几何位移显著。
	var d_b := _max_disp(base, branchy)
	print("branch x2: verts %d/%d, max_disp %.3f (expect >0.15)"
			% [v_base, branchy["vertex_count"], d_b])
	if branchy["vertex_count"] != v_base or d_b < 0.15:
		print("  FAIL branch thickness")
		ok = false

	print("== TuningTest %s ==" % ("PASS" if ok else "FAIL"))
	quit(0 if ok else 1)
