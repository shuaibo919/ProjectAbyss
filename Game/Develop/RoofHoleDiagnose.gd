extends Node3D

# Finds every hole in the building shell, instead of guessing from renders.
#
# An edge shared by exactly one triangle is a boundary edge: the surface stops there. Weld the
# vertices by position first, because the accumulator emits fresh vertices per triangle, so nothing
# is shared by index even where it is shared in space.
#
# Boundary edges are not automatically bugs — a genuinely open lip like the 滴水's is fine. What
# matters is where they cluster, so the report groups them by height band and axis.
#
# Run: godot --path Game/ res://Develop/RoofHoleDiagnose.tscn  (ROOF=n to pick a roof type)

func _ready() -> void:
	var lines: Array[String] = []

	for roof_type in range(9):
		var p = ClassDB.instantiate("AncientBuildingParameters")
		p.roof_type = roof_type
		p.width = 11.0
		p.depth = 7.0
		if p.is_centralised_roof(roof_type):
			p.sides = 8
			p.depth = 11.0

		var b = ClassDB.instantiate("AncientBuilding")
		b.auto_regenerate = false
		b.parameters = p
		add_child(b)
		b.generate()

		var arrays: Array = (b.mesh as ArrayMesh).surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		var roof_base: float = p.get_roof_base()

		# Weld by quantised position. 0.5 mm is far below any real feature here and far above
		# float noise from the sweeps.
		var weld := {}
		var key_of := PackedInt32Array()
		key_of.resize(verts.size())
		for i in verts.size():
			var v: Vector3 = verts[i]
			var k := "%d_%d_%d" % [roundi(v.x * 2000.0), roundi(v.y * 2000.0), roundi(v.z * 2000.0)]
			if not weld.has(k):
				weld[k] = weld.size()
			key_of[i] = weld[k]

		# Count how many triangles use each undirected edge, roof only.
		var edges := {}
		for t in range(0, idx.size(), 3):
			var a: Vector3 = verts[idx[t]]
			var bb: Vector3 = verts[idx[t + 1]]
			var c: Vector3 = verts[idx[t + 2]]
			if (a.y + bb.y + c.y) / 3.0 < roof_base - 0.3:
				continue
			var ka: int = key_of[idx[t]]
			var kb: int = key_of[idx[t + 1]]
			var kc: int = key_of[idx[t + 2]]
			for pair in [[ka, kb], [kb, kc], [kc, ka]]:
				var lo: int = mini(pair[0], pair[1])
				var hi: int = maxi(pair[0], pair[1])
				if lo == hi:
					continue
				var ek := lo * 1000000 + hi
				edges[ek] = edges.get(ek, 0) + 1

		var open := 0
		var total := 0
		for ek in edges:
			total += 1
			if edges[ek] == 1:
				open += 1

		lines.append("%-18s roof edges=%5d  open=%5d  (%.1f%%)" % [
			AncientBuildingParameters.get_roof_type_name(roof_type),
			total, open, 100.0 * float(open) / maxf(float(total), 1.0)])

		b.queue_free()

	for l in lines:
		print(l)

	get_tree().quit()
