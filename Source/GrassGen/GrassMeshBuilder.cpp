#include "GrassGen/GrassMeshBuilder.h"

#include "TreeGen/TreeMath.h"

#include <godot_cpp/core/math.hpp>

#include <algorithm>
#include <cmath>

using namespace godot;

namespace
{
	using GrassGen::Color;
	using GrassGen::EGrassSpecies;
	using GrassGen::GrassSpec;
	using GrassGen::MeshAccumulator;
	using GrassGen::Vector2;
	using GrassGen::Vector3;

	constexpr int32_t SEGMENTS = 8; // 9 rings per blade — enough for the bow + tip features.
	constexpr float FOLD_HALF_ANGLE_DEG = 18.0f; // V-fold depth; shallow so it still reads thin.
	constexpr float DEG_TO_RAD = 0.017453292f;

	// ---------------------------------------------------------------- per-species profile

	/** Everything BuildGrass needs to know to make one species look distinct. */
	struct SpeciesProfile
	{
		int32_t MinCount, MaxCount;
		float MinHeight, MaxHeight; // metres
		float BaseWidth, TipWidth; // metres, before the per-blade height-based width scale
		float RadiusScale; // multiplies GrassSpec::ClumpRadius
		float CurvatureBaseline; // baseline bow amount, scaled by GrassSpec::Curvature
		float FeatureChance; // probability a blade gets the species' distinguishing feature
		float ExtraHueRange; // extra per-blade hue jitter (degrees), on top of ColorVariance
		/** 0 = fully random lean azimuth, 1 = fully radially outward from the clump
		 *  centre — what turns a pile of vertical blades into a believable dome/
		 *  fountain silhouette (Reference/grass/芦苇类_狗尾巴草, Reference/grass/杂草). */
		float SplayBias;
		Color BaseColor, TipColor;
	};

	const SpeciesProfile& GetProfile(EGrassSpecies Species)
	{
		static const SpeciesProfile Profiles[4] = {
			// THATCH 茅草: tall, sparse, stiff; green base fading to a pale straw tip.
			{ 5, 9, 0.6f, 1.4f, 0.018f, 0.004f, 1.4f, 0.4f, 0.30f, 6.0f, 0.0f,
				Color(0.24f, 0.34f, 0.14f), Color(0.62f, 0.56f, 0.28f) },
			// FOXTAIL 狗尾巴草: dense arching leaf dome (most blades) + fewer, taller
			// flowering stems capped with a fluffy pink-gold spike, splayed outward
			// into a fountain shape. FeatureChance here is the flower-stem probability.
			{ 10, 16, 0.2f, 0.45f, 0.010f, 0.0025f, 1.15f, 0.5f, 0.40f, 10.0f, 0.75f,
				Color(0.20f, 0.40f, 0.11f), Color(0.68f, 0.50f, 0.32f) },
			// SHORT 小草: short, dense, plain — no feature, vivid green throughout.
			{ 20, 40, 0.05f, 0.15f, 0.006f, 0.0015f, 0.8f, 0.25f, 0.0f, 5.0f, 0.0f,
				Color(0.14f, 0.38f, 0.10f), Color(0.30f, 0.55f, 0.16f) },
			// WEED 杂草: a dense radial tuft of thin, wildly arching blades (not
			// broadleaf weeds) — some frizzy-tipped, some bulged, a few flowering.
			// FeatureChance is the total odds of getting any one of those three.
			{ 16, 26, 0.15f, 0.45f, 0.007f, 0.0018f, 1.3f, 0.6f, 0.70f, 30.0f, 0.55f,
				Color(0.18f, 0.36f, 0.14f), Color(0.55f, 0.48f, 0.32f) },
		};
		return Profiles[std::clamp(int32_t(Species), 0, 3)];
	}

	// ---------------------------------------------------------------- small math helpers

	inline float Lerp(float A, float B, float T) { return A + (B - A) * T; }

	/** Smooth 0→1→0 bell curve, peaking at T=Centre. Used for the weed leaf bulge. */
	inline float Bell(float T, float Centre, float Width)
	{
		const float D = (T - Centre) / Width;
		return std::max(0.0f, 1.0f - D * D);
	}

