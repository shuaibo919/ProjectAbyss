#include "AncientBuilding/Masonry.h"

#include <algorithm>
#include <cmath>

using namespace BuildingGen;

namespace
{
	const float MASONRY_PI = 3.14159265358979323846f;
	const float MASONRY_EPSILON = 1e-5f;

	/** Local (along, up, through) → world, for one slab. */
	struct SlabFrame
	{
		Vector3 Origin;
		Vector3 U;
		Vector3 W;

		Vector3 At(float Along, float Up, float Through) const
		{
			return Origin + U * Along + Vector3(0.0f, Up, 0.0f) + W * Through;
		}

		Vector3 Dir(float Along, float Up, float Through) const
		{
			return U * Along + Vector3(0.0f, Up, 0.0f) + W * Through;
		}
	};

	/**
	 * One face strip between two (possibly battered) vertical edges, split at the plinth line so
	 * the foot band can carry its own tint. EdgeA/EdgeB give the along-coordinate at a height.
	 */
	template <typename TEdgeA, typename TEdgeB, typename TThrough>
	void AddFaceStrip(MeshAccumulator& Mesh, const SlabFrame& Frame, const TEdgeA& EdgeA, const TEdgeB& EdgeB,
		const TThrough& Through, float Bottom, float Top, const Vector3& Normal, const ArchedSlabDesc& Desc)
	{
		const auto Emit = [&Mesh, &Frame, &EdgeA, &EdgeB, &Through, &Normal](float Y0, float Y1, const Color& Tint)
		{
			if (Y1 - Y0 < MASONRY_EPSILON)
			{
				return;
			}
			const Vector3 A = Frame.At(EdgeA(Y0), Y0, Through(Y0));
			const Vector3 B = Frame.At(EdgeB(Y0), Y0, Through(Y0));
			const Vector3 C = Frame.At(EdgeB(Y1), Y1, Through(Y1));
			const Vector3 D = Frame.At(EdgeA(Y1), Y1, Through(Y1));
			Mesh.AddQuadOriented(A, B, C, D, Normal, Tint);
		};

		const float Split = std::clamp(Desc.PlinthHeight, Bottom, Top);
		if (Desc.PlinthHeight > MASONRY_EPSILON)
		{
			Emit(Bottom, Split, Desc.PlinthTint);
			Emit(Split, Top, Desc.Tint);
		}
		else
		{
			Emit(Bottom, Top, Desc.Tint);
		}
	}

	/** Outward in-plane normals along an arch polyline (away from the opening). */
	std::vector<Vector2> CurveOutward(const std::vector<Vector2>& Curve, const ArchOpening& Opening)
	{
		std::vector<Vector2> Out(Curve.size());
		for (size_t Index = 0; Index < Curve.size(); ++Index)
		{
			const Vector2 Prev = Curve[Index > 0 ? Index - 1 : Index];
			const Vector2 Next = Curve[std::min(Index + 1, Curve.size() - 1)];
			Vector2 Tangent = Next - Prev;
			Vector2 Normal(-Tangent.y, Tangent.x);
			if (Normal.length_squared() < 1e-12f)
			{
				Normal = Vector2(0.0f, 1.0f);
			}
			Normal = Normal.normalized();
			// Away from the opening's spring-line centre.
			const Vector2 Away = Curve[Index] - Vector2(Opening.Centre, Opening.Spring);
			if (Normal.dot(Away) < 0.0f)
			{
				Normal = -Normal;
			}
			Out[Index] = Normal;
		}

		return Out;
	}
} // namespace

std::vector<Vector2> BuildingGen::ArchCurve(const ArchOpening& Opening, int32_t Segments)
{
	const float Half = Opening.Width * 0.5f;
	const float C = Opening.Centre;
	const float S = Opening.Spring;
	const int32_t Count = std::max(Segments, 2);

	std::vector<Vector2> Curve;
	switch (Opening.Profile)
	{
		case EArchProfile::Flat:
			// The lintel line; Rise is the depth of the lintel course above it.
			Curve.push_back(Vector2(C - Half, S));
			Curve.push_back(Vector2(C + Half, S));
			break;

		case EArchProfile::Pointed:
		{
			// 双心券: each half is an arc centred on the far side of the centre line. The radius is
			// solved from the requested rise so the two arcs meet exactly at the apex:
			// r² = (r - Half)² + Rise²  →  r = (Half² + Rise²) / (2 Half).
			const float Rise = std::fmax(Opening.Rise, Half * 1.001f);
			const float Radius = (Half * Half + Rise * Rise) / (2.0f * Half);
			const float LeftCentre = C - Half + Radius;
			const float Apex = std::asin(std::clamp(Rise / Radius, 0.0f, 1.0f));
			for (int32_t Index = 0; Index <= Count / 2; ++Index)
			{
				// Left arc from angle π (the spring) round to π - Apex (the apex).
				const float T = float(Index) / float(Count / 2);
				const float Angle = MASONRY_PI - T * Apex;
				Curve.push_back(Vector2(LeftCentre + Radius * std::cos(Angle), S + Radius * std::sin(Angle)));
			}
			// Mirror for the right half, skipping the shared apex.
			const size_t LeftCount = Curve.size();
			for (size_t Index = LeftCount - 1; Index-- > 0;)
			{
				const Vector2 P = Curve[Index];
				Curve.push_back(Vector2(2.0f * C - P.x, P.y));
			}
			break;
		}

		default:
			for (int32_t Index = 0; Index <= Count; ++Index)
			{
				const float Angle = MASONRY_PI * (1.0f - float(Index) / float(Count));
				Curve.push_back(Vector2(C + Half * std::cos(Angle), S + Opening.Rise * std::sin(Angle)));
			}
			break;
	}

	return Curve;
}

