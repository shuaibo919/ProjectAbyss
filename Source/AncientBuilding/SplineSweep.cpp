#include "AncientBuilding/SplineSweep.h"

#include <algorithm>
#include <cmath>

using namespace BuildingGen;

namespace
{
	const float SWEEP_PI = 3.14159265358979323846f;

	/** Below this a dot product counts as zero, i.e. the miter plane is parallel to the segment. */
	const float MITER_EPSILON = 1e-3f;
	/** How far a vertex may travel relative to the segment before the joint counts as degenerate. */
	const float MAX_MITER_STRETCH = 8.0f;

	const float LENGTH_EPSILON = 1e-6f;

	/** Any unit vector perpendicular to N, chosen without a branch on N's dominant axis. */
	Vector3 AnyPerpendicular(const Vector3& N)
	{
		const Vector3 Candidate = (std::abs(N.x) < 0.9f) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
		const Vector3 Result = Candidate - N * Candidate.dot(N);

		return (Result.length_squared() > LENGTH_EPSILON) ? Result.normalized() : Vector3(0, 0, 1);
	}

	/**
	 * Rotates Up by the minimal rotation carrying FromTangent onto ToTangent. Transporting the
	 * bitangent this way instead of re-deriving it from a fixed world up is what keeps a
	 * curving ridge from twisting.
	 */
	Vector3 ParallelTransport(const Vector3& Up, const Vector3& FromTangent, const Vector3& ToTangent)
	{
		const Vector3 Axis = FromTangent.cross(ToTangent);
		const float AxisLength = Axis.length();

		if (AxisLength < LENGTH_EPSILON)
		{
			// Tangents are parallel (or exactly opposed, where no minimal rotation exists).
			return Up;
		}

		const float Angle = std::atan2(AxisLength, FromTangent.dot(ToTangent));
		const Vector3 Rotated = Up.rotated(Axis / AxisLength, Angle);

		// Re-orthogonalise against drift accumulated over many knots.
		const Vector3 Orthogonal = Rotated - ToTangent * Rotated.dot(ToTangent);

		return (Orthogonal.length_squared() > LENGTH_EPSILON) ? Orthogonal.normalized() : AnyPerpendicular(ToTangent);
	}

	/** Per-knot frame. The paper's N, T and B. */
	struct KnotFrame
	{
		Vector3 Tangent;
		Vector3 Normal;
		Vector3 Bitangent;
	};

	/**
	 * Builds the per-knot frames. The tangent at an interior knot is the bisector of its two
	 * segments, which is what makes the plane in equation 1 a miter plane rather than a butt
	 * joint.
	 */
	void BuildFrames(
		const std::vector<Vector3>& Knots,
		const Vector3& UpReference,
		std::vector<KnotFrame>& OutFrames,
		std::vector<Vector3>& OutSegmentDirections)
	{
		const size_t KnotCount = Knots.size();

		OutSegmentDirections.resize(KnotCount - 1);
		for (size_t Index = 0; Index + 1 < KnotCount; ++Index)
		{
			const Vector3 Delta = Knots[Index + 1] - Knots[Index];
			OutSegmentDirections[Index] = (Delta.length_squared() > LENGTH_EPSILON)
				? Delta.normalized()
				: Vector3(0, 0, 1);
		}

		OutFrames.resize(KnotCount);

		for (size_t Index = 0; Index < KnotCount; ++Index)
		{
			Vector3 Tangent;
			if (Index == 0)
			{
				Tangent = OutSegmentDirections.front();
			}
			else if (Index + 1 == KnotCount)
			{
				Tangent = OutSegmentDirections.back();
			}
			else
			{
				const Vector3 Bisector = OutSegmentDirections[Index - 1] + OutSegmentDirections[Index];
				// A perfect reversal has no bisector; fall back to carrying on straight.
				Tangent = (Bisector.length_squared() > LENGTH_EPSILON)
					? Bisector.normalized()
					: OutSegmentDirections[Index];
			}

			OutFrames[Index].Tangent = Tangent;
		}

		// Seed the bitangent from the reference up, then transport it.
		Vector3 Up = UpReference - OutFrames[0].Tangent * UpReference.dot(OutFrames[0].Tangent);
		Up = (Up.length_squared() > LENGTH_EPSILON) ? Up.normalized() : AnyPerpendicular(OutFrames[0].Tangent);

		for (size_t Index = 0; Index < KnotCount; ++Index)
		{
			if (Index > 0)
			{
				Up = ParallelTransport(Up, OutFrames[Index - 1].Tangent, OutFrames[Index].Tangent);
			}

			OutFrames[Index].Bitangent = Up;
			// (N, B, T) is right-handed, so contour x maps to N and contour y to B.
			const Vector3 Normal = Up.cross(OutFrames[Index].Tangent);
			OutFrames[Index].Normal = (Normal.length_squared() > LENGTH_EPSILON)
				? Normal.normalized()
				: AnyPerpendicular(OutFrames[Index].Tangent);
		}
	}

