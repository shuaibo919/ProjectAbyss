#include "TreeGen/TreeSkeleton.h"

#include <algorithm>

using namespace TreeGen;

namespace
{
	/** Mirrors the GPU sample's record limits; they are part of the look, not just budgets. */
	const uint32_t MAX_CHILD_RECORDS = 128;
	const uint32_t MAX_SEGMENT_RECORDS = 128;
	/** One thread per potential clone. Caps how many times a stem can fork. */
	const uint32_t STEM_THREAD_GROUP_SIZE = 32;
	/** 128 children handled by 32 threads. */
	const int32_t CHILD_ITERATIONS = 4;
	/** Work graph recursion depth of the original, so levels run 0..3. */
	const int32_t MAX_STEM_LEVEL = 3;

	// ==================== Leaf density (LeafDensity.h) ====================

	uint32_t GetChildScaleGroupSize(uint32_t ChildCount)
	{
		return ChildCount > 64 ? 16u : 4u;
	}

	float GetChildScale(float GroupScale, float ElementScale)
	{
		if (GroupScale == 1.0f)
		{
			return ElementScale != 0.0f ? 1.0f : 0.0f;
		}
		if (ElementScale == 0.0f)
		{
			return 0.0f;
		}
		if (ElementScale == 1.0f)
		{
			return 1.0f;
		}

		return GroupScale;
	}

	/**
	 * Returns the index and scale of the n-th surviving child under a given density.
	 * Thinning is spread out in small groups rather than by a per-child dice roll, which
	 * keeps foliage evenly distributed as density drops.
	 */
	void GetNthChildIndexAndScale(
		uint32_t N,
		uint32_t ChildCount,
		float ChildDensity,
		uint32_t& OutChildIndex,
		float& OutChildScale)
	{
		OutChildIndex = 0;
		OutChildScale = 0.0f;

		if (ChildCount == 0)
		{
			return;
		}

		const uint32_t GroupSize = GetChildScaleGroupSize(ChildCount);
		const uint32_t GroupCount = uint32_t(DivideAndRoundUp(int32_t(ChildCount), int32_t(GroupSize)));
		const uint32_t AliveChildCount = uint32_t(std::ceil(float(ChildCount) * ChildDensity));

		if (N >= AliveChildCount)
		{
			return;
		}

		const uint32_t VirtualChildCount = GroupSize * GroupCount;
		const float VirtualChildDensity = (float(ChildCount) * ChildDensity) / float(VirtualChildCount);

		const float GroupFill = float(GroupSize) * VirtualChildDensity;
		const uint32_t ChildrenPerGroupF = uint32_t(std::fmax(std::floor(GroupFill), 1.0f));
		const uint32_t ChildrenPerGroupC = uint32_t(std::fmax(std::ceil(GroupFill), 1.0f));

		const float FractionalFill = FMod(GroupFill, 1.0f);
		const float D = (FractionalFill == 0.0f) ? 1.0f : FractionalFill;

		const uint32_t SmallGroupCount = VirtualChildCount - ChildCount;

		const uint32_t GroupCountC = uint32_t(std::ceil(D * float(GroupCount)));
		const uint32_t GroupCountF = GroupCount - GroupCountC;

		const uint32_t GroupSkipF = std::min(N / ChildrenPerGroupF, GroupCountF);
		const int32_t AfterSkipF = std::max(int32_t(N) - int32_t(GroupSkipF * ChildrenPerGroupF), 0);
		const uint32_t GroupSkipC = uint32_t(AfterSkipF) / ChildrenPerGroupC;

		uint32_t GroupIndex = GroupSkipF + GroupSkipC;
		const uint32_t ElementIndex = uint32_t(AfterSkipF) % ChildrenPerGroupC;

		if (AliveChildCount <= GroupCount)
		{
			GroupIndex = GroupCount - 1 - GroupIndex;
		}

		const float GroupScale = Clamp((D * float(GroupCount)) - float(GroupCount - 1 - GroupIndex), 0.0f, 1.0f);
		const float ElementScale = Clamp(GroupFill - float(ElementIndex), 0.0f, 1.0f);

		const uint32_t SmallGroupSkip = std::min(GroupIndex, SmallGroupCount);

		OutChildIndex = SmallGroupSkip * (GroupSize - 1) + (GroupIndex - SmallGroupSkip) * GroupSize + ElementIndex;
		OutChildScale = GetChildScale(GroupScale, ElementScale);

		if (OutChildIndex >= ChildCount)
		{
			OutChildIndex = 0;
			OutChildScale = 0.0f;
		}
		if (N == 0)
		{
			// Always keep at least one child, so a stem never ends up completely bare.
			OutChildScale = 1.0f;
		}
	}

