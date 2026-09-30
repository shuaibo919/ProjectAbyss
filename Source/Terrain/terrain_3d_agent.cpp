// ProjectAbyss addition, not part of upstream Terrain3D. Compiled only WITH_ABYSS.

#ifdef WITH_ABYSS

#include "terrain_3d_agent.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/portable_compressed_texture2d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/object.hpp>

#include "logger.h"
#include "terrain_3d_data.h"
#include "terrain_3d_erosion.h"
#include "terrain_3d_instancer.h"
#include "terrain_3d_mesh_asset.h"
#include "terrain_3d_texture_asset.h"
#include "terrain_3d_util.h"

namespace
{
	constexpr real_t DEFAULT_BRUSH_SIZE = 20.f; // Metres
	constexpr real_t DEFAULT_BRUSH_STRENGTH = 33.f; // Percent, the plugin UI's default
	constexpr real_t DEFAULT_BRUSH_FALLOFF = 0.5f;
	constexpr real_t DAB_SPACING_FRACTION = 0.25f; // Of the brush size, between replayed dabs
	constexpr real_t INSTANCE_REMOVE_STRENGTH = 1000.f; // Percent; high enough that every instance in range goes
	constexpr int32_t BRUSH_IMAGE_SIZE = 128;
	constexpr int32_t PREVIEW_DEFAULT_SIZE = 1024;
	constexpr int32_t PREVIEW_MAX_SIZE = 4096;
	constexpr int64_t HEIGHT_GRID_MAX_SAMPLES = 4 * 1024 * 1024;
	constexpr int32_t MAX_DABS_PER_STROKE = 100000;
	constexpr int64_t ERODE_MAX_CELLS = 16 * 1024 * 1024;
	constexpr int64_t ERODE_MAX_DROPLETS = 64 * 1024 * 1024;
	constexpr real_t STREAM_POWER_RESOLUTION = 8.f; // Metres per solver cell
	constexpr real_t STREAM_POWER_MIN_AREA = 5000.f; // m^2 of catchment before a cell counts as channel
	constexpr real_t STREAM_POWER_VALLEY_ANGLE = 38.f; // Degrees; valley walls rising from the channel floor
	constexpr real_t STREAM_POWER_ROUTING_JITTER = 1.f;
	constexpr int32_t STREAM_POWER_SMOOTH_PASSES = 1;
	const char* const BRUSH_DIRECTORY = "res://addons/terrain_3d/brushes/";

	uint32_t MapBit(const MapType Type)
	{
		return 1u << uint32_t(Type);
	}

	bool IsNumber(const Variant& Value)
	{
		return Value.get_type() == Variant::INT || Value.get_type() == Variant::FLOAT;
	}

	/** Lower-cases and drops separators, so "Full / Game", "full_game" and "FullGame" compare equal. */
	String NormalizeName(const String& Name)
	{
		return Name.to_lower().replace(" ", "").replace("_", "").replace("-", "").replace("/", "");
	}

	/** A range or 2D size: Vector2 / Vector2i / [a, b]. */
	bool ToPair(const Variant& Value, Vector2& Out)
	{
		switch (Value.get_type())
		{
			case Variant::VECTOR2:
				Out = Value;
				return true;
			case Variant::VECTOR2I:
				Out = Vector2(Vector2i(Value));
				return true;
			case Variant::ARRAY:
			{
				const Array Items = Value;
				if (Items.size() == 2 && IsNumber(Items[0]) && IsNumber(Items[1]))
				{
					Out = Vector2(real_t(Items[0]), real_t(Items[1]));
					return true;
				}
				return false;
			}
			default:
				return false;
		}
	}

	/** A world point. 2D forms ([x, z], Vector2) leave y NaN so the caller can drape it on the terrain. */
	bool ToPoint(const Variant& Value, Vector3& Out)
	{
		switch (Value.get_type())
		{
			case Variant::VECTOR3:
				Out = Value;
				return true;
			case Variant::VECTOR3I:
				Out = Vector3(Vector3i(Value));
				return true;
			case Variant::VECTOR2:
			{
				const Vector2 Point = Value;
				Out = Vector3(Point.x, NAN, Point.y);
				return true;
			}
			case Variant::VECTOR2I:
			{
				const Vector2i Point = Value;
				Out = Vector3(real_t(Point.x), NAN, real_t(Point.y));
				return true;
			}
			case Variant::ARRAY:
			{
				const Array Items = Value;
				for (int64_t Index = 0; Index < Items.size(); ++Index)
				{
					if (!IsNumber(Items[Index]))
					{
						return false;
					}
				}
				if (Items.size() == 2)
				{
					Out = Vector3(real_t(Items[0]), NAN, real_t(Items[1]));
					return true;
				}
				if (Items.size() == 3)
				{
					Out = Vector3(real_t(Items[0]), real_t(Items[1]), real_t(Items[2]));
					return true;
				}
				return false;
			}
			case Variant::DICTIONARY:
			{
				const Dictionary Point = Value;
				if (!Point.has("x") || !Point.has("z"))
				{
					return false;
				}
				Out = Vector3(real_t(Point["x"]), real_t(Point.get("y", NAN)), real_t(Point["z"]));
				return true;
			}
			default:
				return false;
		}
	}

	bool ToColor(const Variant& Value, Color& Out)
	{
		switch (Value.get_type())
		{
			case Variant::COLOR:
				Out = Value;
				return true;
			case Variant::STRING:
			case Variant::STRING_NAME:
			{
				const Color Parsed = Color::from_string(String(Value), COLOR_NAN);
				if (std::isnan(Parsed.r))
				{
					return false;
				}
				Out = Parsed;
				return true;
			}
			case Variant::ARRAY:
			{
				const Array Items = Value;
				if (Items.size() < 3 || Items.size() > 4)
				{
					return false;
				}
				Out = Color(real_t(Items[0]), real_t(Items[1]), real_t(Items[2]), Items.size() == 4 ? real_t(Items[3]) : 1.f);
				return true;
			}
			default:
				return false;
		}
	}

	/** Rect2 / Rect2i / [x, z, width, depth] / {min, max} / {position, size} / {center, size} / {center, radius}. */
	bool ToArea(const Variant& Value, Rect2& Out, bool& bOutRound)
	{
		bOutRound = false;
		switch (Value.get_type())
		{
			case Variant::RECT2:
				Out = Value;
				return true;
			case Variant::RECT2I:
				Out = Rect2(Rect2i(Value));
				return true;
			case Variant::ARRAY:
			{
				const Array Items = Value;
				if (Items.size() != 4)
				{
					return false;
				}
				Out = Rect2(real_t(Items[0]), real_t(Items[1]), real_t(Items[2]), real_t(Items[3]));
				return true;
			}
			case Variant::DICTIONARY:
			{
				const Dictionary Area = Value;
				Vector3 Center;
				Vector2 First;
				Vector2 Second;
				if (Area.has("min") && Area.has("max") && ToPoint(Area["min"], Center))
				{
					First = Vector2(Center.x, Center.z);
					if (!ToPoint(Area["max"], Center))
					{
						return false;
					}
					Second = Vector2(Center.x, Center.z);
					Out = Rect2(First, Second - First).abs();
					return true;
				}
				if (Area.has("position") && Area.has("size") && ToPoint(Area["position"], Center) && ToPair(Area["size"], Second))
				{
					Out = Rect2(Vector2(Center.x, Center.z), Second);
					return true;
				}
				if (Area.has("center") && ToPoint(Area["center"], Center))
				{
					if (Area.has("radius"))
					{
						const real_t Radius = Area["radius"];
						Out = Rect2(Center.x - Radius, Center.z - Radius, 2.f * Radius, 2.f * Radius);
						bOutRound = true;
						return true;
					}
					if (Area.has("size") && ToPair(Area["size"], Second))
					{
						Out = Rect2(Vector2(Center.x, Center.z) - Second * 0.5f, Second);
						return true;
					}
				}
				return false;
			}
			default:
				return false;
		}
	}

	/**
	 * Reads the edit area from `area`, or from `center` + `radius` / `size` at the top level.
	 * @return false if no area was given; bOutValid false if one was given but malformed.
	 */
	bool ReadArea(const Dictionary& Params, Rect2& Out, Terrain3DAgent::EAreaShape& OutShape, bool& bOutValid)
	{
		bool bRound = false;
		bOutValid = true;
		if (Params.has("area"))
		{
			bOutValid = ToArea(Params["area"], Out, bRound);
		}
		else if (Params.has("center") && (Params.has("radius") || Params.has("size")))
		{
			Dictionary Area;
			Area["center"] = Params["center"];
			if (Params.has("radius"))
			{
				Area["radius"] = Params["radius"];
			}
			else
			{
				Area["size"] = Params["size"];
			}
			bOutValid = ToArea(Area, Out, bRound);
		}
		else
		{
			return false;
		}
		const String Shape = String(Params.get("shape", bRound ? "ellipse" : "rect")).to_lower();
		OutShape = (Shape == "ellipse" || Shape == "circle") ? Terrain3DAgent::EAreaShape::Ellipse : Terrain3DAgent::EAreaShape::Rectangle;
		bOutValid = bOutValid && Out.size.x > 0.f && Out.size.y > 0.f;
		return true;
	}

	bool ReadPoints(const Dictionary& Params, PackedVector3Array& Out)
	{
		Vector3 Point;
		if (Params.has("points"))
		{
			const Variant Points = Params["points"];
			if (Points.get_type() == Variant::PACKED_VECTOR3_ARRAY)
			{
				Out = Points;
				return !Out.is_empty();
			}
			if (Points.get_type() == Variant::PACKED_VECTOR2_ARRAY)
			{
				const PackedVector2Array Points2 = Points;
				for (const Vector2& Point2 : Points2)
				{
					Out.push_back(Vector3(Point2.x, NAN, Point2.y));
				}
				return !Out.is_empty();
			}
			if (Points.get_type() != Variant::ARRAY)
			{
				return false;
			}
			const Array Items = Points;
			for (int64_t Index = 0; Index < Items.size(); ++Index)
			{
				if (!ToPoint(Items[Index], Point))
				{
					return false;
				}
				Out.push_back(Point);
			}
			return !Out.is_empty();
		}
		for (const char* Key : { "point", "position", "center" })
		{
			if (Params.has(Key))
			{
				if (!ToPoint(Params[Key], Point))
				{
					return false;
				}
				Out.push_back(Point);
				return true;
			}
		}
		return false;
	}

	real_t ShapeWeight(const Vector2& Point, const Rect2& Area, const Terrain3DAgent::EAreaShape Shape, const real_t Falloff)
	{
		const Vector2 HalfSize = Area.size * 0.5f;
		const Vector2 Offset = (Point - Area.get_center()).abs();
		real_t Inside = 0.f; // Distance in from the edge, metres
		if (Shape == Terrain3DAgent::EAreaShape::Ellipse)
		{
			const real_t Radial = Vector2(Offset.x / HalfSize.x, Offset.y / HalfSize.y).length();
			if (Radial > 1.f + CMP_EPSILON)
			{
				return 0.f;
			}
			Inside = (1.f - Radial) * MIN(HalfSize.x, HalfSize.y);
		}
		else
		{
			Inside = MIN(HalfSize.x - Offset.x, HalfSize.y - Offset.y);
			if (Inside < -CMP_EPSILON)
			{
				return 0.f;
			}
		}
		if (Falloff <= 0.f)
		{
			return 1.f;
		}
		return smoothstep(0.f, Falloff, Inside);
	}

	struct FAction
	{
		const char* Name;
		Terrain3DEditor::Tool Tool;
		Terrain3DEditor::Operation Operation;
		const char* Modifier; // Brush-data flag the plugin sets from a held key, or nullptr
	};

	// Agent-facing names for the plugin's tool + operation (+ modifier key) combinations.
	const FAction ACTIONS[] = {
		{ "raise", Terrain3DEditor::SCULPT, Terrain3DEditor::ADD, nullptr },
		{ "lower", Terrain3DEditor::SCULPT, Terrain3DEditor::SUBTRACT, nullptr },
		{ "smooth", Terrain3DEditor::SCULPT, Terrain3DEditor::AVERAGE, nullptr },
		{ "lift_troughs", Terrain3DEditor::SCULPT, Terrain3DEditor::ADD, "modifier_alt" },
		{ "flatten_peaks", Terrain3DEditor::SCULPT, Terrain3DEditor::SUBTRACT, "modifier_alt" },
		{ "ramp", Terrain3DEditor::SCULPT, Terrain3DEditor::GRADIENT, nullptr },
		{ "flatten", Terrain3DEditor::HEIGHT, Terrain3DEditor::ADD, nullptr },
		{ "paint", Terrain3DEditor::TEXTURE, Terrain3DEditor::REPLACE, nullptr },
		{ "spray", Terrain3DEditor::TEXTURE, Terrain3DEditor::ADD, nullptr },
		{ "unspray", Terrain3DEditor::TEXTURE, Terrain3DEditor::SUBTRACT, nullptr },
		{ "smooth_texture", Terrain3DEditor::TEXTURE, Terrain3DEditor::AVERAGE, nullptr },
		{ "color", Terrain3DEditor::COLOR, Terrain3DEditor::ADD, nullptr },
		{ "erase_color", Terrain3DEditor::COLOR, Terrain3DEditor::SUBTRACT, nullptr },
		{ "roughness", Terrain3DEditor::ROUGHNESS, Terrain3DEditor::ADD, nullptr },
		{ "erase_roughness", Terrain3DEditor::ROUGHNESS, Terrain3DEditor::SUBTRACT, nullptr },
		{ "autoshader", Terrain3DEditor::AUTOSHADER, Terrain3DEditor::ADD, nullptr },
		{ "clear_autoshader", Terrain3DEditor::AUTOSHADER, Terrain3DEditor::SUBTRACT, nullptr },
		{ "hole", Terrain3DEditor::HOLES, Terrain3DEditor::ADD, nullptr },
		{ "fill_hole", Terrain3DEditor::HOLES, Terrain3DEditor::SUBTRACT, nullptr },
		{ "navigation", Terrain3DEditor::NAVIGATION, Terrain3DEditor::ADD, nullptr },
		{ "clear_navigation", Terrain3DEditor::NAVIGATION, Terrain3DEditor::SUBTRACT, nullptr },
		{ "scatter", Terrain3DEditor::INSTANCER, Terrain3DEditor::ADD, nullptr },
		{ "unscatter", Terrain3DEditor::INSTANCER, Terrain3DEditor::SUBTRACT, "modifier_ctrl" },
		{ "add_region", Terrain3DEditor::REGION, Terrain3DEditor::ADD, nullptr },
		{ "remove_region", Terrain3DEditor::REGION, Terrain3DEditor::SUBTRACT, nullptr },
	};

	bool ToTool(const Variant& Value, Terrain3DEditor::Tool& Out)
	{
		if (IsNumber(Value))
		{
			const int32_t Tool = Value;
			Out = Terrain3DEditor::Tool(Tool);
			return Tool >= 0 && Tool < Terrain3DEditor::ANGLE;
		}
		const String Name = NormalizeName(Value);
		for (int32_t Tool = 0; Tool < Terrain3DEditor::ANGLE; ++Tool)
		{
			if (NormalizeName(Terrain3DEditor::TOOLNAME[Tool]) == Name)
			{
				Out = Terrain3DEditor::Tool(Tool);
				return true;
			}
		}
		return false;
	}

	bool ToOperation(const Variant& Value, Terrain3DEditor::Operation& Out)
	{
		if (IsNumber(Value))
		{
			const int32_t Operation = Value;
			Out = Terrain3DEditor::Operation(Operation);
			return Operation >= 0 && Operation < Terrain3DEditor::OP_MAX;
		}
		const String Name = NormalizeName(Value);
		for (int32_t Operation = 0; Operation < Terrain3DEditor::OP_MAX; ++Operation)
		{
			if (NormalizeName(Terrain3DEditor::OPNAME[Operation]) == Name)
			{
				Out = Terrain3DEditor::Operation(Operation);
				return true;
			}
		}
		return false;
	}

	bool ToMapType(const Variant& Value, MapType& Out)
	{
		const String Name = NormalizeName(Value);
		if (Name == "height")
		{
			Out = TYPE_HEIGHT;
		}
		else if (Name == "control")
		{
			Out = TYPE_CONTROL;
		}
		else if (Name == "color" || Name == "colour")
		{
			Out = TYPE_COLOR;
		}
		else
		{
			return false;
		}
		return true;
	}

	bool FindProperty(Object* Target, const String& Name, Dictionary& OutInfo)
	{
		const TypedArray<Dictionary> Properties = Target->get_property_list();
		for (int64_t Index = 0; Index < Properties.size(); ++Index)
		{
			const Dictionary Info = Properties[Index];
			if (String(Info["name"]) == Name)
			{
				OutInfo = Info;
				return true;
			}
		}
		return false;
	}