	Vector3 QuadraticBezier(const Vector3& P0, const Vector3& P1, const Vector3& P2, float T)
	{
		const float U = 1.0f - T;
		return P0 * (U * U) + P1 * (2.0f * U * T) + P2 * (T * T);
	}

	Vector3 QuadraticBezierTangent(const Vector3& P0, const Vector3& P1, const Vector3& P2, float T)
	{
		return (P1 - P0) * (2.0f * (1.0f - T)) + (P2 - P1) * (2.0f * T);
	}

	Color JitterColor(const Color& Base, float Variance, float ExtraHueDeg, uint32_t Seed)
	{
		if (Variance <= 0.0f && ExtraHueDeg <= 0.0f)
		{
			return Base;
		}

		const float HueJitter = (Variance * 20.0f + ExtraHueDeg) * TreeGen::Random::SignedValue(Seed, 11u) / 360.0f;
		const float ValueJitter = Variance * 0.35f * TreeGen::Random::SignedValue(Seed, 23u);
		const float SatJitter = Variance * 0.2f * TreeGen::Random::SignedValue(Seed, 37u);

		Color Result = Color::from_hsv(
			std::fmod(Base.get_h() + HueJitter + 1.0f, 1.0f),
			std::clamp(Base.get_s() + SatJitter, 0.0f, 1.0f),
			std::clamp(Base.get_v() + ValueJitter, 0.0f, 1.0f),
			Base.a);
		return Result;
	}

	// ---------------------------------------------------------------- one blade

	/** Everything about one blade that the shape/feature logic needs. */
	struct BladeSpec
	{
		Vector3 Origin;
		float Height = 0.3f;
		float BaseWidth = 0.01f;
		float TipWidth = 0.003f;
		float LeanAngleDeg = 0.0f;
		float LeanAzimuthDeg = 0.0f;
		float Curvature = 0.2f;
		Color BaseColor, TipColor;
		/** Droops the tip outward and down, independent of the whole-blade bow —
		 *  thatch's flag tip and foxtail's arching leaves/nodding flower stems all
		 *  share this, just with different start/strength. */
		bool bDroopTip = false;
		float DroopStart = 0.8f; // T where the droop begins
		float DroopStrength = 1.0f; // multiplies how far it drops
		/** Replaces the tip with a fluffy spike — foxtail's dense bottlebrush and
		 *  weed's thin frizzy wisp are the same mechanic, tuned differently. */
		bool bSeedSpike = false;
		float SpikeStart = 0.6f; // T where the spike zone begins
		float SpikeWidthMul = 1.3f; // spike belly width, as a multiple of BaseWidth
		float SpikeJitter = 0.3f; // per-ring width jitter fraction, for a ragged silhouette
		bool bLeafBulge = false;
		bool bFlower = false;
		Color FlowerColor;
		uint32_t RandSeed = 0;
	};

	/** Width at T in [0,1], including the species feature's shape (spike bulge / droop
	 *  taper / weed bulge) — everything else about the feature (extra jitter, colour,
	 *  flower geometry) is handled by the caller. */
	float WidthAt(const BladeSpec& B, float T)
	{
		float Width = Lerp(B.BaseWidth, B.TipWidth, T);

		if (B.bDroopTip)
		{
			// Narrow sharply into the droop instead of tapering to a point, starting
			// a little before the droop itself so the taper reads as part of it.
			const float NarrowStart = std::max(B.DroopStart - 0.05f, 0.0f);
			if (T > NarrowStart)
			{
				const float DropT = (T - NarrowStart) / std::max(1.0f - NarrowStart, 0.01f);
				Width = Lerp(Width, B.TipWidth * 0.5f, DropT);
			}
		}

		if (B.bSeedSpike && T > B.SpikeStart)
		{
			// Neck-then-bulge-then-point: a rounded spindle, not a straight taper.
			// The clamp matters: float(Math::PI) rounds a hair past true pi, so
			// sin(PI * 1.0) evaluates to a tiny *negative* number rather than zero,
			// and pow() of a negative base with a fractional exponent is NaN.
			const float SpikeT = (T - B.SpikeStart) / std::max(1.0f - B.SpikeStart, 0.01f);
			const float SinArg = std::clamp(std::sin(float(Math::PI) * std::min(SpikeT, 1.0f)), 0.0f, 1.0f);
			const float Bulge = std::pow(SinArg, 0.6f);
			Width = Lerp(B.TipWidth, B.BaseWidth * B.SpikeWidthMul, Bulge) * (1.0f - SpikeT * 0.15f);
		}

		if (B.bLeafBulge)
		{
			Width *= 1.0f + 0.9f * Bell(T, 0.45f, 0.28f);
		}

		return std::max(Width, 0.0002f);
	}