	// ==================== Generation helpers (TreeGeneration.h) ====================

	float SafeAcos(float X)
	{
		return std::acos(Clamp(X, -1.0f, 1.0f));
	}

	/** Weber-Penn section 4.1: per-segment bend along the stem. */
	float GetStemCurve(
		const TreeParams& Params,
		int32_t Level,
		float CurveResolution,
		uint32_t SegmentSeed,
		float Z)
	{
		float RotateX = (Params.CurveV[Level] / CurveResolution) * Random::SignedValue(SegmentSeed, 6);

		if (Params.CurveBack[Level] == 0.0f)
		{
			RotateX += Params.Curve[Level] / CurveResolution;
		}
		else
		{
			// S-curves: bend one way over the lower half, the other way over the upper half.
			const float HalfResolution = CurveResolution * 0.5f;
			const float Alpha = MapRange(Z, 0.4f, 0.6f, 0.0f, 1.0f);
			const float CurveAmount = Lerp(Params.Curve[Level], Params.CurveBack[Level], Alpha);
			RotateX += CurveAmount / HalfResolution;
		}

		return RotateX;
	}

	/** Weber-Penn section 4.2: angle a freshly cloned branch away from its sibling. */
	void AddSplitSpread(
		const SegmentInfo& Si,
		const TreeParams& Params,
		uint32_t SegmentSeed,
		int32_t Step,
		Quaternion& Rot,
		float& SplitCorrection)
	{
		float Declination = ToDegrees(SafeAcos(QGetZ(Rot).y));

		float SplitAngle = Params.SplitAngle[Si.Level]
			+ Params.SplitAngleV[Si.Level] * Random::SignedValue(SegmentSeed, 0xFA)
			- Declination;
		SplitAngle = std::fmax(0.0f, SplitAngle);

		// The remaining segments bend back so the clone rejoins the parent's overall direction.
		const int32_t RemainingSegments = std::max(1, Params.CurveRes[Si.Level] - Step - 1);
		SplitCorrection -= SplitAngle / float(RemainingSegments);

		Rot = Rot * QRotateX(ToRadians(SplitAngle));

		Declination = ToDegrees(SafeAcos(QGetZ(Rot).y));

		float SpreadAngle = 20.0f + 0.75f * (30.0f + std::abs(Declination - 90.0f));
		const float R = Random::Value(SegmentSeed, 900);
		SpreadAngle *= R * R;
		if (Random::Value(SegmentSeed, uint32_t(Step), 2) < 0.5f)
		{
			SpreadAngle = -SpreadAngle;
		}

		Rot = QRotateY(ToRadians(SpreadAngle)) * Rot;
	}