	/** Converts a JSON-ish value to a property's declared type: arrays to vectors, strings to colours, enum names, resource paths. */
	bool CoerceValue(const Variant& Value, const Dictionary& Info, Variant& Out)
	{
		const Variant::Type Type = Variant::Type(int32_t(Info["type"]));
		Vector3 Point;
		Vector2 Pair;
		Color ColorValue;
		switch (Type)
		{
			case Variant::COLOR:
				if (!ToColor(Value, ColorValue))
				{
					return false;
				}
				Out = ColorValue;
				return true;
			case Variant::VECTOR2:
				if (!ToPair(Value, Pair))
				{
					return false;
				}
				Out = Pair;
				return true;
			case Variant::VECTOR2I:
				if (!ToPair(Value, Pair))
				{
					return false;
				}
				Out = Vector2i(Pair);
				return true;
			case Variant::VECTOR3:
				if (!ToPoint(Value, Point))
				{
					return false;
				}
				Out = Point;
				return true;
			case Variant::INT:
				if (Value.get_type() == Variant::STRING && int32_t(Info["hint"]) == PROPERTY_HINT_ENUM)
				{
					const PackedStringArray Options = String(Info["hint_string"]).split(",");
					const String Wanted = NormalizeName(Value);
					for (int64_t Index = 0; Index < Options.size(); ++Index)
					{
						const PackedStringArray NameValue = Options[Index].split(":");
						if (NormalizeName(NameValue[0]) == Wanted)
						{
							Out = NameValue.size() > 1 ? NameValue[1].to_int() : Index;
							return true;
						}
					}
					return false;
				}
				if (!IsNumber(Value) && Value.get_type() != Variant::BOOL)
				{
					return false;
				}
				Out = int64_t(Value);
				return true;
			case Variant::FLOAT:
				if (!IsNumber(Value))
				{
					return false;
				}
				Out = double(Value);
				return true;
			case Variant::BOOL:
				Out = bool(Value);
				return true;
			case Variant::OBJECT:
				if (Value.get_type() == Variant::STRING)
				{
					const String Path = Value;
					if (Path.is_empty())
					{
						Out = Variant();
						return true;
					}
					const Ref<Resource> Loaded = ResourceLoader::get_singleton()->load(Path);
					if (Loaded.is_null())
					{
						return false;
					}
					Out = Loaded;
					return true;
				}
				Out = Value;
				return true;
			default:
				Out = Value;
				return true;
		}
	}

	/** Sets every key of Values that names a property of Target. @return keys that failed. */
	PackedStringArray ApplyProperties(Object* Target, const Dictionary& Values, const PackedStringArray& Skip)
	{
		PackedStringArray Errors;
		const Array Keys = Values.keys();
		for (int64_t Index = 0; Index < Keys.size(); ++Index)
		{
			const String Key = Keys[Index];
			if (Skip.has(Key))
			{
				continue;
			}
			Dictionary Info;
			Variant Value;
			if (!FindProperty(Target, Key, Info))
			{
				Errors.push_back(Key + String(": no such property on ") + Target->get_class());
			}
			else if (!CoerceValue(Values[Key], Info, Value))
			{
				Errors.push_back(Key + String(": cannot convert ") + Variant(Values[Key]).stringify());
			}
			else
			{
				Target->set(Key, Value);
			}
		}
		return Errors;
	}

	/** Loads a Texture2D from a path or passes one through. */
	Ref<Texture2D> ToTexture(const Variant& Value)
	{
		if (Value.get_type() == Variant::STRING)
		{
			return ResourceLoader::get_singleton()->load(String(Value));
		}
		return Value;
	}

	Color PaletteColor(const int32_t Index)
	{
		// Golden-ratio hue steps keep neighbouring ids apart.
		return Color::from_hsv(Math::fmod(0.13f + 0.618034f * real_t(Index), 1.f), 0.55f, 0.85f);
	}

	Color HeightTint(const real_t Normalized)
	{
		static const Color STOPS[] = {
			Color(0.20f, 0.36f, 0.22f),
			Color(0.42f, 0.58f, 0.30f),
			Color(0.72f, 0.66f, 0.45f),
			Color(0.52f, 0.40f, 0.30f),
			Color(0.95f, 0.95f, 0.95f),
		};
		constexpr int32_t STOP_COUNT = sizeof(STOPS) / sizeof(STOPS[0]);
		const real_t Scaled = CLAMP(Normalized, 0.f, 1.f) * real_t(STOP_COUNT - 1);
		const int32_t Lower = MIN(int32_t(Scaled), STOP_COUNT - 2);
		return STOPS[Lower].lerp(STOPS[Lower + 1], Scaled - real_t(Lower));
	}
} // namespace

///////////////////////////
// Lifecycle
///////////////////////////

Terrain3DAgent::Terrain3DAgent()
{
	Editor = memnew(Terrain3DEditor);
}

Terrain3DAgent::~Terrain3DAgent()
{
	Terrain3D* Terrain = GetTerrain();
	if (Terrain && Terrain->get_operating_editor() == Editor)
	{
		Terrain->set_operating_editor(nullptr);
	}
	memdelete(Editor);
}

void Terrain3DAgent::SetTerrain(Terrain3D* Terrain)
{
	TerrainId = Terrain ? Terrain->get_instance_id() : 0;
	Editor->set_terrain(Terrain);
	// The history holds region snapshots of the previous terrain.
	Editor->clear_local_history();
}

Terrain3D* Terrain3DAgent::GetTerrain() const
{
	return TerrainId ? Object::cast_to<Terrain3D>(ObjectDB::get_instance(TerrainId)) : nullptr;
}

// Terrain3D builds its data, instancer and collision on entering the tree; before that the
// region size is 0 and every lookup would divide by it.
Terrain3D* Terrain3DAgent::GetValidTerrain() const
{
	Terrain3D* Terrain = GetTerrain();
	if (!Terrain || !Terrain->is_inside_tree() || !Terrain->get_data() || !Terrain->get_instancer() ||
			!Terrain->get_collision() || Terrain->get_assets().is_null())
	{
		return nullptr;
	}
	Editor->set_terrain(Terrain);
	return Terrain;
}

Dictionary Terrain3DAgent::Fail(const String& Message)
{
	Dictionary Result;
	Result["ok"] = false;
	Result["error"] = Message;
	LastError = Result;
	UtilityFunctions::push_error("Terrain3DAgent: ", Message);
	return Result;
}

#define AGENT_REQUIRE_TERRAIN(Name)                                                                     \
	Terrain3D* Name = GetValidTerrain();                                                                \
	if (!Name)                                                                                          \
	{                                                                                                   \
		return Fail("No terrain: call set_terrain() with a Terrain3D that is inside the scene tree"); \
	}

///////////////////////////
// Edit plumbing
///////////////////////////

void Terrain3DAgent::BeginEdit(const Terrain3DEditor::Tool Tool, const Terrain3DEditor::Operation Operation,
	const Dictionary& BrushData, const Vector3& Position)
{
	// set_brush_data() keeps and rewrites the Dictionary it is given, so hand it a copy.
	Dictionary Data = BrushData.duplicate();
	if (!Data.has("brush"))
	{
		Data["brush"] = GetBrushImages("soft", DEFAULT_BRUSH_FALLOFF);
	}
	// A replayed dab is a full-pressure mouse drag, and there is no view to align the brush to.
	Data["mouse_pressure"] = 1.f;
	Data["align_to_view"] = Data.get("align_to_view", false);
	Editor->set_tool(Tool);
	Editor->set_operation(Operation);
	Editor->set_brush_data(Data);
	Editor->start_operation(Position);
}

void Terrain3DAgent::EndEdit(const uint32_t EditedMaps, const AABB& EditedArea)
{
	Terrain3D* Terrain = GetValidTerrain();
	if (!Terrain)
	{
		return;
	}
	Terrain3DData* Data = Terrain->get_data();
	// Only layers of regions backup_region() flagged as edited are re-uploaded.
	for (const MapType Type : { TYPE_HEIGHT, TYPE_CONTROL, TYPE_COLOR })
	{
		if (EditedMaps & MapBit(Type))
		{
			Data->update_maps(Type, false, Type == TYPE_COLOR);
		}
	}
	if (EditedMaps != 0u && EditedArea.has_surface())
	{
		Data->add_edited_area(EditedArea);
		if (EditedMaps & (MapBit(TYPE_HEIGHT) | MapBit(TYPE_CONTROL)))
		{
			Terrain->get_instancer()->update_transforms(EditedArea);
		}
	}
	Terrain->get_collision()->update(V2I_MAX, true);
	Editor->stop_operation();
}

// Adds the region through the editor's Region tool so the addition lands in the same undo step.
bool Terrain3DAgent::EnsureRegion(const Vector2i& Location)
{
	Terrain3D* Terrain = GetValidTerrain();
	if (!Terrain || Terrain3DData::get_region_map_index(Location) < 0)
	{
		return false;
	}
	Terrain3DData* Data = Terrain->get_data();
	Ref<Terrain3DRegion> Region = Data->get_region(Location);
	if (Region.is_valid() && !Region->is_deleted())
	{
		return true;
	}
	const Terrain3DEditor::Tool PreviousTool = Editor->get_tool();
	const Terrain3DEditor::Operation PreviousOperation = Editor->get_operation();
	const real_t RegionWorldSize = real_t(Terrain->get_region_size()) * Terrain->get_vertex_spacing();
	Editor->set_tool(Terrain3DEditor::REGION);
	Editor->set_operation(Terrain3DEditor::ADD);
	Editor->operate(Vector3((Location.x + 0.5f) * RegionWorldSize, 0.f, (Location.y + 0.5f) * RegionWorldSize), 0.f);
	Editor->set_tool(PreviousTool);
	Editor->set_operation(PreviousOperation);
	Region = Data->get_region(Location);
	return Region.is_valid() && !Region->is_deleted();
}

int32_t Terrain3DAgent::VisitArea(const Rect2& Area, const EAreaShape Shape, const real_t Falloff, const EAreaSide Side,
	const EMissingRegion Missing, const MapType Type, const FVertexVisitor& Visitor)
{
	if (Side == EAreaSide::Outside)
	{
		// Everything but the shape: walk all regions and invert the weight.
		const Rect2 Bounds = GetRegionBounds();
		return VisitArea(Bounds, EAreaShape::Rectangle, 0.f, EAreaSide::Inside, EMissingRegion::Skip, Type,
			[&Area, Shape, Falloff, &Visitor](const FVertex& Vertex)
			{
				FVertex Outside = Vertex;
				Outside.Weight = 1.f - ShapeWeight(Vector2(Vertex.Position.x, Vertex.Position.z), Area, Shape, Falloff);
				if (Outside.Weight > 0.f)
				{
					Visitor(Outside);
				}
			});
	}
	Terrain3D* Terrain = GetValidTerrain();
	if (!Terrain)
	{
		return 0;
	}
	Terrain3DData* Data = Terrain->get_data();
	const real_t Spacing = Terrain->get_vertex_spacing();
	const int32_t RegionSize = Terrain->get_region_size();

	// Vertices lying within [position, end], edges included. GridEnd is exclusive.
	const Vector2i GridMin(int32_t(Math::ceil(Area.position.x / Spacing)), int32_t(Math::ceil(Area.position.y / Spacing)));
	const Vector2i GridEnd(int32_t(Math::floor(Area.get_end().x / Spacing)) + 1, int32_t(Math::floor(Area.get_end().y / Spacing)) + 1);
	if (GridEnd.x <= GridMin.x || GridEnd.y <= GridMin.y)
	{
		return 0;
	}
	const Vector2i LocationMin = V2I_DIVIDE_FLOOR(GridMin, RegionSize);
	const Vector2i LastVertex = GridEnd - Vector2i(1, 1);
	const Vector2i LocationMax = V2I_DIVIDE_FLOOR(LastVertex, RegionSize);

	int32_t Count = 0;
	for (int32_t LocationY = LocationMin.y; LocationY <= LocationMax.y; ++LocationY)
	{
		for (int32_t LocationX = LocationMin.x; LocationX <= LocationMax.x; ++LocationX)
		{
			const Vector2i Location(LocationX, LocationY);
			if (Terrain3DData::get_region_map_index(Location) < 0)
			{
				continue;
			}
			Ref<Terrain3DRegion> Region = Data->get_region(Location);
			if (Region.is_null() || Region->is_deleted())
			{
				// Only create regions the shape reaches into: a vertex on the far edge of the area
				// belongs to the next region, and an ellipse's bounding box overhangs its corners.
				// Clamping the centre into the region finds its nearest point for either shape.
				const real_t RegionWorldSize = real_t(RegionSize) * Spacing;
				const Rect2 RegionRect(Vector2(Location) * RegionWorldSize, Vector2(RegionWorldSize, RegionWorldSize));
				const Vector2 Nearest = Area.get_center().clamp(RegionRect.position, RegionRect.get_end());
				const bool bReached = RegionRect.intersects(Area) && ShapeWeight(Nearest, Area, Shape, 0.f) > 0.f;
				if (Missing == EMissingRegion::Skip || !bReached || !EnsureRegion(Location))
				{
					continue;
				}
				Region = Data->get_region(Location);
			}
			Image* Map = Region->get_map_ptr(Type);
			Image* HeightMap = Region->get_map_ptr(TYPE_HEIGHT);
			if (!Map || !HeightMap)
			{
				continue;
			}
			const Vector2i Origin = Location * RegionSize;
			const Vector2i From(MAX(GridMin.x, Origin.x), MAX(GridMin.y, Origin.y));
			const Vector2i To(MIN(GridEnd.x, Origin.x + RegionSize), MIN(GridEnd.y, Origin.y + RegionSize));
			bool bBackedUp = false;
			for (int32_t GridY = From.y; GridY < To.y; ++GridY)
			{
				for (int32_t GridX = From.x; GridX < To.x; ++GridX)
				{
					const Vector2 Position2D(real_t(GridX) * Spacing, real_t(GridY) * Spacing);
					const real_t Weight = ShapeWeight(Position2D, Area, Shape, Falloff);
					if (Weight <= 0.f)
					{
						continue;
					}
					if (!bBackedUp)
					{
						// Snapshot before the first write, for undo.
						Editor->backup_region(Region);
						bBackedUp = true;
					}
					FVertex Vertex;
					Vertex.Region = Region.ptr();
					Vertex.Map = Map;
					Vertex.Pixel = Vector2i(GridX - Origin.x, GridY - Origin.y);
					const real_t Height = HeightMap->get_pixelv(Vertex.Pixel).r;
					Vertex.Position = Vector3(Position2D.x, std::isnan(Height) ? 0.f : Height, Position2D.y);
					Vertex.Weight = Weight;
					Visitor(Vertex);
					++Count;
				}
			}
		}
	}
	return Count;
}

Rect2 Terrain3DAgent::GetRegionBounds() const
{
	Terrain3D* Terrain = GetValidTerrain();
	Rect2 Bounds;
	if (!Terrain)
	{
		return Bounds;
	}
	const real_t RegionWorldSize = real_t(Terrain->get_region_size()) * Terrain->get_vertex_spacing();
	const TypedArray<Vector2i> Locations = Terrain->get_data()->get_region_locations();
	for (int64_t Index = 0; Index < Locations.size(); ++Index)
	{
		const Vector2i Location = Locations[Index];
		const Rect2 RegionRect(Vector2(Location) * RegionWorldSize, Vector2(RegionWorldSize, RegionWorldSize));
		Bounds = Index == 0 ? RegionRect : Bounds.merge(RegionRect);
	}
	return Bounds;
}

///////////////////////////
// Brushes
///////////////////////////

Ref<Image> Terrain3DAgent::MakeBrushImage(const String& Shape, const int32_t Size, const real_t Falloff)
{
	const int32_t Resolution = CLAMP(Size, 8, 1024);
	const String Kind = Shape.to_lower();
	const real_t Center = real_t(Resolution - 1) * 0.5f;
	const real_t Radius = real_t(Resolution) * 0.5f;
	const real_t Edge = CLAMP(Falloff, 0.f, 1.f);
	Ref<Image> Mask = Image::create_empty(Resolution, Resolution, false, Image::FORMAT_RF);
	for (int32_t Y = 0; Y < Resolution; ++Y)
	{
		for (int32_t X = 0; X < Resolution; ++X)
		{
			const real_t Distance = Vector2(real_t(X) - Center, real_t(Y) - Center).length() / Radius;
			real_t Alpha = 1.f;
			if (Kind == "round")
			{
				Alpha = Distance <= 1.f ? 1.f : 0.f;
			}
			else if (Kind != "square")
			{
				Alpha = Distance >= 1.f ? 0.f : (Edge <= 0.f ? 1.f : 1.f - smoothstep(1.f - Edge, 1.f, Distance));
			}
			Mask->set_pixel(X, Y, Color(Alpha, 0.f, 0.f, 1.f));
		}
	}
	return Mask;
}

// Terrain3DEditor wants [mask Image, Texture2D]; the texture only feeds the plugin's cursor decal.
Array Terrain3DAgent::GetBrushImages(const Variant& Brush, const real_t Falloff)
{
	Ref<Image> Mask;
	String Key;
	if (Brush.get_type() == Variant::OBJECT)
	{
		Mask = Brush;
	}
	else
	{
		const String Name = Brush;
		Key = Name + String("@") + String::num(Falloff, 3);
		if (BrushCache.has(Key))
		{
			return BrushCache[Key];
		}
		const String Kind = Name.to_lower();
		if (Kind == "soft" || Kind == "round" || Kind == "square")
		{
			Mask = MakeBrushImage(Kind, BRUSH_IMAGE_SIZE, Falloff);
		}
		else
		{
			String Path = Name.contains("/") ? Name : String(BRUSH_DIRECTORY) + Name;
			if (Path.get_extension().is_empty())
			{
				Path += ".exr";
			}
			Mask = Image::load_from_file(Path);
			if (Mask.is_valid())
			{
				Mask->convert(Image::FORMAT_RF);
			}
		}
	}
	Array Images;
	if (Mask.is_null() || Mask->is_empty())
	{
		return Images;
	}
	Images.push_back(Mask);
	Images.push_back(ImageTexture::create_from_image(Mask));
	if (!Key.is_empty())
	{
		BrushCache[Key] = Images;
	}
	return Images;
}

