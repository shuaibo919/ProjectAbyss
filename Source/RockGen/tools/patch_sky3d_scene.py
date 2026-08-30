import io
import re

P = r"D:\VibeSpace\ProjectAbyss\Game\Map\Map_PcgInkValidation.tscn"
s = io.open(P, encoding="utf-8").read()

# ---------------------------------------------------------------- ext resources
ext_extra = (
    '[ext_resource type="Script" path="res://addons/sky_3d/src/Sky3D.gd" id="10_skyscript"]\n'
    '[ext_resource type="Script" path="res://addons/sky_3d/src/TimeOfDay.gd" id="11_tod"]\n'
    '[ext_resource type="Script" path="res://addons/sky_3d/src/SkyDome.gd" id="12_dome"]\n'
    '[ext_resource type="Shader" path="res://addons/sky_3d/shaders/SkyMaterial.gdshader" id="13_skymat"]\n'
    '[ext_resource type="Texture2D" path="res://addons/sky_3d/assets/thirdparty/textures/milkyway/Milkyway.jpg" id="14_milky"]\n'
    '[ext_resource type="Texture2D" path="res://addons/sky_3d/assets/textures/noiseClouds.png" id="15_clouds"]\n'
    '[ext_resource type="Texture2D" path="res://addons/sky_3d/assets/resources/SNoise.tres" id="16_snoise"]\n'
    '[ext_resource type="Texture2D" path="res://addons/sky_3d/assets/thirdparty/textures/moon/MoonMap.png" id="17_moonmap"]\n'
    '[ext_resource type="Texture2D" path="res://addons/sky_3d/assets/textures/noise.jpg" id="18_noisejpg"]\n'
    '[ext_resource type="Texture2D" path="res://addons/sky_3d/assets/thirdparty/textures/milkyway/StarField.jpg" id="19_starfield"]\n'
    '[ext_resource type="Material" path="res://Assets/Shaders/Water/OceanWater.tres" id="20_ocean"]\n'
)
anchor = '[ext_resource type="Texture2D" uid="uid://cmlqxlsawcjrg" path="res://Assets/Shaders/InkPainting/Textures/MatCap.png" id="7_matcap"]\n'
assert anchor in s
s = s.replace(anchor, anchor + ext_extra, 1)

