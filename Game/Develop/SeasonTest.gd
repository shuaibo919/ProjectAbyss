extends Node3D

# Proves the season knob reaches the SlowTree assembly, and that evergreen presets ignore it.
#
# Samples vertex colours, not a render. Geometry is identical across seasons (only colour changes),
# so pick the foliage vertices ONCE at summer and then read those same indices at every season --
# filtering per-season by colour would drop late-autumn leaves, which are dark red, and report zero.

func _preset(name: String) -> int:
	for i in SlowTreeGenerator.get_preset_count():
		if SlowTreeGenerator.get_preset_name(i) == name:
			return i
	return -1

func _colors(idx: int, season: float) -> PackedColorArray:
	var res: Dictionary = SlowTreeGenerator.generate(idx, 0, false, season)
	return (res["mesh"] as ArrayMesh).surface_get_arrays(0)[Mesh.ARRAY_COLOR]

func _ready() -> void:
	for preset_name in ["Ginkgo", "Peach", "Pine", "Bamboo"]:
		var idx := _preset(preset_name)
		var summer := _colors(idx, 2.0)

		# Foliage = saturated green at summer; bark is dark desaturated brown.
		var picks: Array[int] = []
		for i in range(0, summer.size(), 53):
			var c: Color = summer[i]
			if c.g > 0.3 and c.g > c.b * 1.4:
				picks.append(i)
		if picks.is_empty():
			print("%-12s no foliage vertices identified" % preset_name)
			continue

		var line := "%-12s n=%-5d" % [preset_name, picks.size()]
		for season in [0.5, 2.0, 3.2, 3.9]:
			var cols := _colors(idx, season)
			var acc := Color(0, 0, 0)
			for i in picks:
				acc += cols[i]
			acc /= float(picks.size())
			line += "  s%.1f=(%.2f,%.2f,%.2f)" % [season, acc.r, acc.g, acc.b]
		print(line)
	get_tree().quit()