	/** Weber-Penn section 4.8: bend stems back towards vertical, or let them droop. */
	void AddVerticalAttraction(
		int32_t Level,
		const TreeParams& Params,
		const GenerationContext& Context,
		float CurveResolution,
		Quaternion& Rot)
	{
		float AttractionUp = 0.0f;

		if (Level > 1)
		{
			AttractionUp += Params.AttractionUp;
		}

		Vector3 Axis = QGetZ(Rot);
		const float Declination = SafeAcos(Axis.y);

		// Out of season, branches sag under snow load or lack of turgor.
		const float SeasonSpan = 2.0f - std::abs(Context.Season - 2.0f);
		AttractionUp += std::fmin(0.0f, SeasonSpan * SeasonSpan / 0.2f - 0.2f);

		if (Level > 0 && Params.Fruit.Chance > 0.0f)
		{
			const float LevelFactor = 1.0f / std::pow(4.0f, float(Params.Levels - Level - 1));

			float FruitProgress = GetGeneralSeasonFruitProgress(Context.Season);
			if (Context.Season > 3.2f)
			{
				FruitProgress = MapRange(Context.Season, 3.2f, 3.3f, 1.0f, 0.0f);
			}

			const float Size = 15.0f * Params.Fruit.Size * GetFruitScale(FruitProgress);
			// Weight grows with the cube of the fruit's size.
			const float Volume = Size * Size * Size;

			AttractionUp -= LevelFactor * float(Params.Blossom.Count) * Params.Fruit.Chance * Volume;
		}

		if (AttractionUp != 0.0f && Axis.y < 0.9999f)
		{
			// Fades the correction out near vertical, where the rotation axis degenerates.
			const float C = Saturate(MapRange(Axis.y, 1.0f, 0.95f, 0.0f, 1.0f));
			const float CurveUpSegment = AttractionUp * std::abs(Declination * std::sin(Declination)) / CurveResolution;

			const Vector3 RotationAxis(-Axis.z, 0.0f, Axis.x);
			if (RotationAxis.length_squared() > 1e-12f)
			{
				Rot = QRotateAxisAngle(RotationAxis.normalized(), CurveUpSegment * C) * Rot;
			}
		}
	}

	/** Weber-Penn section 4.7: a static wind pose, thin branches bending most. */
	void AddWindSway(
		const GenerationContext& Context,
		float Radius,
		float Length,
		int32_t CurveRes,
		const Vector3& Position,
		Quaternion& Rot)
	{
		if (Context.WindStrength <= 0.0f)
		{
			return;
		}

		// GetWindDirection() is a constant 0 in the sample, i.e. wind blows along -X.
		const Vector3 WindDir(-1.0f, 0.0f, 0.0f);

		const Vector3 ZAxis = QGetZ(Rot);
		const float D = ZAxis.dot(WindDir);
		const float FullAngle = SafeAcos(D);

		const Vector3 RotAxis = ZAxis.cross(WindDir);
		if (RotAxis.length_squared() <= 1e-12f)
		{
			return;
		}

		const float C = Saturate(MapRange(D, -1.0f, -0.95f, 0.0f, 1.0f));
		const float Angle = FullAngle * Context.WindStrength * 0.003f / std::fmax(Radius, 0.03f) * C / float(CurveRes);

		const float Frequency = std::fmin(2.0f / std::sqrt(std::fmax(Length, 1e-4f)), 5.0f);
		const Vector2 NoisePos = Vector2(Position.x, Position.z) * 0.75f
			- Vector2(WindDir.x, WindDir.z) * (Context.WindTime * Frequency * std::fmax(0.8f, Context.WindStrength * 0.06f));
		const float ForcePhase = 0.4f + Random::PerlinNoise2D(NoisePos);

		Rot = QRotateAxisAngle(RotAxis.normalized(), Angle * ForcePhase) * Rot;
	}

	/** Weber-Penn section 4.3: how far a child tilts away from its parent's axis. */
	Quaternion GetChildDownRotation(int32_t Level, const TreeParams& Params, uint32_t Seed, float Ratio)
	{
		const int32_t NextLevelClamped = std::min(Level + 1, MAX_STEM_LEVEL);

		const float DownAngleV = Params.DownAngleV[NextLevelClamped];
		float DownAngle = DownAngleV * Random::Value(Seed, 1998);

		// A negative variance is a flag: spread the tilt along the parent instead of randomly.
		if (HasSignBit(DownAngleV))
		{
			DownAngle *= (1.0f - 2.0f * ShapeRatio(0, Ratio));
		}
		DownAngle += Params.DownAngle[NextLevelClamped];

		return QRotateX(ToRadians(DownAngle));
	}

