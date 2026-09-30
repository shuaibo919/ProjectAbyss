#pragma once

#include <array>
#include <cstdint>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>

class NodeGraph;
struct TreeMeshData;

namespace godot
{
struct TreeFoliageOptions;

/** Immutable curve samples; no Resource is read by the generation worker. */
struct TreeGrowthCurve
{
	bool bOverride = false;
	std::array<float, 33> Samples{};
	float Evaluate(float Position, float Fallback) const;
};

struct TreeGrowthSettings
{
	bool bEnabled = true;
	bool bDebug = false;
	float TrunkBend = 1.0f;
	float BranchBend = 1.0f;
	float Forking = 1.0f;
	float RadiusPower = 2.35f;
	float JunctionShape = 1.0f;
	float BambooInternodeLength = 0.28f;
	float BambooNodeDefinition = 1.0f;
	float BambooLeafScale = 1.0f;
	float PeachTwigDensity = 1.0f;
	float PeachBlossomDensity = 1.0f;
	float PeachBlossomScale = 1.0f;
	TreeGrowthCurve LengthByHeight;
	TreeGrowthCurve DensityByHeight;
	TreeGrowthCurve BendAlongBranch;
	TreeGrowthCurve RadiusAlongBranch;
};

struct TreeGrowthDiagnostics
{
	uint32_t Trunks = 0;
	uint32_t Stems = 0;
	uint32_t Forks = 0;
	uint32_t Segments = 0;
	uint32_t Junctions = 0;
	uint32_t OmittedJunctions = 0;
	uint32_t WoodVertices = 0;
	std::vector<Vector3> Points;
	std::vector<float> Radii;
	std::vector<int32_t> Offsets;
	std::vector<int32_t> Parents;
	std::vector<int32_t> Attachments;
	std::vector<int32_t> Roles;
};

/** Builds connected branch topology before emitting wood and the existing crossed foliage. */
class SlowTreeGrowth
{
public:
	static bool IsEnabled(const TreeFoliageOptions& Options);
	static void Generate(const NodeGraph& Graph, const TreeFoliageOptions& Options, TreeMeshData& Output);
};
} // namespace godot