///////////////////////////
// Command dispatch
///////////////////////////

namespace
{
	struct FCommand
	{
		const char* Name;
		Dictionary (Terrain3DAgent::*Method)(const Dictionary&);
	};

	const FCommand COMMANDS[] = {
		{ "brush", &Terrain3DAgent::Brush },
		{ "add_regions", &Terrain3DAgent::AddRegions },
		{ "remove_regions", &Terrain3DAgent::RemoveRegions },
		{ "set_height_area", &Terrain3DAgent::SetHeightArea },
		{ "apply_noise", &Terrain3DAgent::ApplyNoise },
		{ "apply_height_function", &Terrain3DAgent::ApplyHeightFunction },
		{ "paint_area", &Terrain3DAgent::PaintArea },
		{ "paint_texture_rules", &Terrain3DAgent::PaintTextureRules },
		{ "stamp_path", &Terrain3DAgent::StampPath },
		{ "erode", &Terrain3DAgent::Erode },
		{ "stream_power", &Terrain3DAgent::StreamPower },
		{ "place_instances", &Terrain3DAgent::PlaceInstances },
		{ "clear_instances", &Terrain3DAgent::ClearInstances },
		{ "add_texture", &Terrain3DAgent::AddTexture },
		{ "pack_texture", &Terrain3DAgent::PackTexture },
		{ "add_mesh", &Terrain3DAgent::AddMesh },
		{ "set_properties", &Terrain3DAgent::SetProperties },
		{ "get_summary", &Terrain3DAgent::GetSummary },
		{ "sample", &Terrain3DAgent::Sample },
		{ "get_height_grid", &Terrain3DAgent::GetHeightGrid },
		{ "save_preview", &Terrain3DAgent::SavePreview },
		{ "import_heightmap", &Terrain3DAgent::ImportHeightmap },
		{ "export_map", &Terrain3DAgent::ExportMap },
		{ "bake_mesh", &Terrain3DAgent::BakeMesh },
		{ "undo", &Terrain3DAgent::Undo },
		{ "redo", &Terrain3DAgent::Redo },
		{ "save", &Terrain3DAgent::Save },
	};
} // namespace

PackedStringArray Terrain3DAgent::GetCommandNames()
{
	PackedStringArray Names;
	for (const FCommand& Command : COMMANDS)
	{
		Names.push_back(Command.Name);
	}
	return Names;
}

Variant Terrain3DAgent::Execute(const Variant& Commands)
{
	switch (Commands.get_type())
	{
		case Variant::STRING:
		{
			const Variant Parsed = JSON::parse_string(String(Commands));
			if (Parsed.get_type() != Variant::ARRAY && Parsed.get_type() != Variant::DICTIONARY)
			{
				return Fail("Commands are not a JSON object or array");
			}
			return Execute(Parsed);
		}
		case Variant::ARRAY:
		{
			const Array List = Commands;
			Array Results;
			for (int64_t Index = 0; Index < List.size(); ++Index)
			{
				Results.push_back(List[Index].get_type() == Variant::DICTIONARY ? Execute(List[Index]) : Variant(Fail("Command is not an object")));
			}
			return Results;
		}
		case Variant::DICTIONARY:
		{
			const Dictionary Command = Commands;
			const String Op = Command.get("op", "");
			for (const FCommand& Entry : COMMANDS)
			{
				if (Op == Entry.Name)
				{
					const uint64_t StartTime = Time::get_singleton()->get_ticks_usec();
					Dictionary Result = (this->*Entry.Method)(Command);
					Result["op"] = Op;
					Result["time_ms"] = double(Time::get_singleton()->get_ticks_usec() - StartTime) / 1000.0;
					return Result;
				}
			}
			Dictionary Result = Fail("Unknown op '" + Op + "'. Known: " + String(", ").join(GetCommandNames()));
			Result["op"] = Op;
			return Result;
		}
		default:
			return Fail("execute() takes a Dictionary, an Array or a JSON string");
	}
}

Array Terrain3DAgent::ExecuteFile(const String& Path)
{
	if (!FileAccess::file_exists(Path))
	{
		Array Results;
		Results.push_back(Fail("No such command file: " + Path));
		return Results;
	}
	const Variant Result = Execute(FileAccess::get_file_as_string(Path));
	if (Result.get_type() == Variant::ARRAY)
	{
		return Result;
	}
	Array Results;
	Results.push_back(Result);
	return Results;
}

///////////////////////////
// Brush strokes
///////////////////////////

Dictionary Terrain3DAgent::Brush(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();

	Dictionary BrushData = Params.duplicate();
	Terrain3DEditor::Tool Tool = Terrain3DEditor::SCULPT;
	Terrain3DEditor::Operation Operation = Terrain3DEditor::ADD;
	if (Params.has("action"))
	{
		const String Action = NormalizeName(Params["action"]);
		bool bFound = false;
		for (const FAction& Entry : ACTIONS)
		{
			if (NormalizeName(Entry.Name) == Action)
			{
				Tool = Entry.Tool;
				Operation = Entry.Operation;
				if (Entry.Modifier)
				{
					BrushData[Entry.Modifier] = true;
				}
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			String Known;
			for (const FAction& Entry : ACTIONS)
			{
				Known += (Known.is_empty() ? "" : ", ") + String(Entry.Name);
			}
			return Fail("Unknown brush action '" + String(Params["action"]) + "'. Known: " + Known);
		}
	}
	else
	{
		if (Params.has("tool") && !ToTool(Params["tool"], Tool))
		{
			return Fail("Unknown tool " + Variant(Params["tool"]).stringify());
		}
		if (Params.has("operation") && !ToOperation(Params["operation"], Operation))
		{
			return Fail("Unknown operation " + Variant(Params["operation"]).stringify());
		}
		// The plugin removes instances by holding Ctrl, not by a Subtract operation.
		if (Tool == Terrain3DEditor::INSTANCER && Operation == Terrain3DEditor::SUBTRACT)
		{
			BrushData["modifier_ctrl"] = true;
		}
	}
	if (Params.get("all_meshes", false))
	{
		BrushData["modifier_shift"] = true;
	}

	PackedVector3Array Points;
	if (!ReadPoints(Params, Points))
	{
		return Fail("brush needs `points` (a stroke) or `point`: [x, z] or [x, y, z]");
	}
	// 2D points are draped on the terrain, which is where the plugin's mouse ray would have hit.
	for (int64_t Index = 0; Index < Points.size(); ++Index)
	{
		if (std::isnan(Points[Index].y))
		{
			Points.set(Index, Vector3(Points[Index].x, Data->get_height(Points[Index]), Points[Index].z));
		}
	}

	const real_t Size = CLAMP(real_t(Params.get("size", DEFAULT_BRUSH_SIZE)), 0.1f, 4096.f);
	BrushData["size"] = Size;
	BrushData["strength"] = Params.get("strength", DEFAULT_BRUSH_STRENGTH);
	const Array Images = GetBrushImages(Params.get("brush", "soft"), real_t(Params.get("falloff", DEFAULT_BRUSH_FALLOFF)));
	if (Images.is_empty())
	{
		return Fail("Cannot load brush " + Variant(Params.get("brush", "")).stringify());
	}
	BrushData["brush"] = Images;
	Color Tint;
	if (Params.has("color") && !ToColor(Params["color"], Tint))
	{
		return Fail("Cannot parse color " + Variant(Params["color"]).stringify());
	}
	if (Params.has("color"))
	{
		BrushData["color"] = Tint;
	}
	Vector2 Slope;
	if (Params.has("slope") && ToPair(Params["slope"], Slope))
	{
		BrushData["slope"] = Slope;
	}
	if (Tool == Terrain3DEditor::HEIGHT && !Params.has("height"))
	{
		// Flatten to where the stroke starts, like the plugin's Ctrl-pick.
		BrushData["height"] = std::isnan(Points[0].y) ? 0.f : Points[0].y;
	}
	if (Operation == Terrain3DEditor::GRADIENT && !Params.has("gradient_points"))
	{
		if (Points.size() < 2 || std::isnan(Points[0].y) || std::isnan(Points[Points.size() - 1].y))
		{
			return Fail("A ramp needs at least two points with heights, [x, y, z], or points on existing terrain");
		}
		PackedVector3Array Ends;
		Ends.push_back(Points[0]);
		Ends.push_back(Points[Points.size() - 1]);
		BrushData["gradient_points"] = Ends;
	}

	const real_t Spacing = MAX(real_t(Params.get("spacing", Size * DAB_SPACING_FRACTION)), Terrain->get_vertex_spacing());
	const int32_t Passes = CLAMP(int32_t(Params.get("passes", 1)), 1, 64);

	BeginEdit(Tool, Operation, BrushData, Points[0]);
	int32_t Dabs = 0;
	AABB Bounds(Vector3(Points[0].x, std::isnan(Points[0].y) ? 0.f : Points[0].y, Points[0].z), Vector3());
	for (int32_t Pass = 0; Pass < Passes && Dabs < MAX_DABS_PER_STROKE; ++Pass)
	{
		Editor->operate(Points[0], 0.f);
		++Dabs;
		for (int64_t Index = 1; Index < Points.size() && Dabs < MAX_DABS_PER_STROKE; ++Index)
		{
			const Vector3 From = Points[Index - 1];
			const Vector3 To = Points[Index];
			const real_t Distance = Vector2(To.x - From.x, To.z - From.z).length();
			const int32_t Steps = MAX(1, int32_t(Math::ceil(Distance / Spacing)));
			for (int32_t Step = 1; Step <= Steps; ++Step)
			{
				Editor->operate(From.lerp(To, real_t(Step) / real_t(Steps)), 0.f);
				++Dabs;
			}
			Bounds.expand_to(Vector3(To.x, std::isnan(To.y) ? 0.f : To.y, To.z));
		}
	}
	Bounds = Bounds.grow(Size * 0.5f);
	EndEdit(0u, Bounds);

	Dictionary Result;
	Result["ok"] = true;
	Result["tool"] = Terrain3DEditor::TOOLNAME[Tool];
	Result["operation"] = Terrain3DEditor::OPNAME[Operation];
	Result["dabs"] = Dabs;
	Result["bounds"] = Rect2(Bounds.position.x, Bounds.position.z, Bounds.size.x, Bounds.size.z);
	Result["height_range"] = Data->get_height_range();
	if (Dabs >= MAX_DABS_PER_STROKE)
	{
		Result["warning"] = "Stroke truncated at " + String::num_int64(MAX_DABS_PER_STROKE) + " dabs; raise `spacing`";
	}
	return Result;
}

///////////////////////////
// Regions
///////////////////////////

namespace
{
	/** Region locations from `locations` ([[x, y], ...]), `area`, or `points`. */
	bool ReadRegionLocations(const Dictionary& Params, const real_t RegionWorldSize, TypedArray<Vector2i>& Out)
	{
		if (Params.has("locations"))
		{
			const Array Items = Params["locations"];
			Vector2 Pair;
			for (int64_t Index = 0; Index < Items.size(); ++Index)
			{
				if (!ToPair(Items[Index], Pair))
				{
					return false;
				}
				Out.push_back(Vector2i(Pair));
			}
			return !Out.is_empty();
		}
		Rect2 Area;
		Terrain3DAgent::EAreaShape Shape;
		bool bValid = false;
		if (ReadArea(Params, Area, Shape, bValid))
		{
			if (!bValid)
			{
				return false;
			}
			const Vector2i Min = Vector2i((Area.position / RegionWorldSize).floor());
			// A region is wanted only if the area reaches into it, not merely touches its edge.
			const Vector2i Max = Vector2i((Area.get_end() / RegionWorldSize - Vector2(CMP_EPSILON, CMP_EPSILON)).floor());
			for (int32_t Y = Min.y; Y <= Max.y; ++Y)
			{
				for (int32_t X = Min.x; X <= Max.x; ++X)
				{
					Out.push_back(Vector2i(X, Y));
				}
			}
			return !Out.is_empty();
		}
		PackedVector3Array Points;
		if (!ReadPoints(Params, Points))
		{
			return false;
		}
		for (const Vector3& Point : Points)
		{
			const Vector2i Location = Vector2i((Vector2(Point.x, Point.z) / RegionWorldSize).floor());
			if (!Out.has(Location))
			{
				Out.push_back(Location);
			}
		}
		return true;
	}
} // namespace

Dictionary Terrain3DAgent::AddRegions(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	const real_t RegionWorldSize = real_t(Terrain->get_region_size()) * Terrain->get_vertex_spacing();
	TypedArray<Vector2i> Locations;
	if (!ReadRegionLocations(Params, RegionWorldSize, Locations))
	{
		return Fail("add_regions needs `locations`, `area` or `points`");
	}
	Terrain3DData* Data = Terrain->get_data();
	const int32_t Before = Data->get_region_count();
	TypedArray<Vector2i> OutOfBounds;
	BeginEdit(Terrain3DEditor::REGION, Terrain3DEditor::ADD, Dictionary(), Vector3());
	for (int64_t Index = 0; Index < Locations.size(); ++Index)
	{
		if (!EnsureRegion(Locations[Index]))
		{
			OutOfBounds.push_back(Locations[Index]);
		}
	}
	EndEdit(0u, AABB());

	Dictionary Result;
	Result["ok"] = OutOfBounds.is_empty();
	Result["added"] = Data->get_region_count() - Before;
	Result["region_count"] = Data->get_region_count();
	Result["world_bounds"] = GetRegionBounds();
	if (!OutOfBounds.is_empty())
	{
		Result["error"] = "Locations outside the 32x32 region map (-16..15): " + Variant(OutOfBounds).stringify();
	}
	return Result;
}

Dictionary Terrain3DAgent::RemoveRegions(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	const real_t RegionWorldSize = real_t(Terrain->get_region_size()) * Terrain->get_vertex_spacing();
	TypedArray<Vector2i> Locations;
	if (!ReadRegionLocations(Params, RegionWorldSize, Locations))
	{
		return Fail("remove_regions needs `locations`, `area` or `points`");
	}
	Terrain3DData* Data = Terrain->get_data();
	const int32_t Before = Data->get_region_count();
	BeginEdit(Terrain3DEditor::REGION, Terrain3DEditor::SUBTRACT, Dictionary(), Vector3());
	for (int64_t Index = 0; Index < Locations.size(); ++Index)
	{
		const Vector2i Location = Locations[Index];
		if (Data->has_region(Location))
		{
			Editor->operate(Vector3((Location.x + 0.5f) * RegionWorldSize, 0.f, (Location.y + 0.5f) * RegionWorldSize), 0.f);
		}
	}
	EndEdit(0u, AABB());

	Dictionary Result;
	Result["ok"] = true;
	Result["removed"] = Before - Data->get_region_count();
	Result["region_count"] = Data->get_region_count();
	return Result;
}

///////////////////////////
// Area edits
///////////////////////////

Dictionary Terrain3DAgent::ModifyHeights(const Dictionary& Params, const FHeightFunction& Target)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();
	Rect2 Area;
	EAreaShape Shape = EAreaShape::Rectangle;
	bool bValid = false;
	if (!ReadArea(Params, Area, Shape, bValid) || !bValid)
	{
		return Fail("Needs an `area` ([x, z, width, depth] or {center, radius}) or `center` + `radius`/`size`");
	}
	const real_t Falloff = MAX(real_t(Params.get("falloff", 0.f)), 0.f);
	const EAreaSide Side = bool(Params.get("invert", false)) ? EAreaSide::Outside : EAreaSide::Inside;
	const EMissingRegion Missing = bool(Params.get("auto_regions", true)) ? EMissingRegion::Create : EMissingRegion::Skip;

	BeginEdit(Terrain3DEditor::HEIGHT, Terrain3DEditor::REPLACE, Dictionary(), Vector3(Area.get_center().x, 0.f, Area.get_center().y));
	Vector2 Range(INFINITY, -INFINITY);
	const int32_t Count = VisitArea(Area, Shape, Falloff, Side, Missing, TYPE_HEIGHT,
		[&Target, &Range, Data](const FVertex& Vertex)
		{
			const real_t Wanted = Target(Vertex.Position);
			const real_t Height = std::isnan(Wanted) ? Vertex.Position.y : Math::lerp(Vertex.Position.y, Wanted, Vertex.Weight);
			Vertex.Map->set_pixelv(Vertex.Pixel, Color(Height, 0.f, 0.f, 1.f));
			Vertex.Region->update_height(Height);
			Data->update_master_height(Height);
			Range = Vector2(MIN(Range.x, Height), MAX(Range.y, Height));
		});
	const Rect2 EditedRect = Side == EAreaSide::Outside ? GetRegionBounds() : Area;
	const AABB Edited(Vector3(EditedRect.position.x, Count > 0 ? Range.x : 0.f, EditedRect.position.y),
		Vector3(EditedRect.size.x, Count > 0 ? Range.y - Range.x : 0.f, EditedRect.size.y));
	EndEdit(Count > 0 ? MapBit(TYPE_HEIGHT) : 0u, Edited);

	Dictionary Result;
	Result["ok"] = true;
	Result["vertices"] = Count;
	Result["area"] = Area;
	Result["edited_height_range"] = Count > 0 ? Range : Vector2();
	if (Count == 0)
	{
		Result["warning"] = "No vertices edited: the area has no regions and auto_regions is off, or it is smaller than one vertex";
	}
	return Result;
}