# ---------------------------------------------------------------- sub resources (appended before first [node)
sky_mat = '''[sub_resource type="ShaderMaterial" id="SkyMat"]
shader = ExtResource("13_skymat")
shader_parameter/sky_visible = true
shader_parameter/color_correction = Vector2(0, 1)
shader_parameter/ground_color = Color(0.3, 0.3, 0.3, 1)
shader_parameter/horizon_offset = 0.0
shader_parameter/atm_darkness = 0.5
shader_parameter/atm_sun_intensity = 18.0
shader_parameter/atm_day_tint = Color(0.807843, 0.909804, 1, 1)
shader_parameter/atm_horizon_light_tint = Color(0.980392, 0.635294, 0.462745, 1)
shader_parameter/atm_night_tint = Color(0.057391796, 0.068069525, 0.085420445, 0.34034762)
shader_parameter/atm_level_params = Vector3(1, 0, 0)
shader_parameter/atm_thickness = 0.7
shader_parameter/atm_beta_ray = Vector3(5.804544e-06, 1.3562913e-05, 3.311258e-05)
shader_parameter/atm_beta_mie = Vector3(3.038e-08, 3.038e-08, 3.038e-08)
shader_parameter/sun_light_color = Color(0.98, 0.523, 0.294, 1)
shader_parameter/sun_disk_color = Color(0.996094, 0.541334, 0.140076, 1)
shader_parameter/sun_disk_intensity = 30.0
shader_parameter/sun_disk_size = 0.02
shader_parameter/atm_sun_mie_tint = Color(1, 1, 1, 1)
shader_parameter/atm_sun_mie_intensity = 1.0
shader_parameter/atm_sun_partial_mie_phase = Vector3(0.36, 1.64, 1.6)
shader_parameter/moon_color = Color(1, 1, 1, 1)
shader_parameter/moon_texture = ExtResource("17_moonmap")
shader_parameter/moon_texture_alignment = Vector3(7, 1.4, 4.8)
shader_parameter/moon_texture_flip_u = false
shader_parameter/moon_texture_flip_v = false
shader_parameter/moon_size = 0.07
shader_parameter/atm_moon_mie_tint = Color(0.137255, 0.184314, 0.292196, 1)
shader_parameter/atm_moon_mie_intensity = 0.23658217810094354
shader_parameter/atm_moon_partial_mie_phase = Vector3(0.36, 1.64, 1.6)
shader_parameter/starmap_color = Color(0.709804, 0.709804, 0.709804, 0.854902)
shader_parameter/starmap_texture = ExtResource("14_milky")
shader_parameter/starmap_flip_u = false
shader_parameter/starmap_flip_v = false
shader_parameter/starmap_alignment = Vector3(2.68288, -0.25891, 0.40101)
shader_parameter/star_rotation_offset = 9.38899
shader_parameter/star_rotation = -9.711138284154199
shader_parameter/star_tilt = -1.2915436464758039
shader_parameter/star_field_color = Color(1, 1, 1, 1)
shader_parameter/star_field_texture = ExtResource("19_starfield")
shader_parameter/star_scintillation = 0.75
shader_parameter/star_scintillation_speed = 0.01
shader_parameter/noise_tex = ExtResource("18_noisejpg")
shader_parameter/clouds_night_color = Color(0.090196, 0.094118, 0.129412, 1)
shader_parameter/cirrus_visible = true
shader_parameter/cirrus_intensity = 2.0
shader_parameter/cirrus_coverage = 0.5
shader_parameter/cirrus_thickness = 1.7
shader_parameter/cirrus_absorption = 2.0
shader_parameter/cirrus_sky_tint_fade = 0.5
shader_parameter/cirrus_size = 1.0
shader_parameter/cirrus_uv = Vector2(0.16, 0.11)
shader_parameter/cirrus_position1 = Vector2(0.7308965, 5.0460643e-05)
shader_parameter/cirrus_position2 = Vector2(0.7308965, 5.0460643e-05)
shader_parameter/cirrus_texture = ExtResource("16_snoise")
shader_parameter/cumulus_visible = true
shader_parameter/cumulus_intensity = 0.6
shader_parameter/cumulus_coverage = 0.55
shader_parameter/cumulus_thickness = 0.0243
shader_parameter/cumulus_absorption = 2.0
shader_parameter/cumulus_noise_freq = 2.7
shader_parameter/cumulus_sky_tint_fade = 0.0
shader_parameter/cumulus_size = 0.5
shader_parameter/cumulus_position = Vector2(79.19066, 0.00029048725)
shader_parameter/cumulus_texture = ExtResource("15_clouds")
shader_parameter/cumulus_partial_mie_phase = Vector3(0.957564, 1.042436, 0.412)
shader_parameter/cumulus_mie_intensity = 1.0
shader_parameter/show_azimuthal_grid = false
shader_parameter/show_equatorial_grid = false

[sub_resource type="Sky" id="Sky3dSky"]
sky_material = SubResource("SkyMat")

[sub_resource type="Environment" id="EnvSky3D"]
background_mode = 2
sky = SubResource("Sky3dSky")
ambient_light_source = 3
ambient_light_color = Color(0.38921005, 0.23168632, 0.16043657, 1)
ambient_light_sky_contribution = 0.7
reflected_light_source = 2
tonemap_mode = 3
tonemap_white = 6.0

[sub_resource type="CameraAttributesPractical" id="CamAttrSky3D"]

[sub_resource type="PlaneMesh" id="SeaMesh"]
size = Vector2(3000, 3000)

'''
first_node = s.index("\n[node ")
s = s[:first_node] + "\n" + sky_mat + s[first_node + 1:]

