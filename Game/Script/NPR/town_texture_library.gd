extends RefCounted

# ambientCG texture sets for the StyleShowcase towns — building slot materials, whitebox materials
# and Terrain3D texture assets, all built from the CC0 downloads in Assets/Textures/AmbientCG/
# (fetched with the ambientcg-assets skill, 1K-PNG).
#
# Meshes: StandardMaterial3D, object-space triplanar (the generated buildings' UVs are not laid out
# for tiling textures, and object space keeps a texture glued to a building however it is rotated),
# albedo × vertex colour × a gain that brings the texture's mean luminance to 1 — so the texture adds
# grain and hue while the generator's palette still decides how light or dark a part is — plus the
# NormalGL map.
#
# Terrain: albedo+height / normal+roughness packed by Terrain3DAgent.pack_texture into lossless
# portable textures (one size, one format — Terrain3D builds texture arrays from them), cached
# under Assets/Textures/AmbientCG/_Terrain3D/.
#
# Usage: const TownTextures := preload("res://Script/NPR/town_texture_library.gd")
#        TownTextures.material("brick"); TownTextures.building_slot_materials()

const ACG_DIR := "res://Assets/Textures/AmbientCG/"
const PACKED_DIR := "res://Assets/Textures/AmbientCG/_Terrain3D/"
const TERRAIN_TEXTURE_SIZE := 1024

## name → [asset id, metres per texture repeat, desaturate the albedo].
## Desaturation keeps the texture's grain and value but drops its hue, for sets whose colour must
## come entirely from the generator's palette (黛瓦 roofs).
const PROCESSED_DIR := "res://Assets/Textures/AmbientCG/_Processed/"
const SETS := {
	"tile": ["Clay001", 1.2, true],
	"timber": ["Wood060", 1.2],
	"stone": ["PavingStones125A", 2.4],
	"plaster": ["Plaster001", 2.0],
	"brick": ["Bricks066", 2.0],
	"revetment": ["Bricks075A", 2.5],
	"paving": ["PavingStones070", 2.0],
}

## AncientBuilding material slots (EMaterialSlot order): 瓦, 木构, 石作, 墙面, 脊, 山花.
const SLOT_SETS := ["tile", "timber", "stone", "plaster", "tile", "plaster"]

## Terrain3D texture ids, in order: [asset id, uv scale, detiling rotation].
const TERRAIN_SETS := [
	["Grass004", 0.25, 0.3],   # 0 grass
	["Ground103", 0.30, 0.2],  # 1 dirt (town ground)
	["Rock030", 0.08, 0.1],    # 2 rock
	["Gravel040", 0.35, 0.2],  # 3 gravel (river bed, waterline)
	["Ground037", 0.20, 0.3],  # 4 mossy ground (steep grass)
]
const TERRAIN_GRASS := 0
const TERRAIN_DIRT := 1
const TERRAIN_ROCK := 2
const TERRAIN_GRAVEL := 3
const TERRAIN_MOSS := 4

static var _materials := {}


static func map_path(asset_id: String, map: String) -> String:
	return "%s%s/%s_1K-PNG_%s.png" % [ACG_DIR, asset_id, asset_id, map]


static func has_set(name: String) -> bool:
	return SETS.has(name)


## Cached per name, so every mesh sharing a set shares the material (and the GPU batch).
static func material(name: String) -> StandardMaterial3D:
	if _materials.has(name):
		return _materials[name]
	var entry: Array = SETS[name]
	var asset_id: String = entry[0]
	var m := StandardMaterial3D.new()
	m.resource_name = "acg_" + name
	var albedo_path := map_path(asset_id, "Color")
	if entry.size() > 2 and entry[2]:
		albedo_path = _desaturated(map_path(asset_id, "Color"))
	m.vertex_color_use_as_albedo = true
	m.albedo_texture = load(albedo_path)
	m.albedo_color = _unit_luminance_gain(albedo_path)
	m.normal_enabled = true
	m.normal_texture = load(map_path(asset_id, "NormalGL"))
	m.roughness = 0.9
	m.uv1_triplanar = true
	m.uv1_world_triplanar = false
	m.uv1_triplanar_sharpness = 4.0
	var repeat := 1.0 / float(entry[1])
	m.uv1_scale = Vector3(repeat, repeat, repeat)
	m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	_materials[name] = m
	return m


static func building_slot_materials() -> Array[Material]:
	var out: Array[Material] = []
	for name in SLOT_SETS:
		out.append(material(name))
	return out


## Terrain3DAssets with every TERRAIN_SETS entry, packing (once) any that is not cached yet.
static func terrain_assets(agent: Object) -> Terrain3DAssets:
	var assets := Terrain3DAssets.new()
	for id in TERRAIN_SETS.size():
		var entry: Array = TERRAIN_SETS[id]
		var asset_id: String = entry[0]
		var albedo_path := "%s%s_albedo_height.res" % [PACKED_DIR, asset_id]
		var normal_path := "%s%s_normal_rough.res" % [PACKED_DIR, asset_id]
		if not ResourceLoader.exists(albedo_path) or not ResourceLoader.exists(normal_path):
			var r: Dictionary = agent.execute({
				"op": "pack_texture", "size": TERRAIN_TEXTURE_SIZE,
				"albedo": map_path(asset_id, "Color"),
				"height": map_path(asset_id, "Displacement"),
				"normal": map_path(asset_id, "NormalGL"),
				"roughness": map_path(asset_id, "Roughness"),
				"out_albedo": albedo_path, "out_normal": normal_path,
			})
			if not r.get("ok", false):
				push_error("town textures: packing %s failed: %s" % [asset_id, r.get("error", r)])
				continue
		var ta := Terrain3DTextureAsset.new()
		ta.name = asset_id
		ta.albedo_texture = load(albedo_path)
		ta.normal_texture = load(normal_path)
		ta.uv_scale = entry[1]
		ta.detiling_rotation = entry[2]
		assets.set_texture_asset(id, ta)
	return assets


## The same map in grey, cached under _Processed/ (generated, not from ambientCG).
static func _desaturated(path: String) -> String:
	var out := PROCESSED_DIR + path.get_file().get_basename() + "_grey.png"
	if ResourceLoader.exists(out):
		return out
	var tex := load(path) as Texture2D
	var img := tex.get_image() if tex != null else null
	if img == null or img.is_empty():
		return path
	img.decompress()
	img.convert(Image.FORMAT_RGB8)
	for y in img.get_height():
		for x in img.get_width():
			var l := img.get_pixel(x, y).get_luminance()
			img.set_pixel(x, y, Color(l, l, l))
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(out.get_base_dir()))
	img.save_png(out)
	return out


## A colour that scales the texture's mean luminance to 1.
static func _unit_luminance_gain(path: String) -> Color:
	var tex := load(path) as Texture2D
	var img := tex.get_image() if tex != null else null
	if img == null or img.is_empty():
		return Color.WHITE
	img.decompress()
	img.convert(Image.FORMAT_RGB8)
	img.resize(1, 1, Image.INTERPOLATE_BILINEAR)
	# The resize averages in sRGB; close enough for a gain.
	var mean := img.get_pixel(0, 0)
	var gain := 1.0 / maxf(mean.get_luminance(), 0.05)
	return Color(gain, gain, gain)