Dictionary Terrain3DAgent::SetHeightArea(const Dictionary& Params)
{
	if (!Params.has("height"))
	{
		return Fail("set_height_area needs `height` (metres; the offset for mode \"add\")");
	}
	const real_t Height = Params["height"];
	const String Mode = String(Params.get("mode", "set")).to_lower();
	if (Mode == "add")
	{
		return ModifyHeights(Params, [Height](const Vector3& Position) { return Position.y + Height; });
	}
	if (Mode == "min" || Mode == "carve")
	{
		return ModifyHeights(Params, [Height](const Vector3& Position) { return MIN(Position.y, Height); });
	}
	if (Mode == "max" || Mode == "fill")
	{
		return ModifyHeights(Params, [Height](const Vector3& Position) { return MAX(Position.y, Height); });
	}
	if (Mode != "set")
	{
		return Fail("Unknown mode '" + Mode + "': set, add, min (carve) or max (fill)");
	}
	return ModifyHeights(Params, [Height](const Vector3&) { return Height; });
}

Dictionary Terrain3DAgent::ApplyNoise(const Dictionary& Params)
{
	Ref<FastNoiseLite> Noise = Params.get("noise", Variant());
	if (Noise.is_null())
	{
		Noise.instantiate();
		Noise->set_seed(int32_t(Params.get("seed", 0)));
		Noise->set_frequency(real_t(Params.get("frequency", 0.005f)));
		Noise->set_fractal_octaves(int32_t(Params.get("octaves", 5)));
		Noise->set_fractal_lacunarity(real_t(Params.get("lacunarity", 2.f)));
		Noise->set_fractal_gain(real_t(Params.get("gain", 0.5f)));
		const String NoiseType = NormalizeName(Params.get("noise_type", "simplex_smooth"));
		if (NoiseType == "simplex")
		{
			Noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX);
		}
		else if (NoiseType == "perlin")
		{
			Noise->set_noise_type(FastNoiseLite::TYPE_PERLIN);
		}
		else if (NoiseType == "cellular")
		{
			Noise->set_noise_type(FastNoiseLite::TYPE_CELLULAR);
		}
		else if (NoiseType == "value")
		{
			Noise->set_noise_type(FastNoiseLite::TYPE_VALUE);
		}
		else if (NoiseType == "valuecubic")
		{
			Noise->set_noise_type(FastNoiseLite::TYPE_VALUE_CUBIC);
		}
		else
		{
			Noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
		}
		const String Fractal = NormalizeName(Params.get("fractal_type", "fbm"));
		if (Fractal == "ridged")
		{
			Noise->set_fractal_type(FastNoiseLite::FRACTAL_RIDGED);
		}
		else if (Fractal == "pingpong")
		{
			Noise->set_fractal_type(FastNoiseLite::FRACTAL_PING_PONG);
		}
		else if (Fractal == "none")
		{
			Noise->set_fractal_type(FastNoiseLite::FRACTAL_NONE);
		}
		else
		{
			Noise->set_fractal_type(FastNoiseLite::FRACTAL_FBM);
		}
	}
	const real_t Amplitude = Params.get("amplitude", 20.f);
	const real_t Base = Params.get("base", 0.f);
	const bool bUnsigned = Params.get("unsigned", false); // Map noise to 0..1 instead of -1..1
	const real_t Exponent = MAX(real_t(Params.get("exponent", 1.f)), 0.01f); // >1 flattens valleys, sharpens peaks
	const bool bSet = String(Params.get("mode", "add")).to_lower() == "set";
	const FHeightFunction Target = [Noise, Amplitude, Base, bUnsigned, Exponent, bSet](const Vector3& Position)
	{
		real_t Value = Noise->get_noise_2d(Position.x, Position.z);
		if (bUnsigned)
		{
			Value = Math::pow(CLAMP(Value * 0.5f + 0.5f, 0.f, 1.f), Exponent);
		}
		else if (Exponent != 1.f)
		{
			Value = (Value < 0.f ? -1.f : 1.f) * Math::pow(Math::abs(Value), Exponent);
		}
		const real_t Offset = Base + Amplitude * Value;
		return bSet ? Offset : Position.y + Offset;
	};
	return ModifyHeights(Params, Target);
}

Dictionary Terrain3DAgent::ApplyHeightFunction(const Dictionary& Params)
{
	const Callable Function = Params.get("function", Callable());
	if (!Function.is_valid())
	{
		return Fail("apply_height_function needs `function`: Callable(x: float, z: float, height: float) -> float");
	}
	return ModifyHeights(Params, [&Function](const Vector3& Position)
		{
			const Variant Value = Function.call(Position.x, Position.z, Position.y);
			return IsNumber(Value) ? real_t(Value) : real_t(NAN);
		});
}

namespace
{
	enum class EStampMode
	{
		Set,
		Min,
		Max,
	};

	/** Piecewise-linear cross section: height at a signed distance beyond the path edge. */
	struct FProfile
	{
		std::vector<Vector2> Knots; // (distance beyond half width, height), sorted by distance

		bool Read(const Variant& Value)
		{
			Knots.clear();
			if (Value.get_type() != Variant::ARRAY)
			{
				return false;
			}
			const Array Items = Value;
			Vector2 Knot;
			for (int64_t Index = 0; Index < Items.size(); ++Index)
			{
				if (!ToPair(Items[Index], Knot))
				{
					return false;
				}
				Knots.push_back(Knot);
			}
			std::sort(Knots.begin(), Knots.end(), [](const Vector2& Left, const Vector2& Right) { return Left.x < Right.x; });
			return !Knots.empty();
		}

		real_t Reach() const { return Knots.back().x; }

		real_t Evaluate(const real_t Distance) const
		{
			if (Distance <= Knots.front().x)
			{
				return Knots.front().y;
			}
			for (size_t Index = 1; Index < Knots.size(); ++Index)
			{
				if (Distance <= Knots[Index].x)
				{
					const Vector2& From = Knots[Index - 1];
					const Vector2& To = Knots[Index];
					const real_t Span = To.x - From.x;
					return Span <= CMP_EPSILON ? To.y : Math::lerp(From.y, To.y, (Distance - From.x) / Span);
				}
			}
			return Knots.back().y;
		}
	};

	/** Polyline with a uniform-grid index over its segments, for nearest-segment queries. */
	struct FPathIndex
	{
		std::vector<Vector3> Points; // y = path height, NaN if none was given
		std::vector<real_t> HalfWidths;
		Vector2 Origin;
		real_t CellSize = 1.f;
		Vector2i Cells;
		std::vector<std::vector<int32_t>> Buckets;

		void Build(const real_t Reach)
		{
			Rect2 Bounds(Vector2(Points[0].x, Points[0].z), Vector2());
			for (const Vector3& Point : Points)
			{
				Bounds.expand_to(Vector2(Point.x, Point.z));
			}
			Bounds = Bounds.grow(Reach);
			CellSize = MAX(Reach * 0.5f, real_t(32.f));
			Origin = Bounds.position;
			Cells = Vector2i(int32_t(Math::ceil(Bounds.size.x / CellSize)) + 1, int32_t(Math::ceil(Bounds.size.y / CellSize)) + 1);
			Buckets.assign(size_t(Cells.x) * size_t(Cells.y), std::vector<int32_t>());
			for (int32_t Segment = 0; Segment + 1 < int32_t(Points.size()); ++Segment)
			{
				const Vector2 A(Points[Segment].x, Points[Segment].z);
				const Vector2 B(Points[Segment + 1].x, Points[Segment + 1].z);
				const Vector2 Low = (A.min(B) - Vector2(Reach, Reach) - Origin) / CellSize;
				const Vector2 High = (A.max(B) + Vector2(Reach, Reach) - Origin) / CellSize;
				for (int32_t Y = MAX(int32_t(Low.y), 0); Y <= MIN(int32_t(High.y), Cells.y - 1); ++Y)
				{
					for (int32_t X = MAX(int32_t(Low.x), 0); X <= MIN(int32_t(High.x), Cells.x - 1); ++X)
					{
						Buckets[size_t(Y) * Cells.x + X].push_back(Segment);
					}
				}
			}
		}

		/**
		 * Nearest point on the path to P.
		 * @param OutSide +1 on the side (t.z, 0, -t.x) points to, -1 on the other.
		 * @return false if no segment is within reach.
		 */
		bool Query(const Vector2& P, real_t& OutDistance, real_t& OutSide, real_t& OutHalfWidth, real_t& OutPathY,
			real_t& OutBeyondEnd) const
		{
			const Vector2i Cell = Vector2i(((P - Origin) / CellSize).floor());
			if (Cell.x < 0 || Cell.y < 0 || Cell.x >= Cells.x || Cell.y >= Cells.y)
			{
				return false;
			}
			const std::vector<int32_t>& Bucket = Buckets[size_t(Cell.y) * Cells.x + Cell.x];
			real_t Best = INFINITY;
			const int32_t LastSegment = int32_t(Points.size()) - 2;
			for (const int32_t Segment : Bucket)
			{
				const Vector2 A(Points[Segment].x, Points[Segment].z);
				const Vector2 B(Points[Segment + 1].x, Points[Segment + 1].z);
				const Vector2 AB = B - A;
				const real_t Length2 = AB.length_squared();
				const real_t RawT = Length2 > CMP_EPSILON ? (P - A).dot(AB) / Length2 : 0.f;
				const real_t T = CLAMP(RawT, real_t(0.f), real_t(1.f));
				const Vector2 Offset = P - (A + AB * T);
				const real_t Distance2 = Offset.length_squared();
				if (Distance2 < Best)
				{
					Best = Distance2;
					const Vector2 Normal(AB.y, -AB.x); // (t.z, -t.x), matching the river town frame
					const real_t Length = Math::sqrt(Length2);
					OutSide = Offset.dot(Normal) >= 0.f ? 1.f : -1.f;
					OutHalfWidth = Math::lerp(HalfWidths[Segment], HalfWidths[Segment + 1], T);
					OutPathY = Math::lerp(Points[Segment].y, Points[Segment + 1].y, T);
					OutBeyondEnd = 0.f;
					// Past an end the nearest point is the end itself, which would sweep the profile
					// round it in a half disc (a dam across a river). Measure across the end segment
					// instead, and report how far past the end the point lies.
					const bool bBeforeStart = Segment == 0 && RawT < 0.f;
					const bool bAfterEnd = Segment == LastSegment && RawT > 1.f;
					if ((bBeforeStart || bAfterEnd) && Length > CMP_EPSILON)
					{
						OutBeyondEnd = (bBeforeStart ? -RawT : RawT - 1.f) * Length;
						Best = MIN(Best, real_t(Math::pow(Normal.normalized().dot(P - A), real_t(2.f))));
					}
				}
			}
			if (!std::isfinite(Best))
			{
				return false;
			}
			OutDistance = Math::sqrt(Best);
			return true;
		}
	};
} // namespace

Dictionary Terrain3DAgent::StampPath(const Dictionary& Params)
{
	FPathIndex Path;
	const Variant PointsValue = Params.get("points", Variant());
	if (PointsValue.get_type() != Variant::ARRAY && PointsValue.get_type() != Variant::PACKED_VECTOR3_ARRAY &&
		PointsValue.get_type() != Variant::PACKED_VECTOR2_ARRAY)
	{
		return Fail("stamp_path needs `points`: a polyline of [x, z] or [x, y, z]");
	}
	PackedVector3Array Points;
	Dictionary PointsOnly;
	PointsOnly["points"] = PointsValue;
	if (!ReadPoints(PointsOnly, Points) || Points.size() < 2)
	{
		return Fail("stamp_path needs at least two `points`");
	}
	for (const Vector3& Point : Points)
	{
		Path.Points.push_back(Point);
	}

	const real_t DefaultHalfWidth = MAX(real_t(Params.get("half_width", 0.f)), real_t(0.f));
	Path.HalfWidths.assign(Path.Points.size(), DefaultHalfWidth);
	if (Params.has("half_widths"))
	{
		const Array Widths = Params["half_widths"];
		if (Widths.size() != int64_t(Path.Points.size()))
		{
			return Fail("`half_widths` needs one entry per point");
		}
		for (int64_t Index = 0; Index < Widths.size(); ++Index)
		{
			Path.HalfWidths[size_t(Index)] = MAX(real_t(Widths[Index]), real_t(0.f));
		}
	}

	// paint_only: texture the band without touching heights; re-stamping is not idempotent inside
	// the falloff, so a second pass just to paint would move the ground.
	const bool bPaintOnly = bool(Params.get("paint_only", false));

	FProfile Left;
	FProfile Right;
	const Variant Shared = Params.get("profile", Variant());
	const bool bLeft = Left.Read(Params.get("profile_left", Shared));
	const bool bRight = Right.Read(Params.get("profile_right", Shared));
	if (!bPaintOnly && (!bLeft || !bRight))
	{
		return Fail("stamp_path needs `profile` (or `profile_left` + `profile_right`): [[distance beyond the edge, height], ...]");
	}
	const bool bRelative = Params.get("relative", false);
	if (bRelative)
	{
		for (const Vector3& Point : Path.Points)
		{
			if (std::isnan(Point.y))
			{
				return Fail("`relative` profiles need [x, y, z] points");
			}
		}
	}
	const real_t Falloff = MAX(real_t(Params.get("falloff", 0.f)), real_t(0.f));
	const real_t EndFalloff = MAX(real_t(Params.get("end_falloff", 0.f)), real_t(0.f));
	const String ModeName = String(Params.get("mode", "set")).to_lower();
	EStampMode Mode = EStampMode::Set;
	if (ModeName == "min" || ModeName == "carve")
	{
		Mode = EStampMode::Min;
	}
	else if (ModeName == "max" || ModeName == "fill")
	{
		Mode = EStampMode::Max;
	}
	else if (ModeName != "set")
	{
		return Fail("Unknown mode '" + ModeName + "': set, min (carve) or max (fill)");
	}

	real_t MaxHalfWidth = 0.f;
	for (const real_t HalfWidth : Path.HalfWidths)
	{
		MaxHalfWidth = MAX(MaxHalfWidth, HalfWidth);
	}
	// In paint_only the band defines the reach instead of the (absent) profile.
	const Dictionary Paint = Params.get("paint", Dictionary());
	const real_t ProfileReach = bLeft && bRight ? MAX(Left.Reach(), Right.Reach()) : 0.f;
	const real_t PaintReach = bPaintOnly && Paint.has("asset_id")
		? MAX(Math::abs(real_t(Paint.get("from", 0.f))), Math::abs(real_t(Paint.get("to", 0.f)))) + 1.f
		: 0.f;
	const real_t Reach = MaxHalfWidth + MAX(ProfileReach, PaintReach) + Falloff;
	if (Reach <= 0.f)
	{
		return Fail("The profile never reaches past the path centre");
	}
	Path.Build(Reach);

	Rect2 Bounds(Vector2(Path.Points[0].x, Path.Points[0].z), Vector2());
	for (const Vector3& Point : Path.Points)
	{
		Bounds.expand_to(Vector2(Point.x, Point.z));
	}
	Dictionary AreaParams;
	AreaParams["area"] = Bounds.grow(Reach);
	AreaParams["auto_regions"] = Params.get("auto_regions", false);
	Dictionary Result;
	if (bPaintOnly)
	{
		Result["ok"] = true;
	}
	else
	{
		Result = ModifyHeights(AreaParams,
		[&Path, &Left, &Right, Falloff, EndFalloff, Mode, bRelative](const Vector3& Position)
		{
			real_t Distance = 0.f;
			real_t Side = 0.f;
			real_t HalfWidth = 0.f;
			real_t PathY = 0.f;
			real_t BeyondEnd = 0.f;
			if (!Path.Query(Vector2(Position.x, Position.z), Distance, Side, HalfWidth, PathY, BeyondEnd))
			{
				return real_t(NAN);
			}
			const real_t EndWeight = BeyondEnd <= 0.f ? 1.f : (EndFalloff > 0.f ? 1.f - smoothstep(0.f, EndFalloff, BeyondEnd) : 0.f);
			const FProfile& Profile = Side > 0.f ? Left : Right;
			const real_t Beyond = Distance - HalfWidth;
			real_t Weight = 1.f;
			if (Beyond > Profile.Reach())
			{
				Weight = Falloff > 0.f ? 1.f - smoothstep(Profile.Reach(), Profile.Reach() + Falloff, Beyond) : 0.f;
			}
			Weight *= EndWeight;
			if (Weight <= 0.f)
			{
				return real_t(NAN);
			}
			real_t Height = Profile.Evaluate(Beyond) + (bRelative ? PathY : 0.f);
			if (Mode == EStampMode::Min)
			{
				Height = MIN(Height, Position.y);
			}
			else if (Mode == EStampMode::Max)
			{
				Height = MAX(Height, Position.y);
			}
			return Math::lerp(Position.y, Height, Weight);
		});
	}
	if (!bool(Result.get("ok", false)))
	{
		return Result;
	}
	Result["reach"] = Reach;

	// paint: {asset_id, from, to} textures the band `from`..`to` metres beyond the path edge
	// (negative = inside), on the path's own stretch only. `Paint` was read above.
	if (Paint.has("asset_id"))
	{
		const int32_t AssetId = CLAMP(int32_t(Paint["asset_id"]), 0, Terrain3DAssets::MAX_TEXTURES - 1);
		const real_t From = Paint.get("from", -INFINITY);
		const real_t To = Paint.get("to", 0.f);
		const Rect2 PaintArea = Bounds.grow(MAX(real_t(0.f), MaxHalfWidth + To));
		int32_t Painted = 0;
		BeginEdit(Terrain3DEditor::TEXTURE, Terrain3DEditor::REPLACE, Dictionary(), Vector3(PaintArea.get_center().x, 0.f, PaintArea.get_center().y));
		VisitArea(PaintArea, EAreaShape::Rectangle, 0.f, EAreaSide::Inside, EMissingRegion::Skip, TYPE_CONTROL,
			[&Path, From, To, AssetId, &Painted](const FVertex& Vertex)
			{
				real_t Distance = 0.f;
				real_t Side = 0.f;
				real_t HalfWidth = 0.f;
				real_t PathY = 0.f;
				real_t BeyondEnd = 0.f;
				if (!Path.Query(Vector2(Vertex.Position.x, Vertex.Position.z), Distance, Side, HalfWidth, PathY, BeyondEnd) || BeyondEnd > 0.f)
				{
					return;
				}
				const real_t Beyond = Distance - HalfWidth;
				if (Beyond < From || Beyond > To)
				{
					return;
				}
				uint32_t Bits = as_uint(float(Vertex.Map->get_pixelv(Vertex.Pixel).r));
				Bits = (Bits & ~(enc_base(0x1F) | enc_overlay(0x1F) | enc_blend(0xFF) | enc_auto(true))) |
					enc_base(uint8_t(AssetId)) | enc_overlay(uint8_t(AssetId));
				Vertex.Map->set_pixelv(Vertex.Pixel, Color(as_float(Bits), 0.f, 0.f, 1.f));
				++Painted;
			});
		EndEdit(Painted > 0 ? MapBit(TYPE_CONTROL) : 0u, AABB(Vector3(PaintArea.position.x, 0.f, PaintArea.position.y), Vector3(PaintArea.size.x, 0.f, PaintArea.size.y)));
		Result["painted"] = Painted;
	}
	return Result;
}

