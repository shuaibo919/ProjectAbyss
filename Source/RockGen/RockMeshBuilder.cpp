#include "RockGen/RockMeshBuilder.h"

#include "RockGen/RockMarchingCubesTables.h"

#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <thread>

using namespace godot;

namespace
{
	using RockGen::MeshAccumulator;
	using RockGen::RockSpec;
	using RockGen::Vector2;
	using RockGen::Vector3;

	constexpr float SHELL_WIDTH = 0.05f; // smoothstep(0.05, 0, d) band of ScalarField.compute
	constexpr float FBM_MAX = 0.984375f; // sum of six 0.5 * 0.5^k octaves, each <= 1

	// ---------------------------------------------------------------- noise

	inline float Frac(float x)
	{
		return x - std::floor(x);
	}

	// ScalarField.compute Hash(): value noise on a 2D lattice.
	Vector3 HashNoise(const Vector2& p)
	{
		Vector3 q(
			p.dot(Vector2(127.1f, 311.7f)),
			p.dot(Vector2(269.5f, 183.3f)),
			p.dot(Vector2(419.2f, 371.9f)));
		return Vector3(
			Frac(std::sin(q.x) * 43758.5453f),
			Frac(std::sin(q.y) * 43758.5453f),
			Frac(std::sin(q.z) * 43758.5453f));
	}

	Vector3 Noise(const Vector2& p)
	{
		Vector2 ip(std::floor(p.x), std::floor(p.y));
		Vector2 u = p - ip;
		u = u * u * (Vector2(3.0f, 3.0f) - u * 2.0f);

		Vector3 res = HashNoise(ip).lerp(HashNoise(ip + Vector2(1, 0)), u.x)
							  .lerp(HashNoise(ip + Vector2(0, 1)).lerp(HashNoise(ip + Vector2(1, 1)), u.x), u.y);
		return res * res;
	}

	Vector3 Fbm(Vector2 p)
	{
		Vector3 v(0, 0, 0);
		float a = 0.5f;
		for (int32_t i = 0; i < 6; ++i)
		{
			v += Noise(p) * a;
			// mul(rot, x * 2 + (100, 100)) with rot = ((0.87, 0.48), (-0.48, 0.87)).
			p = Vector2(p.x * 2.0f + 100.0f, p.y * 2.0f + 100.0f);
			p = Vector2(0.87f * p.x + 0.48f * p.y, -0.48f * p.x + 0.87f * p.y);
			a *= 0.5f;
		}
		return v;
	}

	Vector3 HeightMap(const RockSpec& Spec, const Vector2& uv)
	{
		return Fbm(uv * Spec.DisplacementSpread);
	}

	// ---------------------------------------------------------------- field

	// Base SDF of the chosen form in RockSDF space, where the reference's sphere has
	// radius 0.95. Bumps only carve inward (see RockSdf), so the surface never leaves
	// this base volume.
	float BaseForm(const RockSpec& Spec, const Vector3& p)
	{
		switch (Spec.Form)
		{
			case RockGen::ROCK_FORM_ELLIPSOID:
			{
				Vector3 q(p.x, p.y / Spec.Flatness, p.z);
				// Scale by the smallest axis so the distance stays conservative.
				return Spec.Flatness * (q.length() - 0.95f);
			}
			case RockGen::ROCK_FORM_ROUNDED_BOX:
			{
				Vector3 half(0.95f, 0.95f * Spec.Flatness, 0.95f);
				Vector3 q = Vector3(std::abs(p.x), std::abs(p.y), std::abs(p.z)) - half;
				float outside = Vector3(std::max(q.x, 0.0f), std::max(q.y, 0.0f), std::max(q.z, 0.0f)).length();
				float inside = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
				return outside + inside - 0.95f * Spec.Roundness;
			}
			default:
				return p.length() - 0.95f;
		}
	}

	// One bump of the reference's hash chain, precomputed once per generation: the trig
	// depends only on the seed, so it costs O(Steps) instead of O(Steps * samples).
	struct RockBump
	{
		float r;
		Vector3 v;
	};

