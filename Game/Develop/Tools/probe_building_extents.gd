extends SceneTree

# Probe: measured platform / roof extents of baked AncientBuildings vs the courtyard planner's
# assumptions (PLATFORM margin, Table 1 eave overhang). Prints one line per case.

func _initialize() -> void:
	for roof in [0, 3, 1, 4]:
		for w in [5.0, 8.0, 12.0]:
			var b = ClassDB.instantiate("AncientBuilding")
			var p = ClassDB.instantiate("AncientBuildingParameters")
			p.width = w
			p.depth = 6.0
			p.roof_type = roof
			p.bays_x = 3 if w >= 7.0 else 1
			p.bays_z = 2
			p.set("generate_steps", false)
			b.parameters = p
			var mesh: ArrayMesh = b.bake_mesh()
			var lo := Vector3(INF, INF, INF)
			var hi := -lo
			var plat_lo := lo
			var plat_hi := hi
			for s in mesh.get_surface_count():
				var v: PackedVector3Array = mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX]
				for q in v:
					lo = lo.min(q)
					hi = hi.max(q)
					if q.y < 0.3:
						plat_lo = plat_lo.min(q)
						plat_hi = plat_hi.max(q)
			print("roof %d w %.1f d 6: all x[%.2f %.2f] z[%.2f %.2f] y %.2f | ground x[%.2f %.2f] z[%.2f %.2f] | eave %.2f platform h %.2f" % [
				roof, w, lo.x, hi.x, lo.z, hi.z, hi.y, plat_lo.x, plat_hi.x, plat_lo.z, plat_hi.z,
				p.get_eave_overhang(), p.get_platform_height() if p.has_method("get_platform_height") else -1.0])
			b.free()
	quit()
