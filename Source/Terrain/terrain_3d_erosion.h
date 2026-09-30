// ProjectAbyss addition, not part of upstream Terrain3D. Compiled only WITH_ABYSS.

#pragma once

#ifdef WITH_ABYSS

#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Heightfield erosion on a plain float grid, independent of Terrain3D so it can be tested and
 * reused. Terrain3DAgent::Erode copies a terrain area in, runs this, and writes the result back
 * through Terrain3DEditor so it is undoable.
 *
 * Hydraulic erosion is the droplet model of Hans Theobald Beyer, "Implementation of a method for
 * hydraulic erosion" (2015), as popularised by Sebastian Lague, with his speed-update sign fixed
 * (a droplet accelerates going downhill). Heights are handled in cell units internally, so
 * slopes are true tangents and the defaults do not depend on vertex spacing.
 */
namespace TerrainErosion
{
	/** Row-major heights in metres, Width columns (x) by Depth rows (z). NaN = no terrain. */
	struct FGrid
	{
		int32_t Width = 0;
		int32_t Depth = 0;
		float CellSize = 1.f; // Metres between samples
		std::vector<float> Heights;

		bool IsInside(const int32_t X, const int32_t Z) const
		{
			return X >= 0 && Z >= 0 && X < Width && Z < Depth;
		}
		size_t Index(const int32_t X, const int32_t Z) const
		{
			return size_t(Z) * size_t(Width) + size_t(X);
		}
	};

	struct FHydraulicSettings
	{
		int64_t Droplets = 0;
		uint64_t Seed = 0;
		// Gentler than Lague's (capacity 4, erode 0.3, deposit 0.3, lifetime 30). Those suit a 0-1
		// heightmap; real metre-scale mountains are one to two orders of magnitude steeper per cell,
		// so his rates move enough sediment to bury the relief.
		int32_t MaxLifetime = 80; // Steps, one cell each
		int32_t Radius = 3; // Erosion brush radius, cells
		float Inertia = 0.05f; // 0 = follow the gradient exactly, 1 = never turn
		float Capacity = 2.f; // Sediment a droplet can carry per unit slope * speed * water
		float MinSlope = 0.01f; // Keeps some capacity on flats so droplets don't dump everything
		float ErodeRate = 0.05f;
		float DepositRate = 0.2f;
		float Evaporation = 0.02f; // Water lost per step
		float Gravity = 4.f;
		float InitialWater = 1.f;
		float InitialSpeed = 1.f;
		float BaseLevel = -INFINITY; // Metres; a droplet reaching it drops its load and stops (sea level)
	};

	struct FThermalSettings
	{
		int32_t Iterations = 0;
		float TalusSlope = 0.7f; // tan of the angle of repose; steeper than this slides
		float Rate = 0.5f; // Fraction of the excess moved per iteration
	};

	/**
	 * Detachment-limited stream power: dh/dt = U - K A^m S^n + D laplacian(h).
	 * A is the upstream drainage area (m^2), S the slope to the steepest-descent receiver.
	 */
	struct FStreamPowerSettings
	{
		int32_t Iterations = 40;
		float TimeStep = 1000.f; // Years per iteration; the solve is implicit, so any step is stable
		float Erodibility = 1e-5f; // K, in m^(1-2m) / yr
		float AreaExponent = 0.5f; // m; m/n ~ 0.45 is the concavity of real rivers
		float SlopeExponent = 1.f; // n
		float MinArea = 0.f; // m^2; below this a cell is hillslope, not channel, and only diffuses
		float Uplift = 0.f; // m / yr
		float Diffusion = 0.f; // Hillslope diffusivity, m^2 / yr
		float BaseLevel = -INFINITY; // Metres; cells below it are outlets (the sea)
		// Scatters receiver choice by up to this fraction of the slope, with a fixed per-cell hash so
		// routing is stable across iterations. Breaks the straight D8 lines drawn across flats.
		float RoutingJitter = 0.f;
		uint64_t Seed = 0;
	};

	struct FStreamPowerStats
	{
		float MaxArea = 0.f; // m^2
		int64_t Outlets = 0;
		int64_t LakeCells = 0; // In a depression on the last iteration: water routed through, not eroded
		int32_t DiffusionSubsteps = 0; // Per iteration
	};

	/**
	 * Runs the O(n) implicit solver of Braun and Willett (2013), rerouting flow every iteration.
	 * Depressions are routed through with Priority-Flood+epsilon (Barnes et al. 2014), without
	 * filling them: a lake cell is not eroded, but its spill point is, so lakes drain over time.
	 * Outlets are the grid border, cells next to missing data, and cells below the base level;
	 * they never change.
	 * @param OutArea Drainage area of every cell after the last iteration, m^2.
	 */
	FStreamPowerStats RunStreamPower(FGrid& Grid, const FStreamPowerSettings& Settings, std::vector<float>& OutArea);

	/**
	 * Carves valley walls around incised channels. Stream power cuts one-cell slots; this widens
	 * them into V-shaped valleys with two chamfer transforms (octagonal cones):
	 * - the cut, Before - Grid, spreads sideways losing Slope per metre, so a valley is about
	 *   2 Cut / Slope wide and a peak further than that from any channel is untouched;
	 * - the floor cone, min over channels y of Grid(y) + Slope |x - y|, bounds that from below,
	 *   so a cut never spreads down onto ground lower than the channel it came from.
	 * Only ever lowers, and never below BaseLevel ground. Run it at full resolution: the cones
	 * staircase at the cell size.
	 * @param Slope tan of the valley wall angle.
	 */
	void WidenValleys(FGrid& Grid, const std::vector<float>& Before, float Slope, float BaseLevel);

	/**
	 * Separable [1 2 1] / 4 blur, Passes times; NaN samples are skipped and stay NaN. Rounds the
	 * one-cell staircase D8 draws along diagonal channels before it is upsampled and widened,
	 * where it would otherwise show as stripes on the valley walls.
	 */
	void Blur(std::vector<float>& Field, int32_t Width, int32_t Depth, int32_t Passes);

	/**
	 * Averages Factor x Factor blocks of samples; a block with no data is NaN. Block (i, j) covers
	 * samples [i F, (i + 1) F), so a trailing partial block averages what it has.
	 */
	FGrid Downsample(const FGrid& Grid, int32_t Factor);

	/**
	 * Bilinearly resamples a field on a grid made by Downsample(Factor) back to the fine grid's
	 * samples. NaN coarse samples are skipped, and are only NaN in the result if all four are.
	 */
	std::vector<float> Upsample(const std::vector<float>& Coarse, int32_t CoarseWidth, int32_t CoarseDepth,
		int32_t Factor, int32_t FineWidth, int32_t FineDepth);

	/**
	 * Runs droplets over the grid in place.
	 * @param OutFlow Per-cell water that passed through, summed over all droplets; resized to the grid.
	 */
	void RunHydraulic(FGrid& Grid, const FHydraulicSettings& Settings, std::vector<float>& OutFlow);

	/** Moves material downhill wherever the slope to a neighbour exceeds the talus slope. */
	void RunThermal(FGrid& Grid, const FThermalSettings& Settings);
} // namespace TerrainErosion

#endif // WITH_ABYSS
