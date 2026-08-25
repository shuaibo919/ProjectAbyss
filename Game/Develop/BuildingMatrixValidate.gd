extends Node3D

# Matrix check over all nine roof types, applying the same plan-legality rule the editor dock
# enforces (centralised roofs get a polygonal plan with depth == width; the rest stay
# rectangular). This is the dock's contract: if a type is unreachable or produces nothing, the
# dock would silently offer a broken option.
#
# Run: godot --path Game/ res://Develop/BuildingMatrixValidate.tscn

const ROOF_TYPES := [
	{"name": "FlushGable", "type": 0, "polygonal": false},
	{"name": "GableAndHip", "type": 1, "polygonal": false},
	{"name": "Hip", "type": 2, "polygonal": false},
	{"name": "OverhangingGable", "type": 3, "polygonal": false},
	{"name": "RoundRidge", "type": 4, "polygonal": false},
	{"name": "Hollow", "type": 5, "polygonal": false},
	{"name": "Pyramidal", "type": 6, "polygonal": true},
	{"name": "Round", "type": 7, "polygonal": true},
	{"name": "Helmet", "type": 8, "polygonal": true},
]


func _ready() -> void:
	var failures := 0

	for entry in ROOF_TYPES:
		var p = ClassDB.instantiate("AncientBuildingParameters")
		p.roof_type = entry["type"]
		p.width = 10.0
		if entry["polygonal"]:
			# Exactly what the dock does when a centralised roof is chosen.
			p.sides = 8
			p.depth = p.width
		else:
			p.sides = 4
			p.depth = 7.0

		var b = ClassDB.instantiate("AncientBuilding")
		b.auto_regenerate = false
		b.parameters = p
		b.generate()

		var tris: int = b.get_triangle_count()
		var aabb: AABB = b.get_aabb()

		# A building must have geometry, finite bounds, and sit above the ground plane.
		var ok: bool = tris > 500 \
			and aabb.size.y > 1.0 \
			and is_finite(aabb.size.x) and is_finite(aabb.size.y) and is_finite(aabb.size.z) \
			and aabb.position.y > -0.5

		# Table 1 must hold regardless of roof type.
		var module_ok: bool = absf(p.get_module() - p.width * 0.8 / 11.0) < 0.0005
		var eave_ok: bool = absf(p.get_eave_height() - p.width * 0.8) < 0.0005

		if not (ok and module_ok and eave_ok):
			failures += 1

		print("%-18s tris=%6d size=%5.1fx%5.1fx%5.1f table1=%s %s" % [
			entry["name"], tris, aabb.size.x, aabb.size.y, aabb.size.z,
			"ok" if (module_ok and eave_ok) else "BAD",
			"PASS" if (ok and module_ok and eave_ok) else "FAIL",
		])

		b.free()

	print("MATRIX RESULT: %s (%d/%d types)" % [
		"PASS" if failures == 0 else "FAIL", ROOF_TYPES.size() - failures, ROOF_TYPES.size()])

	get_tree().quit()