Dictionary Terrain3DAgent::PackTexture(const Dictionary& Params)
{
	// Terrain3D samples albedo RGB + height A and normal RGB + roughness A from two texture arrays,
	// so every asset must be packed that way, at one size and one format, with mipmaps.
	const auto LoadMap = [](const Variant& Value) -> Ref<Image>
	{
		if (Value.get_type() == Variant::OBJECT)
		{
			return Value;
		}
		const String Path = Value;
		if (Path.is_empty())
		{
			return Ref<Image>();
		}
		Ref<Image> Loaded = Image::load_from_file(Path);
		if (Loaded.is_valid())
		{
			Loaded->decompress();
			Loaded->clear_mipmaps();
			Loaded->convert(Image::FORMAT_RGBA8);
		}
		return Loaded;
	};
	Ref<Image> Albedo = LoadMap(Params.get("albedo", ""));
	Ref<Image> Normal = LoadMap(Params.get("normal", ""));
	if (Albedo.is_null() || Normal.is_null())
	{
		return Fail("pack_texture needs `albedo` and `normal` (image paths)");
	}
	const String OutAlbedo = Params.get("out_albedo", "");
	const String OutNormal = Params.get("out_normal", "");
	if (OutAlbedo.is_empty() || OutNormal.is_empty())
	{
		return Fail("pack_texture needs `out_albedo` and `out_normal` (.res paths)");
	}
	const int32_t Size = CLAMP(int32_t(Params.get("size", Albedo->get_width())), 16, 8192);
	Ref<Image> Height = LoadMap(Params.get("height", ""));
	if (Height.is_null())
	{
		// No displacement map: height-blend on brightness, which is what the eye reads as relief anyway.
		Height = Terrain3DUtil::luminance_to_height(Albedo);
	}
	Ref<Image> Roughness = LoadMap(Params.get("roughness", ""));
	if (Roughness.is_null())
	{
		Roughness = Image::create_empty(Size, Size, false, Image::FORMAT_RGBA8);
		Roughness->fill(Color(0.8f, 0.8f, 0.8f, 1.f));
	}
	for (Ref<Image>* Map : { &Albedo, &Normal, &Height, &Roughness })
	{
		if ((*Map)->get_width() != Size || (*Map)->get_height() != Size)
		{
			(*Map)->resize(Size, Size, Image::INTERPOLATE_LANCZOS);
		}
	}

	constexpr int32_t RED = 0;
	const bool bNormalizeHeight = Params.get("normalize_height", true);
	const bool bInvertGreen = Params.get("directx_normal", false); // Godot is OpenGL / Y+
	const Ref<Image> NoAO;
	Ref<Image> AlbedoHeight = Terrain3DUtil::pack_image(Albedo, Height, NoAO, false, false, bNormalizeHeight, RED, RED);
	Ref<Image> NormalRoughness = Terrain3DUtil::pack_image(Normal, Roughness, NoAO, bInvertGreen, false, false, RED, RED);
	if (AlbedoHeight.is_null() || NormalRoughness.is_null())
	{
		return Fail("Packing failed; see the Terrain3DUtil error above");
	}

	Dictionary Result;
	for (const std::pair<Ref<Image>, String>& Output : { std::make_pair(AlbedoHeight, OutAlbedo), std::make_pair(NormalRoughness, OutNormal) })
	{
		Ref<Image> Packed = Output.first;
		Packed->generate_mipmaps();
		// Portable textures skip the import pipeline, which would otherwise guess compression per
		// file (and turn a normal map into RG) and break Terrain3D's same-format rule.
		Ref<PortableCompressedTexture2D> Texture;
		Texture.instantiate();
		// Outside the editor the texture drops its compressed buffer once uploaded, and would save empty.
		Texture->set_keep_compressed_buffer(true);
		Texture->create_from_image(Packed, PortableCompressedTexture2D::COMPRESSION_MODE_LOSSLESS);
		DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(Output.second.get_base_dir()));
		const Error Saved = ResourceSaver::get_singleton()->save(Texture, Output.second);
		if (Saved != OK)
		{
			return Fail("Cannot save " + Output.second + " (error " + String::num_int64(Saved) + ")");
		}
		Texture->take_over_path(Output.second);
	}
	Result["ok"] = true;
	Result["albedo"] = OutAlbedo;
	Result["normal"] = OutNormal;
	Result["size"] = Size;
	return Result;
}

Dictionary Terrain3DAgent::PaintArea(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Rect2 Area;
	EAreaShape Shape = EAreaShape::Rectangle;
	bool bValid = false;
	if (!ReadArea(Params, Area, Shape, bValid) || !bValid)
	{
		return Fail("paint_area needs an `area` or `center` + `radius`/`size`");
	}
	const real_t Falloff = MAX(real_t(Params.get("falloff", 0.f)), 0.f);
	const EAreaSide Side = bool(Params.get("invert", false)) ? EAreaSide::Outside : EAreaSide::Inside;
	const bool bTexture = Params.has("asset_id") || Params.has("texture");
	const int32_t AssetId = CLAMP(int32_t(Params.get("asset_id", Params.get("texture", 0))), 0, Terrain3DAssets::MAX_TEXTURES - 1);
	const bool bHole = Params.has("hole");
	const bool bNavigation = Params.has("navigation");
	const bool bAuto = Params.has("autoshader");
	const bool bHoleValue = Params.get("hole", false);
	const bool bNavigationValue = Params.get("navigation", false);
	const bool bAutoValue = Params.get("autoshader", false);
	Color Tint;
	const bool bColor = Params.has("color");
	if (bColor && !ToColor(Params["color"], Tint))
	{
		return Fail("Cannot parse color " + Variant(Params["color"]).stringify());
	}
	const bool bRoughness = Params.has("roughness");
	// Same -100..100 percentage as the plugin, stored as 0..1 around 0.5.
	const real_t Roughness = 0.5f + 0.5f * CLAMP(real_t(Params.get("roughness", 0.f)), -100.f, 100.f) * 0.01f;
	const bool bControl = bTexture || bHole || bNavigation || bAuto;
	if (!bControl && !bColor && !bRoughness)
	{
		return Fail("paint_area needs at least one of asset_id, hole, navigation, autoshader, color, roughness");
	}

	BeginEdit(bControl ? Terrain3DEditor::TEXTURE : Terrain3DEditor::COLOR, Terrain3DEditor::REPLACE, Dictionary(),
		Vector3(Area.get_center().x, 0.f, Area.get_center().y));
	int32_t Count = 0;
	uint32_t Maps = 0u;
	if (bControl)
	{
		Maps |= MapBit(TYPE_CONTROL);
		Count = VisitArea(Area, Shape, Falloff, Side, EMissingRegion::Skip, TYPE_CONTROL,
			[bTexture, AssetId, bHole, bHoleValue, bNavigation, bNavigationValue, bAuto, bAutoValue](const FVertex& Vertex)
			{
				// Control bits are discrete; the falloff decides membership at half weight.
				if (Vertex.Weight < 0.5f)
				{
					return;
				}
				uint32_t Bits = as_uint(float(Vertex.Map->get_pixelv(Vertex.Pixel).r));
				if (bTexture)
				{
					Bits = (Bits & ~(enc_base(0x1F) | enc_overlay(0x1F) | enc_blend(0xFF) | enc_auto(true))) |
						enc_base(uint8_t(AssetId)) | enc_overlay(uint8_t(AssetId));
				}
				if (bHole)
				{
					Bits = (Bits & ~enc_hole(true)) | enc_hole(bHoleValue);
				}
				if (bNavigation)
				{
					Bits = (Bits & ~enc_nav(true)) | enc_nav(bNavigationValue);
				}
				if (bAuto)
				{
					Bits = (Bits & ~enc_auto(true)) | enc_auto(bAutoValue);
				}
				Vertex.Map->set_pixelv(Vertex.Pixel, Color(as_float(Bits), 0.f, 0.f, 1.f));
			});
	}
	if (bColor || bRoughness)
	{
		Maps |= MapBit(TYPE_COLOR);
		Count = MAX(Count, VisitArea(Area, Shape, Falloff, Side, EMissingRegion::Skip, TYPE_COLOR,
			[bColor, Tint, bRoughness, Roughness](const FVertex& Vertex)
			{
				const Color Source = Vertex.Map->get_pixelv(Vertex.Pixel);
				Color Result = bColor ? Source.lerp(Tint, Vertex.Weight) : Source;
				Result.a = Source.a;
				if (bRoughness)
				{
					// Quantized like the plugin so picked values match painted ones.
					Result.a = float(int32_t(Math::lerp(real_t(Source.a), Roughness, Vertex.Weight) * 255.f)) / 255.f;
				}
				Vertex.Map->set_pixelv(Vertex.Pixel, Result);
			}));
	}
	const Rect2 EditedRect = Side == EAreaSide::Outside ? GetRegionBounds() : Area;
	EndEdit(Count > 0 ? Maps : 0u, AABB(Vector3(EditedRect.position.x, 0.f, EditedRect.position.y), Vector3(EditedRect.size.x, 0.f, EditedRect.size.y)));

	Dictionary Result;
	Result["ok"] = true;
	Result["vertices"] = Count;
	Result["area"] = Area;
	if (Count == 0)
	{
		Result["warning"] = "No vertices painted: the area covers no regions";
	}
	return Result;
}

Dictionary Terrain3DAgent::PaintTextureRules(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();
	const Array Rules = Params.get("rules", Array());
	if (Rules.is_empty())
	{
		return Fail("paint_texture_rules needs `rules`: [{asset_id, slope: [min, max], height: [min, max], autoshader}]; first match wins");
	}
	struct FRule
	{
		int32_t AssetId = 0;
		Vector2 Slope = Vector2(0.f, 90.f);
		Vector2 Height = Vector2(-INFINITY, INFINITY);
		bool bAutoshader = false;
	};
	std::vector<FRule> Parsed;
	for (int64_t Index = 0; Index < Rules.size(); ++Index)
	{
		const Dictionary Source = Rules[Index];
		FRule Rule;
		Rule.AssetId = CLAMP(int32_t(Source.get("asset_id", 0)), 0, Terrain3DAssets::MAX_TEXTURES - 1);
		Rule.bAutoshader = Source.get("autoshader", false);
		if ((Source.has("slope") && !ToPair(Source["slope"], Rule.Slope)) || (Source.has("height") && !ToPair(Source["height"], Rule.Height)))
		{
			return Fail("Rule " + String::num_int64(Index) + ": `slope` and `height` are [min, max]");
		}
		Parsed.push_back(Rule);
	}

	Rect2 Area = GetRegionBounds();
	EAreaShape Shape = EAreaShape::Rectangle;
	bool bValid = true;
	if (ReadArea(Params, Area, Shape, bValid) && !bValid)
	{
		return Fail("Malformed `area`");
	}
	const real_t Falloff = MAX(real_t(Params.get("falloff", 0.f)), 0.f);
	const EAreaSide Side = bool(Params.get("invert", false)) ? EAreaSide::Outside : EAreaSide::Inside;
	std::vector<int32_t> Counts(Parsed.size(), 0);

	BeginEdit(Terrain3DEditor::TEXTURE, Terrain3DEditor::REPLACE, Dictionary(), Vector3(Area.get_center().x, 0.f, Area.get_center().y));
	const int32_t Visited = VisitArea(Area, Shape, Falloff, Side, EMissingRegion::Skip, TYPE_CONTROL,
		[&Parsed, &Counts, Data](const FVertex& Vertex)
		{
			if (Vertex.Weight < 0.5f)
			{
				return;
			}
			const Vector3 Normal = Data->get_normal(Vertex.Position);
			if (!Normal.is_finite())
			{
				return; // Hole, or the terrain edge
			}
			const real_t SlopeDegrees = Math::rad_to_deg(Normal.angle_to(V3_UP));
			for (size_t Index = 0; Index < Parsed.size(); ++Index)
			{
				const FRule& Rule = Parsed[Index];
				if (SlopeDegrees < Rule.Slope.x || SlopeDegrees > Rule.Slope.y || Vertex.Position.y < Rule.Height.x || Vertex.Position.y > Rule.Height.y)
				{
					continue;
				}
				uint32_t Bits = as_uint(float(Vertex.Map->get_pixelv(Vertex.Pixel).r));
				if (Rule.bAutoshader)
				{
					Bits |= enc_auto(true);
				}
				else
				{
					Bits = (Bits & ~(enc_base(0x1F) | enc_overlay(0x1F) | enc_blend(0xFF) | enc_auto(true))) |
						enc_base(uint8_t(Rule.AssetId)) | enc_overlay(uint8_t(Rule.AssetId));
				}
				Vertex.Map->set_pixelv(Vertex.Pixel, Color(as_float(Bits), 0.f, 0.f, 1.f));
				++Counts[Index];
				return;
			}
		});
	EndEdit(Visited > 0 ? MapBit(TYPE_CONTROL) : 0u, AABB(Vector3(Area.position.x, 0.f, Area.position.y), Vector3(Area.size.x, 0.f, Area.size.y)));

	Array RuleCounts;
	for (const int32_t RuleCount : Counts)
	{
		RuleCounts.push_back(RuleCount);
	}
	Dictionary Result;
	Result["ok"] = true;
	Result["vertices"] = Visited;
	Result["rule_counts"] = RuleCounts;
	return Result;
}

///////////////////////////
// Erosion
///////////////////////////

namespace
{
	/** `base_level` if given, else the ocean surface when the ocean is on, else none. */
	float ReadBaseLevel(const Dictionary& Params, const Terrain3D* Terrain)
	{
		Variant BaseLevel = Params.get("base_level", Variant());
		if (BaseLevel.get_type() == Variant::NIL && Terrain && Terrain->is_ocean_enabled())
		{
			const Ref<ShaderMaterial> Ocean = Terrain->get_ocean_material();
			BaseLevel = Ocean.is_valid() ? Ocean->get_shader_parameter("sea_level") : Variant(0.f);
		}
		return IsNumber(BaseLevel) ? float(BaseLevel) : -INFINITY;
	}
} // namespace