	/** Blade centreline + tangent at T, including the droop and curvature bow. */
	void EvalCentreline(const BladeSpec& B, float T, Vector3& OutPos, Vector3& OutTangent)
	{
		const float LeanRad = B.LeanAngleDeg * DEG_TO_RAD;
		const float AzimuthRad = B.LeanAzimuthDeg * DEG_TO_RAD;
		const Vector3 LeanDir(std::sin(AzimuthRad), 0.0f, std::cos(AzimuthRad));

		const Vector3 P0 = B.Origin;
		const Vector3 P2 = B.Origin + Vector3(0, 1, 0) * (B.Height * std::cos(LeanRad))
			+ LeanDir * (B.Height * std::sin(LeanRad));
		const Vector3 P1 = P0.lerp(P2, 0.5f) + LeanDir * (B.Height * B.Curvature);

		OutPos = QuadraticBezier(P0, P1, P2, T);
		OutTangent = QuadraticBezierTangent(P0, P1, P2, T).normalized();

		if (B.bDroopTip && T > B.DroopStart)
		{
			const float DropT = (T - B.DroopStart) / std::max(1.0f - B.DroopStart, 0.01f);
			const float Drop = DropT * DropT * B.DroopStrength;
			OutPos.y -= B.Height * 0.22f * Drop;
			OutPos += LeanDir * (B.Height * 0.18f * Drop);
		}
	}

	/** Appends a flat quad (as two triangles) with an explicit shared normal. */
	void AddQuad(
		MeshAccumulator& Mesh,
		const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
		const Vector2& UvA, const Vector2& UvB, const Vector2& UvC, const Vector2& UvD,
		const Vector3& Normal,
		const Color& ColorA, const Color& ColorB, const Color& ColorC, const Color& ColorD)
	{
		const uint32_t First = uint32_t(Mesh.Vertices.size());
		Mesh.Vertices.push_back(A);
		Mesh.Vertices.push_back(B);
		Mesh.Vertices.push_back(C);
		Mesh.Vertices.push_back(D);
		Mesh.Normals.push_back(Normal);
		Mesh.Normals.push_back(Normal);
		Mesh.Normals.push_back(Normal);
		Mesh.Normals.push_back(Normal);
		Mesh.UVs.push_back(UvA);
		Mesh.UVs.push_back(UvB);
		Mesh.UVs.push_back(UvC);
		Mesh.UVs.push_back(UvD);
		Mesh.Colors.push_back(ColorA);
		Mesh.Colors.push_back(ColorB);
		Mesh.Colors.push_back(ColorC);
		Mesh.Colors.push_back(ColorD);

		Mesh.Indices.push_back(int32_t(First + 0));
		Mesh.Indices.push_back(int32_t(First + 1));
		Mesh.Indices.push_back(int32_t(First + 2));
		Mesh.Indices.push_back(int32_t(First + 0));
		Mesh.Indices.push_back(int32_t(First + 2));
		Mesh.Indices.push_back(int32_t(First + 3));
	}