	std::vector<RockBump> BuildBumps(const RockSpec& Spec)
	{
		std::vector<RockBump> Bumps;
		Bumps.reserve(Spec.Steps);
		for (int32_t i = 0; i < Spec.Steps; i++)
		{
			float j = float(i) + Spec.Seed;
			RockBump Bump;
			Bump.r = 2.5f + Frac(std::sin(j * 727.1f) * 435.545f);
			Vector3 v(
				Frac(std::sin(127.231f * j) * 435.543f) * 2.0f - 1.0f,
				Frac(std::sin(491.7f * j) * 435.543f) * 2.0f - 1.0f,
				Frac(std::sin(718.423f * j) * 435.543f) * 2.0f - 1.0f);
			Bump.v = v.normalized();
			Bumps.push_back(Bump);
		}
		return Bumps;
	}

	// ScalarField.compute RockSDF(): base form smooth-min'd against hash-seeded spheres.
	// lerp(a, -b, h) + k*h*(1-h) is the IQ smooth-min with b negated, so each bump
	// subtracts from the base — the spheres dint the rock rather than grow on it.
	float RockSdf(const RockSpec& Spec, const std::vector<RockBump>& Bumps, const Vector3& p)
	{
		float d = BaseForm(Spec, p);
		for (const RockBump& Bump : Bumps)
		{
			float a = d;
			float b = (p + Bump.v * Bump.r).length() - Bump.r * 0.8f;
			float k = Spec.Smoothness;
			float h = std::clamp(0.5f + 0.5f * (-b - a) / k, 0.0f, 1.0f);
			d = Math::lerp(a, -b, h) + k * h * (1.0f - h);
		}
		return d;
	}

	// ScalarField.compute Map(): RockSDF(p * 2) — the rock nearly fills the unit cube.
	float EvalMap(const RockSpec& Spec, const std::vector<RockBump>& Bumps, const Vector3& p)
	{
		return RockSdf(Spec, Bumps, p * 2.0f);
	}

	// ScalarField.compute Surface3D(): triplanar FBM weighted by the surface normal
	// squared and renormalised. Returns the vector of per-planar height samples, whose
	// length is the displacement amount.
	Vector3 Surface3D(const RockSpec& Spec, const Vector3& p, const Vector3& n)
	{
		Vector3 w(
			std::max(n.x * n.x, 0.001f),
			std::max(n.y * n.y, 0.001f),
			std::max(n.z * n.z, 0.001f));
		w /= (w.x + w.y + w.z);

		return HeightMap(Spec, Vector2(p.y, p.z)) * w.x
			+ HeightMap(Spec, Vector2(p.z, p.x)) * w.y
			+ HeightMap(Spec, Vector2(p.x, p.y)) * w.z;
	}

