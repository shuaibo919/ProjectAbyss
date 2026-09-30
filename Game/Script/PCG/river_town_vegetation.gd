@tool
extends RefCounted

## The Trees stream selects a species and one of three shared, generated meshes.
## Keep the original masked materials when these are instanced or restyled.
enum Species { PEACH, BAMBOO }

const VARIANT_SEEDS := [0, 17, 42]
const VARIANTS_PER_SPECIES := 3
const VARIANT_ATTRIBUTE := "tree_variant"
const SPECIES_ATTRIBUTE := "tree_species"
const INSTANCE_CELL_SIZE := 24.0
const BAMBOO_FOLIAGE := preload("res://Assets/Shaders/Vegetation/river_bamboo_foliage.gdshader")

static var _variants: Array[Mesh] = []


static func meshes() -> Array[Mesh]:
	if not _variants.is_empty():
		return _variants
	var generated: Array[Mesh] = []
	for species in [Species.PEACH, Species.BAMBOO]:
		var preset := 6 if species == Species.PEACH else 4
		var season := 1.0 if species == Species.PEACH else 2.0
		var species_name := "Peach" if species == Species.PEACH else "Bamboo"
		for seed_value in VARIANT_SEEDS:
			var result := SlowTreeGenerator.generate(preset, seed_value, false, season, {
				"radial_segments": 12,
				"crossed_cards": true,
				"species_rules": true,
				"structural_branches": true,
			})
			if not result.error.is_empty() or result.mesh == null or result.truncated:
				push_error("river_town: failed to generate %s/%d: %s" % [species_name, seed_value, result.error])
				return []
			var mesh: ArrayMesh = result.mesh
			if species == Species.BAMBOO:
				# Preserve the complete near mesh, then simplify the sub-pixel culm rings
				# and twigs at distance. Each spatial MultiMesh selects its own LOD.
				var imported := ImporterMesh.from_mesh(mesh)
				imported.generate_lods(25.0, 60.0, [])
				mesh = imported.get_mesh()
				for key in result.mesh.get_meta_list():
					mesh.set_meta(key, result.mesh.get_meta(key))
				var original: ShaderMaterial = mesh.surface_get_material(1)
				var foliage := ShaderMaterial.new()
				foliage.shader = BAMBOO_FOLIAGE
				foliage.set_shader_parameter("foliage_atlas", original.get_shader_parameter("foliage_atlas"))
				foliage.set_shader_parameter("season", season)
				mesh.surface_set_material(1, foliage)
			mesh.resource_name = "RiverTown %s %d" % [species_name, seed_value]
			mesh.set_meta("town_tree", true)
			mesh.set_meta("town_tree_species", species_name.to_lower())
			mesh.set_meta("town_tree_variant", generated.size())
			generated.append(mesh)
	_variants = generated
	return _variants


static func variant_for(species: int, point_seed: int) -> int:
	return species * VARIANTS_PER_SPECIES + (point_seed & 0x7fffffff) % VARIANTS_PER_SPECIES