bool Terrain3DAgent::ExtractErosionGrid(const Dictionary& Params, FErosionJob& Job, String& OutError)
{
	Terrain3D* Terrain = GetValidTerrain();
	if (!Terrain)
	{
		OutError = "No terrain: call set_terrain() with a Terrain3D that is inside the scene tree";
		return false;
	}
	Terrain3DData* Data = Terrain->get_data();
	Job.Area = GetRegionBounds();
	bool bValid = true;
	if (ReadArea(Params, Job.Area, Job.Shape, bValid) && !bValid)
	{
		OutError = "Malformed `area`";
		return false;
	}
	if (!Job.Area.has_area())
	{
		OutError = "Nothing to erode: the terrain has no regions and no `area` was given";
		return false;
	}
	Job.Falloff = MAX(real_t(Params.get("falloff", 0.f)), 0.f);

	// The grid holds exactly the vertices VisitArea will visit, so a vertex maps to a cell by index.
	const real_t Spacing = Terrain->get_vertex_spacing();
	Job.GridMin = Vector2i(int32_t(Math::ceil(Job.Area.position.x / Spacing)), int32_t(Math::ceil(Job.Area.position.y / Spacing)));
	const Vector2i GridEnd(int32_t(Math::floor(Job.Area.get_end().x / Spacing)) + 1, int32_t(Math::floor(Job.Area.get_end().y / Spacing)) + 1);
	TerrainErosion::FGrid& Grid = Job.Grid;
	Grid.Width = GridEnd.x - Job.GridMin.x;
	Grid.Depth = GridEnd.y - Job.GridMin.y;
	Grid.CellSize = float(Spacing);
	const int64_t CellCount = int64_t(Grid.Width) * int64_t(Grid.Depth);
	if (Grid.Width < 3 || Grid.Depth < 3)
	{
		OutError = "Area is smaller than 3x3 vertices";
		return false;
	}
	if (CellCount > ERODE_MAX_CELLS)
	{
		OutError = "Area has " + String::num_int64(CellCount) + " vertices; erode at most " + String::num_int64(ERODE_MAX_CELLS) + " at a time";
		return false;
	}
	Grid.Heights.resize(size_t(CellCount));
	Job.ValidCells = 0;
	for (int32_t Z = 0; Z < Grid.Depth; ++Z)
	{
		for (int32_t X = 0; X < Grid.Width; ++X)
		{
			const Vector3 Position(real_t(Job.GridMin.x + X) * Spacing, 0.f, real_t(Job.GridMin.y + Z) * Spacing);
			const float Height = Data->has_regionp(Position) ? float(Data->get_height(Position)) : NAN;
			Grid.Heights[Grid.Index(X, Z)] = Height;
			Job.ValidCells += std::isnan(Height) ? 0 : 1;
		}
	}
	if (Job.ValidCells == 0)
	{
		OutError = "The area covers no regions";
		return false;
	}
	Job.Original = Grid.Heights;
	return true;
}

// Writes the eroded grid back through the editor (one undo step), blended by the area falloff, and
// optionally textures channels (Flow), sediment (deposited) and scoured ground (eroded).
Dictionary Terrain3DAgent::CommitErosionGrid(const Dictionary& Params, const FErosionJob& Job, const std::vector<float>& Flow)
{
	Terrain3D* Terrain = GetValidTerrain();
	Terrain3DData* Data = Terrain->get_data();
	const real_t Spacing = Terrain->get_vertex_spacing();
	const TerrainErosion::FGrid& Grid = Job.Grid;
	const std::vector<float>& Original = Job.Original;
	const Vector2i GridMin = Job.GridMin;

	const Dictionary Paint = Params.get("paint", Dictionary());
	const int32_t FlowAsset = Paint.get("flow", -1);
	const int32_t DepositedAsset = Paint.get("deposited", -1);
	const int32_t ErodedAsset = Paint.get("eroded", -1);
	const float DepositedThreshold = Paint.get("deposited_threshold", 0.5f);
	const float ErodedThreshold = Paint.get("eroded_threshold", 1.f);
	float FlowThreshold = INFINITY;
	if (FlowAsset >= 0 && !Flow.empty())
	{
		if (Paint.has("flow_min"))
		{
			FlowThreshold = Paint["flow_min"];
		}
		else
		{
			// Paint the busiest fraction of cells, so the knob doesn't depend on droplet count or scale.
			std::vector<float> Busy;
			for (const float Value : Flow)
			{
				if (Value > 0.f)
				{
					Busy.push_back(Value);
				}
			}
			const real_t Fraction = CLAMP(real_t(Paint.get("flow_fraction", 0.03f)), 0.f, 1.f);
			const size_t Keep = size_t(real_t(Job.ValidCells) * Fraction);
			if (Keep > 0 && Keep <= Busy.size())
			{
				std::nth_element(Busy.begin(), Busy.begin() + (Busy.size() - Keep), Busy.end());
				FlowThreshold = Busy[Busy.size() - Keep];
			}
		}
	}

	const auto CellOf = [&GridMin, Spacing](const Vector3& Position)
	{
		return Vector2i(int32_t(Math::round(Position.x / Spacing)) - GridMin.x, int32_t(Math::round(Position.z / Spacing)) - GridMin.y);
	};
	const Rect2& Area = Job.Area;
	BeginEdit(Terrain3DEditor::HEIGHT, Terrain3DEditor::REPLACE, Dictionary(), Vector3(Area.get_center().x, 0.f, Area.get_center().y));
	Vector2 Range(INFINITY, -INFINITY);
	double Eroded = 0.0;
	double Deposited = 0.0;
	float MaxCut = 0.f;
	float MaxFill = 0.f;
	const int32_t Count = VisitArea(Area, Job.Shape, Job.Falloff, EAreaSide::Inside, EMissingRegion::Skip, TYPE_HEIGHT,
		[&CellOf, &Grid, &Range, &Eroded, &Deposited, &MaxCut, &MaxFill, Data](const FVertex& Vertex)
		{
			const Vector2i Cell = CellOf(Vertex.Position);
			if (!Grid.IsInside(Cell.x, Cell.y))
			{
				return;
			}
			const size_t Index = Grid.Index(Cell.x, Cell.y);
			if (std::isnan(Grid.Heights[Index]))
			{
				return;
			}
			const real_t Height = Math::lerp(Vertex.Position.y, real_t(Grid.Heights[Index]), Vertex.Weight);
			const float Change = float(Height - Vertex.Position.y);
			Eroded += Change < 0.f ? -Change : 0.f;
			Deposited += Change > 0.f ? Change : 0.f;
			MaxCut = MAX(MaxCut, -Change);
			MaxFill = MAX(MaxFill, Change);
			Vertex.Map->set_pixelv(Vertex.Pixel, Color(Height, 0.f, 0.f, 1.f));
			Vertex.Region->update_height(Height);
			Data->update_master_height(Height);
			Range = Vector2(MIN(Range.x, Height), MAX(Range.y, Height));
		});
	uint32_t Maps = Count > 0 ? MapBit(TYPE_HEIGHT) : 0u;
	Dictionary PaintCounts;
	if (FlowAsset >= 0 || DepositedAsset >= 0 || ErodedAsset >= 0)
	{
		int32_t FlowCount = 0;
		int32_t DepositedCount = 0;
		int32_t ErodedCount = 0;
		VisitArea(Area, Job.Shape, Job.Falloff, EAreaSide::Inside, EMissingRegion::Skip, TYPE_CONTROL,
			[&CellOf, &Grid, &Original, &Flow, FlowAsset, FlowThreshold, DepositedAsset, DepositedThreshold, ErodedAsset,
				ErodedThreshold, &FlowCount, &DepositedCount, &ErodedCount](const FVertex& Vertex)
			{
				const Vector2i Cell = CellOf(Vertex.Position);
				if (Vertex.Weight < 0.5f || !Grid.IsInside(Cell.x, Cell.y))
				{
					return;
				}
				const size_t Index = Grid.Index(Cell.x, Cell.y);
				const float Change = Grid.Heights[Index] - Original[Index];
				int32_t AssetId = -1;
				if (FlowAsset >= 0 && Flow[Index] >= FlowThreshold)
				{
					AssetId = FlowAsset;
					++FlowCount;
				}
				else if (DepositedAsset >= 0 && Change >= DepositedThreshold)
				{
					AssetId = DepositedAsset;
					++DepositedCount;
				}
				else if (ErodedAsset >= 0 && -Change >= ErodedThreshold)
				{
					AssetId = ErodedAsset;
					++ErodedCount;
				}
				if (AssetId < 0)
				{
					return;
				}
				uint32_t Bits = as_uint(float(Vertex.Map->get_pixelv(Vertex.Pixel).r));
				Bits = (Bits & ~(enc_base(0x1F) | enc_overlay(0x1F) | enc_blend(0xFF) | enc_auto(true))) |
					enc_base(uint8_t(AssetId)) | enc_overlay(uint8_t(AssetId));
				Vertex.Map->set_pixelv(Vertex.Pixel, Color(as_float(Bits), 0.f, 0.f, 1.f));
			});
		Maps |= MapBit(TYPE_CONTROL);
		PaintCounts["flow"] = FlowCount;
		PaintCounts["deposited"] = DepositedCount;
		PaintCounts["eroded"] = ErodedCount;
	}
	const AABB Edited(Vector3(Area.position.x, Count > 0 ? Range.x : 0.f, Area.position.y),
		Vector3(Area.size.x, Count > 0 ? Range.y - Range.x : 0.f, Area.size.y));
	EndEdit(Maps, Edited);

	const real_t CellArea = Spacing * Spacing;
	Dictionary Result;
	Result["ok"] = true;
	Result["vertices"] = Count;
	Result["eroded_volume"] = Eroded * CellArea; // m^3
	Result["deposited_volume"] = Deposited * CellArea;
	Result["max_cut"] = MaxCut; // Metres
	Result["max_fill"] = MaxFill;
	Result["edited_height_range"] = Count > 0 ? Range : Vector2();
	if (!PaintCounts.is_empty())
	{
		Result["painted"] = PaintCounts;
	}
	return Result;
}

Dictionary Terrain3DAgent::Erode(const Dictionary& Params)
{
	FErosionJob Job;
	String Error;
	if (!ExtractErosionGrid(Params, Job, Error))
	{
		return Fail(Error);
	}

	TerrainErosion::FHydraulicSettings Hydraulic;
	Hydraulic.Droplets = Params.has("droplets")
		? int64_t(Params["droplets"])
		: int64_t(real_t(Job.ValidCells) * CLAMP(real_t(Params.get("droplets_per_vertex", 1.f)), 0.f, 16.f));
	Hydraulic.Droplets = CLAMP(Hydraulic.Droplets, int64_t(0), ERODE_MAX_DROPLETS);
	Hydraulic.Seed = uint64_t(int64_t(Params.get("seed", 0)));
	Hydraulic.MaxLifetime = CLAMP(int32_t(Params.get("lifetime", Hydraulic.MaxLifetime)), 1, 1024);
	Hydraulic.Radius = CLAMP(int32_t(Params.get("radius", Hydraulic.Radius)), 1, 16);
	Hydraulic.Inertia = Params.get("inertia", Hydraulic.Inertia);
	Hydraulic.Capacity = Params.get("capacity", Hydraulic.Capacity);
	Hydraulic.MinSlope = Params.get("min_slope", Hydraulic.MinSlope);
	Hydraulic.ErodeRate = CLAMP(float(Params.get("erode_rate", Hydraulic.ErodeRate)), 0.f, 1.f);
	Hydraulic.DepositRate = CLAMP(float(Params.get("deposit_rate", Hydraulic.DepositRate)), 0.f, 1.f);
	Hydraulic.Evaporation = Params.get("evaporation", Hydraulic.Evaporation);
	Hydraulic.Gravity = Params.get("gravity", Hydraulic.Gravity);
	// Rivers stop at the sea. With the ocean on, the base level defaults to its surface.
	Hydraulic.BaseLevel = ReadBaseLevel(Params, GetValidTerrain());

	TerrainErosion::FThermalSettings Thermal;
	Thermal.Iterations = CLAMP(int32_t(Params.get("thermal_iterations", 0)), 0, 1000);
	Thermal.TalusSlope = float(Math::tan(Math::deg_to_rad(CLAMP(real_t(Params.get("talus_angle", 38.f)), 1.f, 89.f))));
	Thermal.Rate = Params.get("thermal_rate", Thermal.Rate);

	const uint64_t StartTime = Time::get_singleton()->get_ticks_usec();
	std::vector<float> Flow;
	TerrainErosion::RunHydraulic(Job.Grid, Hydraulic, Flow);
	const uint64_t HydraulicTime = Time::get_singleton()->get_ticks_usec();
	// Thermal after hydraulic: it relaxes the gully walls the droplets just cut.
	TerrainErosion::RunThermal(Job.Grid, Thermal);
	const uint64_t ThermalTime = Time::get_singleton()->get_ticks_usec();

	Dictionary Result = CommitErosionGrid(Params, Job, Flow);
	Result["droplets"] = Hydraulic.Droplets;
	Result["thermal_iterations"] = Thermal.Iterations;
	Result["base_level"] = std::isinf(Hydraulic.BaseLevel) ? Variant() : Variant(Hydraulic.BaseLevel);
	Result["hydraulic_ms"] = double(HydraulicTime - StartTime) / 1000.0;
	Result["thermal_ms"] = double(ThermalTime - HydraulicTime) / 1000.0;
	return Result;
}

Dictionary Terrain3DAgent::StreamPower(const Dictionary& Params)
{
	FErosionJob Job;
	String Error;
	if (!ExtractErosionGrid(Params, Job, Error))
	{
		return Fail(Error);
	}

	TerrainErosion::FStreamPowerSettings Settings;
	Settings.Iterations = CLAMP(int32_t(Params.get("iterations", Settings.Iterations)), 0, 10000);
	Settings.TimeStep = MAX(float(Params.get("dt", Settings.TimeStep)), 0.f);
	Settings.Erodibility = MAX(float(Params.get("k", Settings.Erodibility)), 0.f);
	Settings.AreaExponent = CLAMP(float(Params.get("m", Settings.AreaExponent)), 0.f, 2.f);
	Settings.SlopeExponent = CLAMP(float(Params.get("n", Settings.SlopeExponent)), 0.5f, 4.f);
	Settings.MinArea = MAX(float(Params.get("min_area", STREAM_POWER_MIN_AREA)), 0.f);
	Settings.Uplift = Params.get("uplift", Settings.Uplift);
	Settings.Diffusion = MAX(float(Params.get("diffusion", Settings.Diffusion)), 0.f);
	const real_t ValleyAngle = CLAMP(real_t(Params.get("valley_angle", STREAM_POWER_VALLEY_ANGLE)), 0.f, 80.f);
	Settings.RoutingJitter = CLAMP(float(Params.get("routing_jitter", STREAM_POWER_ROUTING_JITTER)), 0.f, 4.f);
	Settings.Seed = uint64_t(int64_t(Params.get("seed", 0)));
	Settings.BaseLevel = ReadBaseLevel(Params, GetValidTerrain());

	const uint64_t StartTime = Time::get_singleton()->get_ticks_usec();
	// Rivers are a large-scale feature: solve on a coarser grid (`resolution` metres per cell) and add
	// the upsampled height change. It is ~Factor^2 cheaper, and the bilinear upsample hides the
	// 45-degree staircase that D8 routing draws on the native grid.
	const real_t Spacing = Job.Grid.CellSize;
	const int32_t Factor = CLAMP(int32_t(Math::round(real_t(Params.get("resolution", STREAM_POWER_RESOLUTION)) / Spacing)), 1, 64);
	TerrainErosion::FGrid Coarse = Factor > 1 ? TerrainErosion::Downsample(Job.Grid, Factor) : Job.Grid;
	const std::vector<float> CoarseBefore = Coarse.Heights;
	std::vector<float> CoarseArea;
	const TerrainErosion::FStreamPowerStats Stats = TerrainErosion::RunStreamPower(Coarse, Settings, CoarseArea);
	std::vector<float> DrainageArea;
	if (Factor > 1)
	{
		std::vector<float> CoarseChange(Coarse.Heights.size());
		for (size_t Index = 0; Index < CoarseChange.size(); ++Index)
		{
			CoarseChange[Index] = Coarse.Heights[Index] - CoarseBefore[Index];
		}
		TerrainErosion::Blur(CoarseChange, Coarse.Width, Coarse.Depth, CLAMP(int32_t(Params.get("smooth", STREAM_POWER_SMOOTH_PASSES)), 0, 16));
		const std::vector<float> Change = TerrainErosion::Upsample(CoarseChange, Coarse.Width, Coarse.Depth, Factor, Job.Grid.Width, Job.Grid.Depth);
		for (size_t Index = 0; Index < Job.Grid.Heights.size(); ++Index)
		{
			if (!std::isnan(Change[Index]))
			{
				Job.Grid.Heights[Index] += Change[Index];
			}
		}
		DrainageArea = TerrainErosion::Upsample(CoarseArea, Coarse.Width, Coarse.Depth, Factor, Job.Grid.Width, Job.Grid.Depth);
		for (float& Area : DrainageArea)
		{
			Area = std::isnan(Area) ? 0.f : Area;
		}
	}
	else
	{
		Job.Grid.Heights = Coarse.Heights;
		DrainageArea = CoarseArea;
	}
	if (ValleyAngle > 0.f)
	{
		TerrainErosion::WidenValleys(Job.Grid, Job.Original, float(Math::tan(Math::deg_to_rad(ValleyAngle))), Settings.BaseLevel);
	}
	const uint64_t SolverTime = Time::get_singleton()->get_ticks_usec();

	// paint.flow marks channels by drainage area: `flow_min` in m^2, or the `flow_fraction` largest.
	Dictionary Result = CommitErosionGrid(Params, Job, DrainageArea);
	Result["resolution"] = Spacing * real_t(Factor);
	Result["iterations"] = Settings.Iterations;
	Result["years"] = double(Settings.Iterations) * double(Settings.TimeStep);
	Result["base_level"] = std::isinf(Settings.BaseLevel) ? Variant() : Variant(Settings.BaseLevel);
	Result["max_drainage_area"] = Stats.MaxArea; // m^2
	Result["outlets"] = Stats.Outlets;
	Result["lake_cells"] = Stats.LakeCells;
	Result["diffusion_substeps"] = Stats.DiffusionSubsteps;
	Result["solver_ms"] = double(SolverTime - StartTime) / 1000.0;
	return Result;
}

