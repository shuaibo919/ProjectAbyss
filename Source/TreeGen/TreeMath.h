#pragma once

// Math helpers ported from the HLSL sample accompanying
// "Real-Time GPU Tree Generation" (Kuth et al., High Performance Graphics 2025),
// plus the random/noise utilities from AMD's Work Graph Playground `Common.h`.
// Both are MIT licensed; see Licenses/RealTimeGPUTreeGeneration.License.md.
//
// The HLSL uses a right-handed, Y-up world with stems growing along their local
// +Z axis. Godot shares that handedness and up-axis, so positions and
// quaternions transfer without a change of basis.

#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cmath>
#include <cstdint>

namespace TreeGen
{
	using godot::Color;
	using godot::Quaternion;
	using godot::Vector2;
	using godot::Vector3;

	static const float TREE_PI = 3.14159265358979323846f;
	static const float TREE_TAU = 2.0f * TREE_PI;

	// ==================== HLSL intrinsic equivalents ====================

	inline float Saturate(float X)
	{
		return X < 0.0f ? 0.0f : (X > 1.0f ? 1.0f : X);
	}

	inline float Clamp(float X, float Min, float Max)
	{
		return X < Min ? Min : (X > Max ? Max : X);
	}

	inline int32_t ClampInt(int32_t X, int32_t Min, int32_t Max)
	{
		return X < Min ? Min : (X > Max ? Max : X);
	}

	inline float Lerp(float A, float B, float T)
	{
		return A + (B - A) * T;
	}

	/** HLSL frac(): always returns a value in [0, 1), even for negative input. */
	inline float Frac(float X)
	{
		return X - std::floor(X);
	}

	/** HLSL round(): round-to-nearest-even, matching DXIL's Round_ne. */
	inline float RoundNE(float X)
	{
		return std::nearbyint(X);
	}

	/** HLSL % on floats is truncated (sign follows the dividend), i.e. fmod. */
	inline float FMod(float X, float Y)
	{
		return std::fmod(X, Y);
	}

	inline float ToRadians(float AngleDegrees)
	{
		return AngleDegrees * (TREE_PI / 180.0f);
	}

	inline float ToDegrees(float AngleRadians)
	{
		return AngleRadians * (180.0f / TREE_PI);
	}

	/** Matches the sample's Sign(): never returns zero, so +0 maps to +1 and -0 to -1. */
	inline float SignNonZero(float F)
	{
		return std::signbit(F) ? -1.0f : 1.0f;
	}

	inline float MapRange(float Value, float SourceMin, float SourceMax, float DestMin, float DestMax)
	{
		const float Value01 = Saturate((Value - SourceMin) / (SourceMax - SourceMin));

		return DestMin + (Value01 * (DestMax - DestMin));
	}

	inline bool IsBitSet(uint32_t Data, int32_t BitIndex)
	{
		return (Data & (1u << BitIndex)) != 0u;
	}

	inline int32_t BitSign(uint32_t Data, int32_t BitIndex)
	{
		return IsBitSet(Data, BitIndex) ? 1 : -1;
	}

	/** True when a float's sign bit is set, mirroring the sample's `asuint(x) & 0x80000000` tests. */
	inline bool HasSignBit(float F)
	{
		return std::signbit(F);
	}

	inline int32_t DivideAndRoundUp(int32_t Dividend, int32_t Divisor)
	{
		return (Dividend + Divisor - 1) / Divisor;
	}

	inline float RoundUpMultiple2(float X)
	{
		return std::ceil(X * 0.5f) * 2.0f;
	}

	// ==================== Random & noise ====================

	// Verbatim ports of the Work Graph Playground `random` namespace. Keeping the
	// exact bit arithmetic means a given seed produces the same tree as the paper's
	// sample, which is the only reason to hand-roll a PRNG here.
	namespace Random
	{
		inline uint32_t Hash(uint32_t Seed)
		{
			Seed = (Seed ^ 61u) ^ (Seed >> 16u);
			Seed *= 9u;
			Seed = Seed ^ (Seed >> 4u);
			Seed *= 0x27d4eb2du;
			Seed = Seed ^ (Seed >> 15u);
			return Seed;
		}

		/** Note the HLSL operator precedence: `+` binds tighter than `^`, and C++ agrees. */
		inline uint32_t CombineSeed(uint32_t A, uint32_t B)
		{
			return A ^ (Hash(B) + 0x9e3779b9u + (A << 6) + (A >> 2));
		}

