#include "TreeGen/TreeLeafOutline.h"

#include "TreeGen/TreeMath.h"

#include <algorithm>
#include <cmath>

using namespace TreeGen;
using godot::Vector2;

namespace
{
	Vector2 QuadraticBezierPoint(const Vector2& P0, const Vector2& P1, const Vector2& P2, float T)
	{
		const float U = 1.0f - T;

		return P0 * (U * U) + P1 * (2.0f * U * T) + P2 * (T * T);
	}
} // namespace

void TreeGen::ComputeLeafOutlinePoints(
	const LeafOutlineShape& Shape, uint32_t Seed, Vector2 OutPoints[9])
{
	const float BotAngle = ToRadians(Shape.BotAngle);
	const float MidAngle = ToRadians(Shape.MidAngle);
	const float TopAngle = ToRadians(Shape.TopAngle) + 0.2f * Random::SignedValue(Seed, 222);

	const Vector2 Waypoint[3] = {
		Vector2(0.0f, 0.0f),
		Vector2(-0.5f + 0.1f * Random::SignedValue(Seed, 92),
			Shape.SideOffset + 0.1f * Random::SignedValue(Seed, 29)),
		Vector2(0.0f, 1.0f)
	};
	const Vector2 Tangent[3] = {
		Vector2(std::sin(BotAngle), std::cos(BotAngle)),
		Vector2(std::sin(MidAngle), std::cos(MidAngle)),
		Vector2(std::sin(TopAngle), std::cos(TopAngle))
	};

	// Rows 0 and 2 evaluate the Hermite curve at t = 0 and t = 0.5; rows 1 and 3 are the
	// equivalent Bezier handles.
	static const float BlendRow[4][4] = {
		{ 1.0f, 0.0f, 0.0f, 0.0f },
		{ 1.0f, 0.25f, 0.0f, 0.0f },
		{ 0.5f, 0.125f, 0.5f, 0.125f },
		{ 0.0f, 0.0f, 1.0f, 0.25f }
	};

	for (int32_t InHalf = 0; InHalf < 9; ++InHalf)
	{
		int32_t IsSecond = (InHalf > 3) ? 1 : 0;
		const float ChordLength = IsSecond
			? Waypoint[1].distance_to(Waypoint[2])
			: Waypoint[0].distance_to(Waypoint[1]);
		IsSecond += (InHalf > 7) ? 1 : 0;

		const int32_t Start = std::min(IsSecond, 2);
		const int32_t End = std::min(IsSecond + 1, 2);

		const Vector2 Control[4] = {
			Waypoint[Start],
			Tangent[Start] * ChordLength,
			Waypoint[End],
			Tangent[End] * -ChordLength
		};

		const float* Row = BlendRow[InHalf % 4];
		OutPoints[InHalf] = Control[0] * Row[0] + Control[1] * Row[1]
			+ Control[2] * Row[2] + Control[3] * Row[3];
	}
}

void TreeGen::BuildLeafCutout(
	const LeafOutlineShape& Shape,
	uint32_t Seed,
	int32_t ArcSegments,
	float Aspect,
	std::vector<Vector2>& OutPoints,
	std::vector<uint32_t>& OutTris)
{
	OutPoints.clear();
	OutTris.clear();

	Vector2 Outline[9];
	ComputeLeafOutlinePoints(Shape, Seed, Outline);

	const int32_t Segments = std::max(1, ArcSegments);

	// One half of the blade: base, around the widest point, to the tip.
	std::vector<Vector2> Half;
	Half.reserve(size_t(Segments * 4 + 1));
	Half.push_back(Outline[0]);
	for (int32_t Arc = 0; Arc < 4; ++Arc)
	{
		const Vector2& P0 = Outline[Arc * 2 + 0];
		const Vector2& P1 = Outline[Arc * 2 + 1];
		const Vector2& P2 = Outline[Arc * 2 + 2];

		for (int32_t Step = 1; Step <= Segments; ++Step)
		{
			Half.push_back(QuadraticBezierPoint(P0, P1, P2, float(Step) / float(Segments)));
		}
	}

	// The generator works in a space where x is the half-width (negative outward) and y runs 0 at
	// the base to 1 at the tip. A cutout wants [0,1]^2 with u = 0.5 on the midrib, so fold x by the
	// requested aspect and keep y as v.
	const float HalfAspect = std::fmax(Aspect, 0.02f) * 0.5f;
	const auto ToUv = [HalfAspect](const Vector2& Local, float SideSign) -> Vector2
	{
		return Vector2(0.5f + SideSign * -Local.x * 2.0f * HalfAspect,
			std::fmin(std::fmax(Local.y, 0.0f), 1.0f));
	};

	// Closed ring: up the right side, back down the left, skipping the duplicated base and tip.
	for (size_t Index = 0; Index < Half.size(); ++Index)
	{
		OutPoints.push_back(ToUv(Half[Index], 1.0f));
	}
	for (size_t Index = Half.size() - 1; Index >= 1; --Index)
	{
		if (Index == Half.size() - 1)
		{
			// Tip is shared by both sides.
			continue;
		}
		OutPoints.push_back(ToUv(Half[Index], -1.0f));
	}

	if (OutPoints.size() < 3)
	{
		OutPoints.clear();
		return;
	}

	// Fan from the base. Star-shaped about it, so every triangle lies inside the outline; skip
	// degenerate ones rather than emitting zero-area geometry into the leaf card.
	for (size_t Index = 1; Index + 1 < OutPoints.size(); ++Index)
	{
		const Vector2& A = OutPoints[0];
		const Vector2& B = OutPoints[Index];
		const Vector2& C = OutPoints[Index + 1];
		const float Cross = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
		if (std::abs(Cross) < 1e-9f)
		{
			continue;
		}

		// Wind consistently so the card's two faces stay coherent once SlowTree maps these onto
		// the leaf plane.
		OutTris.push_back(0);
		if (Cross > 0.0f)
		{
			OutTris.push_back(uint32_t(Index));
			OutTris.push_back(uint32_t(Index + 1));
		}
		else
		{
			OutTris.push_back(uint32_t(Index + 1));
			OutTris.push_back(uint32_t(Index));
		}
	}
}