///////////////////////////
// Instances
///////////////////////////

Dictionary Terrain3DAgent::PlaceInstances(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();
	const int32_t MeshId = Params.get("asset_id", 0);
	if (MeshId < 0 || MeshId >= Terrain->get_assets()->get_mesh_count())
	{
		return Fail("asset_id " + String::num_int64(MeshId) + " is not a mesh asset; there are " +
			String::num_int64(Terrain->get_assets()->get_mesh_count()) + " (add one with add_mesh)");
	}

	TypedArray<Transform3D> Transforms;
	PackedColorArray Colors;
	int32_t Skipped = 0;
	Color Tint = COLOR_WHITE;
	if (Params.has("color") && !ToColor(Params["color"], Tint))
	{
		return Fail("Cannot parse color " + Variant(Params["color"]).stringify());
	}
	if (Params.has("transforms"))
	{
		// Exact placement from GDScript; not re-draped.
		const Array Source = Params["transforms"];
		for (int64_t Index = 0; Index < Source.size(); ++Index)
		{
			if (Source[Index].get_type() != Variant::TRANSFORM3D)
			{
				return Fail("`transforms` must hold Transform3D values");
			}
			Transforms.push_back(Source[Index]);
			Colors.push_back(Tint);
		}
	}
	else
	{
		PackedVector3Array Points;
		if (!ReadPoints(Params, Points))
		{
			return Fail("place_instances needs `points` or `transforms`");
		}
		Ref<RandomNumberGenerator> Random;
		Random.instantiate();
		Random->set_seed(uint64_t(int64_t(Params.get("seed", 0))));
		const bool bSnap = Params.get("snap", true);
		const bool bAlign = Params.get("align_to_normal", false);
		const real_t Scale = Params.get("scale", 1.f);
		const real_t RandomScale = CLAMP(real_t(Params.get("random_scale", 0.f)), 0.f, 0.99f); // +/- fraction
		const real_t Spin = Math::deg_to_rad(real_t(Params.get("spin", 0.f)));
		const real_t RandomSpin = Math::deg_to_rad(real_t(Params.get("random_spin", 360.f)));
		const real_t HeightOffset = Params.get("height_offset", 0.f);
		for (const Vector3& Point : Points)
		{
			Vector3 Position = Point;
			if (bSnap || std::isnan(Position.y))
			{
				Position.y = Data->get_height(Position);
			}
			if (std::isnan(Position.y))
			{
				++Skipped; // No region, or a hole
				continue;
			}
			Vector3 Up = V3_UP;
			Basis Orientation;
			if (bAlign)
			{
				const Vector3 Normal = Data->get_normal(Position);
				if (Normal.is_finite())
				{
					Up = Normal.normalized();
					const Vector3 Forward(0.f, 0.f, 1.f);
					const Vector3 Right = -Forward.cross(Up);
					if (Right.length_squared() > 0.001f)
					{
						Orientation = Basis(Right, Up, Forward).orthonormalized();
					}
				}
			}
			const real_t Angle = Spin + RandomSpin * Random->randf();
			if (Math::abs(Angle) > CMP_EPSILON)
			{
				Orientation = Orientation.rotated(Up, Angle);
			}
			const real_t InstanceScale = Scale * (1.f + RandomScale * (2.f * Random->randf() - 1.f));
			Orientation = Orientation.scaled(Vector3(InstanceScale, InstanceScale, InstanceScale));
			Transforms.push_back(Transform3D(Orientation, Position + Up * HeightOffset));
			Colors.push_back(Tint);
		}
	}

	if (!Transforms.is_empty())
	{
		Dictionary BrushData;
		BrushData["asset_id"] = MeshId;
		BeginEdit(Terrain3DEditor::INSTANCER, Terrain3DEditor::ADD, BrushData, Transform3D(Transforms[0]).origin);
		Terrain->get_instancer()->add_transforms(MeshId, Transforms, Colors, true);
		EndEdit(0u, AABB());
	}
	Dictionary Result;
	Result["ok"] = true;
	Result["placed"] = Transforms.size();
	Result["skipped"] = Skipped;
	Result["instance_count"] = int64_t(Terrain->get_assets()->get_mesh_asset(MeshId)->get_instance_count());
	return Result;
}

Dictionary Terrain3DAgent::ClearInstances(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	const int32_t MeshCount = Terrain->get_assets()->get_mesh_count();
	const int32_t MeshId = Params.get("asset_id", -1); // -1 = every mesh
	if (MeshId >= MeshCount)
	{
		return Fail("asset_id " + String::num_int64(MeshId) + " is not a mesh asset");
	}
	if (Params.has("center") || Params.has("point"))
	{
		// Circular removal, through the instancer brush so it is undoable.
		Dictionary Stroke = Params.duplicate();
		Stroke["action"] = "unscatter";
		Stroke["asset_id"] = MAX(MeshId, 0);
		Stroke["all_meshes"] = MeshId < 0;
		Stroke["size"] = 2.f * real_t(Params.get("radius", 10.f));
		Stroke["strength"] = INSTANCE_REMOVE_STRENGTH;
		Stroke["brush"] = "round";
		if (MeshCount == 0)
		{
			Dictionary Result;
			Result["ok"] = true;
			return Result;
		}
		return Brush(Stroke);
	}
	for (int32_t Id = 0; Id < MeshCount; ++Id)
	{
		if (MeshId < 0 || Id == MeshId)
		{
			Terrain->get_instancer()->clear_by_mesh(Id);
		}
	}
	Dictionary Result;
	Result["ok"] = true;
	Result["note"] = "Whole-mesh clears bypass undo";
	return Result;
}

///////////////////////////
// Assets and settings
///////////////////////////

Dictionary Terrain3DAgent::AddTexture(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Ref<Terrain3DAssets> Assets = Terrain->get_assets();
	const int32_t Id = Params.get("id", Assets->get_texture_count());
	if (Id < 0 || Id >= Terrain3DAssets::MAX_TEXTURES)
	{
		return Fail("Texture id must be 0-" + String::num_int64(Terrain3DAssets::MAX_TEXTURES - 1));
	}
	Ref<Terrain3DTextureAsset> Asset;
	Asset.instantiate();
	Asset->set_name(Params.get("name", "Texture " + String::num_int64(Id)));
	for (const char* Key : { "albedo", "normal" })
	{
		if (!Params.has(Key))
		{
			continue;
		}
		const Ref<Texture2D> Texture = ToTexture(Params[Key]);
		if (Texture.is_null())
		{
			return Fail(String("Cannot load ") + Key + " texture " + Variant(Params[Key]).stringify());
		}
		String(Key) == "albedo" ? Asset->set_albedo_texture(Texture) : Asset->set_normal_texture(Texture);
	}
	PackedStringArray Skip;
	for (const char* Key : { "op", "id", "name", "albedo", "normal" })
	{
		Skip.push_back(Key);
	}
	const PackedStringArray Errors = ApplyProperties(Asset.ptr(), Params, Skip);
	Assets->set_texture_asset(Id, Asset);

	Dictionary Result;
	Result["ok"] = Errors.is_empty();
	Result["id"] = Id;
	Result["texture_count"] = Assets->get_texture_count();
	if (!Errors.is_empty())
	{
		Result["error"] = String("; ").join(Errors);
	}
	return Result;
}

Dictionary Terrain3DAgent::AddMesh(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Ref<Terrain3DAssets> Assets = Terrain->get_assets();
	const int32_t Id = Params.get("id", Assets->get_mesh_count());
	if (Id < 0 || Id >= Terrain3DAssets::MAX_MESHES)
	{
		return Fail("Mesh id must be 0-" + String::num_int64(Terrain3DAssets::MAX_MESHES - 1));
	}
	Ref<Terrain3DMeshAsset> Asset;
	Asset.instantiate();
	Asset->set_name(Params.get("name", "Mesh " + String::num_int64(Id)));
	if (Params.has("scene"))
	{
		const Ref<PackedScene> Scene = Params["scene"].get_type() == Variant::STRING
			? Ref<PackedScene>(ResourceLoader::get_singleton()->load(String(Params["scene"])))
			: Ref<PackedScene>(Params["scene"]);
		if (Scene.is_null())
		{
			return Fail("Cannot load scene " + Variant(Params["scene"]).stringify());
		}
		Asset->set_scene_file(Scene);
	}
	else
	{
		Asset->set_generated_type(Terrain3DMeshAsset::TYPE_TEXTURE_CARD);
	}
	PackedStringArray Skip;
	for (const char* Key : { "op", "id", "name", "scene" })
	{
		Skip.push_back(Key);
	}
	const PackedStringArray Errors = ApplyProperties(Asset.ptr(), Params, Skip);
	Assets->set_mesh_asset(Id, Asset);

	Dictionary Result;
	Result["ok"] = Errors.is_empty();
	Result["id"] = Id;
	Result["mesh_count"] = Assets->get_mesh_count();
	if (!Errors.is_empty())
	{
		Result["error"] = String("; ").join(Errors);
	}
	return Result;
}

Dictionary Terrain3DAgent::SetProperties(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	const Dictionary Properties = Params.get("properties", Dictionary());
	if (Properties.is_empty())
	{
		return Fail("set_properties needs `properties`: {\"vertex_spacing\": 2, \"material.world_background\": \"none\", ...}");
	}
	Dictionary Changed;
	PackedStringArray Errors;
	const Array Paths = Properties.keys();
	for (int64_t Index = 0; Index < Paths.size(); ++Index)
	{
		const String Path = Paths[Index];
		const PackedStringArray Segments = Path.split(".");
		Object* Target = Terrain;
		for (int64_t Segment = 0; Target && Segment < Segments.size() - 1; ++Segment)
		{
			Target = Target->get(Segments[Segment]);
		}
		if (!Target)
		{
			Errors.push_back(Path + String(": no object at ") + Path.get_slice(".", 0));
			continue;
		}
		Dictionary Values;
		Values[Segments[Segments.size() - 1]] = Properties[Path];
		const PackedStringArray PropertyErrors = ApplyProperties(Target, Values, PackedStringArray());
		if (!PropertyErrors.is_empty())
		{
			Errors.push_back(Path + String(": ") + PropertyErrors[0]);
			continue;
		}
		Changed[Path] = Target->get(Segments[Segments.size() - 1]);
	}
	Dictionary Result;
	Result["ok"] = Errors.is_empty();
	Result["values"] = Changed;
	if (!Errors.is_empty())
	{
		Result["error"] = String("; ").join(Errors);
	}
	return Result;
}

///////////////////////////
// Inspection
///////////////////////////

Dictionary Terrain3DAgent::GetSummary(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();
	Ref<Terrain3DAssets> Assets = Terrain->get_assets();

	Array Textures;
	for (int32_t Id = 0; Id < Assets->get_texture_count(); ++Id)
	{
		const Ref<Terrain3DTextureAsset> Asset = Assets->get_texture_asset(Id);
		Dictionary Entry;
		Entry["id"] = Id;
		Entry["name"] = Asset.is_valid() ? Asset->get_name() : String();
		Entry["has_albedo"] = Asset.is_valid() && Asset->get_albedo_texture().is_valid();
		Entry["preview_color"] = GetTexturePreviewColor(Id).to_html(false);
		Textures.push_back(Entry);
	}
	Array Meshes;
	for (int32_t Id = 0; Id < Assets->get_mesh_count(); ++Id)
	{
		const Ref<Terrain3DMeshAsset> Asset = Assets->get_mesh_asset(Id);
		Dictionary Entry;
		Entry["id"] = Id;
		Entry["name"] = Asset.is_valid() ? Asset->get_name() : String();
		Entry["instance_count"] = Asset.is_valid() ? int64_t(Asset->get_instance_count()) : int64_t(0);
		Entry["scene"] = Asset.is_valid() && Asset->get_scene_file().is_valid() ? Asset->get_scene_file()->get_path() : String();
		Meshes.push_back(Entry);
	}

	Dictionary Result;
	Result["ok"] = true;
	Result["version"] = Terrain->get_version();
	Result["data_directory"] = Terrain->get_data_directory();
	Result["region_size"] = int32_t(Terrain->get_region_size());
	Result["vertex_spacing"] = Terrain->get_vertex_spacing();
	Result["region_world_size"] = real_t(Terrain->get_region_size()) * Terrain->get_vertex_spacing();
	Result["region_count"] = Data->get_region_count();
	Result["region_locations"] = Data->get_region_locations();
	Result["world_bounds"] = GetRegionBounds();
	Result["height_range"] = Data->get_height_range();
	Result["textures"] = Textures;
	Result["meshes"] = Meshes;
	Result["collision_mode"] = int32_t(Terrain->get_collision_mode());
	Result["is_editor"] = IS_EDITOR;
	Result["has_plugin"] = Terrain->get_plugin() != nullptr;
	Result["undo_count"] = Editor->get_local_undo_count();
	Result["redo_count"] = Editor->get_local_redo_count();
	Result["undo_names"] = Editor->get_local_undo_names();
	Result["commands"] = GetCommandNames();
	return Result;
}

Dictionary Terrain3DAgent::Sample(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();
	PackedVector3Array Points;
	if (!ReadPoints(Params, Points))
	{
		return Fail("sample needs `points` or `point`");
	}
	Array Samples;
	for (const Vector3& Point : Points)
	{
		Dictionary Entry;
		Entry["position"] = Vector2(Point.x, Point.z);
		Entry["region"] = Data->get_region_location(Point);
		Entry["has_region"] = Data->has_regionp(Point);
		Entry["height"] = Data->get_height(Point);
		const Vector3 Normal = Data->get_normal(Point);
		Entry["normal"] = Normal;
		Entry["slope"] = Normal.is_finite() ? Math::rad_to_deg(Normal.angle_to(V3_UP)) : real_t(NAN);
		const uint32_t Control = Data->get_control(Point);
		if (Control != UINT32_MAX)
		{
			Entry["base_id"] = get_base(Control);
			Entry["overlay_id"] = get_overlay(Control);
			Entry["blend"] = real_t(get_blend(Control)) / 255.f;
			Entry["hole"] = is_hole(Control);
			Entry["navigation"] = is_nav(Control);
			Entry["autoshader"] = is_auto(Control);
			Entry["color"] = Data->get_color(Point);
			Entry["roughness"] = 200.f * (Data->get_roughness(Point) - 0.5f); // Plugin's -100..100
		}
		Samples.push_back(Entry);
	}
	Dictionary Result;
	Result["ok"] = true;
	Result["samples"] = Samples;
	return Result;
}

Dictionary Terrain3DAgent::GetHeightGrid(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();
	Rect2 Area = GetRegionBounds();
	EAreaShape Shape = EAreaShape::Rectangle;
	bool bValid = true;
	if (ReadArea(Params, Area, Shape, bValid) && !bValid)
	{
		return Fail("Malformed `area`");
	}
	const real_t Step = MAX(real_t(Params.get("step", Terrain->get_vertex_spacing())), real_t(0.01f));
	const int64_t Width = int64_t(Math::floor(Area.size.x / Step)) + 1;
	const int64_t Depth = int64_t(Math::floor(Area.size.y / Step)) + 1;
	if (Width * Depth > HEIGHT_GRID_MAX_SAMPLES)
	{
		return Fail("Grid of " + String::num_int64(Width) + "x" + String::num_int64(Depth) + " is too large; raise `step`");
	}
	PackedFloat32Array Heights;
	Heights.resize(Width * Depth);
	Vector2 Range(INFINITY, -INFINITY);
	for (int64_t Z = 0; Z < Depth; ++Z)
	{
		for (int64_t X = 0; X < Width; ++X)
		{
			const real_t Height = Data->get_height(Vector3(Area.position.x + X * Step, 0.f, Area.position.y + Z * Step));
			Heights.set(Z * Width + X, Height);
			if (!std::isnan(Height))
			{
				Range = Vector2(MIN(Range.x, Height), MAX(Range.y, Height));
			}
		}
	}
	Dictionary Result;
	Result["ok"] = true;
	Result["origin"] = Area.position;
	Result["step"] = Step;
	Result["width"] = Width;
	Result["depth"] = Depth;
	Result["heights"] = Heights; // Row-major, z rows of x; NaN where there is no region
	Result["height_range"] = Range.x <= Range.y ? Range : Vector2();
	return Result;
}

Color Terrain3DAgent::GetTexturePreviewColor(const int32_t AssetId) const
{
	Terrain3D* Terrain = GetValidTerrain();
	if (!Terrain)
	{
		return PaletteColor(AssetId);
	}
	const Ref<Terrain3DTextureAsset> Asset = Terrain->get_assets()->get_texture_asset(AssetId);
	if (Asset.is_null() || Asset->get_albedo_texture().is_null())
	{
		return PaletteColor(AssetId);
	}
	Ref<Image> Albedo = Asset->get_albedo_texture()->get_image();
	if (Albedo.is_null() || Albedo->is_empty())
	{
		return PaletteColor(AssetId);
	}
	if (Albedo->is_compressed())
	{
		Albedo->decompress();
	}
	Albedo->convert(Image::FORMAT_RGBA8);
	Albedo->resize(1, 1, Image::INTERPOLATE_BILINEAR);
	Color Average = Albedo->get_pixel(0, 0) * Asset->get_albedo_color();
	Average.a = 1.f;
	return Average;
}

