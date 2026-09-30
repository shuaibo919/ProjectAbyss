// ProjectAbyss addition, not part of upstream Terrain3D. Compiled only WITH_ABYSS.

#ifdef WITH_ABYSS

#include "terrain_3d_erosion.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

namespace TerrainErosion
{
	namespace
	{
		constexpr float SQRT_2 = 1.41421356f;

		/** SplitMix64: tiny, fast, and fully determined by the seed on every platform. */
		struct FRandom
		{
			uint64_t State;

			explicit FRandom(const uint64_t Seed)
				: State(Seed)
			{
			}

			uint64_t Next()
			{
				uint64_t Value = (State += 0x9E3779B97F4A7C15ull);
				Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
				Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
				return Value ^ (Value >> 31);
			}

			/** Uniform in [0, 1). */
			float NextFloat()
			{
				return float(Next() >> 40) * (1.f / float(1ull << 24));
			}
		};

		struct FBrushTap
		{
			int32_t OffsetX;
			int32_t OffsetZ;
			float Weight;
		};

		/** Height and gradient at a fractional cell position; false if a corner is missing. */
		bool SampleHeight(const std::vector<float>& Heights, const int32_t Width, const float X, const float Z,
			float& OutHeight, float& OutGradientX, float& OutGradientZ)
		{
			const int32_t CellX = int32_t(X);
			const int32_t CellZ = int32_t(Z);
			const float U = X - float(CellX);
			const float V = Z - float(CellZ);
			const size_t Index = size_t(CellZ) * size_t(Width) + size_t(CellX);
			const float H00 = Heights[Index];
			const float H10 = Heights[Index + 1];
			const float H01 = Heights[Index + Width];
			const float H11 = Heights[Index + Width + 1];
			if (std::isnan(H00) || std::isnan(H10) || std::isnan(H01) || std::isnan(H11))
			{
				return false;
			}
			OutGradientX = (H10 - H00) * (1.f - V) + (H11 - H01) * V;
			OutGradientZ = (H01 - H00) * (1.f - U) + (H11 - H10) * U;
			OutHeight = H00 * (1.f - U) * (1.f - V) + H10 * U * (1.f - V) + H01 * (1.f - U) * V + H11 * U * V;
			return true;
		}
		/** Spreads Amount bilinearly over the four nodes around a fractional position. */
		void DepositAt(std::vector<float>& Heights, const int32_t Width, const float X, const float Z, const float Amount)
		{
			const int32_t CellX = int32_t(X);
			const int32_t CellZ = int32_t(Z);
			const float U = X - float(CellX);
			const float V = Z - float(CellZ);
			const size_t Corner = size_t(CellZ) * size_t(Width) + size_t(CellX);
			Heights[Corner] += Amount * (1.f - U) * (1.f - V);
			Heights[Corner + 1] += Amount * U * (1.f - V);
			Heights[Corner + Width] += Amount * (1.f - U) * V;
			Heights[Corner + Width + 1] += Amount * U * V;
		}

		/**
		 * Adds Amount (negative erodes) around a cell, weighted by the brush over the cells that
		 * exist, so the total is exact even at the grid edge or beside missing data.
		 * @return false if no brush cell had terrain.
		 */
		bool ApplyBrush(FGrid& Grid, const std::vector<FBrushTap>& Brush, const int32_t CellX, const int32_t CellZ, const float Amount)
		{
			float WeightSum = 0.f;
			for (const FBrushTap& Tap : Brush)
			{
				const int32_t TapX = CellX + Tap.OffsetX;
				const int32_t TapZ = CellZ + Tap.OffsetZ;
				if (Grid.IsInside(TapX, TapZ) && !std::isnan(Grid.Heights[Grid.Index(TapX, TapZ)]))
				{
					WeightSum += Tap.Weight;
				}
			}
			if (WeightSum <= 0.f)
			{
				return false;
			}
			for (const FBrushTap& Tap : Brush)
			{
				const int32_t TapX = CellX + Tap.OffsetX;
				const int32_t TapZ = CellZ + Tap.OffsetZ;
				if (Grid.IsInside(TapX, TapZ))
				{
					float& Target = Grid.Heights[Grid.Index(TapX, TapZ)];
					if (!std::isnan(Target))
					{
						Target += Amount * Tap.Weight / WeightSum;
					}
				}
			}
			return true;
		}
	} // namespace