		inline uint32_t CombineSeed(uint32_t A, uint32_t B, uint32_t C)
		{
			return CombineSeed(CombineSeed(A, B), C);
		}

		inline uint32_t CombineSeed(uint32_t A, uint32_t B, uint32_t C, uint32_t D)
		{
			return CombineSeed(CombineSeed(A, B), C, D);
		}

		/** Random value in [0, 1]. */
		inline float Value(uint32_t Seed)
		{
			return float(Hash(Seed)) / 4294967295.0f;
		}

		inline float Value(uint32_t A, uint32_t B)
		{
			return Value(CombineSeed(A, B));
		}

		inline float Value(uint32_t A, uint32_t B, uint32_t C)
		{
			return Value(CombineSeed(A, B), C);
		}

		inline float Value(uint32_t A, uint32_t B, uint32_t C, uint32_t D)
		{
			return Value(CombineSeed(A, B), C, D);
		}

		/** Random value in [-1, 1]. */
		inline float SignedValue(uint32_t Seed)
		{
			return Value(Seed) * 2.0f - 1.0f;
		}

		inline float SignedValue(uint32_t A, uint32_t B)
		{
			return SignedValue(CombineSeed(A, B));
		}

		inline float SignedValue(uint32_t A, uint32_t B, uint32_t C)
		{
			return SignedValue(CombineSeed(A, B), C);
		}

		inline Vector2 PerlinNoiseDir2D(int32_t X, int32_t Y)
		{
			const int32_t PosX = X % 289;
			const int32_t PosY = Y % 289;

			float F = float(34 * PosX + 1);
			F = FMod(F * float(PosX), 289.0f) + float(PosY);
			F = FMod((34.0f * F + 1.0f) * F, 289.0f);
			F = Frac(F / 43.0f) * 2.0f - 1.0f;

			const Vector2 Dir(F - RoundNE(F), std::abs(F) - 0.5f);

			return Dir.normalized();
		}

		inline float PerlinNoise2D(const Vector2& Position)
		{
			const int32_t GridX = int32_t(std::floor(Position.x));
			const int32_t GridY = int32_t(std::floor(Position.y));
			const Vector2 Offset(Frac(Position.x), Frac(Position.y));

			const float D00 = PerlinNoiseDir2D(GridX + 0, GridY + 0).dot(Offset - Vector2(0, 0));
			const float D01 = PerlinNoiseDir2D(GridX + 0, GridY + 1).dot(Offset - Vector2(0, 1));
			const float D10 = PerlinNoiseDir2D(GridX + 1, GridY + 0).dot(Offset - Vector2(1, 0));
			const float D11 = PerlinNoiseDir2D(GridX + 1, GridY + 1).dot(Offset - Vector2(1, 1));

			const Vector2 W(
				Offset.x * Offset.x * Offset.x * (Offset.x * (Offset.x * 6.0f - 15.0f) + 10.0f),
				Offset.y * Offset.y * Offset.y * (Offset.y * (Offset.y * 6.0f - 15.0f) + 10.0f));

			const float D0 = Lerp(D00, D01, W.y);
			const float D1 = Lerp(D10, D11, W.y);

			return Lerp(D0, D1, W.x);
		}
	} // namespace Random

	// ==================== Bark noise (SplineSegment.h) ====================

	namespace Bark
	{
		inline int32_t WrapIndex(int32_t V, int32_t XWrap)
		{
			return (V + 256 * XWrap) % XWrap;
		}

		/** Perlin noise that tiles seamlessly every `XWrap` cells along x, so tubes close. */
		inline float WrappingXPerlinNoise(const Vector2& Position, int32_t XWrap)
		{
			const int32_t GridX = int32_t(std::floor(Position.x));
			const int32_t GridY = int32_t(std::floor(Position.y));
			const Vector2 Offset(Frac(Position.x), Frac(Position.y));

			const int32_t A = (GridX + 0) % XWrap;
			const int32_t B = (GridX + 1) % XWrap;

			const float D00 = Random::PerlinNoiseDir2D(A, GridY + 0).dot(Offset - Vector2(0, 0));
			const float D01 = Random::PerlinNoiseDir2D(A, GridY + 1).dot(Offset - Vector2(0, 1));
			const float D10 = Random::PerlinNoiseDir2D(B, GridY + 0).dot(Offset - Vector2(1, 0));
			const float D11 = Random::PerlinNoiseDir2D(B, GridY + 1).dot(Offset - Vector2(1, 1));

			const Vector2 W(
				Offset.x * Offset.x * Offset.x * (Offset.x * (Offset.x * 6.0f - 15.0f) + 10.0f),
				Offset.y * Offset.y * Offset.y * (Offset.y * (Offset.y * 6.0f - 15.0f) + 10.0f));

			const float D0 = Lerp(D00, D01, W.y);
			const float D1 = Lerp(D10, D11, W.y);

			return Lerp(D0, D1, W.x);
		}