	/** Weber-Penn section 4.3: where around the parent a child sits. */
	float GetChildParentZAngle(int32_t Level, const TreeParams& Params, uint32_t Seed, uint32_t BranchIndex)
	{
		const int32_t NextLevelClamped = std::min(Level + 1, MAX_STEM_LEVEL);

		const float Rotate = Params.Rotate[NextLevelClamped];
		const float RotateV = Params.RotateV[NextLevelClamped];

		if (!HasSignBit(Rotate))
		{
			// Spiral phyllotaxis: each child advances by a fixed angle.
			const float Angle = RotateV * Random::SignedValue(Seed, 50);

			return ToRadians(float(BranchIndex) * Rotate + Angle);
		}

		// A negative rotate is a flag for alternating (opposite-pair) arrangement.
		return float(BitSign(BranchIndex, 0)) * ToRadians(180.0f - Rotate + RotateV * Random::SignedValue(Seed, 50));
	}

	/** Rotates a fruit-bearing stalk towards straight down, proportional to fruit weight. */
	void AddFruitWeight(Quaternion& ChildRotation, float P)
	{
		const Vector3 ChildZ = QGetZ(ChildRotation);

		// Equivalent to cross(childZ, (0,-1,0)); zero when the stalk already hangs straight down.
		const Vector3 Axis(ChildZ.z, 0.0f, -ChildZ.x);
		if (Axis.length_squared() <= 1e-12f)
		{
			return;
		}

		const float Angle = SafeAcos(-ChildZ.y);
		ChildRotation = QRotateAxisAngle(Axis.normalized(), Angle * P) * ChildRotation;
	}

	bool IsLeafBlossom(const TreeParams& Params, uint32_t StemChildIndex)
	{
		const int32_t Total = Params.Blossom.Count + Params.Leaf.Count;
		if (Total <= 0)
		{
			return false;
		}

		return Random::Value(StemChildIndex) < (float(Params.Blossom.Count) / float(Total));
	}

	/** Tracks one branch as it is repeatedly cloned by splits. */
	struct CloneState
	{
		Vector3 Pos;
		Quaternion Rot;
		float SplitCorrection = 0.0f;
		/** GPU thread budget. Splitting subdivides it, which is what caps the clone count. */
		int32_t ThreadCount = 0;
		bool bIsOriginal = true;
	};
} // namespace

// ==================== TreeSkeleton ====================

void TreeSkeleton::Generate(const TreeParams& InParams, const GenerationContext& InContext, uint32_t InSeed)
{
	Params = InParams;
	Context = InContext;
	Context.LeafDensity = Clamp(Context.LeafDensity, 0.0001f, 1.0f);
	Params.Levels = ClampInt(Params.Levels, 1, MAX_STEM_LEVEL + 1);

	Segments.clear();
	Leaves.clear();
	Fruits.clear();
	FoliageCount = 0;
	bSegmentsTruncated = false;
	bFoliageTruncated = false;

	StemRecord Root;
	Root.Pos = Vector3(0, 0, 0);
	// Stems grow along their local +Z, so the trunk's frame is rotated to face world up.
	Root.Rot = QRotateX(TREE_PI * -0.5f);
	Root.Seed = InSeed;
	Root.AoDistance = 0.0f;
	Root.Scale = Params.Scale + 0.5f * Params.ScaleV * Random::SignedValue(InSeed, 2413);
	Root.Length = Root.Scale * (Params.Length[0] + Params.LengthV[0] * Random::SignedValue(InSeed, 123));
	Root.Radius = Root.Length * Params.Ratio;
	Root.Children = (Params.Levels == 1)
		? uint32_t(std::max(0, Params.Leaf.Count + Params.Blossom.Count))
		: uint32_t(std::max(0, Params.Branches[1]));

	GenerateStem(Root, 0);
}