void BuildingGen::AddArchedSlab(MeshAccumulator& Mesh, const ArchedSlabDesc& Desc)
{
	if (Desc.Height <= MASONRY_EPSILON || Desc.HalfLength <= MASONRY_EPSILON || Desc.HalfThickness <= MASONRY_EPSILON)
	{
		return;
	}

	Mesh.SetSlot(EMaterialSlot::Stone);

	SlabFrame Frame;
	Frame.Origin = Desc.Origin;
	Frame.U = Desc.AxisU.normalized();
	Frame.W = Desc.AxisW.normalized();

	const float H = Desc.Height;
	const auto HalfLengthAt = [&Desc](float Y) { return Desc.HalfLength - Desc.BatterEnds * Y; };
	const auto HalfThicknessAt = [&Desc](float Y) { return Desc.HalfThickness - Desc.BatterFaces * Y; };

	// Openings that fit under the top and inside the battered ends, left to right.
	std::vector<ArchOpening> Openings;
	for (const ArchOpening& Opening : Desc.Openings)
	{
		const float Top = Opening.Spring + Opening.Rise;
		const float Half = Opening.Width * 0.5f;
		if (Opening.Width <= MASONRY_EPSILON || Top >= H - 0.05f || Opening.Sill >= Opening.Spring)
		{
			continue;
		}
		if (std::abs(Opening.Centre) + Half > HalfLengthAt(Top) - 0.05f)
		{
			continue;
		}
		Openings.push_back(Opening);
	}
	std::sort(Openings.begin(), Openings.end(),
		[](const ArchOpening& Left, const ArchOpening& Right) { return Left.Centre < Right.Centre; });

	// Arch polylines, shared by both faces, the intrados and the ring.
	std::vector<std::vector<Vector2>> Curves;
	for (const ArchOpening& Opening : Openings)
	{
		Curves.push_back(ArchCurve(Opening, 16));
	}

	// ---- The two faces: piers, under-sill strips, spandrels ----
	for (int32_t Side = -1; Side <= 1; Side += 2)
	{
		const float S = float(Side);
		const auto Through = [&HalfThicknessAt, S](float Y) { return S * HalfThicknessAt(Y); };
		const Vector3 Normal = Frame.Dir(0.0f, Desc.BatterFaces, S).normalized();

		float Cursor = 0.0f;
		bool bCursorIsEnd = true;
		for (size_t Index = 0; Index <= Openings.size(); ++Index)
		{
			const bool bLast = Index == Openings.size();
			const float Right = bLast ? 0.0f : Openings[Index].Centre - Openings[Index].Width * 0.5f;
			const float CursorValue = Cursor;
			const auto LeftEdge = [bCursorIsEnd, CursorValue, &HalfLengthAt](float Y)
			{
				return bCursorIsEnd ? -HalfLengthAt(Y) : CursorValue;
			};
			const auto RightEdge = [bLast, Right, &HalfLengthAt](float Y)
			{
				return bLast ? HalfLengthAt(Y) : Right;
			};
			AddFaceStrip(Mesh, Frame, LeftEdge, RightEdge, Through, 0.0f, H, Normal, Desc);

			if (bLast)
			{
				break;
			}

			const ArchOpening& Opening = Openings[Index];
			const float A = Opening.Centre - Opening.Width * 0.5f;
			const float B = Opening.Centre + Opening.Width * 0.5f;
			if (Opening.Sill > MASONRY_EPSILON)
			{
				AddFaceStrip(Mesh, Frame, [A](float) { return A; }, [B](float) { return B; },
					Through, 0.0f, Opening.Sill, Normal, Desc);
			}

			// Spandrel: every intrados segment rises to the top as one quad. The curve starts and
			// ends exactly on the jamb lines, so the piers either side close the face.
			const std::vector<Vector2>& Curve = Curves[Index];
			for (size_t K = 0; K + 1 < Curve.size(); ++K)
			{
				const Vector2& P0 = Curve[K];
				const Vector2& P1 = Curve[K + 1];
				Mesh.AddQuadOriented(
					Frame.At(P0.x, P0.y, Through(P0.y)), Frame.At(P1.x, P1.y, Through(P1.y)),
					Frame.At(P1.x, H, Through(H)), Frame.At(P0.x, H, Through(H)),
					Normal, Desc.Tint);
			}

			Cursor = B;
			bCursorIsEnd = false;
		}
	}

	// ---- Inside each opening: jambs, sill, intrados ----
	for (size_t Index = 0; Index < Openings.size(); ++Index)
	{
		const ArchOpening& Opening = Openings[Index];
		const float A = Opening.Centre - Opening.Width * 0.5f;
		const float B = Opening.Centre + Opening.Width * 0.5f;
		const float JambTop = Opening.Spring;

		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			const float X = Side < 0 ? A : B;
			const Vector3 Normal = Frame.Dir(-float(Side), 0.0f, 0.0f);
			Mesh.AddQuadOriented(
				Frame.At(X, Opening.Sill, -HalfThicknessAt(Opening.Sill)),
				Frame.At(X, Opening.Sill, HalfThicknessAt(Opening.Sill)),
				Frame.At(X, JambTop, HalfThicknessAt(JambTop)),
				Frame.At(X, JambTop, -HalfThicknessAt(JambTop)),
				Normal, Desc.Tint * 0.92f);
		}

		if (Opening.Sill > MASONRY_EPSILON)
		{
			const float Y = Opening.Sill;
			Mesh.AddQuadOriented(
				Frame.At(A, Y, -HalfThicknessAt(Y)), Frame.At(B, Y, -HalfThicknessAt(Y)),
				Frame.At(B, Y, HalfThicknessAt(Y)), Frame.At(A, Y, HalfThicknessAt(Y)),
				Vector3(0, 1, 0), Desc.Tint);
		}

		const std::vector<Vector2>& Curve = Curves[Index];
		for (size_t K = 0; K + 1 < Curve.size(); ++K)
		{
			const Vector2& P0 = Curve[K];
			const Vector2& P1 = Curve[K + 1];
			const Vector2 Mid = (P0 + P1) * 0.5f;
			// Faces into the opening, towards its spring-line centre (straight down for 平券).
			const Vector2 Inward = (Opening.Profile == EArchProfile::Flat)
				? Vector2(0.0f, -1.0f)
				: (Vector2(Opening.Centre, Opening.Spring) - Mid);
			Mesh.AddQuadOriented(
				Frame.At(P0.x, P0.y, HalfThicknessAt(P0.y)), Frame.At(P1.x, P1.y, HalfThicknessAt(P1.y)),
				Frame.At(P1.x, P1.y, -HalfThicknessAt(P1.y)), Frame.At(P0.x, P0.y, -HalfThicknessAt(P0.y)),
				Frame.Dir(Inward.x, Inward.y, 0.0f), Desc.Tint * 0.88f);
		}
	}

	// ---- Ends ----
	for (int32_t Side = -1; Side <= 1; Side += 2)
	{
		const float S = float(Side);
		const Vector3 Normal = Frame.Dir(S, Desc.BatterEnds, 0.0f).normalized();
		const auto Quad = [&](float Y0, float Y1, const Color& Tint)
		{
			if (Y1 - Y0 < MASONRY_EPSILON)
			{
				return;
			}
			Mesh.AddQuadOriented(
				Frame.At(S * HalfLengthAt(Y0), Y0, -HalfThicknessAt(Y0)),
				Frame.At(S * HalfLengthAt(Y0), Y0, HalfThicknessAt(Y0)),
				Frame.At(S * HalfLengthAt(Y1), Y1, HalfThicknessAt(Y1)),
				Frame.At(S * HalfLengthAt(Y1), Y1, -HalfThicknessAt(Y1)),
				Normal, Tint);
		};
		if (Desc.PlinthHeight > MASONRY_EPSILON)
		{
			const float Split = std::fmin(Desc.PlinthHeight, H);
			Quad(0.0f, Split, Desc.PlinthTint);
			Quad(Split, H, Desc.Tint);
		}
		else
		{
			Quad(0.0f, H, Desc.Tint);
		}
	}

	// ---- Top ----
	const float TopLength = HalfLengthAt(H);
	const float TopThickness = HalfThicknessAt(H);
	if (Desc.bTopCap)
	{
		Mesh.AddQuadOriented(
			Frame.At(-TopLength, H, -TopThickness), Frame.At(TopLength, H, -TopThickness),
			Frame.At(TopLength, H, TopThickness), Frame.At(-TopLength, H, TopThickness),
			Vector3(0, 1, 0), Desc.Tint * 1.04f);
	}

	// ---- 券脸: a dressed ring round each arch on both faces ----
	if (Desc.RingThickness > MASONRY_EPSILON && Desc.RingProjection > MASONRY_EPSILON)
	{
		for (size_t Index = 0; Index < Openings.size(); ++Index)
		{
			const ArchOpening& Opening = Openings[Index];
			const std::vector<Vector2>& Curve = Curves[Index];
			const std::vector<Vector2> Outward = CurveOutward(Curve, Opening);

			for (int32_t Side = -1; Side <= 1; Side += 2)
			{
				const float S = float(Side);
				std::vector<Vector3> Knots;
				for (size_t K = 0; K < Curve.size(); ++K)
				{
					const Vector2 P = Curve[K] + Outward[K] * (Desc.RingThickness * 0.5f);
					Knots.push_back(Frame.At(P.x, P.y, S * HalfThicknessAt(P.y)));
				}
				// A 平券 gets a lintel course that runs past the jambs on both sides.
				if (Opening.Profile == EArchProfile::Flat && Knots.size() == 2)
				{
					Knots.front() -= Frame.U * Desc.RingThickness;
					Knots.back() += Frame.U * Desc.RingThickness;
				}

				SweepSettings Settings;
				const float Half = Desc.RingThickness * 0.5f;
				Settings.Contour = {
					Vector2(-Half, -Desc.RingProjection * 0.25f), Vector2(Half, -Desc.RingProjection * 0.25f),
					Vector2(Half, Desc.RingProjection), Vector2(-Half, Desc.RingProjection) };
				Settings.bClosedContour = true;
				Settings.bGenerateCaps = true;
				Settings.UpReference = Frame.Dir(0.0f, Desc.BatterFaces, S).normalized();

				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					Mesh.AddSweep(Sweep, Desc.RingTint);
				}
			}
		}
	}

	// ---- Parapet on the top edges ----
	if (Desc.Parapet != EParapet::None && Desc.bTopCap)
	{
		// [自定] Metre-scale 垛口 set out: a 0.5 m 垛墙 base, 1.2 m merlons at 1.9 m centres.
		const float Thick = std::fmin(0.55f, TopThickness * 0.5f);
		const float Base = (Desc.Parapet == EParapet::Crenel) ? 0.5f : 1.05f;
		const Color Tint = Desc.Tint * 0.97f;
		struct Edge
		{
			float Along;
			float Through;
			bool bAlongU;
			float Length;
		};
		const Edge Edges[4] = {
			{ 0.0f, TopThickness - Thick * 0.5f, true, TopLength * 2.0f },
			{ 0.0f, -(TopThickness - Thick * 0.5f), true, TopLength * 2.0f },
			{ TopLength - Thick * 0.5f, 0.0f, false, (TopThickness - Thick) * 2.0f },
			{ -(TopLength - Thick * 0.5f), 0.0f, false, (TopThickness - Thick) * 2.0f },
		};
		for (const Edge& Run : Edges)
		{
			if (Run.Length <= MASONRY_EPSILON)
			{
				continue;
			}
			const Vector3 Along = Run.bAlongU ? Frame.U : Frame.W;
			const Vector3 Across = Run.bAlongU ? Frame.W : Frame.U;
			const Vector3 Centre = Frame.At(Run.Along, H, Run.Through);
			// AddOrientedBox takes the box centre.
			Mesh.AddOrientedBox(Centre + Vector3(0.0f, Base * 0.5f, 0.0f), Along, Vector3(0, 1, 0), Across,
				Vector3(Run.Length * 0.5f, Base * 0.5f, Thick * 0.5f), Tint);

			if (Desc.Parapet != EParapet::Crenel)
			{
				continue;
			}
			const float Pitch = 1.9f;
			const int32_t Count = std::max(int32_t(Run.Length / Pitch), 1);
			const float Step = Run.Length / float(Count);
			for (int32_t K = 0; K < Count; ++K)
			{
				const float T = -Run.Length * 0.5f + Step * (float(K) + 0.5f);
				Mesh.AddOrientedBox(Centre + Along * T + Vector3(0.0f, Base + 0.55f, 0.0f), Along,
					Vector3(0, 1, 0), Across, Vector3(std::fmin(0.6f, Step * 0.32f), 0.55f, Thick * 0.5f), Tint);
			}
		}
	}
}