	/** A small 3-blade "flower" cross at the blade tip, for a fraction of weed stems. */
	void EmitFlowerFan(const BladeSpec& B, const Vector3& Tip, const Vector3& Tangent, MeshAccumulator& Mesh)
	{
		const float PetalLength = std::max(B.BaseWidth * 5.0f, 0.02f);
		Vector3 Up = Tangent;
		if (Up.length_squared() < 1e-8f)
		{
			Up = Vector3(0, 1, 0); // degenerate tip tangent (near-zero-length blade bow)
		}
		Vector3 Ref = std::abs(Up.y) < 0.95f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
		const Vector3 Side = Up.cross(Ref).normalized();
		const Vector3 Fwd = Side.cross(Up).normalized();

		for (int32_t Petal = 0; Petal < 3; ++Petal)
		{
			const float Angle = (float(Petal) / 3.0f) * float(Math::TAU)
				+ TreeGen::Random::Value(B.RandSeed, uint32_t(Petal), 71u) * 0.5f;
			const Vector3 Out = Side * std::cos(Angle) + Fwd * std::sin(Angle);
			const Vector3 Base = Tip - Out * (PetalLength * 0.15f);
			const Vector3 PetalTip = Tip + Out * PetalLength + Up * (PetalLength * 0.2f);
			const Vector3 Left = Base + Up.cross(Out).normalized() * (PetalLength * 0.18f);
			const Vector3 Right = Base - Up.cross(Out).normalized() * (PetalLength * 0.18f);
			Vector3 Normal = (PetalTip - Left).cross(Right - Left);
			Normal = Normal.length_squared() > 1e-12f ? Normal.normalized() : Up;

			AddQuad(Mesh,
				Left, PetalTip, PetalTip, Right,
				Vector2(0, 0), Vector2(0.5f, 1), Vector2(0.5f, 1), Vector2(1, 0),
				Normal,
				B.FlowerColor, B.FlowerColor, B.FlowerColor, B.FlowerColor);
		}
	}

	void EmitBlade(const BladeSpec& B, MeshAccumulator& Mesh)
	{
		const float FoldRad = FOLD_HALF_ANGLE_DEG * DEG_TO_RAD;
		const float CosFold = std::cos(FoldRad);
		const float SinFold = std::sin(FoldRad);

		const float AzimuthRad = B.LeanAzimuthDeg * DEG_TO_RAD;
		const Vector3 LeanDir(std::sin(AzimuthRad), 0.0f, std::cos(AzimuthRad));
		const Vector3 Side = Vector3(0, 1, 0).cross(LeanDir).normalized();
		const Vector3 Forward = LeanDir; // fold/bow plane's horizontal axis

		std::vector<Vector3> Left(SEGMENTS + 1), Spine(SEGMENTS + 1), Right(SEGMENTS + 1);
		std::vector<Vector3> NormalLeft(SEGMENTS + 1), NormalRight(SEGMENTS + 1);
		std::vector<float> HalfWidth(SEGMENTS + 1);

		for (int32_t i = 0; i <= SEGMENTS; ++i)
		{
			const float T = float(i) / float(SEGMENTS);
			Vector3 Centre, Tangent;
			EvalCentreline(B, T, Centre, Tangent);

			float Width = WidthAt(B, T) * 0.5f;
			if (B.bSeedSpike && T > B.SpikeStart)
			{
				// Ragged silhouette along the spike, not a smooth spindle.
				Width += Width * B.SpikeJitter * TreeGen::Random::SignedValue(B.RandSeed, uint32_t(i), 131u);
				Width = std::max(Width, 0.0002f);
			}

			const Vector3 LeftDir = (-CosFold * Side + SinFold * Forward);
			const Vector3 RightDir = (CosFold * Side + SinFold * Forward);

			Spine[i] = Centre;
			Left[i] = Centre + LeftDir * Width;
			Right[i] = Centre + RightDir * Width;
			HalfWidth[i] = Width;

			NormalLeft[i] = Tangent.cross(LeftDir).normalized();
			NormalRight[i] = RightDir.cross(Tangent).normalized();
		}

		for (int32_t i = 0; i < SEGMENTS; ++i)
		{
			const float T0 = float(i) / float(SEGMENTS);
			const float T1 = float(i + 1) / float(SEGMENTS);
			const Color Col0 = B.BaseColor.lerp(B.TipColor, T0);
			const Color Col1 = B.BaseColor.lerp(B.TipColor, T1);

			AddQuad(Mesh,
				Left[i], Left[i + 1], Spine[i + 1], Spine[i],
				Vector2(0, T0), Vector2(0, T1), Vector2(0.5f, T1), Vector2(0.5f, T0),
				NormalLeft[i].lerp(NormalLeft[i + 1], 0.5f).normalized(),
				Col0, Col1, Col1, Col0);

			AddQuad(Mesh,
				Spine[i], Spine[i + 1], Right[i + 1], Right[i],
				Vector2(0.5f, T0), Vector2(0.5f, T1), Vector2(1, T1), Vector2(1, T0),
				NormalRight[i].lerp(NormalRight[i + 1], 0.5f).normalized(),
				Col0, Col1, Col1, Col0);
		}

		if (B.bFlower)
		{
			Vector3 TipTangent;
			Vector3 TipPos;
			EvalCentreline(B, 1.0f, TipPos, TipTangent);
			EmitFlowerFan(B, TipPos, TipTangent, Mesh);
		}
	}