# ---------------------------------------------------------------- nodes: replace InkEnvironment with Sky3D tree
m_env = re.search(
    r'\[node name="InkEnvironment" type="WorldEnvironment" parent="\."[^\]]*\](?:(?!\n\[node|\n\[sub_resource).)*?\n\n',
    s, re.S)
assert m_env, "InkEnvironment block not found"
old_env = m_env.group(0)
sky_node = '''[node name="Sky3D" type="WorldEnvironment" parent="."]
environment = SubResource("EnvSky3D")
camera_attributes = SubResource("CamAttrSky3D")
script = ExtResource("10_skyscript")
current_time = 6.15
wind_speed = 7.0
wind_direction = 1.5708

[node name="SunLight" type="DirectionalLight3D" parent="Sky3D"]
transform = Transform3D(0.3947903, 0.037070245, 0.9180231, 0, 0.9991857, -0.040347632, -0.91877127, 0.015928853, 0.3944688, 0.9180231, -0.040347632, 0.3944688)
light_color = Color(0.98, 0.523, 0.294, 1)
light_energy = 0.0
directional_shadow_blend_splits = true
directional_shadow_max_distance = 600.0

[node name="MoonLight" type="DirectionalLight3D" parent="Sky3D"]
transform = Transform3D(0, -0.9609946, 0.27656707, -0.27634087, -0.26579747, -0.9235732, 0.9610597, -0.07642679, -0.2655621, 0.27656707, -0.9235732, -0.2655621)
light_color = Color(0.572549, 0.776471, 0.956863, 1)
light_energy = 0.0
directional_shadow_blend_splits = true
directional_shadow_max_distance = 256.0

[node name="SkyDome" type="Node" parent="Sky3D"]
script = ExtResource("12_dome")
sun_azimuth = -1.9766359
sun_altitude = -1.6111549
moon_azimuth = -0.80569494
moon_altitude = -2.7480938
fog_density = 0.01
wind_speed = 7.0
wind_direction = 1.5708

[node name="TimeOfDay" type="Node" parent="Sky3D"]
script = ExtResource("11_tod")
dome_path = NodePath("../SkyDome")
current_time = 6.15

'''
s = s.replace(old_env, sky_node, 1)

# ---------------------------------------------------------------- drop old SunLight (flat ink light)
m = re.search(
    r'\[node name="SunLight" type="DirectionalLight3D" parent="\."[^\]]*\](?:(?!\n\[node).)*?\n\n',
    s, re.S)
assert m, "old SunLight not found"
s = s.replace(m.group(0), "")

# ---------------------------------------------------------------- drop obsolete env subs
for sub_id in ["ProceduralSkyMaterial_w7yj3", "Sky_a8ff4", "Environment_ink"]:
    m = re.search(r'\[sub_resource type="[^"]+" id="%s"\](?:(?:.*?)(?=\n\[|\Z))' % sub_id, s, re.S)
    if m:
        s = s.replace(m.group(0).rstrip("\n") + "\n\n", "")

# ---------------------------------------------------------------- sea node (after Camera3D)
sea = '''[node name="Sea" type="MeshInstance3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -0.6, 0)
mesh = SubResource("SeaMesh")
material_override = ExtResource("20_ocean")

'''
campos = 'current = true\n'
idx = s.find(campos)
assert idx != -1
s = s[:idx + len(campos)] + "\n" + sea + s[idx + len(campos):]

# ---------------------------------------------------------------- load_steps recount (optional; editor omits it)
m = re.search(r'load_steps=(\d+)', s)
if m:
    s = s.replace("load_steps=%d" % int(m.group(1)), "load_steps=%d" % (int(m.group(1)) + 21), 1)

io.open(P, "w", encoding="utf-8", newline="\n").write(s)
print("patched")
