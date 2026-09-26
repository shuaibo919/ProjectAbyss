#include "AncientBuilding/RoofCurve.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>

using namespace BuildingGen;

namespace
{
	const float ROOF_EPSILON = 1e-6f;
	const int32_t ROOF_MAX_DEPTH = 8;

	/** Mean rise ratio of the ramp; the integral's normalising denominator. */
	float MeanRatio(const BuildingSpec& Spec)
	{
		return (Spec.EaveRiseRatio + Spec.RidgeRiseRatio) * 0.5f;
	}

	/** 2D distance from Q to the line through P0 and P1. */
	float LineDistance(const Vector2& Q, const Vector2& P0, const Vector2& P1)
	{
		const Vector2 Chord = P1 - P0;
		const float Length = Chord.length();
		if (Length <= ROOF_EPSILON)
		{
			return Q.distance_to(P0);
		}

		return std::abs(Chord.x * (Q.y - P0.y) - Chord.y * (Q.x - P0.x)) / Length;
	}
} // namespace

Vector2 BuildingGen::RoofCurvePoint(const BuildingSpec& Spec, float HalfSpan, float Rise, float T)
{
	const float Mean = MeanRatio(Spec);

	// Degenerate ramps (zero mean, e.g. a and b cancelling) fall back to a flat profile, which
	// is what the legacy normalisation degenerates to as well (its scale hits 0).
	if (std::abs(Mean) <= ROOF_EPSILON || Rise <= 0.0f)
	{
		return Vector2(HalfSpan * (1.0f - T), 0.0f);
	}

	const float Y = Rise
		* (Spec.EaveRiseRatio * T + (Spec.RidgeRiseRatio - Spec.EaveRiseRatio) * T * T * 0.5f)
		/ Mean;

	return Vector2(HalfSpan * (1.0f - T), Y);
}

Vector2 BuildingGen::RoofCurveTangent(const BuildingSpec& Spec, float HalfSpan, float Rise, float T)
{
	const float Mean = MeanRatio(Spec);
	if (std::abs(Mean) <= ROOF_EPSILON || Rise <= 0.0f)
	{
		return Vector2(-1.0f, 0.0f);
	}

	const float DY = Rise * (Spec.EaveRiseRatio + (Spec.RidgeRiseRatio - Spec.EaveRiseRatio) * T) / Mean;

	return Vector2(-HalfSpan, DY).normalized();
}

Vector2 BuildingGen::RoofCurveNormal(const BuildingSpec& Spec, float HalfSpan, float Rise, float T)
{
	const float Mean = MeanRatio(Spec);
	if (std::abs(Mean) <= ROOF_EPSILON || Rise <= 0.0f)
	{
		return Vector2(0.0f, 1.0f);
	}

	// Perpendicular to the tangent, chosen so the span component points away from the ridge and
	// the up component is positive: (dy, -dx) with dx = -HalfSpan.
	const float DY = Rise * (Spec.EaveRiseRatio + (Spec.RidgeRiseRatio - Spec.EaveRiseRatio) * T) / Mean;

	return Vector2(DY, HalfSpan).normalized();
}

Vector2 BuildingGen::RoofCurveNormalAtX(const BuildingSpec& Spec, float HalfSpan, float Rise, float X)
{
	const float T = (HalfSpan > ROOF_EPSILON) ? std::clamp(1.0f - X / HalfSpan, 0.0f, 1.0f) : 0.0f;

	return RoofCurveNormal(Spec, HalfSpan, Rise, T);
}