		inline float CloudNoiseWrapX(Vector2 Position, int32_t XWrap, int32_t Levels)
		{
			const int32_t N = ClampInt(Levels, 1, 8);

			float Noise = 0.0f;
			for (int32_t I = 0; I < N; ++I)
			{
				const float S = float(1 << I);
				Noise += WrappingXPerlinNoise(Position * S, XWrap) / S;
				Position += Vector2(20, 20);
				XWrap *= 2;
			}

			return Noise;
		}

		inline Vector2 Hash2(const Vector2& P)
		{
			return Vector2(
				Frac(std::sin(P.dot(Vector2(127.1f, 311.7f))) * 43758.5453f),
				Frac(std::sin(P.dot(Vector2(269.5f, 183.3f))) * 43758.5453f));
		}

		/** Distance to the nearest Voronoi cell centre. Used for lichen speckle. */
		inline float Voronoi(const Vector2& X)
		{
			const Vector2 P(std::floor(X.x), std::floor(X.y));
			const Vector2 F(Frac(X.x), Frac(X.y));

			float Result = 8.0f;
			for (int32_t J = -1; J <= 1; ++J)
			{
				for (int32_t I = -1; I <= 1; ++I)
				{
					const Vector2 B(static_cast<float>(I), static_cast<float>(J));
					const Vector2 R = B - F + Hash2(P + B);
					Result = std::fmin(Result, R.dot(R));
				}
			}

			return std::sqrt(Result);
		}

		/** Distance to the nearest Voronoi cell *border*, tiling along x. Drives bark cracks. */
		inline float VoronoiDistanceWrapX(const Vector2& X, int32_t XWrap)
		{
			const int32_t PX = WrapIndex(int32_t(std::floor(X.x)), XWrap);
			const int32_t PY = int32_t(std::floor(X.y));
			const Vector2 F(Frac(X.x), Frac(X.y));

			Vector2 ClosestOffset;
			Vector2 ClosestCell;

			float Result = 8.0f;
			for (int32_t J = -1; J <= 1; ++J)
			{
				for (int32_t I = -1; I <= 1; ++I)
				{
					const Vector2 B(static_cast<float>(I), static_cast<float>(J));
					const Vector2 H(float(WrapIndex(PX + I, XWrap)), float(PY + J));
					const Vector2 R = B + Hash2(H) - F;
					const float D = R.dot(R);

					if (D < Result)
					{
						Result = D;
						ClosestOffset = R;
						ClosestCell = B;
					}
				}
			}

			Result = 8.0f;
			for (int32_t J = -2; J <= 2; ++J)
			{
				for (int32_t I = -2; I <= 2; ++I)
				{
					const Vector2 B = ClosestCell + Vector2(float(I), float(J));
					const Vector2 H(float(WrapIndex(PX + int32_t(B.x), XWrap)), float(PY + int32_t(B.y)));
					const Vector2 R = B + Hash2(H) - F;
					const Vector2 Delta = R - ClosestOffset;

					if (Delta.length_squared() > 0.00001f)
					{
						Result = std::fmin(Result, (0.5f * (ClosestOffset + R)).dot(Delta.normalized()));
					}
				}
			}

			return Result;
		}

		inline float Unormalize(float X, float Min, float Max)
		{
			return (X - Min) / (Max - Min);
		}

		inline float LerpIn(float NearValue, float SwitchStart, float SwitchEnd, float Distance)
		{
			return NearValue * Saturate(Unormalize(Distance, SwitchStart, SwitchEnd));
		}
	} // namespace Bark

	// ==================== Quaternions ====================

	// The sample stores quaternions as float4(xyz, w), matching godot::Quaternion's
	// layout, and its qMul is the same Hamilton product as Quaternion::operator*.

