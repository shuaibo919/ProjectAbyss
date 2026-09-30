// ProjectAbyss addition, not part of upstream Terrain3D. Compiled only WITH_ABYSS.

#pragma once

#ifdef WITH_ABYSS

#include <functional>
#include <vector>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>

#include "terrain_3d.h"
#include "terrain_3d_editor.h"
#include "terrain_3d_erosion.h"
#include "terrain_3d_region.h"

/**
 * Scriptable front end to Terrain3D for agents and headless tools.
 *
 * The editor plugin drives Terrain3DEditor from mouse events and GDScript UI state, so nothing
 * without a viewport can use it. This class owns its own Terrain3DEditor and replays the same
 * operations from data: brush strokes along a point list, exact per-vertex area edits,
 * procedural height, rule-based texturing, instancing, assets, inspection and a PNG preview an
 * agent can read back. Everything goes through Terrain3DEditor operations, so every edit is
 * undoable (Terrain3DEditor's local history at runtime, the editor's UndoRedo when the plugin
 * is live).
 *
 * Every command takes one Dictionary and returns one Dictionary with at least `ok` (and
 * `error` on failure), so `execute()` can run a JSON command list. Vectors may be given as
 * Godot types or as JSON arrays: a point is [x, z] or [x, y, z]; an area is [x, z, width, depth],
 * {min, max}, {center, size} or {center, radius}.
 */
class Terrain3DAgent : public RefCounted
{
	GDCLASS(Terrain3DAgent, RefCounted);
	CLASS_NAME();

public:
	enum class EMissingRegion
	{
		Skip,
		Create,
	};

	enum class EAreaShape
	{
		Rectangle,
		Ellipse,
	};

	/** Which side of the shape an area edit affects; Outside covers every existing region. */
	enum class EAreaSide
	{
		Inside,
		Outside,
	};

	/** One vertex visited by an area edit. Weight is the shape falloff at this vertex, 0-1. */
	struct FVertex
	{
		Terrain3DRegion* Region = nullptr;
		Image* Map = nullptr;
		Vector2i Pixel;
		Vector3 Position;
		real_t Weight = 1.f;
	};

	using FVertexVisitor = std::function<void(const FVertex&)>;

	/** Receives a vertex whose Position.y is its current height; returns the target height. */
	using FHeightFunction = std::function<real_t(const Vector3&)>;

	/** A terrain area copied into a plain grid for an erosion model to work on. */
	struct FErosionJob
	{
		Rect2 Area;
		EAreaShape Shape = EAreaShape::Rectangle;
		real_t Falloff = 0.f;
		Vector2i GridMin;
		int64_t ValidCells = 0;
		TerrainErosion::FGrid Grid;
		std::vector<float> Original;
	};

private:
	uint64_t TerrainId = 0;
	Terrain3DEditor* Editor = nullptr;
	Dictionary BrushCache;
	Dictionary LastError;

	Terrain3D* GetValidTerrain() const;
	Dictionary Fail(const String& Message);

	void BeginEdit(Terrain3DEditor::Tool Tool, Terrain3DEditor::Operation Operation, const Dictionary& BrushData,
		const Vector3& Position);
	void EndEdit(uint32_t EditedMaps, const AABB& EditedArea);
	bool EnsureRegion(const Vector2i& Location);
	int32_t VisitArea(const Rect2& Area, EAreaShape Shape, real_t Falloff, EAreaSide Side, EMissingRegion Missing, MapType Type,
		const FVertexVisitor& Visitor);

	Array GetBrushImages(const Variant& Brush, real_t Falloff);
	Dictionary ModifyHeights(const Dictionary& Params, const FHeightFunction& Target);
	Rect2 GetRegionBounds() const;
	Color GetTexturePreviewColor(int32_t AssetId) const;
	bool ExtractErosionGrid(const Dictionary& Params, FErosionJob& Job, String& OutError);
	Dictionary CommitErosionGrid(const Dictionary& Params, const FErosionJob& Job, const std::vector<float>& Flow);

protected:
	static void _bind_methods();

public:
	Terrain3DAgent();
	~Terrain3DAgent() override;

	void SetTerrain(Terrain3D* Terrain);
	Terrain3D* GetTerrain() const;
	Terrain3DEditor* GetEditor() const { return Editor; }
	Dictionary GetLastError() const { return LastError; }

	/**
	 * Runs one command ({op, ...params}), an Array of them, or a JSON string of either.
	 * @return The command's result Dictionary, or an Array of them. A failing command in a list
	 *         does not stop the rest; check each `ok`.
	 */
	Variant Execute(const Variant& Commands);
	Array ExecuteFile(const String& Path);
	static PackedStringArray GetCommandNames();

	// Brush strokes, replaying exactly what the editor plugin's tools do.
	Dictionary Brush(const Dictionary& Params);

	// Regions
	Dictionary AddRegions(const Dictionary& Params);
	Dictionary RemoveRegions(const Dictionary& Params);

	// Exact per-vertex area edits
	Dictionary SetHeightArea(const Dictionary& Params);
	Dictionary ApplyNoise(const Dictionary& Params);
	Dictionary ApplyHeightFunction(const Dictionary& Params);
	Dictionary PaintArea(const Dictionary& Params);
	Dictionary PaintTextureRules(const Dictionary& Params);
	Dictionary StampPath(const Dictionary& Params);
	Dictionary Erode(const Dictionary& Params);
	Dictionary StreamPower(const Dictionary& Params);

	// Instances
	Dictionary PlaceInstances(const Dictionary& Params);
	Dictionary ClearInstances(const Dictionary& Params);

	// Assets and settings
	Dictionary AddTexture(const Dictionary& Params);
	Dictionary PackTexture(const Dictionary& Params);
	Dictionary AddMesh(const Dictionary& Params);
	Dictionary SetProperties(const Dictionary& Params);

	// Inspection
	Dictionary GetSummary(const Dictionary& Params);
	Dictionary Sample(const Dictionary& Params);
	Dictionary GetHeightGrid(const Dictionary& Params);
	Dictionary SavePreview(const Dictionary& Params);

	// Import / export / history / persistence
	Dictionary ImportHeightmap(const Dictionary& Params);
	Dictionary ExportMap(const Dictionary& Params);
	Dictionary BakeMesh(const Dictionary& Params);
	Dictionary Undo(const Dictionary& Params);
	Dictionary Redo(const Dictionary& Params);
	Dictionary Save(const Dictionary& Params);

	/**
	 * Procedural brush mask, the same kind of Image the plugin loads from addons/terrain_3d/brushes.
	 * @param Shape "soft" (smoothstep edge), "round" (hard disc) or "square".
	 * @param Falloff Fraction of the radius over which "soft" fades out, 0-1.
	 */
	static Ref<Image> MakeBrushImage(const String& Shape, int32_t Size, real_t Falloff);
};

#endif // WITH_ABYSS