void TreeSkeleton::GenerateStem(const StemRecord& In, int32_t Level)
{
	if (Level > MAX_STEM_LEVEL || In.Length <= 0.0f)
	{
		return;
	}
	if (int32_t(Segments.size()) >= Context.MaxSegments)
	{
		bSegmentsTruncated = true;
		return;
	}

	SegmentInfo Si;
	Si.Level = Level;
	Si.Length = In.Length;
	Si.Radius = In.Radius;
	Si.FromZ = 0.0f;
	Si.ToZ = 0.0f;

	const float LengthBase = Params.BaseSize[0] * In.Scale;
	const int32_t NextLevelClamped = std::min(Level + 1, Params.Levels - 1);
	const bool bIsLeafLevel = (Level == Params.Levels - 1);

	const float CurveResolution = Clamp(float(Params.CurveRes[Level]), 1.0f, 32.0f);
	const float SegmentSplits = Params.SegSplits[Level];

	const uint32_t Children = std::min(In.Children, MAX_CHILD_RECORDS);

	float ChildDensity = 1.0f;
	float ChildScale = 1.0f;
	if (bIsLeafLevel)
	{
		// The GPU thins leaves by camera distance; here it is an explicit quality knob.
		ChildDensity = Context.LeafDensity;
		ChildScale = Clamp(1.0f / std::sqrt(ChildDensity), 1.0f, 5.0f);
	}

	const float ZOffset = Params.BaseSize[Level];
	const float ChildStepfDelta = (Children > 0)
		? (CurveResolution * (1.0f - ZOffset)) / float(Children)
		: 0.0f;
	const float FirstChildStepf = CurveResolution * ZOffset + ChildStepfDelta * 0.5f;

	const Vector3 StartPos = In.Pos;

	std::vector<CloneState> Clones;
	Clones.reserve(STEM_THREAD_GROUP_SIZE);
	Clones.push_back(CloneState{ StartPos, In.Rot, 0.0f, int32_t(STEM_THREAD_GROUP_SIZE), true });

	std::vector<CloneState> SplitScratch;
	std::vector<Vector3> PrePos;
	std::vector<Quaternion> PreRot;
	std::vector<Vector3> PostPos;
	std::vector<Quaternion> PostRot;

	float SplitError = -Params.SegSplitBaseOffset[Level];
	uint32_t DrawOutputCount = 0;
	uint32_t ChildOutputCount = 0;

	const int32_t StepCount = int32_t(CurveResolution);
	for (int32_t Step = 0; Step < StepCount; ++Step)
	{
		Si.FromZ = Si.ToZ;
		Si.ToZ = SegmentInfo::QuantizeZ(float(Step + 1) / CurveResolution);

		const float StepTSize = (Si.ToZ - Si.FromZ) * CurveResolution;

		// --- Weber-Penn section 4.2, stage 1: decide how many clones this step spawns ---
		int32_t NumSplits = std::max(0, int32_t(RoundNE(SegmentSplits + SplitError)));
		if (Step == 0)
		{
			NumSplits += Params.BaseSplits[Level];
		}
		// Error diffusion keeps the average split count on target for fractional SegSplits.
		SplitError -= float(NumSplits) - SegmentSplits;
		NumSplits += 1;

		const bool bHasSplits = NumSplits > 1;

		if (bHasSplits)
		{
			// Each clone's thread budget is subdivided; new clones are inserted directly after
			// their parent so that clone order keeps matching thread order, which is how the
			// GPU derives its clone index (a wave prefix count over thread indices).
			SplitScratch.clear();
			for (const CloneState& Parent : Clones)
			{
				const int32_t ThreadsPerSplit = std::max(1, Parent.ThreadCount / NumSplits);
				const int32_t ThreadSurplus = std::max(Parent.ThreadCount - NumSplits * ThreadsPerSplit, 0);

				CloneState Original = Parent;
				Original.ThreadCount = ThreadsPerSplit + ThreadSurplus;
				Original.bIsOriginal = true;
				SplitScratch.push_back(Original);

				const int32_t NumNewClones = (Parent.ThreadCount - Original.ThreadCount) / ThreadsPerSplit;
				for (int32_t NewClone = 0; NewClone < NumNewClones; ++NewClone)
				{
					CloneState Clone = Parent;
					Clone.ThreadCount = ThreadsPerSplit;
					Clone.bIsOriginal = false;
					SplitScratch.push_back(Clone);
				}
			}
			Clones.swap(SplitScratch);
		}

		const size_t CloneCount = Clones.size();
		PrePos.resize(CloneCount);
		PreRot.resize(CloneCount);
		PostPos.resize(CloneCount);
		PostRot.resize(CloneCount);

		const uint32_t StepSeed = Random::CombineSeed(In.Seed, uint32_t(Step));

		for (size_t CloneIndex = 0; CloneIndex < CloneCount; ++CloneIndex)
		{
			CloneState& Clone = Clones[CloneIndex];
			const uint32_t SegmentSeed = Random::CombineSeed(StepSeed, uint32_t(CloneIndex));

			PrePos[CloneIndex] = Clone.Pos;
			PreRot[CloneIndex] = Clone.Rot;

			// --- Weber-Penn section 4.1 ---
			if (Params.CurveV[Level] >= 0.0f)
			{
				const float StemCurve = GetStemCurve(Params, Level, CurveResolution, SegmentSeed, Si.FromZ);
				Clone.Rot = Clone.Rot * QRotateX(ToRadians(StemCurve + Clone.SplitCorrection));
			}
			else
			{
				// A negative curve variance means "helix" rather than "random".
				Clone.Rot = Clone.Rot * QRotateX(ToRadians(std::abs(Params.CurveV[Level])) / CurveResolution);
				Clone.Rot = Clone.Rot * QRotateZ(ToRadians(360.0f) / CurveResolution);
			}

			// --- Weber-Penn section 4.2, stage 2 ---
			if (bHasSplits && !Clone.bIsOriginal)
			{
				AddSplitSpread(Si, Params, SegmentSeed, Step, Clone.Rot, Clone.SplitCorrection);
			}

			AddVerticalAttraction(Level, Params, Context, CurveResolution, Clone.Rot);

			if (!bHasSplits)
			{
				AddWindSway(Context, Si.Radius, Si.Length, int32_t(CurveResolution), StartPos, Clone.Rot);
			}

			const float SegmentLength = (Si.ToZ - Si.FromZ) * Si.Length;
			Clone.Pos += QGetZ(Clone.Rot) * SegmentLength;

			PostPos[CloneIndex] = Clone.Pos;
			PostRot[CloneIndex] = Clone.Rot;
		}

		// --- Emit drawable segments ---
		const float SegmentLength = (Si.ToZ - Si.FromZ) * Si.Length;
		uint32_t ActiveClones = 0;
		for (size_t CloneIndex = 0; CloneIndex < CloneCount; ++CloneIndex)
		{
			if (DrawOutputCount + uint32_t(CloneIndex) >= MAX_SEGMENT_RECORDS)
			{
				break;
			}
			if (int32_t(Segments.size()) >= Context.MaxSegments)
			{
				bSegmentsTruncated = true;
				break;
			}

			StemSegment Segment;
			Segment.FromPos = PrePos[CloneIndex];
			Segment.FromRot = PreRot[CloneIndex];
			Segment.ToPos = PostPos[CloneIndex];
			Segment.ToRot = PostRot[CloneIndex];
			Segment.Si = Si;
			Segment.AoDistance = In.AoDistance + Si.Length - SegmentLength * float(Step);

			Segments.push_back(Segment);
			++ActiveClones;
		}
		DrawOutputCount += ActiveClones;

		if (Children == 0 || ActiveClones == 0)
		{
			continue;
		}

		// --- Emit children (branches, leaves or fruit) attached to this step ---
		for (int32_t ChildIteration = 0; ChildIteration < CHILD_ITERATIONS; ++ChildIteration)
		{
			// One entry per simulated thread, gathered before ChildOutputCount advances so
			// that indices match the GPU's wave-wide evaluation.
			uint32_t PendingIndex[STEM_THREAD_GROUP_SIZE];
			float PendingScale[STEM_THREAD_GROUP_SIZE];
			uint32_t PendingCount = 0;

			for (uint32_t Lane = 0; Lane < STEM_THREAD_GROUP_SIZE; ++Lane)
			{
				uint32_t StemChildIndex = 0;
				float StemChildScale = 0.0f;
				GetNthChildIndexAndScale(ChildOutputCount + Lane, Children, ChildDensity, StemChildIndex, StemChildScale);

				const float LocalChildStepf = FirstChildStepf + float(StemChildIndex) * ChildStepfDelta;

				const bool bHasChildInStep = (StemChildScale != 0.0f)
					&& (std::floor(LocalChildStepf) == float(Step))
					&& ((ChildOutputCount + Lane) < Children);

				if (bHasChildInStep)
				{
					PendingIndex[PendingCount] = StemChildIndex;
					PendingScale[PendingCount] = StemChildScale;
					++PendingCount;
				}
			}

			if (PendingCount == 0)
			{
				break;
			}
			ChildOutputCount += PendingCount;

			for (uint32_t Pending = 0; Pending < PendingCount; ++Pending)
			{
				const uint32_t StemChildIndex = PendingIndex[Pending];
				const float StemChildScale = PendingScale[Pending];

				const float LocalChildStepf = FirstChildStepf + float(StemChildIndex) * ChildStepfDelta;
				const float T = (StepTSize > 0.0f) ? (Frac(LocalChildStepf) / StepTSize) : 0.0f;
				// Quantising z keeps child placement stable across curve resolutions.
				const float Z = SegmentInfo::QuantizeZ(LocalChildStepf / CurveResolution);

				const size_t ChildCloneIndex = std::min(size_t(StemChildIndex % ActiveClones), CloneCount - 1);
				const uint32_t ChildSeed = Random::CombineSeed(StemChildIndex, In.Seed) & 0x7FFFFFu;

				const float LengthDenominator = Si.Length - LengthBase;
				const float Ratio = (std::abs(LengthDenominator) > 1e-6f)
					? (Si.Length * (1.0f - Z)) / LengthDenominator
					: 0.0f;

				const Quaternion DownAngleRot = GetChildDownRotation(Level, Params, ChildSeed, Ratio);
				const float RadiusParent = GetTaperedRadius(Si, Params.Taper[Level], Params.Flare, Z);
				const float ParentZRotAngle = GetChildParentZAngle(Level, Params, ChildSeed, StemChildIndex);

				const Vector3 ChildPosition = StemSpline(
					PrePos[ChildCloneIndex], QGetZ(PreRot[ChildCloneIndex]),
					PostPos[ChildCloneIndex], QGetZ(PostRot[ChildCloneIndex]), T);
				const Quaternion RotParent = QSlerp(PreRot[ChildCloneIndex], PostRot[ChildCloneIndex], T);
				const Vector3 ParentZ = QGetZ(RotParent);

				if (bIsLeafLevel)
				{
					if (FoliageCount >= Context.MaxLeaves)
					{
						bFoliageTruncated = true;
						break;
					}

					const bool bIsBlossom = IsLeafBlossom(Params, ChildSeed);
					const float FruitProgress = GetSeasonFruitProgress(ChildSeed, Context.Season);
					const float FruitScale = GetFruitScale(FruitProgress);
					const bool bIsFruit = bIsBlossom
						&& (Random::Value(ChildSeed, StemChildIndex) < Params.Fruit.Chance)
						&& (FruitProgress > 0.0f);

					const LeafParams& LeafP = bIsBlossom ? Params.Blossom : Params.Leaf;

					const float BaseScale = LeafP.Scale
						* ShapeRatio(LeafP.ScaleShape, 1.0f - Z)
						* ChildScale * StemChildScale;

					float FinalScale;
					if (LeafP.bIsNeedle || LeafP.bEvergreen)
					{
						// Needles and broadleaf evergreens persist through the year.
						FinalScale = BaseScale;
					}
					else if (bIsFruit)
					{
						FinalScale = ChildScale * StemChildScale * Params.Fruit.Size * FruitScale;
					}
					else
					{
						FinalScale = BaseScale * GetSeasonLeafScale(ChildSeed, bIsBlossom, Context.Season);
					}

					// Size variation, so a canopy is not built from identical stamps.
					if (!bIsFruit && LeafP.ScaleJitter > 0.0f)
					{
						FinalScale *= std::fmax(
							0.0f, 1.0f + LeafP.ScaleJitter * Random::SignedValue(ChildSeed, 0x5CA1E));
					}

					Quaternion ChildRotation = QRotateAxisAngle(ParentZ, ParentZRotAngle) * (RotParent * DownAngleRot);

					if (bIsFruit)
					{
						AddFruitWeight(ChildRotation, Params.Fruit.DownForce * FruitScale);
					}

					// Push the leaf out to the surface of the parent's tube, plus its petiole.
					const Vector3 ChildZ = QGetZ(ChildRotation);
					const float D = Clamp(ChildZ.dot(ParentZ), 0.05f, 0.95f);
					const Vector3 Offset = ChildZ * (RadiusParent / std::sqrt(1.0f - D * D) + LeafP.StemLen);

					LeafInstance Instance;
					Instance.Position = ChildPosition + Offset;
					Instance.Seed = ChildSeed;
					Instance.Scale = FinalScale;
					Instance.AoDistance = In.AoDistance + Si.Length * (1.0f - Z);
					Instance.bIsBlossom = bIsBlossom;

					if (!bIsFruit)
					{
						AddWindSway(Context, 0.03f, LeafP.Scale, 1, Instance.Position, ChildRotation);
					}
					Instance.Rotation = ChildRotation;

					if (FinalScale > 0.0f)
					{
						++FoliageCount;

						if (bIsFruit)
						{
							Fruits.push_back(Instance);
						}
						else
						{
							Leaves.push_back(Instance);
						}
					}
				}
				else
				{
					StemRecord Child;
					Child.Pos = ChildPosition;
					Child.Rot = QRotateAxisAngle(ParentZ, ParentZRotAngle) * (RotParent * DownAngleRot);
					Child.Scale = In.Scale;
					Child.Seed = ChildSeed;
					Child.AoDistance = In.AoDistance + Si.Length * (1.0f - Z);

					const float ChildLengthMax = Params.Length[NextLevelClamped]
						+ Random::SignedValue(ChildSeed, 89) * Params.LengthV[NextLevelClamped];
					// The trunk distributes child length by height; deeper levels by depth along the parent.
					const float Shape = ShapeRatio(Params.Shape[Level], Level == 0 ? Ratio : (1.0f - 0.6f * Z));
					const float ChildLength = ChildLengthMax * (Si.Length * Shape);

					Child.Length = std::fmax(0.0f, ChildLength);

					const float LengthFraction = (Si.Length > 1e-6f) ? std::fmax(0.0f, ChildLength / Si.Length) : 0.0f;
					Child.Radius = std::fmin(RadiusParent * 0.9f, In.Radius * std::pow(LengthFraction, Params.RatioPower));

					if ((Level + 2) == Params.Levels)
					{
						// Grandchildren are leaves; their count follows the canopy shape.
						const float LeafCount = float(std::abs(Params.Leaf.Count) + std::abs(Params.Blossom.Count))
							* ShapeRatio(Params.Shape[NextLevelClamped], 1.0f - Z);
						Child.Children = uint32_t(std::max(0, int32_t(LeafCount)));
					}
					else
					{
						const float Grandchildren = float(Params.Branches[std::min(Level + 2, Params.Levels - 1)]);
						const float Count = (Level == 0)
							? Grandchildren * (0.2f + 0.8f * LengthFraction / std::fmax(ChildLengthMax, 1e-6f))
							: Grandchildren * (1.0f - 0.5f * Z);
						Child.Children = uint32_t(std::max(0, int32_t(Count)));
					}

					GenerateStem(Child, Level + 1);
				}
			}

			if (PendingCount < STEM_THREAD_GROUP_SIZE)
			{
				break;
			}
		}
	}
}