	inline Quaternion QIdentity()
	{
		return Quaternion(0, 0, 0, 1);
	}

	inline Quaternion QConjugate(const Quaternion& Q)
	{
		return Quaternion(-Q.x, -Q.y, -Q.z, Q.w);
	}

	inline Vector3 QTransform(const Quaternion& Q, const Vector3& P)
	{
		const Quaternion Result = (Q * Quaternion(P.x, P.y, P.z, 0.0f)) * QConjugate(Q);

		return Vector3(Result.x, Result.y, Result.z);
	}

	/** Local X axis of the rotation, i.e. column 0 of the equivalent basis. */
	inline Vector3 QGetX(const Quaternion& Q)
	{
		return Vector3(
			Q.w * Q.w + Q.x * Q.x - Q.y * Q.y - Q.z * Q.z,
			2 * Q.w * Q.z + 2 * Q.x * Q.y,
			-2 * Q.w * Q.y + 2 * Q.x * Q.z);
	}

	/** Local Y axis; leaves use this as their surface normal. */
	inline Vector3 QGetY(const Quaternion& Q)
	{
		return Vector3(
			-2 * Q.w * Q.z + 2 * Q.x * Q.y,
			Q.w * Q.w - Q.x * Q.x + Q.y * Q.y - Q.z * Q.z,
			2 * Q.w * Q.x + 2 * Q.y * Q.z);
	}

	/** Local Z axis, i.e. the direction a stem grows in. */
	inline Vector3 QGetZ(const Quaternion& Q)
	{
		return Vector3(
			2 * Q.w * Q.y + 2 * Q.x * Q.z,
			-2 * Q.w * Q.x + 2 * Q.y * Q.z,
			Q.w * Q.w - Q.x * Q.x - Q.y * Q.y + Q.z * Q.z);
	}

	inline Quaternion QRotateAxisAngle(const Vector3& Axis, float AngleRadians)
	{
		const float HalfAngle = AngleRadians * 0.5f;
		const Vector3 N = Axis.normalized();
		const float S = std::sin(HalfAngle);

		return Quaternion(N.x * S, N.y * S, N.z * S, std::cos(HalfAngle));
	}

	inline Quaternion QRotateX(float AngleRadians)
	{
		return QRotateAxisAngle(Vector3(1, 0, 0), AngleRadians);
	}

	inline Quaternion QRotateY(float AngleRadians)
	{
		return QRotateAxisAngle(Vector3(0, 1, 0), AngleRadians);
	}

	inline Quaternion QRotateZ(float AngleRadians)
	{
		return QRotateAxisAngle(Vector3(0, 0, 1), AngleRadians);
	}

	/**
	 * Raw slerp without shortest-path correction, matching the sample. The result is
	 * deliberately left unnormalised in the near-parallel case so that stem splines
	 * evaluate identically to the GPU version.
	 */
	inline Quaternion QSlerp(const Quaternion& A, const Quaternion& B, float T)
	{
		const float D = A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w;
		const float Theta = std::acos(Clamp(D, -1.0f, 1.0f));

		if (std::abs(Theta) < 1e-5f)
		{
			return Quaternion(
				Lerp(A.x, B.x, T),
				Lerp(A.y, B.y, T),
				Lerp(A.z, B.z, T),
				Lerp(A.w, B.w, T));
		}

		const float WeightA = std::sin((1.0f - T) * Theta);
		const float WeightB = std::sin(T * Theta);
		const float InvSin = 1.0f / std::sin(Theta);

		return Quaternion(
			(A.x * WeightA + B.x * WeightB) * InvSin,
			(A.y * WeightA + B.y * WeightB) * InvSin,
			(A.z * WeightA + B.z * WeightB) * InvSin,
			(A.w * WeightA + B.w * WeightB) * InvSin);
	}

	// ==================== Splines ====================

	inline Vector2 QuadraticBezier(const Vector2& V0, const Vector2& V1, const Vector2& V2, float T)
	{
		const float U = 1.0f - T;

		return V0 * (U * U) + V1 * (2.0f * U * T) + V2 * (T * T);
	}

	inline Vector2 CubicBezier(const Vector2& V0, const Vector2& V1, const Vector2& V2, const Vector2& V3, float T)
	{
		const float U = 1.0f - T;

		return V0 * (U * U * U) + V1 * (3.0f * T * U * U) + V2 * (3.0f * T * T * U) + V3 * (T * T * T);
	}