	void RunHydraulic(FGrid& Grid, const FHydraulicSettings& Settings, std::vector<float>& OutFlow)
	{
		const int32_t Width = Grid.Width;
		const int32_t Depth = Grid.Depth;
		OutFlow.assign(Grid.Heights.size(), 0.f);
		if (Width < 3 || Depth < 3 || Settings.Droplets <= 0)
		{
			return;
		}

		// Work in cell units so slopes are tangents whatever the vertex spacing.
		const float ToCells = 1.f / Grid.CellSize;
		std::vector<float>& Heights = Grid.Heights;
		for (float& Height : Heights)
		{
			Height *= ToCells;
		}

		std::vector<FBrushTap> Brush;
		const int32_t Radius = std::max(1, Settings.Radius);
		for (int32_t OffsetZ = -Radius; OffsetZ <= Radius; ++OffsetZ)
		{
			for (int32_t OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
			{
				const float Distance = std::sqrt(float(OffsetX * OffsetX + OffsetZ * OffsetZ));
				if (Distance < float(Radius))
				{
					Brush.push_back({ OffsetX, OffsetZ, float(Radius) - Distance });
				}
			}
		}

		FRandom Random(Settings.Seed);
		const float Inertia = std::clamp(Settings.Inertia, 0.f, 1.f);
		const float Evaporation = std::clamp(Settings.Evaporation, 0.f, 1.f);
		const float BaseLevel = Settings.BaseLevel * ToCells;
		for (int64_t Droplet = 0; Droplet < Settings.Droplets; ++Droplet)
		{
			float PositionX = Random.NextFloat() * float(Width - 1);
			float PositionZ = Random.NextFloat() * float(Depth - 1);
			float DirectionX = 0.f;
			float DirectionZ = 0.f;
			float Speed = Settings.InitialSpeed;
			float Water = Settings.InitialWater;
			float Sediment = 0.f;
			bool bLost = false;

			for (int32_t Step = 0; Step < Settings.MaxLifetime; ++Step)
			{
				const int32_t CellX = int32_t(PositionX);
				const int32_t CellZ = int32_t(PositionZ);
				const float U = PositionX - float(CellX);
				const float V = PositionZ - float(CellZ);
				float Height = 0.f;
				float GradientX = 0.f;
				float GradientZ = 0.f;
				if (!SampleHeight(Heights, Width, PositionX, PositionZ, Height, GradientX, GradientZ))
				{
					bLost = true; // Only possible at the spawn point, which then has no terrain.
					break;
				}
				// In the sea it no longer erodes; it keeps sinking downhill and sheds its load as it
				// goes, which fans out into a delta. Dumping it at the waterline builds a berm instead.
				const bool bUnderwater = Height < BaseLevel;
				if (bUnderwater && Sediment <= 0.f)
				{
					break;
				}
				if (!bUnderwater)
				{
					OutFlow[Grid.Index(CellX, CellZ)] += Water;
				}

				DirectionX = DirectionX * Inertia - GradientX * (1.f - Inertia);
				DirectionZ = DirectionZ * Inertia - GradientZ * (1.f - Inertia);
				const float Length = std::sqrt(DirectionX * DirectionX + DirectionZ * DirectionZ);
				if (Length < 1e-6f)
				{
					break; // A perfect flat or pit; the droplet has nowhere to go.
				}
				DirectionX /= Length;
				DirectionZ /= Length;
				const float NextX = PositionX + DirectionX;
				const float NextZ = PositionZ + DirectionZ;
				if (NextX < 0.f || NextZ < 0.f || NextX >= float(Width - 1) || NextZ >= float(Depth - 1))
				{
					bLost = true; // Off the grid: its sediment leaves with it.
					break;
				}
				float NewHeight = 0.f;
				float Unused = 0.f;
				if (!SampleHeight(Heights, Width, NextX, NextZ, NewHeight, Unused, Unused))
				{
					break; // Runs into a hole in the data; settle where it is.
				}
				PositionX = NextX;
				PositionZ = NextZ;
				const float DeltaHeight = NewHeight - Height;
				const float CapacityHere = bUnderwater ? 0.f : std::max(-DeltaHeight, Settings.MinSlope) * Speed * Water * Settings.Capacity;

				if (Sediment > CapacityHere || DeltaHeight > 0.f)
				{
					// Uphill: fill the pit behind it, at most up to the new position's height.
					const float Deposit = DeltaHeight > 0.f ? std::min(DeltaHeight, Sediment) : (Sediment - CapacityHere) * Settings.DepositRate;
					Sediment -= Deposit;
					DepositAt(Heights, Width, float(CellX) + U, float(CellZ) + V, Deposit);
				}
				else
				{
					// Never take more than the drop, or the droplet digs a hole it then sits in.
					const float Erode = std::min((CapacityHere - Sediment) * Settings.ErodeRate, -DeltaHeight);
					if (ApplyBrush(Grid, Brush, CellX, CellZ, -Erode))
					{
						Sediment += Erode;
					}
				}

				Speed = std::sqrt(std::max(0.f, Speed * Speed - DeltaHeight * Settings.Gravity));
				Water *= 1.f - Evaporation;
			}
			// Whatever it still carries when it dies, stalls or reaches the sea settles in place,
			// so mass is conserved except for what runs off the grid.
			// Spread with the brush: dumped on four nodes it leaves a speckle of bumps.
			if (!bLost && Sediment > 0.f)
			{
				ApplyBrush(Grid, Brush, int32_t(PositionX), int32_t(PositionZ), Sediment);
			}
		}

		for (float& Height : Heights)
		{
			Height *= Grid.CellSize;
		}
	}

	FStreamPowerStats RunStreamPower(FGrid& Grid, const FStreamPowerSettings& Settings, std::vector<float>& OutArea)
	{
		// Lifts each flooded cell above the one that reached it, so every cell drains somewhere.
		constexpr float FILL_EPSILON = 1e-4f;
		// FTCS diffusion is stable for D dt / dx^2 <= 1/4; stay clear of the edge.
		constexpr float DIFFUSION_STABLE_ALPHA = 0.2f;
		constexpr int32_t MAX_DIFFUSION_SUBSTEPS = 256;
		constexpr int32_t NEWTON_ITERATIONS = 20;
		static const int32_t NEIGHBOUR_X[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
		static const int32_t NEIGHBOUR_Z[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
		constexpr int32_t NEIGHBOUR_COUNT = 8;

		FStreamPowerStats Stats;
		const int32_t Width = Grid.Width;
		const int32_t Depth = Grid.Depth;
		const size_t Count = Grid.Heights.size();
		OutArea.assign(Count, 0.f);
		if (Width < 3 || Depth < 3 || Settings.Iterations <= 0)
		{
			return Stats;
		}
		std::vector<float>& Heights = Grid.Heights;
		const float CellArea = Grid.CellSize * Grid.CellSize;
		float Distances[NEIGHBOUR_COUNT];
		int32_t Offsets[NEIGHBOUR_COUNT];
		for (int32_t Neighbour = 0; Neighbour < NEIGHBOUR_COUNT; ++Neighbour)
		{
			const bool bDiagonal = NEIGHBOUR_X[Neighbour] != 0 && NEIGHBOUR_Z[Neighbour] != 0;
			Distances[Neighbour] = Grid.CellSize * (bDiagonal ? SQRT_2 : 1.f);
			Offsets[Neighbour] = NEIGHBOUR_Z[Neighbour] * Width + NEIGHBOUR_X[Neighbour];
		}

		// Outlets are fixed for the whole run; heights only change inland.
		std::vector<uint8_t> Outlet(Count, 0);
		for (int32_t Z = 0; Z < Depth; ++Z)
		{
			for (int32_t X = 0; X < Width; ++X)
			{
				const size_t Cell = Grid.Index(X, Z);
				if (std::isnan(Heights[Cell]))
				{
					continue;
				}
				const bool bBorder = X == 0 || Z == 0 || X == Width - 1 || Z == Depth - 1;
				bool bOutlet = bBorder || Heights[Cell] < Settings.BaseLevel;
				for (int32_t Neighbour = 0; !bOutlet && Neighbour < NEIGHBOUR_COUNT; ++Neighbour)
				{
					bOutlet = std::isnan(Heights[Cell + Offsets[Neighbour]]);
				}
				Outlet[Cell] = bOutlet ? 1 : 0;
				Stats.Outlets += bOutlet ? 1 : 0;
			}
		}

		const float Uplift = Settings.Uplift * Settings.TimeStep;
		const float ErosionScale = Settings.Erodibility * Settings.TimeStep;
		const float AreaExponent = Settings.AreaExponent;
		const float SlopeExponent = Settings.SlopeExponent;
		const bool bLinear = std::abs(SlopeExponent - 1.f) < 1e-4f;
		const float DiffusionTotal = Settings.Diffusion * Settings.TimeStep / CellArea;
		const int32_t Substeps = DiffusionTotal <= 0.f
			? 0
			: std::min(MAX_DIFFUSION_SUBSTEPS, int32_t(std::ceil(DiffusionTotal / DIFFUSION_STABLE_ALPHA)));
		const float Alpha = Substeps > 0 ? std::min(DiffusionTotal / float(Substeps), DIFFUSION_STABLE_ALPHA) : 0.f;
		Stats.DiffusionSubsteps = Substeps;

		std::vector<float> Filled(Count);
		std::vector<uint8_t> Closed(Count);
		std::vector<int32_t> Receiver(Count);
		std::vector<float> ReceiverDistance(Count);
		std::vector<int32_t> DonorStart(Count + 1);
		std::vector<int32_t> Donors(Count);
		std::vector<int32_t> Cursor;
		std::vector<int32_t> Stack;
		std::vector<int32_t> Todo;
		std::vector<int32_t> Pit;
		std::vector<float> Scratch;
		Stack.reserve(Count);
		using FOpenCell = std::pair<float, int32_t>;
		std::priority_queue<FOpenCell, std::vector<FOpenCell>, std::greater<FOpenCell>> Open;

		for (int32_t Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
		{
			// 1. Priority-Flood+epsilon from the outlets: a surface with no depressions, used only to route.
			Filled = Heights;
			for (size_t Cell = 0; Cell < Count; ++Cell)
			{
				Closed[Cell] = (Outlet[Cell] || std::isnan(Heights[Cell])) ? 1 : 0;
				if (Outlet[Cell])
				{
					Open.push({ Heights[Cell], int32_t(Cell) });
				}
			}
			Pit.clear();
			size_t PitHead = 0;
			while (!Open.empty() || PitHead < Pit.size())
			{
				int32_t Cell = 0;
				if (PitHead < Pit.size())
				{
					Cell = Pit[PitHead++];
				}
				else
				{
					Pit.clear();
					PitHead = 0;
					Cell = Open.top().second;
					Open.pop();
				}
				const int32_t X = Cell % Width;
				const int32_t Z = Cell / Width;
				const float Raised = std::max(std::nextafter(Filled[Cell], INFINITY), Filled[Cell] + FILL_EPSILON);
				for (int32_t Neighbour = 0; Neighbour < NEIGHBOUR_COUNT; ++Neighbour)
				{
					if (!Grid.IsInside(X + NEIGHBOUR_X[Neighbour], Z + NEIGHBOUR_Z[Neighbour]))
					{
						continue;
					}
					const int32_t Next = Cell + Offsets[Neighbour];
					if (Closed[Next])
					{
						continue;
					}
					Closed[Next] = 1;
					if (Filled[Next] <= Raised)
					{
						Filled[Next] = Raised;
						Pit.push_back(Next);
					}
					else
					{
						Open.push({ Filled[Next], Next });
					}
				}
			}

			// 2. Receivers: steepest descent on the filled surface. The flood guarantees a lower neighbour.
			Stats.LakeCells = 0;
			for (int32_t Z = 0; Z < Depth; ++Z)
			{
				for (int32_t X = 0; X < Width; ++X)
				{
					const int32_t Cell = int32_t(Grid.Index(X, Z));
					Receiver[Cell] = Cell;
					ReceiverDistance[Cell] = 0.f;
					if (Outlet[Cell] || std::isnan(Heights[Cell]))
					{
						continue;
					}
					Stats.LakeCells += Filled[Cell] > Heights[Cell] ? 1 : 0;
					float BestSlope = 0.f;
					for (int32_t Neighbour = 0; Neighbour < NEIGHBOUR_COUNT; ++Neighbour)
					{
						const int32_t Next = Cell + Offsets[Neighbour];
						float Slope = (Filled[Cell] - Filled[Next]) / Distances[Neighbour];
						if (Settings.RoutingJitter > 0.f && Slope > 0.f)
						{
							FRandom Hash(Settings.Seed ^ (uint64_t(Cell) * NEIGHBOUR_COUNT + uint64_t(Neighbour)));
							Slope *= 1.f + Settings.RoutingJitter * Hash.NextFloat();
						}
						if (!std::isnan(Heights[Next]) && Slope > BestSlope)
						{
							BestSlope = Slope;
							Receiver[Cell] = Next;
							ReceiverDistance[Cell] = Distances[Neighbour];
						}
					}
				}
			}

			// 3. Stack order (Braun and Willett 2013): donors as CSR, then a pre-order walk from each
			// outlet, so every cell comes after its receiver.
			std::fill(DonorStart.begin(), DonorStart.end(), 0);
			for (size_t Cell = 0; Cell < Count; ++Cell)
			{
				if (Receiver[Cell] != int32_t(Cell))
				{
					++DonorStart[size_t(Receiver[Cell]) + 1];
				}
			}
			for (size_t Cell = 0; Cell < Count; ++Cell)
			{
				DonorStart[Cell + 1] += DonorStart[Cell];
			}
			Cursor.assign(DonorStart.begin(), DonorStart.end() - 1);
			for (size_t Cell = 0; Cell < Count; ++Cell)
			{
				if (Receiver[Cell] != int32_t(Cell))
				{
					Donors[Cursor[Receiver[Cell]]++] = int32_t(Cell);
				}
			}
			Stack.clear();
			for (size_t Root = 0; Root < Count; ++Root)
			{
				if (Receiver[Root] != int32_t(Root) || std::isnan(Heights[Root]))
				{
					continue;
				}
				Todo.push_back(int32_t(Root));
				while (!Todo.empty())
				{
					const int32_t Cell = Todo.back();
					Todo.pop_back();
					Stack.push_back(Cell);
					for (int32_t Donor = DonorStart[Cell]; Donor < DonorStart[size_t(Cell) + 1]; ++Donor)
					{
						Todo.push_back(Donors[Donor]);
					}
				}
			}

			// 4. Drainage area, accumulated downstream.
			for (const int32_t Cell : Stack)
			{
				OutArea[Cell] = CellArea;
			}
			for (auto Cell = Stack.rbegin(); Cell != Stack.rend(); ++Cell)
			{
				if (Receiver[*Cell] != *Cell)
				{
					OutArea[Receiver[*Cell]] += OutArea[*Cell];
				}
			}

			// 5. Implicit solve downstream-first, so each receiver already holds its new height.
			for (const int32_t Cell : Stack)
			{
				const int32_t Next = Receiver[Cell];
				if (Next == Cell)
				{
					continue; // Outlet
				}
				const float Target = Heights[Cell] + Uplift;
				const float Drop = Target - Heights[Next];
				if (Drop <= 0.f || OutArea[Cell] < Settings.MinArea)
				{
					Heights[Cell] = Target; // In a lake, or hillslope: no fluvial erosion
					continue;
				}
				const float Factor = ErosionScale * std::pow(OutArea[Cell], AreaExponent) / std::pow(ReceiverDistance[Cell], SlopeExponent);
				float Rise = Drop / (1.f + Factor);
				if (!bLinear)
				{
					// Solve Rise - Drop + Factor Rise^n = 0 by Newton, from the uneroded height.
					Rise = Drop;
					for (int32_t Step = 0; Step < NEWTON_ITERATIONS; ++Step)
					{
						const float Residual = Rise - Drop + Factor * std::pow(Rise, SlopeExponent);
						const float Derivative = 1.f + SlopeExponent * Factor * std::pow(Rise, SlopeExponent - 1.f);
						const float NextRise = std::clamp(Rise - Residual / Derivative, 0.f, Drop);
						const bool bConverged = std::abs(NextRise - Rise) <= 1e-6f * Drop;
						Rise = NextRise;
						if (bConverged)
						{
							break;
						}
					}
				}
				Heights[Cell] = Heights[Next] + Rise;
			}

			// 6. Hillslope diffusion; missing neighbours are no-flux, outlets and the border fixed.
			for (int32_t Substep = 0; Substep < Substeps; ++Substep)
			{
				Scratch = Heights;
				for (int32_t Z = 1; Z < Depth - 1; ++Z)
				{
					for (int32_t X = 1; X < Width - 1; ++X)
					{
						const size_t Cell = Grid.Index(X, Z);
						if (Outlet[Cell] || std::isnan(Scratch[Cell]))
						{
							continue;
						}
						const float Height = Scratch[Cell];
						float Laplacian = 0.f;
						for (const size_t Next : { Cell - 1, Cell + 1, Cell - size_t(Width), Cell + size_t(Width) })
						{
							Laplacian += std::isnan(Scratch[Next]) ? 0.f : Scratch[Next] - Height;
						}
						Heights[Cell] = Height + Alpha * Laplacian;
					}
				}
			}
		}
		for (const float Area : OutArea)
		{
			Stats.MaxArea = std::max(Stats.MaxArea, Area);
		}
		return Stats;
	}

	void Blur(std::vector<float>& Field, const int32_t Width, const int32_t Depth, const int32_t Passes)
	{
		std::vector<float> Scratch(Field.size());
		const auto Filter = [Width, Depth](const std::vector<float>& From, std::vector<float>& To, const int32_t StepX, const int32_t StepZ)
		{
			for (int32_t Z = 0; Z < Depth; ++Z)
			{
				for (int32_t X = 0; X < Width; ++X)
				{
					const size_t Cell = size_t(Z) * size_t(Width) + size_t(X);
					if (std::isnan(From[Cell]))
					{
						To[Cell] = From[Cell];
						continue;
					}
					float Sum = 2.f * From[Cell];
					float WeightSum = 2.f;
					for (const int32_t Side : { -1, 1 })
					{
						const int32_t SampleX = X + Side * StepX;
						const int32_t SampleZ = Z + Side * StepZ;
						if (SampleX < 0 || SampleZ < 0 || SampleX >= Width || SampleZ >= Depth)
						{
							continue;
						}
						const float Sample = From[size_t(SampleZ) * size_t(Width) + size_t(SampleX)];
						if (!std::isnan(Sample))
						{
							Sum += Sample;
							WeightSum += 1.f;
						}
					}
					To[Cell] = Sum / WeightSum;
				}
			}
		};
		for (int32_t Pass = 0; Pass < Passes; ++Pass)
		{
			Filter(Field, Scratch, 1, 0);
			Filter(Scratch, Field, 0, 1);
		}
	}

	FGrid Downsample(const FGrid& Grid, const int32_t Factor)
	{
		FGrid Coarse;
		Coarse.Width = (Grid.Width + Factor - 1) / Factor;
		Coarse.Depth = (Grid.Depth + Factor - 1) / Factor;
		Coarse.CellSize = Grid.CellSize * float(Factor);
		Coarse.Heights.assign(size_t(Coarse.Width) * size_t(Coarse.Depth), NAN);
		for (int32_t CoarseZ = 0; CoarseZ < Coarse.Depth; ++CoarseZ)
		{
			for (int32_t CoarseX = 0; CoarseX < Coarse.Width; ++CoarseX)
			{
				double Sum = 0.0;
				int32_t Samples = 0;
				for (int32_t Z = CoarseZ * Factor; Z < std::min(Grid.Depth, (CoarseZ + 1) * Factor); ++Z)
				{
					for (int32_t X = CoarseX * Factor; X < std::min(Grid.Width, (CoarseX + 1) * Factor); ++X)
					{
						const float Height = Grid.Heights[Grid.Index(X, Z)];
						if (!std::isnan(Height))
						{
							Sum += Height;
							++Samples;
						}
					}
				}
				if (Samples > 0)
				{
					Coarse.Heights[Coarse.Index(CoarseX, CoarseZ)] = float(Sum / double(Samples));
				}
			}
		}
		return Coarse;
	}

	std::vector<float> Upsample(const std::vector<float>& Coarse, const int32_t CoarseWidth, const int32_t CoarseDepth,
		const int32_t Factor, const int32_t FineWidth, const int32_t FineDepth)
	{
		std::vector<float> Fine(size_t(FineWidth) * size_t(FineDepth), NAN);
		// Coarse sample i sits at the centre of its block, fine coordinate i F + (F - 1) / 2.
		const float Centre = 0.5f * float(Factor - 1);
		for (int32_t Z = 0; Z < FineDepth; ++Z)
		{
			const float CoarseZ = std::clamp((float(Z) - Centre) / float(Factor), 0.f, float(CoarseDepth - 1));
			const int32_t Z0 = std::min(int32_t(CoarseZ), CoarseDepth - 1);
			const int32_t Z1 = std::min(Z0 + 1, CoarseDepth - 1);
			const float V = CoarseZ - float(Z0);
			for (int32_t X = 0; X < FineWidth; ++X)
			{
				const float CoarseX = std::clamp((float(X) - Centre) / float(Factor), 0.f, float(CoarseWidth - 1));
				const int32_t X0 = std::min(int32_t(CoarseX), CoarseWidth - 1);
				const int32_t X1 = std::min(X0 + 1, CoarseWidth - 1);
				const float U = CoarseX - float(X0);
				const float Values[] = {
					Coarse[size_t(Z0) * CoarseWidth + X0],
					Coarse[size_t(Z0) * CoarseWidth + X1],
					Coarse[size_t(Z1) * CoarseWidth + X0],
					Coarse[size_t(Z1) * CoarseWidth + X1],
				};
				const float Weights[] = { (1.f - U) * (1.f - V), U * (1.f - V), (1.f - U) * V, U * V };
				float Sum = 0.f;
				float WeightSum = 0.f;
				for (int32_t Corner = 0; Corner < 4; ++Corner)
				{
					if (!std::isnan(Values[Corner]))
					{
						Sum += Values[Corner] * Weights[Corner];
						WeightSum += Weights[Corner];
					}
				}
				if (WeightSum > 1e-6f)
				{
					Fine[size_t(Z) * FineWidth + X] = Sum / WeightSum;
				}
				else if (!std::isnan(Values[0]) || !std::isnan(Values[1]) || !std::isnan(Values[2]) || !std::isnan(Values[3]))
				{
					// Only a zero-weight corner has data (exactly on a coarse sample next to a gap).
					for (const float Value : Values)
					{
						if (!std::isnan(Value))
						{
							Fine[size_t(Z) * FineWidth + X] = Value;
							break;
						}
					}
				}
			}
		}
		return Fine;
	}

	void WidenValleys(FGrid& Grid, const std::vector<float>& Before, const float Slope, const float BaseLevel)
	{
		constexpr float MIN_CUT = 0.25f; // Metres; below this a cell is hillslope noise, not a channel
		const int32_t Width = Grid.Width;
		const int32_t Depth = Grid.Depth;
		const float Straight = Slope * Grid.CellSize;
		const float Diagonal = Straight * SQRT_2;
		const size_t Count = Grid.Heights.size();
		std::vector<float> Cut(Count, 0.f);
		std::vector<float> Floor(Count, INFINITY);
		for (size_t Cell = 0; Cell < Count; ++Cell)
		{
			const float Lowered = Before[Cell] - Grid.Heights[Cell];
			if (Lowered > MIN_CUT)
			{
				Cut[Cell] = Lowered;
				Floor[Cell] = Grid.Heights[Cell];
			}
		}
		// One pass per direction relaxes both fields: the cut spreads by losing, the floor by gaining.
		const auto Relax = [&Cut, &Floor, &Grid](const int32_t X, const int32_t Z, const int32_t FromX, const int32_t FromZ, const float Step)
		{
			if (Grid.IsInside(FromX, FromZ))
			{
				const size_t Target = Grid.Index(X, Z);
				const size_t From = Grid.Index(FromX, FromZ);
				Cut[Target] = std::max(Cut[Target], Cut[From] - Step);
				Floor[Target] = std::min(Floor[Target], Floor[From] + Step);
			}
		};
		for (int32_t Z = 0; Z < Depth; ++Z)
		{
			for (int32_t X = 0; X < Width; ++X)
			{
				Relax(X, Z, X - 1, Z, Straight);
				Relax(X, Z, X, Z - 1, Straight);
				Relax(X, Z, X - 1, Z - 1, Diagonal);
				Relax(X, Z, X + 1, Z - 1, Diagonal);
			}
		}
		for (int32_t Z = Depth - 1; Z >= 0; --Z)
		{
			for (int32_t X = Width - 1; X >= 0; --X)
			{
				Relax(X, Z, X + 1, Z, Straight);
				Relax(X, Z, X, Z + 1, Straight);
				Relax(X, Z, X + 1, Z + 1, Diagonal);
				Relax(X, Z, X - 1, Z + 1, Diagonal);
			}
		}
		for (size_t Cell = 0; Cell < Count; ++Cell)
		{
			// The sea floor is the base level, not a valley side.
		if (!std::isnan(Grid.Heights[Cell]) && Cut[Cell] > 0.f && Before[Cell] >= BaseLevel)
			{
				const float Widened = std::max(Before[Cell] - Cut[Cell], Floor[Cell]);
				Grid.Heights[Cell] = std::min(Grid.Heights[Cell], Widened);
			}
		}
	}

	void RunThermal(FGrid& Grid, const FThermalSettings& Settings)
	{
		const int32_t Width = Grid.Width;
		const int32_t Depth = Grid.Depth;
		if (Width < 3 || Depth < 3 || Settings.Iterations <= 0)
		{
			return;
		}
		static const int32_t NEIGHBOUR_X[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
		static const int32_t NEIGHBOUR_Z[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
		constexpr int32_t NEIGHBOUR_COUNT = 8;
		float Limits[NEIGHBOUR_COUNT];
		for (int32_t Neighbour = 0; Neighbour < NEIGHBOUR_COUNT; ++Neighbour)
		{
			const bool bDiagonal = NEIGHBOUR_X[Neighbour] != 0 && NEIGHBOUR_Z[Neighbour] != 0;
			Limits[Neighbour] = Settings.TalusSlope * Grid.CellSize * (bDiagonal ? SQRT_2 : 1.f);
		}
		const float Rate = std::clamp(Settings.Rate, 0.f, 1.f);

		std::vector<float>& Heights = Grid.Heights;
		std::vector<float> Delta(Heights.size(), 0.f);
		for (int32_t Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
		{
			std::fill(Delta.begin(), Delta.end(), 0.f);
			for (int32_t Z = 0; Z < Depth; ++Z)
			{
				for (int32_t X = 0; X < Width; ++X)
				{
					const float Height = Heights[Grid.Index(X, Z)];
					if (std::isnan(Height))
					{
						continue;
					}
					float Excess[NEIGHBOUR_COUNT] = {};
					float TotalExcess = 0.f;
					float MaxExcess = 0.f;
					for (int32_t Neighbour = 0; Neighbour < NEIGHBOUR_COUNT; ++Neighbour)
					{
						const int32_t NeighbourX = X + NEIGHBOUR_X[Neighbour];
						const int32_t NeighbourZ = Z + NEIGHBOUR_Z[Neighbour];
						if (!Grid.IsInside(NeighbourX, NeighbourZ))
						{
							continue;
						}
						const float Other = Heights[Grid.Index(NeighbourX, NeighbourZ)];
						const float Over = Height - Other - Limits[Neighbour];
						if (!std::isnan(Other) && Over > 0.f)
						{
							Excess[Neighbour] = Over;
							TotalExcess += Over;
							MaxExcess = std::max(MaxExcess, Over);
						}
					}
					if (TotalExcess <= 0.f)
					{
						continue;
					}
					// Half the largest excess levels the steepest pair; share it by excess.
					const float Moved = Rate * MaxExcess * 0.5f;
					Delta[Grid.Index(X, Z)] -= Moved;
					for (int32_t Neighbour = 0; Neighbour < NEIGHBOUR_COUNT; ++Neighbour)
					{
						if (Excess[Neighbour] > 0.f)
						{
							Delta[Grid.Index(X + NEIGHBOUR_X[Neighbour], Z + NEIGHBOUR_Z[Neighbour])] += Moved * Excess[Neighbour] / TotalExcess;
						}
					}
				}
			}
			for (size_t Index = 0; Index < Heights.size(); ++Index)
			{
				Heights[Index] += Delta[Index];
			}
		}
	}
} // namespace TerrainErosion

#endif // WITH_ABYSS