Dictionary Terrain3DAgent::SavePreview(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();
	Rect2 Area = GetRegionBounds();
	EAreaShape Shape = EAreaShape::Rectangle;
	bool bValid = true;
	if (ReadArea(Params, Area, Shape, bValid) && !bValid)
	{
		return Fail("Malformed `area`");
	}
	if (!Area.has_area())
	{
		return Fail("Nothing to preview: the terrain has no regions and no `area` was given");
	}
	const String Path = Params.get("path", "user://terrain_agent_preview.png");
	const String Mode = String(Params.get("mode", "height")).to_lower();
	if (Mode != "height" && Mode != "texture" && Mode != "color" && Mode != "shade")
	{
		return Fail("Unknown mode '" + Mode + "': height, texture, color or shade");
	}
	const int32_t MaxSize = CLAMP(int32_t(Params.get("max_size", PREVIEW_DEFAULT_SIZE)), 16, PREVIEW_MAX_SIZE);
	// A whole number of vertices per pixel: heights are sampled nearest, so a fractional ratio
	// beats against the vertex grid and the hill shading shows moire.
	const real_t VertexSpacing = Terrain->get_vertex_spacing();
	const real_t Step = VertexSpacing * MAX(real_t(1.f), Math::ceil(MAX(Area.size.x, Area.size.y) / real_t(MaxSize) / VertexSpacing));
	const int32_t Width = MAX(1, int32_t(Math::ceil(Area.size.x / Step)));
	const int32_t Depth = MAX(1, int32_t(Math::ceil(Area.size.y / Step)));
	const real_t ContourInterval = Params.get("contour_interval", 0.f);
	const bool bShowInstances = Params.get("show_instances", true);
	const bool bShowRegions = Params.get("show_region_grid", true);

	// Heights first: shading and contours need neighbours.
	std::vector<real_t> Heights(size_t(Width) * size_t(Depth), NAN);
	Vector2 Range(INFINITY, -INFINITY);
	for (int32_t Z = 0; Z < Depth; ++Z)
	{
		for (int32_t X = 0; X < Width; ++X)
		{
			const Vector3 Position(Area.position.x + (X + 0.5f) * Step, 0.f, Area.position.y + (Z + 0.5f) * Step);
			real_t Height = Data->get_height(Position);
			const uint32_t Control = Data->get_control(Position);
			if (Control != UINT32_MAX && is_hole(Control))
			{
				Height = NAN;
			}
			Heights[size_t(Z) * Width + X] = Height;
			if (!std::isnan(Height))
			{
				Range = Vector2(MIN(Range.x, Height), MAX(Range.y, Height));
			}
		}
	}
	const real_t Span = Range.y > Range.x ? Range.y - Range.x : 1.f;
	const Vector3 Sun = Vector3(-1.f, 1.2f, -1.f).normalized(); // From the north-west, the cartographic convention
	const Color Background(0.12f, 0.12f, 0.14f);
	const Color AutoTint(0.62f, 0.45f, 0.78f);
	std::vector<Color> TextureColors;
	for (int32_t Id = 0; Id < Terrain3DAssets::MAX_TEXTURES; ++Id)
	{
		TextureColors.push_back(Mode == "texture" ? GetTexturePreviewColor(Id) : Color());
	}

	Ref<Image> Preview = Image::create_empty(Width, Depth, false, Image::FORMAT_RGB8);
	for (int32_t Z = 0; Z < Depth; ++Z)
	{
		for (int32_t X = 0; X < Width; ++X)
		{
			const real_t Height = Heights[size_t(Z) * Width + X];
			if (std::isnan(Height))
			{
				Preview->set_pixel(X, Z, Background);
				continue;
			}
			const auto HeightAt = [&Heights, Width, Depth, Height](const int32_t SampleX, const int32_t SampleZ)
			{
				const real_t Sampled = Heights[size_t(CLAMP(SampleZ, 0, Depth - 1)) * Width + CLAMP(SampleX, 0, Width - 1)];
				return std::isnan(Sampled) ? Height : Sampled;
			};
			const Vector3 Normal = Vector3(HeightAt(X - 1, Z) - HeightAt(X + 1, Z), 2.f * Step, HeightAt(X, Z - 1) - HeightAt(X, Z + 1)).normalized();
			const real_t Shade = 0.35f + 0.65f * CLAMP(Normal.dot(Sun), 0.f, 1.f);
			const Vector3 Position(Area.position.x + (X + 0.5f) * Step, Height, Area.position.y + (Z + 0.5f) * Step);

			Color Base(0.8f, 0.8f, 0.8f);
			if (Mode == "height")
			{
				Base = HeightTint((Height - Range.x) / Span);
			}
			else if (Mode == "texture")
			{
				const uint32_t Control = Data->get_control(Position);
				const real_t Blend = real_t(get_blend(Control)) / 255.f;
				Base = TextureColors[get_base(Control)].lerp(TextureColors[get_overlay(Control)], Blend);
				if (is_auto(Control))
				{
					Base = Base.lerp(AutoTint, 0.6f);
				}
			}
			else if (Mode == "color")
			{
				Base = Data->get_color(Position);
			}
			Color Pixel = Base * Shade;
			if (ContourInterval > 0.f)
			{
				const real_t Band = Math::floor(Height / ContourInterval);
				if (Math::floor(HeightAt(X + 1, Z) / ContourInterval) != Band || Math::floor(HeightAt(X, Z + 1) / ContourInterval) != Band)
				{
					Pixel = Pixel * 0.55f;
				}
			}
			Pixel.a = 1.f;
			Preview->set_pixel(X, Z, Pixel);
		}
	}

	const real_t RegionWorldSize = real_t(Terrain->get_region_size()) * Terrain->get_vertex_spacing();
	if (bShowRegions)
	{
		const Color GridColor(1.f, 1.f, 1.f);
		for (int32_t Z = 0; Z < Depth; ++Z)
		{
			for (int32_t X = 0; X < Width; ++X)
			{
				const real_t WorldX = Area.position.x + X * Step;
				const real_t WorldZ = Area.position.y + Z * Step;
				const bool bOnLine = Math::floor(WorldX / RegionWorldSize) != Math::floor((WorldX + Step) / RegionWorldSize) ||
					Math::floor(WorldZ / RegionWorldSize) != Math::floor((WorldZ + Step) / RegionWorldSize);
				if (bOnLine)
				{
					Preview->set_pixel(X, Z, Preview->get_pixel(X, Z).lerp(GridColor, 0.35f));
				}
			}
		}
	}

	int64_t InstancesDrawn = 0;
	if (bShowInstances)
	{
		const TypedArray<Vector2i> Locations = Data->get_region_locations();
		for (int64_t Index = 0; Index < Locations.size(); ++Index)
		{
			const Vector2i Location = Locations[Index];
			const Ref<Terrain3DRegion> Region = Data->get_region(Location);
			if (Region.is_null())
			{
				continue;
			}
			const Vector2 RegionOrigin = Vector2(Location) * RegionWorldSize;
			const Dictionary MeshInstances = Region->get_instances();
			const Array MeshIds = MeshInstances.keys();
			for (int64_t MeshIndex = 0; MeshIndex < MeshIds.size(); ++MeshIndex)
			{
				const int32_t MeshId = MeshIds[MeshIndex];
				const Color Dot = PaletteColor(MeshId + 7).lightened(0.3f);
				const Dictionary Cells = MeshInstances[MeshIds[MeshIndex]];
				const Array CellKeys = Cells.keys();
				for (int64_t CellIndex = 0; CellIndex < CellKeys.size(); ++CellIndex)
				{
					const Array Triple = Cells[CellKeys[CellIndex]];
					const TypedArray<Transform3D> Transforms = Triple[0];
					for (int64_t Instance = 0; Instance < Transforms.size(); ++Instance)
					{
						const Vector3 Origin = Transform3D(Transforms[Instance]).origin;
						const int32_t X = int32_t((RegionOrigin.x + Origin.x - Area.position.x) / Step);
						const int32_t Z = int32_t((RegionOrigin.y + Origin.z - Area.position.y) / Step);
						if (X >= 0 && Z >= 0 && X < Width && Z < Depth)
						{
							Preview->set_pixel(X, Z, Dot);
							++InstancesDrawn;
						}
					}
				}
			}
		}
	}

	const String AbsolutePath = ProjectSettings::get_singleton()->globalize_path(Path);
	DirAccess::make_dir_recursive_absolute(AbsolutePath.get_base_dir());
	const Error Saved = Preview->save_png(AbsolutePath);
	if (Saved != OK)
	{
		return Fail("Cannot write " + AbsolutePath + " (error " + String::num_int64(Saved) + ")");
	}

	Dictionary Legend;
	Legend["background"] = "no region, or a hole";
	Legend["orientation"] = "image x = world +x, image down = world +z";
	if (Mode == "height")
	{
		Legend["tint"] = "dark green (low) -> green -> tan -> brown -> white (high)";
	}
	else if (Mode == "texture")
	{
		Dictionary Textures;
		for (int32_t Id = 0; Id < Terrain->get_assets()->get_texture_count(); ++Id)
		{
			Textures[Id] = TextureColors[Id].to_html(false);
		}
		Legend["textures"] = Textures;
		Legend["autoshader"] = "tinted towards " + AutoTint.to_html(false);
	}
	if (bShowRegions)
	{
		Legend["region_grid"] = "light lines every " + String::num(RegionWorldSize) + " m";
	}
	if (bShowInstances)
	{
		Legend["instances"] = "single bright pixels, one colour per mesh id";
	}
	Dictionary Result;
	Result["ok"] = true;
	Result["path"] = Path;
	Result["absolute_path"] = AbsolutePath;
	Result["size"] = Vector2i(Width, Depth);
	Result["metres_per_pixel"] = Step;
	Result["area"] = Area;
	Result["height_range"] = Range.x <= Range.y ? Range : Vector2();
	Result["instances_drawn"] = InstancesDrawn;
	Result["legend"] = Legend;
	return Result;
}

///////////////////////////
// Import / export / history / persistence
///////////////////////////

Dictionary Terrain3DAgent::ImportHeightmap(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	Terrain3DData* Data = Terrain->get_data();
	Vector2 R16Range(0.f, 255.f);
	Vector2 R16Size;
	ToPair(Params.get("r16_range", Variant()), R16Range);
	ToPair(Params.get("r16_size", Variant()), R16Size);

	TypedArray<Image> Images;
	Images.resize(TYPE_MAX);
	int32_t Loaded = 0;
	const char* const KEYS[] = { "height", "control", "color" };
	for (int32_t Type = 0; Type < TYPE_MAX; ++Type)
	{
		// `path` / `image` are the height map, the common case.
		Variant Source = Params.get(String(KEYS[Type]) + "_path", Params.get(String(KEYS[Type]) + "_image", Variant()));
		if (Type == TYPE_HEIGHT && Source.get_type() == Variant::NIL)
		{
			Source = Params.get("path", Params.get("image", Variant()));
		}
		Ref<Image> Map;
		if (Source.get_type() == Variant::STRING)
		{
			Map = Util::load_image(Source, ResourceLoader::CACHE_MODE_IGNORE, R16Range, Vector2i(R16Size));
			if (Map.is_null())
			{
				return Fail("Cannot load " + String(Source));
			}
		}
		else
		{
			Map = Source;
		}
		if (Map.is_valid())
		{
			Images[Type] = Map;
			++Loaded;
		}
	}
	if (Loaded == 0)
	{
		return Fail("import_heightmap needs `path` or `image` (and optionally control_path / color_path)");
	}
	Vector3 Position(0.f, 0.f, 0.f);
	if (Params.has("position") && !ToPoint(Params["position"], Position))
	{
		return Fail("Malformed `position`");
	}
	Position.y = 0.f;
	Data->import_images(Images, Position, real_t(Params.get("offset", 0.f)), real_t(Params.get("scale", 1.f)));
	Terrain->get_collision()->update(V2I_MAX, true);

	Dictionary Result;
	Result["ok"] = true;
	Result["note"] = "Imports bypass undo";
	Result["region_count"] = Data->get_region_count();
	Result["world_bounds"] = GetRegionBounds();
	Result["height_range"] = Data->get_height_range();
	return Result;
}

Dictionary Terrain3DAgent::ExportMap(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	const String Path = Params.get("path", "");
	MapType Type = TYPE_HEIGHT;
	if (Path.is_empty() || (Params.has("map") && !ToMapType(Params["map"], Type)))
	{
		return Fail("export_map needs `path` (.exr/.r16/.png/.res...) and optionally `map`: height, control or color");
	}
	const Error Exported = Terrain->get_data()->export_image(Path, Type);
	if (Exported != OK)
	{
		return Fail("Export to " + Path + " failed (error " + String::num_int64(Exported) + ")");
	}
	Dictionary Result;
	Result["ok"] = true;
	Result["path"] = Path;
	return Result;
}

Dictionary Terrain3DAgent::BakeMesh(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	const String Path = Params.get("path", "");
	if (Path.is_empty())
	{
		return Fail("bake_mesh needs `path` (e.g. res://Terrain.res)");
	}
	const int32_t Lod = CLAMP(int32_t(Params.get("lod", 4)), 0, 8);
	const Terrain3DData::HeightFilter Filter = NormalizeName(Params.get("filter", "nearest")) == "minimum"
		? Terrain3DData::HEIGHT_FILTER_MINIMUM
		: Terrain3DData::HEIGHT_FILTER_NEAREST;
	const Ref<Mesh> Baked = Terrain->bake_mesh(Lod, Filter);
	if (Baked.is_null())
	{
		return Fail("bake_mesh produced no mesh; does the terrain have regions?");
	}
	const Error Saved = ResourceSaver::get_singleton()->save(Baked, Path);
	if (Saved != OK)
	{
		return Fail("Cannot save " + Path + " (error " + String::num_int64(Saved) + ")");
	}
	Dictionary Result;
	Result["ok"] = true;
	Result["path"] = Path;
	Result["surface_count"] = Baked->get_surface_count();
	Result["aabb"] = Baked->get_aabb();
	return Result;
}

Dictionary Terrain3DAgent::Undo(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	if (!Editor->undo_local())
	{
		return Fail(Terrain->get_plugin() && Terrain->get_editor()
				? "Nothing to undo here: with the editor plugin live, agent edits are on the editor's UndoRedo (Ctrl+Z)"
				: "Nothing to undo");
	}
	Terrain->get_collision()->update(V2I_MAX, true);
	Dictionary Result;
	Result["ok"] = true;
	Result["undo_count"] = Editor->get_local_undo_count();
	Result["redo_count"] = Editor->get_local_redo_count();
	return Result;
}

Dictionary Terrain3DAgent::Redo(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	if (!Editor->redo_local())
	{
		return Fail("Nothing to redo");
	}
	Terrain->get_collision()->update(V2I_MAX, true);
	Dictionary Result;
	Result["ok"] = true;
	Result["undo_count"] = Editor->get_local_undo_count();
	Result["redo_count"] = Editor->get_local_redo_count();
	return Result;
}

Dictionary Terrain3DAgent::Save(const Dictionary& Params)
{
	AGENT_REQUIRE_TERRAIN(Terrain);
	const String Directory = Params.get("directory", Terrain->get_data_directory());
	if (Directory.is_empty())
	{
		return Fail("save needs `directory`: the terrain has no data_directory");
	}
	DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(Directory));
	Terrain->get_data()->save_directory(Directory);

	Dictionary Result;
	Result["ok"] = true;
	Result["directory"] = Directory;
	const Ref<Terrain3DAssets> Assets = Terrain->get_assets();
	// Built-in assets (a scene sub-resource, path "...::id") are saved with the scene, not here.
	const String AssetsPath = Params.get("assets_path", Assets->get_path().contains("::") ? String() : Assets->get_path());
	if (!AssetsPath.is_empty())
	{
		const Error Saved = Assets->save(AssetsPath);
		Result["assets_path"] = AssetsPath;
		if (Saved != OK)
		{
			Result["ok"] = false;
			Result["error"] = "Cannot save assets to " + AssetsPath + " (error " + String::num_int64(Saved) + ")";
		}
	}
	return Result;
}

///////////////////////////
// Bindings
///////////////////////////

void Terrain3DAgent::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_terrain", "terrain"), &Terrain3DAgent::SetTerrain);
	ClassDB::bind_method(D_METHOD("get_terrain"), &Terrain3DAgent::GetTerrain);
	ClassDB::bind_method(D_METHOD("get_editor"), &Terrain3DAgent::GetEditor);
	ClassDB::bind_method(D_METHOD("get_last_error"), &Terrain3DAgent::GetLastError);
	ClassDB::bind_method(D_METHOD("execute", "commands"), &Terrain3DAgent::Execute);
	ClassDB::bind_method(D_METHOD("execute_file", "path"), &Terrain3DAgent::ExecuteFile);
	ClassDB::bind_static_method("Terrain3DAgent", D_METHOD("get_command_names"), &Terrain3DAgent::GetCommandNames);
	ClassDB::bind_static_method("Terrain3DAgent", D_METHOD("make_brush_image", "shape", "size", "falloff"),
		&Terrain3DAgent::MakeBrushImage, DEFVAL(BRUSH_IMAGE_SIZE), DEFVAL(DEFAULT_BRUSH_FALLOFF));
	for (const FCommand& Command : COMMANDS)
	{
		ClassDB::bind_method(D_METHOD(Command.Name, "params"), Command.Method, DEFVAL(Dictionary()));
	}
}

#endif // WITH_ABYSS