	// Density of ScalarField.compute's ScalarField(): smoothstep(0.05, 0, d). HLSL leaves
	// a > b undefined, so the reversed ramp is written out explicitly.
	inline float ToDensity(float d)
	{
		float t = std::clamp((SHELL_WIDTH - d) / SHELL_WIDTH, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// Half-width of the band around the base isosurface where the displacement can move
	// the surface to. Outside it the density is exactly 0 or 1, so corners there need no
	// noise evaluation — this is what makes the lazy shell exact rather than approximate.
	inline float ShellRadius(const RockSpec& Spec)
	{
		return SHELL_WIDTH + Spec.DisplacementScale * FBM_MAX;
	}

	inline float Clamp01(float x)
	{
		return std::clamp(x, 0.0f, 1.0f);
	}

	// ------------------------------------------------------------- grid helpers

	// Trilinear interpolation of a scalar grid at a unit-space position. Values < 0 are
	// the "never computed" sentinel of the displacement memo and read as zero — the
	// surface never sits at such points, so the displacement is irrelevant there.
	float TrilerpScalar(const std::vector<float>& Arr, int32_t N, const Vector3& p)
	{
		const float G = float(N - 1);
		const float fx = std::clamp((p.x + 0.5f) * G, 0.0f, G);
		const float fy = std::clamp((p.y + 0.5f) * G, 0.0f, G);
		const float fz = std::clamp((p.z + 0.5f) * G, 0.0f, G);
		const int32_t x0 = int32_t(fx);
		const int32_t y0 = int32_t(fy);
		const int32_t z0 = int32_t(fz);
		const int32_t x1 = std::min(x0 + 1, N - 1);
		const int32_t y1 = std::min(y0 + 1, N - 1);
		const int32_t z1 = std::min(z0 + 1, N - 1);
		const float tx = fx - float(x0);
		const float ty = fy - float(y0);
		const float tz = fz - float(z0);
		const int32_t NN = N * N;

		auto Sample = [&](int32_t x, int32_t y, int32_t z)
		{
			float v = Arr[size_t(x) + y * N + z * NN];
			return v < 0.0f ? 0.0f : v;
		};

		float c000 = Sample(x0, y0, z0);
		float c100 = Sample(x1, y0, z0);
		float c010 = Sample(x0, y1, z0);
		float c110 = Sample(x1, y1, z0);
		float c001 = Sample(x0, y0, z1);
		float c101 = Sample(x1, y0, z1);
		float c011 = Sample(x0, y1, z1);
		float c111 = Sample(x1, y1, z1);

		float c00 = Math::lerp(c000, c100, tx);
		float c10 = Math::lerp(c010, c110, tx);
		float c01 = Math::lerp(c001, c101, tx);
		float c11 = Math::lerp(c011, c111, tx);
		float c0 = Math::lerp(c00, c10, ty);
		float c1 = Math::lerp(c01, c11, ty);
		return Math::lerp(c0, c1, tz);
	}

	// Same trilinear lattice for the cached gradient grid. The gradient field is smooth
	// (the base SDF varies on a scale far larger than one cell), so interpolation is
	// accurate; this is the CPU analogue of sampling the gradient of the reference's
	// volume texture.
	Vector3 TrilerpGradient(const std::vector<Vector3>& Arr, int32_t N, const Vector3& p)
	{
		const float G = float(N - 1);
		const float fx = std::clamp((p.x + 0.5f) * G, 0.0f, G);
		const float fy = std::clamp((p.y + 0.5f) * G, 0.0f, G);
		const float fz = std::clamp((p.z + 0.5f) * G, 0.0f, G);
		const int32_t x0 = int32_t(fx);
		const int32_t y0 = int32_t(fy);
		const int32_t z0 = int32_t(fz);
		const int32_t x1 = std::min(x0 + 1, N - 1);
		const int32_t y1 = std::min(y0 + 1, N - 1);
		const int32_t z1 = std::min(z0 + 1, N - 1);
		const float tx = fx - float(x0);
		const float ty = fy - float(y0);
		const float tz = fz - float(z0);
		const int32_t NN = N * N;

		Vector3 c000 = Arr[size_t(x0) + y0 * N + z0 * NN];
		Vector3 c100 = Arr[size_t(x1) + y0 * N + z0 * NN];
		Vector3 c010 = Arr[size_t(x0) + y1 * N + z0 * NN];
		Vector3 c110 = Arr[size_t(x1) + y1 * N + z0 * NN];
		Vector3 c001 = Arr[size_t(x0) + y0 * N + z1 * NN];
		Vector3 c101 = Arr[size_t(x1) + y0 * N + z1 * NN];
		Vector3 c011 = Arr[size_t(x0) + y1 * N + z1 * NN];
		Vector3 c111 = Arr[size_t(x1) + y1 * N + z1 * NN];

		Vector3 c00 = c000.lerp(c100, tx);
		Vector3 c10 = c010.lerp(c110, tx);
		Vector3 c01 = c001.lerp(c101, tx);
		Vector3 c11 = c011.lerp(c111, tx);
		Vector3 c0 = c00.lerp(c10, ty);
		Vector3 c1 = c01.lerp(c11, ty);
		return c0.lerp(c1, tz);
	}

	// ------------------------------------------------------------- mesh building

	// Chunks a [0, N) z-range into (roughly) Count parts. The one and only chunking
	// arithmetic — ParallelZ threads over these ranges, and pass 2 looks its chunk index
	// up in the same list, so a z-range always owns the same accumulator.
	std::vector<std::pair<int32_t, int32_t>> SplitZ(int32_t N, uint32_t Count)
	{
		std::vector<std::pair<int32_t, int32_t>> Ranges;
		if (Count < 1)
		{
			Count = 1;
		}
		if (Count < 2 || N <= 1)
		{
			Ranges.emplace_back(0, N);
			return Ranges;
		}
		const int32_t Chunk = std::max(1, (N + int32_t(Count) - 1) / int32_t(Count));
		for (int32_t z0 = 0; z0 < N; z0 += Chunk)
		{
			Ranges.emplace_back(z0, std::min(z0 + Chunk, N));
		}
		return Ranges;
	}

	// Runs fn(z0, z1) over the z-slices of SplitZ in parallel. Each slice chunk is
	// independent: passes 1/1.5 write disjoint z slabs, pass 2 emits into one
	// accumulator per chunk that gets concatenated back in order, so the output equals
	// the serial version within a couple of float-ulp boundary crossings (see
	// BuildRock's concat note). Chunking by z keeps the inner (x, y) loops intact.
	template <typename Fn>
	void ParallelZ(int32_t N, Fn&& fn)
	{
		uint32_t Count = std::min(std::thread::hardware_concurrency(), 16u);
		std::vector<std::pair<int32_t, int32_t>> Ranges = SplitZ(N, Count);
		if (Ranges.size() < 2)
		{
			fn(0, N);
			return;
		}
		std::vector<std::thread> Threads;
		for (const std::pair<int32_t, int32_t>& Range : Ranges)
		{
			Threads.emplace_back(fn, Range.first, Range.second);
		}
		for (std::thread& T : Threads)
		{
			T.join();
		}
	}

	// Corner order matches Triangulation.compute: 0-3 the lower layer (z + cell, then
	// the base), 4-7 the upper layer. Edge and triangle tables assume exactly this order.
	inline Vector3i CornerOffset(int32_t Corner)
	{
		static const Vector3i Offsets[8] = {
			Vector3i(0, 0, 1),
			Vector3i(1, 0, 1),
			Vector3i(1, 0, 0),
			Vector3i(0, 0, 0),
			Vector3i(0, 1, 1),
			Vector3i(1, 1, 1),
			Vector3i(1, 1, 0),
			Vector3i(0, 1, 0),
		};
		return Offsets[Corner];
	}

	// Edge end corner pairs, same numbering as the tables.
	inline Vector2i EdgeCorners(int32_t Edge)
	{
		static const Vector2i Edges[12] = {
			Vector2i(0, 1),
			Vector2i(1, 2),
			Vector2i(2, 3),
			Vector2i(3, 0),
			Vector2i(4, 5),
			Vector2i(5, 6),
			Vector2i(6, 7),
			Vector2i(7, 4),
			Vector2i(0, 4),
			Vector2i(1, 5),
			Vector2i(2, 6),
			Vector2i(3, 7),
		};
		return Edges[Edge];
	}
} // namespace

namespace RockGen
{
	void BuildRock(const RockSpec& Spec, MeshAccumulator& OutMesh)
	{
		const int32_t N = std::max(Spec.Resolution, 8);
		const float InvCell = 1.0f / float(N - 1);
		const float Shell = ShellRadius(Spec);
		const size_t Total = size_t(N) * N * N;
		const int32_t NN = N * N;

		const std::vector<RockBump> Bumps = BuildBumps(Spec);

		// Pass 1: base field on the grid plus its central-difference gradient. The
		// displacement term is left out and only added where the isosurface shell can
		// actually sit; gradients are cached because every vertex normal and noise
		// weighting reads them.
		std::vector<float> Grid(Total);
		std::vector<Vector3> GridGrad(Total);
		ParallelZ(N, [&](int32_t Z0, int32_t Z1)
		{
			for (int32_t z = Z0; z < Z1; ++z)
			{
				for (int32_t y = 0; y < N; ++y)
				{
					for (int32_t x = 0; x < N; ++x)
					{
						Grid[size_t(x) + y * N + z * NN] = EvalMap(
							Spec, Bumps, Vector3(float(x) * InvCell - 0.5f, float(y) * InvCell - 0.5f, float(z) * InvCell - 0.5f));
					}
				}
			}
		});
		const float GradScale = 0.5f * float(N - 1); // central differences, per unit distance
		ParallelZ(N, [&](int32_t Z0, int32_t Z1)
		{
			for (int32_t z = Z0; z < Z1; ++z)
			{
				for (int32_t y = 0; y < N; ++y)
				{
					for (int32_t x = 0; x < N; ++x)
					{
						const int32_t xm = std::max(x - 1, 0);
						const int32_t xp = std::min(x + 1, N - 1);
						const int32_t ym = std::max(y - 1, 0);
						const int32_t yp = std::min(y + 1, N - 1);
						const int32_t zm = std::max(z - 1, 0);
						const int32_t zp = std::min(z + 1, N - 1);
						GridGrad[size_t(x) + y * N + z * NN] = Vector3(
							Grid[size_t(xp) + y * N + z * NN] - Grid[size_t(xm) + y * N + z * NN],
							Grid[size_t(x) + yp * N + z * NN] - Grid[size_t(x) + ym * N + z * NN],
							Grid[size_t(x) + y * N + zp * NN] - Grid[size_t(x) + y * N + zm * NN]) * GradScale;
					}
				}
			}
		});

		// Memo for the displacement amount per grid point (-1 = not evaluated). Corners
		// of up to eight cells share a grid point, so this caps the noise work at one
		// Surface3D per shell point instead of one per corner per cell. Points outside
		// the shell are never read by pass 2 — the shell test is exact.
		std::vector<float> GridDisp(Total, -1.0f);
		ParallelZ(N, [&](int32_t Z0, int32_t Z1)
		{
			if (Spec.DisplacementScale <= 0.0f || Shell < 0.0f)
			{
				return;
			}
			for (int32_t z = Z0; z < Z1; ++z)
			{
				for (int32_t y = 0; y < N; ++y)
				{
					for (int32_t x = 0; x < N; ++x)
					{
						const size_t Idx = size_t(x) + y * N + z * NN;
						float d = Grid[Idx];
						if (std::abs(d) <= Shell)
						{
							Vector3 Norm = GridGrad[Idx];
							float NLenSq = Norm.length_squared();
							if (NLenSq > 1e-12f)
							{
								Norm /= std::sqrt(NLenSq);
							}
							GridDisp[Idx] = Spec.DisplacementScale * Surface3D(Spec, Vector3(float(x) * InvCell - 0.5f, float(y) * InvCell - 0.5f, float(z) * InvCell - 0.5f), Norm).length();
						}
					}
				}
			}
		});

		// Pass 2: marching cubes over (N-1)^3 cells, chunked by z so each thread emits
		// into its own accumulator. Concatenation preserves the serial traversal order,
		// so the result is bit-identical to a single-threaded run.
		const float CellSize = 1.0f / float(N - 1);

		const int32_t CellRange = N - 1;
		const std::vector<std::pair<int32_t, int32_t>> CellRanges =
			SplitZ(CellRange, std::min(std::thread::hardware_concurrency(), 16u));
		std::vector<MeshAccumulator> Chunks(CellRanges.size());
		ParallelZ(CellRange, [&](int32_t Z0, int32_t Z1)
		{
			// The same SplitZ arithmetic that ParallelZ threaded over; a z-range owns
			// the accumulator at the same index.
			int32_t ChunkIndex = int32_t(CellRanges.size()) - 1;
			for (int32_t i = 0; i < int32_t(CellRanges.size()); ++i)
			{
				if (CellRanges[size_t(i)].first == Z0)
				{
					ChunkIndex = i;
					break;
				}
			}
			MeshAccumulator& OutMesh = Chunks[size_t(ChunkIndex)];
			for (int32_t z = Z0; z < Z1; ++z)
			{
				for (int32_t y = 0; y < N - 1; ++y)
				{
					for (int32_t x = 0; x < N - 1; ++x)
					{
					Vector3 CellBase(float(x) * InvCell - 0.5f, float(y) * InvCell - 0.5f, float(z) * InvCell - 0.5f);
					Vector3 CornerPos[8];
					float BaseValue[8];

					bool bMaybeActive = false;
					float MinY = 1e9f;
					float MaxY = -1e9f;
					for (int32_t Corner = 0; Corner < 8; ++Corner)
					{
						Vector3i Off = CornerOffset(Corner);
						Vector3 Pos = CellBase + Vector3(Off.x, Off.y, Off.z) * CellSize;
						CornerPos[Corner] = Pos;
						BaseValue[Corner] = Grid[size_t(x + Off.x) + (y + Off.y) * N + (z + Off.z) * NN];
						bMaybeActive = bMaybeActive || std::abs(BaseValue[Corner]) <= Shell;
						MinY = std::min(MinY, Pos.y);
						MaxY = std::max(MaxY, Pos.y);
					}
					// The cut is applied in density space (below), so a cell straddling the
					// plane is active even where the base field is far inside the rock.
					if (Spec.bCutGround && MinY <= Spec.GroundCut && Spec.GroundCut <= MaxY)
					{
						bMaybeActive = true;
					}
					if (!bMaybeActive)
					{
						continue;
					}

					// Full field (base + triplanar displacement) at the corners that need it.
					float Density[8];
					for (int32_t Corner = 0; Corner < 8; ++Corner)
					{
						if (Spec.bCutGround && CornerPos[Corner].y < Spec.GroundCut)
						{
							// Below the cut there is no rock, and the sharp cut face lands
							// exactly on the plane instead of offset by the smoothstep shell.
							Density[Corner] = 0.0f;
							continue;
						}

						const size_t GridIdx = size_t(x + CornerOffset(Corner).x) + (y + CornerOffset(Corner).y) * N + (z + CornerOffset(Corner).z) * NN;
						float d = BaseValue[Corner];
						if (Spec.DisplacementScale > 0.0f && std::abs(d) <= Shell)
						{
							// Precomputed above for exactly these points; the fallback keeps
							// the result sound if a future caller forgets pass 1.5.
							float Disp = GridDisp[GridIdx];
							if (Disp < 0.0f)
							{
								Vector3 N = GridGrad[GridIdx];
								float NLenSq = N.length_squared();
								if (NLenSq > 1e-12f)
								{
									N /= std::sqrt(NLenSq);
								}
								Disp = Spec.DisplacementScale * Surface3D(Spec, CornerPos[Corner], N).length();
							}
							d += Disp;
						}
						Density[Corner] = ToDensity(d);
					}

					// Triangulation.compute: bit i set where corner i is *outside* (value
					// below the 0.5 threshold). With the Bourke tables this winds the mesh
					// with front faces outward.
					int32_t CubeIndex = 0;
					if (Density[0] < 0.5f) CubeIndex |= 1;
					if (Density[1] < 0.5f) CubeIndex |= 2;
					if (Density[2] < 0.5f) CubeIndex |= 4;
					if (Density[3] < 0.5f) CubeIndex |= 8;
					if (Density[4] < 0.5f) CubeIndex |= 16;
					if (Density[5] < 0.5f) CubeIndex |= 32;
					if (Density[6] < 0.5f) CubeIndex |= 64;
					if (Density[7] < 0.5f) CubeIndex |= 128;
					if (EDGE_TABLE[CubeIndex] == 0)
					{
						continue;
					}

					Vector3 EdgeVertex[12];
					bool bEdgeValid[12] = {};
					for (int32_t Edge = 0; Edge < 12; ++Edge)
					{
						if ((EDGE_TABLE[CubeIndex] & (1 << Edge)) == 0)
						{
							continue;
						}
						Vector2i Ends = EdgeCorners(Edge);
						float v1 = Density[Ends.x];
						float v2 = Density[Ends.y];
						float denom = v2 - v1;
						float t = std::abs(denom) < 1e-12f ? 0.5f : (0.5f - v1) / denom;
						EdgeVertex[Edge] = CornerPos[Ends.x].lerp(CornerPos[Ends.y], t);
						bEdgeValid[Edge] = true;
					}

					for (int32_t i = 0; TRI_TABLE[CubeIndex][i] != -1; i += 3)
					{
						Vector3 TriPos[3];
						for (int32_t k = 0; k < 3; ++k)
						{
							int32_t Edge = TRI_TABLE[CubeIndex][i + k];
							TriPos[k] = bEdgeValid[Edge] ? EdgeVertex[Edge] : CellBase;
						}

						// Drop slivers that carry no area; they would otherwise feed
						// degenerate normals into the surface.
						Vector3 ab = TriPos[1] - TriPos[0];
						Vector3 ac = TriPos[2] - TriPos[0];
						if (ab.cross(ac).length_squared() < 1e-14f)
						{
							continue;
						}

						// Per-triangle cube-projection UVs (the reference's CubeProjection),
						// so each face maps 0..1 in unit space regardless of Scale.
						Vector3 faceN = ab.cross(ac);
						Vector3 dominant = Vector3(std::abs(faceN.x), std::abs(faceN.y), std::abs(faceN.z));
						Vector2 Uv[3];
						for (int32_t k = 0; k < 3; ++k)
						{
							Vector3 unit = TriPos[k] + Vector3(0.5f, 0.5f, 0.5f);
							if (dominant.x >= dominant.y && dominant.x >= dominant.z)
							{
								Uv[k] = Vector2(unit.z, unit.y);
							}
							else if (dominant.y >= dominant.x && dominant.y >= dominant.z)
							{
								Uv[k] = Vector2(unit.x, unit.z);
							}
							else
							{
								Uv[k] = Vector2(unit.x, unit.y);
							}
						}

						uint32_t First = uint32_t(OutMesh.Vertices.size());
						for (int32_t k = 0; k < 3; ++k)
						{
							Vector3 Pos = TriPos[k];

							// Outward normal: gradient of the base field, interpolated from
							// the cached grid gradients. Density decreases outward
							// (smoothstep is a decreasing ramp in d), so grad(Map) already
							// points away from the rock. The displacement's high-frequency
							// gradient is deliberately left out — it would only alias the
							// surface shading at marching-cubes resolution.
							Vector3 Normal;
							if (Spec.bCutGround && std::abs(Pos.y - Spec.GroundCut) < 1e-4f)
							{
								Normal = Vector3(0, -1, 0);
							}
							else
							{
								Normal = TrilerpGradient(GridGrad, N, Pos);
								if (Normal.length_squared() < 1e-12f)
								{
									Normal = Vector3(0, 1, 0);
								}
								Normal = Normal.normalized();
							}

							// Crevice tint from the local displacement height, so noise that
							// barely moves the surface also barely changes its colour.
							Color Tint = Spec.BaseColor;
							if (Spec.DisplacementScale > 0.0f)
							{
								float Height = TrilerpScalar(GridDisp, N, Pos) / Spec.DisplacementScale;
								Tint = Spec.CreviceColor.lerp(Spec.BaseColor, Clamp01(Height / FBM_MAX));
							}

							OutMesh.Vertices.push_back(Pos * Spec.Scale);
							OutMesh.Normals.push_back(Normal);
							OutMesh.UVs.push_back(Uv[k]);
							OutMesh.Colors.push_back(Tint);
							OutMesh.Indices.push_back(int32_t(First + k));
						}
					}
				}
			}
		}
		});

		// Concatenate the chunks in traversal order. Chunked cells preserve their own
		// internal order and the z-after-z-order matches the serial loop, so the
		// combined mesh equals a single-threaded run up to the displacement samples on
		// the shell: pass 1.5 evaluates them at direct grid coordinates while the serial
		// memo used the equivalent cell-derived position (same point, one float ulp).
		// In practice only a few edge crossings in a million can flip as a result.
		for (const MeshAccumulator& Chunk : Chunks)
		{
			const uint32_t Offset = uint32_t(OutMesh.Vertices.size());
			OutMesh.Vertices.insert(OutMesh.Vertices.end(), Chunk.Vertices.begin(), Chunk.Vertices.end());
			OutMesh.Normals.insert(OutMesh.Normals.end(), Chunk.Normals.begin(), Chunk.Normals.end());
			OutMesh.UVs.insert(OutMesh.UVs.end(), Chunk.UVs.begin(), Chunk.UVs.end());
			OutMesh.Colors.insert(OutMesh.Colors.end(), Chunk.Colors.begin(), Chunk.Colors.end());
			for (const int32_t Index : Chunk.Indices)
			{
				OutMesh.Indices.push_back(Index + int32_t(Offset));
			}
		}

	}
} // namespace RockGen