	// ---------------------------------------------------------------- clump scatter

	BladeSpec MakeBladeSpec(const GrassGen::GrassSpec& Spec, const SpeciesProfile& Profile, int32_t Index, uint32_t BaseSeed)
	{
		const uint32_t SeedI = TreeGen::Random::CombineSeed(BaseSeed, uint32_t(Index) * 2654435761u);

		// Denser toward the clump centre (area-uniform would spread too evenly for a
		// believable tuft), sqrt-biased radius sampling pulled further inward.
		const float RadiusT = std::pow(TreeGen::Random::Value(SeedI, 1u), 1.6f);
		const float Angle = TreeGen::Random::Value(SeedI, 2u) * float(Math::TAU);
		const float Radius = Spec.ClumpRadius * Profile.RadiusScale * RadiusT;

		BladeSpec B;
		B.Origin = Vector3(std::cos(Angle) * Radius, 0.0f, std::sin(Angle) * Radius);

		const float HeightT = TreeGen::Random::Value(SeedI, 3u);
		B.Height = Lerp(Profile.MinHeight, Profile.MaxHeight, HeightT);
		const float WidthScale = Lerp(0.8f, 1.2f, HeightT);
		B.BaseWidth = Profile.BaseWidth * WidthScale;
		B.TipWidth = Profile.TipWidth * WidthScale;

		// Outward "splay": blends a fully random lean azimuth with one pointing
		// straight away from the clump centre, per species. Zero for Thatch/Short
		// keeps their prior straight-up-ish look; Foxtail/Weed use it to turn a pile
		// of vertical blades into a believable dome/fountain silhouette.
		const float OutwardAzimuthDeg = Angle * (180.0f / float(Math::PI));
		const float RandomAzimuth = TreeGen::Random::Value(SeedI, 5u) * 360.0f;
		const float SplayedAzimuth = OutwardAzimuthDeg + TreeGen::Random::SignedValue(SeedI, 15u) * 20.0f;
		const float BaseAzimuth = Lerp(RandomAzimuth, SplayedAzimuth, Profile.SplayBias);

		B.LeanAngleDeg = Spec.LeanAngle + TreeGen::Random::SignedValue(SeedI, 4u) * 10.0f;
		B.LeanAzimuthDeg = (Spec.LeanAngle > 0.5f)
			? (Spec.LeanAzimuth + TreeGen::Random::SignedValue(SeedI, 5u) * 25.0f)
			: BaseAzimuth;
		B.Curvature = Profile.CurvatureBaseline * Spec.Curvature * Lerp(0.7f, 1.3f, TreeGen::Random::Value(SeedI, 6u));

		B.RandSeed = SeedI;

		const float FeatureRoll = TreeGen::Random::Value(SeedI, 7u);
		switch (Spec.Species)
		{
			case GrassGen::GRASS_SPECIES_THATCH:
				B.bDroopTip = FeatureRoll < Profile.FeatureChance;
				break;

			case GrassGen::GRASS_SPECIES_FOXTAIL:
			{
				// Two populations, like the real plant (Reference/grass/芦苇类_狗尾巴草):
				// most blades are plain leaves that arc outward into the fountain-
				// shaped base; a minority are taller flowering stems that carry the
				// fluffy seed spike above the leaf dome and nod gracefully at the tip.
				if (FeatureRoll < Profile.FeatureChance)
				{
					B.Height *= 1.25f;
					B.Curvature *= 0.75f;
					B.bDroopTip = true;
					B.DroopStart = 0.82f;
					B.DroopStrength = 0.6f; // a graceful nod, not a full droop
					B.bSeedSpike = true;
					B.SpikeStart = 0.55f;
					B.SpikeWidthMul = 1.5f;
					B.SpikeJitter = 0.35f;
				}
				else
				{
					B.Height *= 0.7f;
					B.Curvature *= 1.4f;
					B.bDroopTip = true;
					B.DroopStart = 0.55f;
					B.DroopStrength = 1.3f; // arcs right down toward the ground
				}
				break;
			}

			case GrassGen::GRASS_SPECIES_WEED:
			{
				// A dense radial tuft (Reference/grass/杂草), not broadleaf weeds: most
				// of the character comes from the splay/droop above, these just add
				// per-blade variety on top of it.
				if (FeatureRoll < Profile.FeatureChance * 0.55f)
				{
					// Thin, frizzy wisp — the same "replace the tip" mechanic as
					// foxtail's spike, tuned short and sparse instead of dense.
					B.bSeedSpike = true;
					B.SpikeStart = 0.7f;
					B.SpikeWidthMul = 0.6f;
					B.SpikeJitter = 0.6f;
				}
				else if (FeatureRoll < Profile.FeatureChance * 0.75f)
				{
					B.bLeafBulge = true;
				}
				else if (FeatureRoll < Profile.FeatureChance)
				{
					B.bFlower = true;
				}
				break;
			}

			default:
				break;
		}

		const Color SpeciesBase = Spec.bUseSpeciesColors ? Profile.BaseColor : Spec.BaseColor;
		const Color SpeciesTip = Spec.bUseSpeciesColors ? Profile.TipColor : Spec.TipColor;
		B.BaseColor = JitterColor(SpeciesBase, Spec.ColorVariance, Profile.ExtraHueRange, TreeGen::Random::CombineSeed(SeedI, 8u));
		B.TipColor = JitterColor(SpeciesTip, Spec.ColorVariance, Profile.ExtraHueRange, TreeGen::Random::CombineSeed(SeedI, 9u));

		if (B.bFlower)
		{
			// A handful of accent hues so a weed patch doesn't read as one flat colour.
			const float Roll = TreeGen::Random::Value(SeedI, 10u);
			B.FlowerColor = Roll < 0.34f ? Color(0.95f, 0.82f, 0.20f)
				: Roll < 0.67f ? Color(0.92f, 0.92f, 0.88f)
								: Color(0.58f, 0.28f, 0.55f);
		}

		return B;
	}
} // namespace

namespace GrassGen
{
	void BuildGrass(const GrassSpec& Spec, MeshAccumulator& OutMesh)
	{
		const SpeciesProfile& Profile = GetProfile(Spec.Species);
		const uint32_t BaseSeed = TreeGen::Random::Hash(uint32_t(Spec.Seed * 1000.0f) + 1u);

		int32_t Count = Spec.BladeCount;
		if (Count <= 0)
		{
			const float CountT = TreeGen::Random::Value(BaseSeed, 0u);
			Count = Profile.MinCount + int32_t(CountT * float(Profile.MaxCount - Profile.MinCount + 1));
			Count = std::clamp(Count, Profile.MinCount, Profile.MaxCount);
		}

		for (int32_t Index = 0; Index < Count; ++Index)
		{
			BladeSpec Blade = MakeBladeSpec(Spec, Profile, Index, BaseSeed);
			EmitBlade(Blade, OutMesh);
		}

		if (Spec.Scale != 1.0f)
		{
			for (Vector3& V : OutMesh.Vertices)
			{
				V *= Spec.Scale;
			}
		}
	}
} // namespace GrassGen