	/**
	 * Catmull-Rom / cubic Hermite basis. The sample weights tangents by the chord
	 * length so that a segment's shape is independent of its scale.
	 */
	inline Vector3 StemSpline(
		const Vector3& FromPos,
		const Vector3& FromZ,
		const Vector3& ToPos,
		const Vector3& ToZ,
		float T)
	{
		const float L = FromPos.distance_to(ToPos);
		const float T2 = T * T;
		const float T3 = T2 * T;

		const float W0 = +2 * T3 - 3 * T2 + 1;
		const float W1 = +1 * T3 - 2 * T2 + T;
		const float W2 = -2 * T3 + 3 * T2;
		const float W3 = +1 * T3 - 1 * T2;

		return FromPos * W0 + FromZ * (L * W1) + ToPos * W2 + ToZ * (L * W3);
	}

	// ==================== Weber-Penn shape functions ====================

	/** Weber-Penn shape ratio, selecting one of eight canopy silhouettes. */
	inline float ShapeRatio(int32_t Shape, float Ratio)
	{
		switch (Shape)
		{
			// Arbaro's conical differs from the paper's `0.2 + 0.8 * ratio`; the
			// sample follows Arbaro and so do we.
			case 0: return Ratio;                                                                        // conical
			case 1: return 0.2f + 0.8f * std::sin(TREE_PI * Ratio);                                      // spherical
			case 2: return 0.2f + 0.8f * std::sin(0.5f * TREE_PI * Ratio);                               // hemispherical
			case 3: return 1.0f;                                                                         // cylindrical
			case 4: return 0.5f + 0.5f * Ratio;                                                          // tapered cylindrical
			case 5: return (Ratio <= 0.7f) ? (Ratio / 0.7f) : ((1.0f - Ratio) / 0.3f);                    // flame
			case 6: return 1.0f - 0.8f * Ratio;                                                          // inverse conical
			case 7: return 0.5f + 0.5f * ((Ratio <= 0.7f) ? (Ratio / 0.7f) : ((1.0f - Ratio) / 0.3f));    // tend flame
			default: return 0.0f;
		}
	}

	inline float SmoothAbs(float X, float S = 0.00001f)
	{
		return std::sqrt(X * X + S);
	}

	/** Describes one tessellated slice of a stem, in the same terms as the GPU record. */
	struct SegmentInfo
	{
		int32_t Level = 0;
		/** Normalised position along the parent stem, quantised to 15 bits as on the GPU. */
		float FromZ = 0.0f;
		float ToZ = 0.0f;
		/** Total length of the stem this segment belongs to. */
		float Length = 0.0f;
		/** Base radius of the stem this segment belongs to. */
		float Radius = 0.0f;

		static const int32_t SEGMENT_Z_BITS = 15;

		/**
		 * The generator quantises z before deriving child positions from it. Keeping the
		 * quantisation is not cosmetic: it is what makes child placement stable when the
		 * curve resolution changes.
		 */
		static float QuantizeZ(float Z)
		{
			const float ZClamped = Saturate(Z);
			const uint32_t MaxEncodedValue = (1u << SEGMENT_Z_BITS) - 1u;
			const uint32_t Encoded = uint32_t(RoundNE(ZClamped * float(MaxEncodedValue)));

			return Saturate(float(Encoded) / float(MaxEncodedValue));
		}
	};

	/** Root flare: thickens the very bottom of the trunk. Level 0 only. */
	inline float StemFlare(const SegmentInfo& Si, float FlareAmount, float Z)
	{
		if (Si.Level != 0)
		{
			return 1.0f;
		}

		const float Y = std::fmax(0.0f, 1.0f - 8.0f * Z);

		return FlareAmount * (std::pow(100.0f, Y) - 1.0f) / 100.0f + 1.0f;
	}

