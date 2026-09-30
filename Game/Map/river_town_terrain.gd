extends RefCounted

# Terrain3D ground for Map_RiverTown: a mountain valley whose floor the town sits on, with the river
# carved into it and shown by Terrain3D's own water (the ocean mesh, its sea level at the river
# surface; every other point of the terrain stays above it).
#
# Everything is driven through Terrain3DAgent commands:
#   highlands (noise) → mountain massifs (ridged noise) → stream power + droplet erosion
#   → the valley and town stamps published by river_town_lots (stamp_path)
#   → slope/height texture rules → drainage channels → town ground band
# The result is cached per seed and layout in user://river_town_terrain/, so only the first run of a
# given layout pays for the build (~10 s); later runs just load the regions.
#
# Usage (from a coroutine): var terrain := await RiverTownTerrain.build(parent, description, seed)

const TownTextures := preload("res://Script/NPR/town_texture_library.gd")

const CACHE_DIR := "user://river_town_terrain/"
const CACHE_VERSION := 1   # bump when the command list below changes
const SPACING := 2.0
const HALF_SPAN := 1536.0  # 6 × 6 regions of 256 vertices at 2 m
const OCEAN_MATERIAL := "res://addons/terrain_3d/extras/shaders/M_ocean.tres"

# River water: a canal, not an ocean — small waves, murky within a few metres, faint shore foam.
const WATER_COLOR := Color(0.16, 0.22, 0.20)


## Adds a Terrain3D under `parent`, loads or builds the ground for `description`
## (river_town_lots.last_terrain), and returns it.
static func build(parent: Node3D, description: Dictionary, seed: int) -> Terrain3D:
	var commands := _commands(description, seed)
	var key := "%d_%s" % [seed, JSON.stringify([CACHE_VERSION, commands]).md5_text().substr(0, 12)]
	var dir := CACHE_DIR + key
	var cached := DirAccess.dir_exists_absolute(dir) and not DirAccess.get_files_at(dir).is_empty()
	DirAccess.make_dir_recursive_absolute(dir)

	var terrain := Terrain3D.new()
	terrain.name = "Terrain"
	terrain.vertex_spacing = SPACING
	terrain.data_directory = dir
	terrain.collision_mode = Terrain3DCollision.DISABLED
	parent.add_child(terrain)
	# Terrain3D builds its data on entering the tree, which completes a frame later.
	await parent.get_tree().process_frame

	var agent := Terrain3DAgent.new()
	agent.set_terrain(terrain)
	terrain.assets = TownTextures.terrain_assets(agent)
	terrain.material.world_background = Terrain3DMaterial.NONE
	terrain.material.show_checkered = false
	_setup_water(terrain, float(description.water_y))

	var t0 := Time.get_ticks_msec()
	if not cached:
		var failed := 0
		for result in agent.execute(commands):
			if not result.get("ok", false):
				failed += 1
				push_error("river terrain: %s failed: %s" % [result.get("op", "?"), result.get("error", result)])
		DirAccess.make_dir_recursive_absolute(dir)
		agent.execute({"op": "save", "directory": dir, "assets_path": ""})
		print("river_town: terrain built in %d ms (%d commands, %d failed) -> %s" % [
			Time.get_ticks_msec() - t0, commands.size(), failed, dir])
	else:
		print("river_town: terrain loaded from %s" % dir)
	return terrain


static func _setup_water(terrain: Terrain3D, water_y: float) -> void:
	var water: ShaderMaterial = (load(OCEAN_MATERIAL) as ShaderMaterial).duplicate()
	water.set_shader_parameter("sea_level", water_y)
	water.set_shader_parameter("water_color", WATER_COLOR)
	water.set_shader_parameter("visible_depth", 3.5)
	water.set_shader_parameter("height_scale", 0.18)
	water.set_shader_parameter("ocean_scale", 4.0)
	water.set_shader_parameter("time_scale", 2.0)
	water.set_shader_parameter("foam_enabled", false)
	water.set_shader_parameter("foam_shore_intensity", 0.6)
	terrain.ocean_material = water
	terrain.ocean_enabled = true


static func _commands(description: Dictionary, seed: int) -> Array:
	var area := [-HALF_SPAN, -HALF_SPAN, 2.0 * HALF_SPAN, 2.0 * HALF_SPAN]
	var water_y: float = description.water_y
	var cmds: Array = [
		{"op": "add_regions", "area": area},
		# Highlands: 30-120 m of rolling ground, everything well above the river's water.
		{"op": "apply_noise", "area": area, "mode": "set", "seed": seed, "frequency": 0.0009,
			"octaves": 5, "unsigned": true, "exponent": 1.3, "amplitude": 90.0, "base": 30.0},
	]
	# Massifs either side of the valley and closing its ends; the valley stamp cuts through them.
	# The town frame puts the city bank on +x (n = (t.z, 0, -t.x) for a river along +z).
	for m in [[[780, -150], 720, 420.0, 11], [[-820, 260], 680, 360.0, 12], [[260, 1250], 560, 300.0, 13],
			[[-300, -1250], 560, 320.0, 14], [[1250, 900], 520, 260.0, 15], [[-1200, -800], 520, 260.0, 16]]:
		cmds.append({"op": "apply_noise", "center": m[0], "radius": m[1], "falloff": m[1] * 0.85,
			"mode": "add", "seed": seed * 31 + m[3], "frequency": 0.0022, "fractal_type": "ridged",
			"octaves": 6, "gain": 0.45, "unsigned": true, "exponent": 1.5, "amplitude": m[2]})
	cmds.append_array([
		{"op": "stream_power", "area": area, "k": 2e-5, "seed": seed, "base_level": water_y},
		{"op": "erode", "area": area, "seed": seed, "droplets_per_vertex": 0.6, "thermal_iterations": 4,
			"talus_angle": 40, "base_level": water_y},
	])
	cmds.append_array(description.stamps)
	cmds.append_array([
		{"op": "paint_texture_rules", "area": area, "rules": [
			{"asset_id": TownTextures.TERRAIN_GRAVEL, "height": [-1000.0, water_y + 0.6]},
			{"asset_id": TownTextures.TERRAIN_ROCK, "slope": [36.0, 90.0]},
			{"asset_id": TownTextures.TERRAIN_MOSS, "slope": [24.0, 36.0]},
			{"asset_id": TownTextures.TERRAIN_GRASS},
		]},
		# Gravel along the mountain drainage (pure analysis: k = 0 moves nothing).
		{"op": "stream_power", "area": area, "k": 0.0, "iterations": 1, "valley_angle": 0,
			"paint": {"flow": TownTextures.TERRAIN_GRAVEL, "flow_fraction": 0.012}},
	])
	# Packed earth over the town ground, grass beyond it.
	var town: Dictionary = description.stamps[description.stamps.size() - 1]
	cmds.append({"op": "stamp_path", "paint_only": true, "points": town.points,
		"half_widths": town.half_widths, "profile": [[0.0, 0.0], [110.0, 0.0]],
		"paint": {"asset_id": TownTextures.TERRAIN_DIRT, "from": 0.0, "to": 110.0}})
	return cmds