	float SampleDisplacement(const std::vector<float>& Samples, float T)
	{
		if (Samples.size() < 2)
		{
			return 0.0f;
		}

		const float Clamped = std::fmin(std::fmax(T, 0.0f), 1.0f);
		const float Scaled = Clamped * float(Samples.size() - 1);
		const size_t Low = size_t(Scaled);
		const size_t High = std::min(Low + 1, Samples.size() - 1);
		const float Fraction = Scaled - float(Low);

		return Samples[Low] + (Samples[High] - Samples[Low]) * Fraction;
	}
} // namespace

bool BuildingGen::BuildSweep(
	const std::vector<Vector3>& Knots,
	const SweepSettings& Settings,
	SweepResult& OutResult)
{
	OutResult = SweepResult();

	const size_t KnotCount = Knots.size();
	const size_t ContourCount = Settings.Contour.size();
	if (KnotCount < 2 || ContourCount < 2)
	{
		return false;
	}

	std::vector<KnotFrame> Frames;
	std::vector<Vector3> SegmentDirections;
	BuildFrames(Knots, Settings.UpReference, Frames, SegmentDirections);

	// ---- Propagate the contour along the spline (equation 1, or the naive alternative) ----

	// Ring-major: Rings[Knot * ContourCount + Contour].
	std::vector<Vector3> Rings(KnotCount * ContourCount);

	for (size_t J = 0; J < ContourCount; ++J)
	{
		const Vector2& Point = Settings.Contour[J];
		Rings[J] = Knots[0] + Frames[0].Normal * Point.x + Frames[0].Bitangent * Point.y;
	}

	if (Settings.Mode == ESweepMode::Frame)
	{
		// Re-place the contour in every knot's own frame. Cheap, and wrong at sharp corners.
		for (size_t I = 1; I < KnotCount; ++I)
		{
			for (size_t J = 0; J < ContourCount; ++J)
			{
				const Vector2& Point = Settings.Contour[J];
				Rings[I * ContourCount + J] =
					Knots[I] + Frames[I].Normal * Point.x + Frames[I].Bitangent * Point.y;
			}
		}
	}
	else
	{
		for (size_t I = 0; I + 1 < KnotCount; ++I)
		{
			const Vector3& Direction = SegmentDirections[I];
			const Vector3& NextTangent = Frames[I + 1].Tangent;
			const Vector3& NextPoint = Knots[I + 1];

			const float Denominator = NextTangent.dot(Direction);
			const float SegmentLength = Knots[I + 1].distance_to(Knots[I]);
			const float MaxTravel = std::fmax(SegmentLength, LENGTH_EPSILON) * MAX_MITER_STRETCH;

			// A near-zero denominator means the turn approaches 180 degrees and t diverges.
			// The paper does not mention this; clamping keeps the mesh finite and reports it.
			const bool bDegenerate = std::abs(Denominator) < MITER_EPSILON;
			if (bDegenerate)
			{
				++OutResult.DegenerateJointCount;
			}

			for (size_t J = 0; J < ContourCount; ++J)
			{
				const Vector3& Current = Rings[I * ContourCount + J];

				float Travel;
				if (bDegenerate)
				{
					Travel = SegmentLength;
				}
				else
				{
					Travel = NextTangent.dot(NextPoint - Current) / Denominator;
					Travel = std::fmin(std::fmax(Travel, -MaxTravel), MaxTravel);
				}

				if (SegmentLength > LENGTH_EPSILON)
				{
					OutResult.MaxMiterStretch = std::fmax(
						OutResult.MaxMiterStretch, std::abs(Travel) / SegmentLength);
				}

				Rings[(I + 1) * ContourCount + J] = Current + Direction * Travel;
			}
		}
	}

	// ---- Displacement curve and direction constraints (equations 2-5) ----

	const bool bHasDisplacement = Settings.DisplacementScale != 0.0f && Settings.DisplacementSamples.size() >= 2;
	if (bHasDisplacement)
	{
		const float ConstraintAngle = Settings.ConstraintAngleDegrees * (SWEEP_PI / 180.0f);
		const bool bDisplaceRadially = ConstraintAngle > SWEEP_PI * 0.5f;

		for (size_t I = 0; I < KnotCount; ++I)
		{
			// The paper indexes y by knot number as i/n; i/(n-1) is used here so the curve's
			// far end actually lands on the last knot. The difference shows only at the ends.
			const float Parameter = float(I) / float(KnotCount - 1);
			const float Amount = Settings.DisplacementScale * SampleDisplacement(Settings.DisplacementSamples, Parameter);
			if (Amount == 0.0f)
			{
				continue;
			}

			for (size_t J = 0; J < ContourCount; ++J)
			{
				Vector3& Vertex = Rings[I * ContourCount + J];

				const Vector3 Radial = Vertex - Knots[I];
				const float RadialLength = Radial.length();
				if (RadialLength < LENGTH_EPSILON)
				{
					continue;
				}
				const Vector3 RadialDirection = Radial / RadialLength;

				// Equation 5: keep only the vertices within delta of the bitangent.
				const float Alpha = std::acos(
					std::fmin(std::fmax(Frames[I].Bitangent.dot(RadialDirection), -1.0f), 1.0f));
				if (Alpha > ConstraintAngle)
				{
					continue;
				}

				// Equation 4.
				const Vector3 Direction = bDisplaceRadially ? RadialDirection : Frames[I].Bitangent;
				Vertex += Direction * Amount;
			}
		}
	}

	// ---- Triangulate ----

	// Arc length along the spline and around the contour, for UVs.
	std::vector<float> SplineU(KnotCount, 0.0f);
	for (size_t I = 1; I < KnotCount; ++I)
	{
		SplineU[I] = SplineU[I - 1] + Knots[I].distance_to(Knots[I - 1]);
	}
	const float SplineLength = std::fmax(SplineU.back(), LENGTH_EPSILON);

	const size_t EdgeCount = Settings.bClosedContour ? ContourCount : ContourCount - 1;

	std::vector<float> ContourV(EdgeCount + 1, 0.0f);
	for (size_t E = 0; E < EdgeCount; ++E)
	{
		const Vector2& From = Settings.Contour[E];
		const Vector2& To = Settings.Contour[(E + 1) % ContourCount];
		ContourV[E + 1] = ContourV[E] + From.distance_to(To);
	}
	const float ContourLength = std::fmax(ContourV.back(), LENGTH_EPSILON);

	OutResult.Vertices.reserve(EdgeCount * KnotCount * 2);
	OutResult.Normals.reserve(EdgeCount * KnotCount * 2);
	OutResult.UVs.reserve(EdgeCount * KnotCount * 2);
	OutResult.Indices.reserve(EdgeCount * (KnotCount - 1) * 6);

	// One strip per contour edge, so the profile's corners stay hard.
	for (size_t E = 0; E < EdgeCount; ++E)
	{
		const size_t J0 = E;
		const size_t J1 = (E + 1) % ContourCount;
		const int32_t StripBase = int32_t(OutResult.Vertices.size());

		for (size_t I = 0; I < KnotCount; ++I)
		{
			const Vector3& A = Rings[I * ContourCount + J0];
			const Vector3& B = Rings[I * ContourCount + J1];

			// Along-strip direction, one-sided at the ends.
			const size_t Prev = (I == 0) ? I : I - 1;
			const size_t Next = (I + 1 == KnotCount) ? I : I + 1;
			const Vector3 AlongSpline =
				(Rings[Next * ContourCount + J0] + Rings[Next * ContourCount + J1]) -
				(Rings[Prev * ContourCount + J0] + Rings[Prev * ContourCount + J1]);

			Vector3 Normal = AlongSpline.cross(B - A);
			Normal = (Normal.length_squared() > LENGTH_EPSILON) ? Normal.normalized() : Frames[I].Normal;

			const float V = SplineU[I] / SplineLength;

			OutResult.Vertices.push_back(A);
			OutResult.Normals.push_back(Normal);
			OutResult.UVs.push_back(Vector2(ContourV[E] / ContourLength, V));

			OutResult.Vertices.push_back(B);
			OutResult.Normals.push_back(Normal);
			OutResult.UVs.push_back(Vector2(ContourV[E + 1] / ContourLength, V));
		}

		for (size_t I = 0; I + 1 < KnotCount; ++I)
		{
			const int32_t I00 = StripBase + int32_t(I) * 2;
			const int32_t I01 = I00 + 1;
			const int32_t I10 = I00 + 2;
			const int32_t I11 = I00 + 3;

			// Wound so the side facing `Normal` is Godot's front face.
			OutResult.Indices.push_back(I00);
			OutResult.Indices.push_back(I10);
			OutResult.Indices.push_back(I01);

			OutResult.Indices.push_back(I01);
			OutResult.Indices.push_back(I10);
			OutResult.Indices.push_back(I11);
		}
	}

	// ---- Caps ----

	if (Settings.bGenerateCaps && Settings.bClosedContour)
	{
		for (int32_t End = 0; End < 2; ++End)
		{
			const size_t I = (End == 0) ? 0 : KnotCount - 1;
			// Outward is away from the body of the sweep at each end.
			const Vector3 Normal = (End == 0) ? -Frames[I].Tangent : Frames[I].Tangent;

			Vector3 Centre(0, 0, 0);
			for (size_t J = 0; J < ContourCount; ++J)
			{
				Centre += Rings[I * ContourCount + J];
			}
			Centre /= float(ContourCount);

			const int32_t CentreIndex = int32_t(OutResult.Vertices.size());
			OutResult.Vertices.push_back(Centre);
			OutResult.Normals.push_back(Normal);
			OutResult.UVs.push_back(Vector2(0.5f, 0.5f));

			for (size_t J = 0; J < ContourCount; ++J)
			{
				OutResult.Vertices.push_back(Rings[I * ContourCount + J]);
				OutResult.Normals.push_back(Normal);
				OutResult.UVs.push_back(Vector2(
					0.5f + 0.5f * Settings.Contour[J].x, 0.5f + 0.5f * Settings.Contour[J].y));
			}

			for (size_t J = 0; J < ContourCount; ++J)
			{
				const int32_t A = CentreIndex + 1 + int32_t(J);
				const int32_t B = CentreIndex + 1 + int32_t((J + 1) % ContourCount);

				if (End == 0)
				{
					OutResult.Indices.push_back(CentreIndex);
					OutResult.Indices.push_back(B);
					OutResult.Indices.push_back(A);
				}
				else
				{
					OutResult.Indices.push_back(CentreIndex);
					OutResult.Indices.push_back(A);
					OutResult.Indices.push_back(B);
				}
			}
		}
	}

	return true;
}