std::vector<Vector2> BuildingGen::SampleRoofCurve(const BuildingSpec& Spec, float HalfSpan, float Rise)
{
	const float ChordError = std::fmax(Spec.RoofChordError, 0.0005f);
	const float MaxSegment = std::fmax(Spec.RoofMaxSegment, 0.05f);

	std::vector<Vector2> Out;
	Out.reserve(64);
	Out.push_back(RoofCurvePoint(Spec, HalfSpan, Rise, 0.0f));

	struct Job
	{
		float T0;
		float T1;
		int32_t Depth;
	};

	std::vector<Job> Stack;
	Stack.push_back(Job{ 0.0f, 1.0f, 0 });

	bool bWarned = false;
	while (!Stack.empty())
	{
		const Job Current = Stack.back();
		Stack.pop_back();

		const Vector2 P0 = RoofCurvePoint(Spec, HalfSpan, Rise, Current.T0);
		const Vector2 P1 = RoofCurvePoint(Spec, HalfSpan, Rise, Current.T1);

		// Deviation at the 1/4, 1/2 and 3/4 points of the segment — a midpoint-only check can
		// miss a curve that wanders off and back, and the quarter points cost nothing extra.
		float MaxDev = 0.0f;
		for (const float F : { 0.25f, 0.5f, 0.75f })
		{
			const float T = Current.T0 + (Current.T1 - Current.T0) * F;
			MaxDev = std::fmax(MaxDev, LineDistance(RoofCurvePoint(Spec, HalfSpan, Rise, T), P0, P1));
		}

		const float ChordLength = P0.distance_to(P1);
		const bool bNeedsSplit = MaxDev > ChordError || ChordLength > MaxSegment;

		if (bNeedsSplit && Current.Depth < ROOF_MAX_DEPTH)
		{
			const float TM = (Current.T0 + Current.T1) * 0.5f;
			Stack.push_back(Job{ TM, Current.T1, Current.Depth + 1 });
			Stack.push_back(Job{ Current.T0, TM, Current.Depth + 1 });
		}
		else
		{
			if (bNeedsSplit && !bWarned)
			{
				bWarned = true;
				WARN_PRINT_ONCE(
					"SampleRoofCurve hit the recursion depth cap with residual chord error; "
					"the output does not satisfy roof_chord_error at this quality.");
			}
			Out.push_back(P1);
		}
	}

	return Out;
}

size_t BuildingGen::RoofCoverageStart(
	const BuildingSpec& Spec, const std::vector<Vector2>& Profile, float HalfSpan)
{
	const float Coverage = std::clamp(Spec.TileCoverage, 0.0f, 1.0f);

	if (Spec.RoofCurveMode != 1 || Profile.empty())
	{
		// Legacy: Cr is measured in profile nodes, from the ridge down.
		return size_t(std::floor(float(Profile.size() - 1) * (1.0f - Coverage)));
	}

	// Continuous: Cr is a curve parameter — the span coordinate at which tiles stop, from the
	// ridge out to HalfSpan * coverage. Sampling density no longer moves this edge.
	const float KeepX = HalfSpan * Coverage + ROOF_EPSILON;
	for (size_t Index = 0; Index < Profile.size(); ++Index)
	{
		if (Profile[Index].x <= KeepX)
		{
			return Index;
		}
	}

	return Profile.size() - 1;
}

bool BuildingGen::RoofCoverageBoundary(
	const BuildingSpec& Spec, const std::vector<Vector2>& Profile, float HalfSpan,
	size_t Start, Vector2& OutBoundary)
{
	if (Spec.RoofCurveMode != 1 || Start == 0 || Start >= Profile.size())
	{
		return false;
	}

	const float Coverage = std::clamp(Spec.TileCoverage, 0.0f, 1.0f);
	if (Coverage <= 0.0f || Coverage >= 1.0f)
	{
		return false;
	}

	const Vector2& High = Profile[Start - 1];
	const Vector2& Low = Profile[Start];
	const float Span = High.x - Low.x;
	if (Span <= ROOF_EPSILON)
	{
		return false;
	}

	OutBoundary = High + (Low - High) * ((High.x - HalfSpan * Coverage) / Span);

	// A boundary that lands on the first kept sample is not a boundary, just the sample.
	return (OutBoundary - Low).length() > ROOF_EPSILON;
}
