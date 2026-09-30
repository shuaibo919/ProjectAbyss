extends SceneTree

# Renders the agent-built mountain (Develop/TerrainAgentMountain) from a few framings.
# Needs a real renderer, so no --headless:
#   godot.console.exe --path Game/ --resolution 1600x900 --script res://Develop/TerrainAgentMountain/MountainRender.gd

const OUT := "res://../Reference/Shots/TerrainAgent/"
const SHOTS := {
	"mountain_3d_southeast": [Vector3(1350, 300, 1250), Vector3(60, 300, -110)],
	"mountain_3d_valley": [Vector3(640, 60, 900), Vector3(150, 380, -60)],
	"mountain_3d_aerial": [Vector3(-1700, 1300, 1500), Vector3(0, 150, -100)],
	"mountain_3d_ridge": [Vector3(-1150, 260, -1100), Vector3(60, 420, -110)],
}


func _initialize() -> void:
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.environment.background_mode = Environment.BG_SKY
	env.environment.sky = sky
	env.environment.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.environment.fog_enabled = true
	env.environment.fog_density = 0.00025
	env.environment.fog_aerial_perspective = 0.6
	env.environment.fog_light_color = Color(0.62, 0.7, 0.82)
	root.add_child(env)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-32, -135, 0)
	sun.light_energy = 1.3
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 3000
	root.add_child(sun)

	var scene: Node = load("res://Develop/TerrainAgentMountain/Mountain.tscn").instantiate()
	root.add_child(scene)
	var terrain: Terrain3D = scene.get_node("Terrain3D")

	var cam := Camera3D.new()
	cam.far = 8000
	cam.fov = 55
	root.add_child(cam)
	terrain.set_camera(cam)
	await process_frame # Nodes added in _initialize enter the tree only now; look_at needs that

	for shot in SHOTS:
		cam.position = SHOTS[shot][0]
		cam.look_at(SHOTS[shot][1])
		for i in 20:
			await process_frame
		await RenderingServer.frame_post_draw
		var peak := Vector3(60, terrain.data.get_height(Vector3(60, 0, -110)), -110)
		print(shot, " peak on screen at ", cam.unproject_position(peak), " behind=", cam.is_position_behind(peak))
		root.get_texture().get_image().save_png(ProjectSettings.globalize_path(OUT + shot + ".png"))
		print("saved ", shot)
	quit()