	/**
	 * Weber-Penn taper. Taper values above 1 produce the periodic bulges of a palm
	 * trunk, values below 1 a spherical tip; see paper section 4.5.
	 */
	inline float GetTaperedRadius(const SegmentInfo& Si, float TaperValue, float FlareAmount, float Z)
	{
		if (Z > 0.9999f)
		{
			return 0.0f;
		}

		float Taper = TaperValue;
		if (Taper < 1.0f)
		{
			Taper = 2.0f - Taper;
		}
		Taper -= 1.0f;

		const bool bIsPalm = Taper >= 1.0f;

		const float UnitTaper = std::fmax(0.0f, 1.0f - Taper);
		const float Tr = Si.Radius * (1.0f - UnitTaper * Z);

		const float Z2 = (1.0f - Z) * Si.Length;
		const float Z3 = !bIsPalm ? Z2 : SmoothAbs(Z2 - 2.0f * Tr * RoundNE(Z2 / (2.0f * Tr)));

		const float A = Tr * Tr;
		const float B = (Z3 - Tr) * (Z3 - Tr);
		const float R = std::sqrt(std::fmax(0.0f, A - B));

		float Depth = 0.0f;
		if (bIsPalm && (Z2 >= Tr))
		{
			Depth = 2.0f - Taper;
		}
		if (!bIsPalm && !(Z3 < Tr))
		{
			Depth = 1.0f;
		}

		return Lerp(R, Tr, Depth) * StemFlare(Si, FlareAmount, Z);
	}

	/** Non-circular trunk cross-section, fading out along the first segment. */
	inline float GetLobeFactor(int32_t LobeCount, float LobeDepth, float Theta, float T)
	{
		return Lerp(1.0f + LobeDepth * std::sin(float(LobeCount) * Theta), 1.0f, Saturate(T));
	}

	// ==================== Season & ambient occlusion ====================

	/** Crude path-length based occlusion: deeper inside the crown means darker. */
	inline float FakeAOFromDistance(float Distance)
	{
		return std::pow(Clamp(1.0f - Distance * 0.015f, 0.8f, 1.0f), 3.0f);
	}

	inline float GetNoisedLeafSeason(uint32_t Seed, float Season)
	{
		const float Scale = 0.2f + 0.1f * Season;

		return Season + Scale * Random::Value(Seed, 0xDEAD);
	}

	inline float GetSeasonLeafScale(uint32_t Seed, bool bIsBlossom, float Season)
	{
		const float NoisedSeason = GetNoisedLeafSeason(Seed, Season);

		float Growth = Saturate(NoisedSeason);
		if (!bIsBlossom)
		{
			Growth = Growth * Growth;
		}

		const bool bFall = NoisedSeason < (bIsBlossom ? 1.75f : 3.75f);

		return bFall ? Growth : 0.0f;
	}

	inline float GetGeneralSeasonFruitProgress(float Season)
	{
		if (Season < 1.5f || Season > 3.25f)
		{
			return 0.0f;
		}

		return std::fmin(Season - 1.5f, 1.0f);
	}

	inline float GetSeasonFruitProgress(uint32_t Seed, float Season)
	{
		return GetGeneralSeasonFruitProgress(Season + 0.025f * Random::SignedValue(Seed, 0xBEAF));
	}

	inline float GetFruitScale(float Progress)
	{
		return std::pow(Progress, 0.25f);
	}

	/**
	 * Seasonal leaf tint: spring is a brighter green than summer, then the leaf goes gold
	 * and finally rust. Needles and blossoms keep their own colour year-round.
	 */
	inline Color GetSeasonLeafColor(const Color& LeafColor, float Season, bool bIsNeedle, bool bIsBlossom)
	{
		const Color Summer = LeafColor * 1.1f;

		if (bIsNeedle || bIsBlossom)
		{
			return Summer;
		}

		const Color Spring = Summer * 1.1f;
		const Color EarlyFall = Color(0.99f, 0.74f, 0.17f, 1.0f) * 0.5f;
		const Color LateFall = Color(0.55f, 0.10f, 0.03f, 1.0f) * 0.5f;

		const float Clamped = Clamp(Season, 0.0f, 4.0f);

		if (Clamped < 1.0f)
		{
			return Spring;
		}
		if (Clamped < 2.3f)
		{
			return Spring.lerp(Summer, (Clamped - 1.0f) / 1.3f);
		}
		if (Clamped < 3.0f)
		{
			return Summer.lerp(EarlyFall, (Clamped - 2.3f) / 0.7f);
		}
		if (Clamped < 3.5f)
		{
			return EarlyFall.lerp(LateFall, (Clamped - 3.0f) / 0.5f);
		}

		return LateFall;
	}

	inline Vector2 Rotate2D(const Vector2& V, float AngleRadians)
	{
		const float C = std::cos(AngleRadians);
		const float S = std::sin(AngleRadians);

		return Vector2(C * V.x - S * V.y, S * V.x + C * V.y);
	}
} // namespace TreeGen
