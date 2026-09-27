#include "AncientBuilding/BuildingBuilder.h"

#include "AncientBuilding/RoofCurve.h"
#include "AncientBuilding/TileSkin.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace BuildingGen;

namespace
{
	const float BUILD_PI = 3.14159265358979323846f;
	const float BUILD_TAU = 2.0f * BUILD_PI;
	const float BUILD_EPSILON = 1e-6f;

	/** Ridge tile: flat soffit, shoulders, rounded crown. */
	std::vector<Vector2> MakeRidgeContour(float Scale)
	{
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.5f, -0.10f) * Scale);
		Contour.push_back(Vector2(0.5f, -0.10f) * Scale);
		Contour.push_back(Vector2(0.5f, 0.30f) * Scale);
		Contour.push_back(Vector2(0.30f, 0.62f) * Scale);
		Contour.push_back(Vector2(0.0f, 0.75f) * Scale);
		Contour.push_back(Vector2(-0.30f, 0.62f) * Scale);
		Contour.push_back(Vector2(-0.5f, 0.30f) * Scale);

		return Contour;
	}

	/**
	 * The far tier's ridge: a placeholder block ( tier 2, "脊饰降为占位块").
	 *
	 * Same footprint and soffit as the profiled section, so the ridge still covers exactly what it
	 * covered — the silhouette at the distance the tier exists for is a bar with a shoulder, and the
	 * three intermediate contour points of the crown buy nothing there.
	 */
	std::vector<Vector2> MakeRidgeContourBlock(float Scale)
	{
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.5f, -0.10f) * Scale);
		Contour.push_back(Vector2(0.5f, -0.10f) * Scale);
		Contour.push_back(Vector2(0.5f, 0.30f) * Scale);
		Contour.push_back(Vector2(-0.5f, 0.30f) * Scale);

		return Contour;
	}

	// ---- 分层断面 (RidgeDetail = 1, 50_脊饰 §2) ----
	//
	// The three members the handbook names — 当沟条 at the bottom, 脊身 in the middle, 盖脊筒 on top —
	// as three bands separated by real ledges, so the silhouette reads as a stack of members rather
	// than as one half-round bar. No 雕花: at this project's scale the carving is the material's job
	// (user decision 2026-09-27), and the mesh only has to get the 剪影/断面层次 right.
	//
	// Project proportions, not historical measurements: keep the tile-covering foot at
	// +/-0.50 and -0.10, recess the body to +/-0.29, and use a circular coping (r=0.29).
	// The main crown stays at 0.75. Verge profiles reduce positive heights by 30%, keeping
	// the full-width foot and its soffit for the existing tile clipping contract.

	/** Fine section: 23 points, including an eight-facet circular crown. */
	std::vector<Vector2> MakeRidgeContourTiered()
	{
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.50f, -0.10f));   // soffit, left
		Contour.push_back(Vector2(0.50f, -0.10f));    // soffit, right
		Contour.push_back(Vector2(0.50f, 0.035f));    // broad, thin tile-covering foot
		Contour.push_back(Vector2(0.46f, 0.09f));
		Contour.push_back(Vector2(0.29f, 0.09f));     // recessed masonry body
		Contour.push_back(Vector2(0.29f, 0.36f));
		Contour.push_back(Vector2(0.34f, 0.39f));     // restrained coping lip
		Contour.push_back(Vector2(0.34f, 0.43f));
		// Circular crown, eight facets with shared shading normals. No carved microgeometry.
		for (int32_t I = 0; I <= 8; ++I)
		{
			const float Angle = BUILD_PI * float(I) / 8.0f;
			Contour.push_back(Vector2(0.29f * std::cos(Angle), 0.46f + 0.29f * std::sin(Angle)));
		}
		for (int32_t I = 7; I >= 2; --I)
		{
			Contour.push_back(Vector2(-Contour[I].x, Contour[I].y));
		}

		return Contour;
	}

	/** Mid section: 17 points; removes small mouldings, preserves the crown and ornament seats. */
	std::vector<Vector2> MakeRidgeContourTieredMid()
	{
		std::vector<Vector2> Contour = MakeRidgeContourTiered();
		// Remove mirrored lower bevel / upper lip, in descending index order.
		for (int32_t I : { 21, 18, 17, 7, 6, 3 }) { Contour.erase(Contour.begin() + I); }
		return Contour;
	}

	/**
	 * The far tier of the 分层 section: one block, 6 points (and the legacy far-tier block stays
	 * what it is at RidgeDetail 0 — that tier's bytes are not this feature's to change).
	 *
	 * Unlike the legacy block it keeps the crown at 0.75 and its chamfer lies outside every finer
	 * tier's outer surface, so a 脊饰 seated at the finest tier's height is still buried in the crown
	 * here instead of floating over a low bar.
	 */
	std::vector<Vector2> MakeRidgeContourTieredBlock()
	{
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.50f, -0.10f));
		Contour.push_back(Vector2(0.50f, -0.10f));
		Contour.push_back(Vector2(0.50f, 0.44f));
		Contour.push_back(Vector2(0.30f, 0.75f));
		Contour.push_back(Vector2(-0.30f, 0.75f));
		Contour.push_back(Vector2(-0.50f, 0.44f));

		return Contour;
	}

	/** The finest section this spec's 脊断面档 builds, at unit scale. */
	std::vector<Vector2> RidgeDetailContour(const BuildingSpec& Spec, bool bVerge = false)
	{
		std::vector<Vector2> Contour = Spec.RidgeDetail >= 1 ? MakeRidgeContourTiered() : MakeRidgeContour(1.0f);
		if (Spec.RidgeDetail >= 1 && bVerge)
		{
			for (Vector2& P : Contour) { if (P.y > 0.0f) { P.y *= 0.70f; } }
		}
		return Contour;
	}

	/**
	 * Height of a section's outer surface at a given half-width, in contour units: the highest point
	 * of the outline at that |x|.
	 *
	 * Taken as the upper envelope over every edge rather than as a walk away from the crown, because
	 * the contour is listed as a loop: from the crown the next entry is the far side, and a walk that
	 * only ever increases x finds nothing there and answers "the crown" for every width — which seats
	 * a 脊饰 on the apex and leaves its whole base hovering over the flanks.
	 *
	 * Past the widest point the section has no surface at that width, so the height of the widest
	 * point is returned: a piece wider than the ridge sinks to the ridge's own shoulder, the only
	 * place its base can meet material at all.
	 */
	float RidgeSurfaceAt(const std::vector<Vector2>& Contour, float HalfWidth)
	{
		const size_t Count = Contour.size();
		if (Count < 3)
		{
			return 0.0f;
		}

		const float Width = std::fabs(HalfWidth);
		float Best = -std::numeric_limits<float>::max();
		float WidestX = -1.0f;
		float WidestY = -std::numeric_limits<float>::max();

		for (size_t Index = 0; Index < Count; ++Index)
		{
			const Vector2& A = Contour[Index];
			const Vector2& B = Contour[(Index + 1) % Count];

			// Both sides, so the contour's winding does not enter into it.
			for (int32_t Side = 0; Side <= 1; ++Side)
			{
				const float Sign = Side ? 1.0f : -1.0f;
				const float AX = A.x * Sign;
				const float BX = B.x * Sign;
				const float Low = std::fmin(AX, BX);
				const float High = std::fmax(AX, BX);

				if (Width < Low - BUILD_EPSILON || Width > High + BUILD_EPSILON)
				{
					continue;
				}
				if (High - Low <= BUILD_EPSILON)
				{
					// A vertical wall at exactly this width: its top end is the surface there.
					Best = std::fmax(Best, std::fmax(A.y, B.y));
					continue;
				}

				const float T = (Width - Low) / (High - Low);
				const float Y = (AX < BX) ? (A.y + (B.y - A.y) * T) : (B.y + (A.y - B.y) * T);
				Best = std::fmax(Best, Y);
			}

			const float AbsX = std::fabs(A.x);
			if (AbsX > WidestX)
			{
				WidestX = AbsX;
				WidestY = A.y;
			}
			else if (AbsX == WidestX)
			{
				WidestY = std::fmax(WidestY, A.y);
			}
		}

		return (Best > -std::numeric_limits<float>::max()) ? Best : WidestY;
	}

	/**
	 * Side of the placeholder cube for one ornament class, in modules D (0 when the class is off).
	 * [自定] Sizes have no source (06:1172 records the gap); they are the project's answer to
	 * "a 脊饰 that reads as a separate block at the acceptance distance, on the ridge it sits on".
	 */
	float OrnamentSizeFor(const BuildingSpec& Spec, ERidgeOrnamentKind Kind)
	{
		switch (Kind)
		{
		case ERidgeOrnamentKind::Finial:
			return Spec.Module * std::fmax(Spec.RidgeFinialSize, 0.0f);
		case ERidgeOrnamentKind::Beast:
			return Spec.Module * std::fmax(Spec.RidgeBeastSize, 0.0f);
		default:
			return Spec.Module * std::fmax(Spec.RidgeWalkerSize, 0.0f);
		}
	}

	/** Upward normal of a knot polyline at one knot, in the vertical plane the polyline lies in. */
	Vector3 KnotNormalUp(const std::vector<Vector3>& Knots, size_t Index)
	{
		const Vector3 Before = Knots[Index > 0 ? Index - 1 : 0];
		const Vector3 After = Knots[Index + 1 < Knots.size() ? Index + 1 : Knots.size() - 1];

		const Vector3 Along = After - Before;
		const Vector3 PlaneNormal = Along.cross(Vector3(0.0f, 1.0f, 0.0f));
		if (Along.length_squared() < 1e-12f || PlaneNormal.length_squared() < 1e-12f)
		{
			return Vector3(0.0f, 1.0f, 0.0f);
		}

		const Vector3 Normal = PlaneNormal.cross(Along).normalized();

		return Normal.y < 0.0f ? -Normal : Normal;
	}


	/**
	 * 连檐: the timber board the tile courses die onto at the eave.
	 *
	 * This used to be a fat bar standing in for the whole 滴水 course. Now that the tile skin hangs
	 * a real 滴水 from every channel and caps every barrel with a 瓦当, the bar's only remaining job
	 * is to be the board behind them — so it is slim, and TuckedEaveKnot drops it clear of the
	 * tiles instead of leaving it coincident with them, where it hid the lot.
	 */
	std::vector<Vector2> MakeEaveContour(float Scale)
	{
		std::vector<Vector2> Contour;
		Contour.push_back(Vector2(-0.5f, -0.16f) * Scale);
		Contour.push_back(Vector2(0.5f, -0.16f) * Scale);
		Contour.push_back(Vector2(0.5f, 0.16f) * Scale);
		Contour.push_back(Vector2(-0.5f, 0.16f) * Scale);

		return Contour;
	}

	/** Scale for the 连檐 board, slim enough to sit behind the eave tiles rather than over them. */
	float EaveBoardScale(const BuildingSpec& Spec)
	{
		return Spec.Module * 0.42f * Spec.RidgeScale;
	}

	/**
	 * Moves an eave knot down and back up the slope, so the board lands under the tile ends.
	 * OutwardZ is the sign of the eave's outward direction along Z, or 0 for an eave running along Z.
	 */
	Vector3 TuckedEaveKnot(const BuildingSpec& Spec, const Vector3& Knot, float OutwardZ, float OutwardX)
	{
		const float Drop = Spec.Module * 0.20f;
		const float Back = Spec.Module * 0.26f;

		return Knot + Vector3(-OutwardX * Back, -Drop, -OutwardZ * Back);
	}

	/** Even bay boundary positions across a span, inclusive of both ends. */
	std::vector<float> BayPositions(float HalfSpan, int32_t Bays)
	{
		const int32_t Count = std::max(Bays, 1);
		std::vector<float> Result;
		for (int32_t Index = 0; Index <= Count; ++Index)
		{
			Result.push_back(-HalfSpan + (2.0f * HalfSpan) * float(Index) / float(Count));
		}

		return Result;
	}

	/**
	 * Directions the balustrade is broken and stairs are placed, per Table 1's lambda.
	 * lambda = 0 gives the front only, 1 front and back, 2 all four sides.
	 */
	std::vector<float> CullingAngles(int32_t RunCount)
	{
		std::vector<float> Angles;
		for (int32_t Index = 0; Index < std::max(RunCount, 1); ++Index)
		{
			Angles.push_back(BUILD_TAU * float(Index) / float(std::max(RunCount, 1)));
		}

		return Angles;
	}
} // namespace

// ==================== MeshAccumulator ====================

Color MeshAccumulator::MottleColor(const Color& Tint)
{
	if (MottleAmount <= 0.0f)
	{
		return Tint;
	}

	// Splitmix-style hash on the running piece counter; no dependence on anything a
	// rebuild might reorder.
	uint32_t Hash = PieceCounter * 2654435761u;
	Hash ^= Hash >> 16;
	Hash *= 2246822519u;
	Hash ^= Hash >> 13;
	PieceCounter += 1;

	const float Frac = float(Hash & 0xFFFF) / 65535.0f;
	const float Scale = 1.0f + (Frac * 2.0f - 1.0f) * MottleAmount;
	return Color(
		std::clamp(Tint.r * Scale, 0.0f, 1.0f),
		std::clamp(Tint.g * Scale, 0.0f, 1.0f),
		std::clamp(Tint.b * Scale, 0.0f, 1.0f),
		Tint.a);
}

void MeshAccumulator::PushTriangle(int32_t A, int32_t B, int32_t C)
{
	Indices.push_back(A);
	Indices.push_back(B);
	Indices.push_back(C);
	TriangleSlots.push_back(uint8_t(CurrentSlot));
}

void MeshAccumulator::BuildSurfaces(std::vector<SurfaceData>& Out) const
{
	Out.clear();

	const size_t TriangleCount = size_t(GetTriangleCount());
	// Source vertex -> local index in the surface being built, or -1 when not carried over yet.
	std::vector<int32_t> Remap(Vertices.size(), -1);

	for (int32_t SlotIndex = 0; SlotIndex < MATERIAL_SLOT_COUNT; ++SlotIndex)
	{
		const EMaterialSlot Slot = EMaterialSlot(SlotIndex);

		SurfaceData Surface;
		Surface.Slot = Slot;
		// Only the entries this slot touched are reset, so the remap is O(vertices) once in total
		// rather than once per slot.
		std::vector<int32_t> Touched;

		for (size_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
		{
			if (TriangleSlots[Triangle] != uint8_t(Slot))
			{
				continue;
			}

			for (size_t Corner = 0; Corner < 3; ++Corner)
			{
				const size_t Source = size_t(Indices[Triangle * 3 + Corner]);
				if (Remap[Source] < 0)
				{
					Remap[Source] = int32_t(Surface.Vertices.size());
					Surface.Vertices.push_back(Vertices[Source]);
					Surface.Normals.push_back(Normals[Source]);
					Surface.UVs.push_back(UVs[Source]);
					Surface.Colors.push_back(Colors[Source]);
					Touched.push_back(int32_t(Source));
				}

				Surface.Indices.push_back(Remap[Source]);
			}
		}

		for (const int32_t Source : Touched)
		{
			Remap[size_t(Source)] = -1;
		}

		if (!Surface.Indices.empty())
		{
			Out.push_back(std::move(Surface));
		}
	}
}

int32_t MeshAccumulator::GetSlotTriangleCount(EMaterialSlot Slot) const
{
	int32_t Count = 0;
	for (const uint8_t Stamped : TriangleSlots)
	{
		if (Stamped == uint8_t(Slot))
		{
			++Count;
		}
	}

	return Count;
}

void MeshAccumulator::AddTriangle(const Vector3& A, const Vector3& B, const Vector3& C, const Color& Tint)
{
	const Vector3 Edge0 = B - A;
	const Vector3 Edge1 = C - A;
	Vector3 Normal = Edge0.cross(Edge1);
	if (Normal.length_squared() < 1e-14f)
	{
		return;
	}
	// Godot's front face is the side the cross product points away from.
	Normal = -Normal.normalized();

	const int32_t Base = int32_t(Vertices.size());
	const Vector3 Points[3] = { A, B, C };
	const Vector2 Coords[3] = { Vector2(0, 0), Vector2(1, 0), Vector2(1, 1) };

	for (int32_t Index = 0; Index < 3; ++Index)
	{
		Vertices.push_back(Points[Index]);
		Normals.push_back(Normal);
		UVs.push_back(Coords[Index]);
		Colors.push_back(Tint);
	}

	PushTriangle(Base, Base + 1, Base + 2);
}

void MeshAccumulator::AddQuad(
	const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, const Color& Tint)
{
	AddTriangle(A, B, C, Tint);
	AddTriangle(A, C, D, Tint);
}

void MeshAccumulator::AddBox(const Vector3& Centre, const Vector3& HalfExtents, const Color& Tint)
{
	const Vector3& H = HalfExtents;
	if (H.x <= 0.0f || H.y <= 0.0f || H.z <= 0.0f)
	{
		return;
	}

	// One mottle per component: the whole box reads as one material piece.
	const Color Col = MottleColor(Tint);

	const Vector3 P000 = Centre + Vector3(-H.x, -H.y, -H.z);
	const Vector3 P100 = Centre + Vector3(H.x, -H.y, -H.z);
	const Vector3 P110 = Centre + Vector3(H.x, H.y, -H.z);
	const Vector3 P010 = Centre + Vector3(-H.x, H.y, -H.z);
	const Vector3 P001 = Centre + Vector3(-H.x, -H.y, H.z);
	const Vector3 P101 = Centre + Vector3(H.x, -H.y, H.z);
	const Vector3 P111 = Centre + Vector3(H.x, H.y, H.z);
	const Vector3 P011 = Centre + Vector3(-H.x, H.y, H.z);

	// Clockwise when viewed from outside, matching Godot's front-face convention.
	// AddTriangle derives the shading normal from the NEGATED cross product;
	// counter-clockwise faces would turn both the culling and lighting inside out.
	AddQuad(P001, P011, P111, P101, Col);   // +Z
	AddQuad(P100, P110, P010, P000, Col);   // -Z
	AddQuad(P101, P111, P110, P100, Col);   // +X
	AddQuad(P000, P010, P011, P001, Col);   // -X
	AddQuad(P010, P110, P111, P011, Col);   // +Y
	AddQuad(P000, P001, P101, P100, Col);   // -Y
}

void MeshAccumulator::AddPolygon(const std::vector<Vector3>& Points, const Vector3& Normal, const Color& Tint)
{
	if (Points.size() < 3)
	{
		return;
	}

	const int32_t Base = int32_t(Vertices.size());
	const Color Col = MottleColor(Tint);

	for (const Vector3& Point : Points)
	{
		Vertices.push_back(Point);
		Normals.push_back(Normal);
		UVs.push_back(Vector2(Point.x, Point.y));
		Colors.push_back(Col);
	}

	// Fan, with each triangle wound against the supplied normal individually.
	//
	// Taking one winding decision for the whole fan — from the first three points — is only valid
	// for a convex outline. 山花 is not convex: the 举架 curve caves in, so once the fan passes the
	// ridge apex the triangles' own orientation reverses and half the tympanum was being culled.
	// That showed up as a solid missing triangle beside the gable, which reads as a hole in the
	// side of the roof.
	for (size_t Index = 1; Index + 1 < Points.size(); ++Index)
	{
		const Vector3 Reference = (Points[Index] - Points[0]).cross(Points[Index + 1] - Points[0]);
		if (Reference.length_squared() < 1e-14f)
		{
			// Degenerate sliver, which the duplicated apex vertex produces. Nothing to draw, and
			// its cross product carries no usable orientation.
			continue;
		}

		if (Reference.dot(Normal) > 0.0f)
		{
			PushTriangle(Base, Base + int32_t(Index + 1), Base + int32_t(Index));
		}
		else
		{
			PushTriangle(Base, Base + int32_t(Index), Base + int32_t(Index + 1));
		}
	}
}

void MeshAccumulator::AddSweep(const SweepResult& Sweep, const Color& Tint)
{
	const int32_t Base = int32_t(Vertices.size());

	// One mottle per sweep — a rail or a ridge reads as one piece, not per-vertex noise.
	const Color Col = MottleColor(Tint);

	Vertices.insert(Vertices.end(), Sweep.Vertices.begin(), Sweep.Vertices.end());
	Normals.insert(Normals.end(), Sweep.Normals.begin(), Sweep.Normals.end());
	UVs.insert(UVs.end(), Sweep.UVs.begin(), Sweep.UVs.end());
	Colors.insert(Colors.end(), Sweep.Vertices.size(), Col);

	// Sweeps are triangle lists, so stamping three at a time is the same order the index loop
	// used to emit.
	for (size_t Index = 0; Index + 2 < Sweep.Indices.size(); Index += 3)
	{
		PushTriangle(
			Base + Sweep.Indices[Index], Base + Sweep.Indices[Index + 1], Base + Sweep.Indices[Index + 2]);
	}
}

void MeshAccumulator::AddColumn(
	const Vector3& Base,
	float Height,
	float BottomRadius,
	float TopRadius,
	int32_t Sides,
	const Color& Tint, bool bSmooth, uint32_t ComponentId)
{
	const int32_t SideCount = std::clamp(Sides, 3, 128);
	if (Height <= 0.0f || BottomRadius <= 0.0f)
	{
		return;
	}

	// One mottle per column: the whole shaft reads as one timber.
	const Color Col = ComponentId ? ComponentColor(Tint, ComponentId) : MottleColor(Tint);
	if (bSmooth)
	{
		AddRevolvedProfile(Base, { Vector2(BottomRadius, 0.0f), Vector2(TopRadius, Height) }, SideCount, Col);
		return;
	}

	for (int32_t Index = 0; Index < SideCount; ++Index)
	{
		const float Angle0 = BUILD_TAU * float(Index) / float(SideCount);
		const float Angle1 = BUILD_TAU * float(Index + 1) / float(SideCount);

		const Vector3 B0 = Base + Vector3(BottomRadius * std::cos(Angle0), 0.0f, BottomRadius * std::sin(Angle0));
		const Vector3 B1 = Base + Vector3(BottomRadius * std::cos(Angle1), 0.0f, BottomRadius * std::sin(Angle1));
		const Vector3 T0 = Base + Vector3(TopRadius * std::cos(Angle0), Height, TopRadius * std::sin(Angle0));
		const Vector3 T1 = Base + Vector3(TopRadius * std::cos(Angle1), Height, TopRadius * std::sin(Angle1));

		AddQuad(B0, B1, T1, T0, Col);
	}
}

Color MeshAccumulator::ComponentColor(const Color& Tint, uint32_t ComponentId) const
{
	uint32_t Hash = ComponentId * 2654435761u;
	Hash ^= Hash >> 16;
	Hash *= 2246822519u;
	Hash ^= Hash >> 13;
	const float Scale = 1.0f + (float(Hash & 0xFFFF) / 65535.0f * 2.0f - 1.0f) * MottleAmount;
	return Color(std::clamp(Tint.r * Scale, 0.0f, 1.0f), std::clamp(Tint.g * Scale, 0.0f, 1.0f),
		std::clamp(Tint.b * Scale, 0.0f, 1.0f), Tint.a);
}

void MeshAccumulator::AddRevolvedProfile(const Vector3& Base, const std::vector<Vector2>& Profile,
	int32_t Sides, const Color& Tint)
{
	if (Profile.size() < 2) return;
	const int32_t Count = std::clamp(Sides, 3, 128);
	// Validate the whole profile before emitting anything. Vertical and horizontal bands are legal.
	for (size_t J = 0; J < Profile.size(); ++J)
	{
		if (!std::isfinite(Profile[J].x) || !std::isfinite(Profile[J].y) || Profile[J].x <= 0.0f
			|| (J && Profile[J].y < Profile[J - 1].y)) return;
	}
	for (size_t J = 1; J < Profile.size(); ++J)
	{
		const Vector2 P = Profile[J - 1], Q = Profile[J];
		const float DR = Q.x - P.x, DY = Q.y - P.y;
		if (DR * DR + DY * DY < BUILD_EPSILON * BUILD_EPSILON) continue;
		const int32_t First = int32_t(Vertices.size());
		for (int32_t I = 0; I <= Count; ++I)
		{
			// Exact duplicate seam position/normal, with separate UVs.
			const float Angle = I == Count ? 0.0f : BUILD_TAU * float(I) / float(Count);
			const float C = std::cos(Angle), S = std::sin(Angle);
			const Vector3 Normal = Vector3(DY * C, -DR, DY * S).normalized();
			for (const Vector2& Ring : { P, Q })
			{
				Vertices.push_back(Base + Vector3(Ring.x * C, Ring.y, Ring.x * S));
				Normals.push_back(Normal);
				UVs.push_back(Vector2(BUILD_TAU * Profile.front().x * float(I) / float(Count), Ring.y));
				Colors.push_back(Tint);
			}
		}
		for (int32_t I = 0; I < Count; ++I)
		{
			const int32_t A = First + I * 2;
			PushTriangle(A, A + 2, A + 3);
			PushTriangle(A, A + 3, A + 1);
		}
	}
	// Independent cap vertices keep the rim hard. No per-face colour changes.
	for (int32_t End = 0; End < 2; ++End)
	{
		const Vector2 P = End ? Profile.back() : Profile.front();
		const Vector3 Normal(0, End ? 1.0f : -1.0f, 0);
		const int32_t First = int32_t(Vertices.size());
		Vertices.push_back(Base + Vector3(0, P.y, 0));
		Normals.push_back(Normal); UVs.push_back(Vector2()); Colors.push_back(Tint);
		for (int32_t I = 0; I < Count; ++I)
		{
			const float Angle = BUILD_TAU * float(I) / float(Count);
			const Vector2 At(P.x * std::cos(Angle), P.x * std::sin(Angle));
			Vertices.push_back(Base + Vector3(At.x, P.y, At.y));
			Normals.push_back(Normal); UVs.push_back(At); Colors.push_back(Tint);
		}
		for (int32_t I = 0; I < Count; ++I)
		{
			const int32_t A = First + 1 + I, B = First + 1 + (I + 1) % Count;
			PushTriangle(First, End ? A : B, End ? B : A);
		}
	}
}

void BuildingGen::AddBuildingColumn(const BuildingSpec& Spec, MeshAccumulator& Mesh,
	const Vector3& Base, uint32_t ComponentId)
{
	const float R = Spec.ColumnRadius;
	if (R <= 0.0f || Spec.ColumnHeight <= 0.0f) return;
	const float H = std::clamp(Spec.ColumnBaseHeight, 0.0f, Spec.ColumnHeight * 0.15f);
	if (H > BUILD_EPSILON)
	{
		// 柱础 is stone and the shaft above it is timber, so this one call straddles two slots.
		Mesh.SetSlot(EMaterialSlot::Stone);
		const Color PlinthTint = Mesh.ComponentColor(Spec.StoneColor, ComponentId ^ 0x40000000u);
		if (Spec.bColumnBaseSquare)
		{
			// 方形石础 (60_台基地面 R12): the dwelling elevation puts a square stone block under
			// every eave column, not a turned drum. [自定] The block's width, base step and
			// proportions have no measurable source — the plate is a hand-drawn elevation — so
			// they are chosen to sit at 1.5 R half width with a 1/4-H foot course. The height
			// still comes from ColumnBaseHeight, so the parameter keeps its meaning.
			// The boxes pick up the accumulator's per-piece mottle (a stone block weathering
			// differently from its neighbours) on top of the keyed stone tint.
			const float Half = R * 1.5f;
			const float Foot = H * 0.25f;
			Mesh.AddBox(Base + Vector3(0.0f, Foot * 0.5f, 0.0f),
				Vector3(R * 1.62f, Foot * 0.5f, R * 1.62f), PlinthTint * 0.97f);
			Mesh.AddBox(Base + Vector3(0.0f, Foot + (H - Foot) * 0.5f, 0.0f),
				Vector3(Half, (H - Foot) * 0.5f, Half), PlinthTint);
		}
		else
		{
			// A restrained engineering plinth: foot bevel, drum, shoulder and neck.
			// Profile corners stay crisp while each circular ring shades continuously.
			const std::vector<Vector2> Profile = {
				{ R * 1.48f, 0 }, { R * 1.60f, H * 0.12f }, { R * 1.60f, H * 0.28f },
				{ R * 1.40f, H * 0.42f }, { R * 1.30f, H * 0.72f },
				{ R * 1.12f, H * 0.88f }, { R * 1.12f, H }
			};
			Mesh.AddRevolvedProfile(Base, Profile, Spec.ColumnSides, PlinthTint);
		}
	}
	// Preserve the original taper at the joint as well as the original column top.
	Mesh.SetSlot(EMaterialSlot::Timber);
	const float BottomRadius = R * (1.0f - 0.12f * H / Spec.ColumnHeight);
	Mesh.AddColumn(Base + Vector3(0, H, 0), Spec.ColumnHeight - H,
		BottomRadius, R * 0.88f, Spec.ColumnSides, Spec.TimberColor, Spec.bSmoothColumns, ComponentId);
}

// ==================== Roof profile ====================

std::vector<Vector2> BuildingGen::BuildRoofProfile(const BuildingSpec& Spec, float HalfSpan)
{
	if (Spec.RoofCurveMode == 1)
	{
		return SampleRoofCurve(Spec, HalfSpan, Spec.RoofHeight);
	}

	const int32_t Courses = std::max(Spec.RafterCourses, 3);
	const float Run = HalfSpan / float(Courses);

	// Raw rises first, then scale the total to the requested roof height. That keeps the
	// *shape* of the curve independent of how tall the roof ends up.
	std::vector<float> Rises(static_cast<size_t>(Courses), 0.0f);
	float Total = 0.0f;
	for (int32_t Index = 0; Index < Courses; ++Index)
	{
		const float T = (float(Index) + 0.5f) / float(Courses);
		const float Ratio = Spec.EaveRiseRatio + (Spec.RidgeRiseRatio - Spec.EaveRiseRatio) * T;
		Rises[size_t(Index)] = Run * Ratio;
		Total += Rises[size_t(Index)];
	}

	const float Scale = (Total > BUILD_EPSILON) ? (Spec.RoofHeight / Total) : 0.0f;

	std::vector<Vector2> Profile;
	Profile.reserve(size_t(Courses) + 1);

	float Height = 0.0f;
	float Distance = HalfSpan;
	Profile.push_back(Vector2(Distance, Height));

	for (int32_t Index = 0; Index < Courses; ++Index)
	{
		Height += Rises[size_t(Index)] * Scale;
		Distance -= Run;
		Profile.push_back(Vector2(std::fmax(Distance, 0.0f), Height));
	}

	return Profile;
}

void MeshAccumulator::AddQuadOriented(
	const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
	const Vector3& DesiredNormal, const Color& Tint)
{
	// AddTriangle negates the cross product, so the winding that yields DesiredNormal is the
	// one whose cross product opposes it.
	const Vector3 Cross = (B - A).cross(C - A);
	if (Cross.dot(DesiredNormal) > 0.0f)
	{
		AddQuad(A, D, C, B, Tint);
	}
	else
	{
		AddQuad(A, B, C, D, Tint);
	}
}

void MeshAccumulator::AddQuadSmooth(
	const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
	const Vector3& NormalA, const Vector3& NormalB, const Vector3& NormalC, const Vector3& NormalD,
	const Color& Tint)
{
	// Geometric normal decides the winding; the supplied normals only shade. AddTriangle negates
	// the cross product, so the quad faces the way the negated cross points.
	const Vector3 Geometric = -(B - A).cross(C - A);
	if (Geometric.length_squared() < 1e-14f)
	{
		// Coincident corners: a section crease, or a course pinched out at a hip. Nothing to draw.
		return;
	}

	const bool bFlip = Geometric.dot(NormalA + NormalB + NormalC + NormalD) < 0.0f;

	const int32_t Base = int32_t(Vertices.size());
	const Vector3 Points[4] = { A, B, C, D };
	const Vector3 Corners[4] = { NormalA, NormalB, NormalC, NormalD };
	const Vector2 Coords[4] = { Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1) };
	const Color Col = MottleColor(Tint);

	for (int32_t Index = 0; Index < 4; ++Index)
	{
		Vertices.push_back(Points[Index]);
		Normals.push_back(Corners[Index]);
		UVs.push_back(Coords[Index]);
		Colors.push_back(Col);
	}

	// Two triangles, wound consistently with the shading normals so backface culling keeps them.
	const int32_t Order[6] = { 0, 1, 2, 0, 2, 3 };
	for (int32_t Step = 0; Step < 6; Step += 3)
	{
		if (bFlip)
		{
			PushTriangle(Base + Order[Step + 2], Base + Order[Step + 1], Base + Order[Step]);
		}
		else
		{
			PushTriangle(Base + Order[Step], Base + Order[Step + 1], Base + Order[Step + 2]);
		}
	}
}

void BuildingGen::AddEaveRafterHeads(
	MeshAccumulator& Mesh,
	const BuildingSpec& Spec,
	const std::vector<Vector3>& Points,
	const std::vector<Vector2>& Inward,
	float TangentSlope,
	const CornerFlip* Flip,
	const Color& Tint)
{
	if (Spec.EaveRafterStyle <= 0 || Points.size() < 2 || Points.size() != Inward.size() || Spec.Module <= 0.0f)
	{
		return;
	}

	// 檐椽头 / 飞椽头 are sawn timber, and every roof's eave shares this one section.
	Mesh.SetSlot(EMaterialSlot::Timber);

	const bool bFly = Spec.EaveRafterStyle >= 2;

	// The shared 檐口断面, sized off the module D: the lower wide step is the 檐椽头, the
	// upper narrow step the 飞椽头 laid on its back. Contour Y is up, X across the eave.
	const float W1 = Spec.Module * 0.30f; // 檐椽 width
	const float H1 = Spec.Module * 0.26f; // 檐椽 depth below the soffit
	const float W2 = Spec.Module * 0.21f; // 飞椽 width
	const float H2 = Spec.Module * 0.18f; // 飞椽 height above the 檐椽
	const float Length = Spec.Module * 1.8f;

	std::vector<Vector2> Contour;
	if (bFly)
	{
		Contour.push_back(Vector2(-W1 * 0.5f, 0.0f));
		Contour.push_back(Vector2(W1 * 0.5f, 0.0f));
		Contour.push_back(Vector2(W1 * 0.5f, H1));
		Contour.push_back(Vector2(W2 * 0.5f, H1));
		Contour.push_back(Vector2(W2 * 0.5f, H1 + H2));
		Contour.push_back(Vector2(-W2 * 0.5f, H1 + H2));
		Contour.push_back(Vector2(-W2 * 0.5f, H1));
		Contour.push_back(Vector2(-W1 * 0.5f, H1));
	}
	else
	{
		Contour.push_back(Vector2(-W1 * 0.5f, 0.0f));
		Contour.push_back(Vector2(W1 * 0.5f, 0.0f));
		Contour.push_back(Vector2(W1 * 0.5f, H1));
		Contour.push_back(Vector2(-W1 * 0.5f, H1));
	}

	// The 望板 soffit at the eave sits one board thickness below the roof base along the
	// panel normal. The section stands square to the rafter axis, so its frame Up leans with
	// the eave slope and a contour-Y of H only rises H * CosTheta in the world — the knot
	// sits that much higher so the section's top edge still kisses the soffit exactly, and
	// the whole row stays clear of the 连檐 tucked above it.
	const float CosTheta = 1.0f / std::sqrt(1.0f + TangentSlope * TangentSlope);
	const float SoffitY = Spec.RoofBase - GetBoardThickness(Spec) * CosTheta;
	const float KnotY = SoffitY - (bFly ? H1 + H2 : H1) * CosTheta;

	SweepSettings Settings;
	Settings.Contour = Contour;
	Settings.bClosedContour = true;
	Settings.UpReference = Vector3(0, 1, 0);

	for (size_t Index = 0; Index < Points.size(); ++Index)
	{
		const Vector2& In = Inward[Index];
		if (In.length_squared() < 1e-12f || !std::isfinite(TangentSlope))
		{
			continue;
		}

		// The rafter axis follows the eave tangent: rising along the plan inward direction at
		// the eave slope. A straight timber, parallel to the curve where it starts.
		const Vector3 Tangent = Vector3(In.x, TangentSlope, In.y).normalized();

		std::vector<Vector3> Knots;
		Knots.push_back(Vector3(Points[Index].x, KnotY, Points[Index].z));
		Knots.push_back(Knots[0] + Tangent * Length);

		// Each knot flips with its own weight, so a corner head lifts and extends with the
		// eave — the same 翼角起翘 that lifts the ring it hangs from.
		if (Flip)
		{
			Knots[0] = Flip->Apply(Knots[0]);
			Knots[1] = Flip->Apply(Knots[1]);
		}

		SweepResult Sweep;
		if (BuildSweep(Knots, Settings, Sweep))
		{
			Mesh.AddSweep(Sweep, Tint);
		}
	}
}

// ==================== Corner flip (翼角起翘) ====================
namespace
{
	float FlipRound(float Value)
	{
		return std::floor(Value + 0.5f);
	}
} // namespace

float CornerFlip::Weight(float X, float Z) const
{
	if (Rise <= 0.0f && Extend <= 0.0f)
	{
		return 0.0f;
	}

	float Distance;
	if (bPolygonal)
	{
		// Corners sit at the vertex angles, so the distance from one is an arc length. Half a
		// facet is the furthest any point can be from its nearest corner.
		const int32_t Count = std::max(Sides, 3);
		const float Step = BUILD_TAU / float(Count);
		const float Offset = Step * 0.5f;
		const float Angle = std::atan2(Z, X) - Offset;
		// Signed distance to the nearest multiple of Step.
		const float Wrapped = Angle - Step * FlipRound(Angle / Step);
		Distance = std::abs(Wrapped) * std::fmax(Radius, BUILD_EPSILON);
	}
	else
	{
		// Distance inward from each eave edge. On the +Z edge only the X term is non-zero, so
		// the larger of the two is the distance travelled away from the corner along an edge.
		const float InsetX = std::fmax(HalfWidth - std::abs(X), 0.0f);
		const float InsetZ = std::fmax(HalfDepth - std::abs(Z), 0.0f);
		Distance = std::fmax(InsetX, InsetZ);
	}

	const float T = 1.0f - std::fmin(Distance / std::fmax(Span, BUILD_EPSILON), 1.0f);

	// Squared falloff: the lift stays flat along most of the eave and turns up sharply near the
	// corner, which is what the real 角梁 geometry does.
	return T * T;
}

Vector3 CornerFlip::Apply(const Vector3& Point) const
{
	const float W = Weight(Point.x, Point.z);
	if (W <= 0.0f)
	{
		return Point;
	}

	if (bPolygonal)
	{
		const Vector3 Radial(Point.x, 0.0f, Point.z);
		const Vector3 Outward = (Radial.length_squared() > BUILD_EPSILON)
			? Radial.normalized()
			: Vector3(0, 0, 0);

		return Point + Outward * (Extend * W) + Vector3(0.0f, Rise * W, 0.0f);
	}

	const float SignX = (Point.x >= 0.0f) ? 1.0f : -1.0f;
	const float SignZ = (Point.z >= 0.0f) ? 1.0f : -1.0f;

	return Point + Vector3(SignX * Extend * W, Rise * W, SignZ * Extend * W);
}

std::vector<Vector2> BuildingGen::BuildRoofProfileScaled(
	const BuildingSpec& Spec, float HalfSpan, float TargetRise)
{
	if (Spec.RoofCurveMode == 1)
	{
		return SampleRoofCurve(Spec, HalfSpan, TargetRise);
	}

	BuildingSpec Scaled = Spec;
	Scaled.RoofHeight = TargetRise;

	return BuildRoofProfile(Scaled, HalfSpan);
}

float BuildingGen::GetBoardThickness(const BuildingSpec& Spec)
{
	// Thin enough to read as boarding on rafters rather than as a slab, thick enough to stay
	// visible at the eave from ground level.
	return Spec.Module * 0.30f;
}

void BuildingGen::AddRoofPanel(
	MeshAccumulator& Mesh,
	const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D,
	const Vector3& Normal,
	float Thickness,
	ERoofPanelEdges OpenEdges,
	const Color& Tint,
	const Color& SoffitTint,
	const Vector3* VertexNormals)
{
	if (VertexNormals != nullptr)
	{
		Mesh.AddQuadSmooth(
			A, B, C, D, VertexNormals[0], VertexNormals[1], VertexNormals[2], VertexNormals[3],
			Tint);
	}
	else
	{
		Mesh.AddQuadOriented(A, B, C, D, Normal, Tint);
	}

	if (Thickness <= 0.0f)
	{
		return;
	}

	// Straight down, deliberately not along the normal. Offsetting each panel along its own normal
	// splits the soffit at every seam where the slope changes — adjacent panels push their shared
	// corners in different directions — and the roof ends up laced with hairline cracks. Dropping
	// vertically gives every panel the same displacement, so shared corners stay shared and the
	// soffit is watertight. It also matches how 举架 depths are reckoned, which are vertical.
	const Vector3 Drop(0.0f, -Thickness, 0.0f);
	const Vector3 UnderA = A + Drop;
	const Vector3 UnderB = B + Drop;
	const Vector3 UnderC = C + Drop;
	const Vector3 UnderD = D + Drop;

	if (VertexNormals != nullptr)
	{
		// The underside mirrors the top face's curvature, so it shades with the negated normals.
		Mesh.AddQuadSmooth(
			UnderA, UnderB, UnderC, UnderD,
			-VertexNormals[0], -VertexNormals[1], -VertexNormals[2], -VertexNormals[3],
			SoffitTint);
	}
	else
	{
		Mesh.AddQuadOriented(UnderA, UnderB, UnderC, UnderD, -Normal, SoffitTint);
	}

	// Closing an open edge is also what stops the eave reading as a knife edge.
	if (HasEdge(OpenEdges, ERoofPanelEdges::Lower))
	{
		const Vector3 Outward = ((A + B) - (D + C)).normalized();
		Mesh.AddQuadOriented(A, B, UnderB, UnderA, Outward, SoffitTint);
	}

	if (HasEdge(OpenEdges, ERoofPanelEdges::Upper))
	{
		const Vector3 Outward = ((D + C) - (A + B)).normalized();
		Mesh.AddQuadOriented(D, C, UnderC, UnderD, Outward, SoffitTint);
	}
}

// ==================== Parts ====================

namespace
{
	/**
	 * 下碱砌块带 + 上部抹灰带 (20_墙体 R15).
	 *
	 * The solid part of a wall bay — the 槛墙 under the window — reads as two zones: a masonry
	 * block band at the bottom and plaster above it, optionally split by a 分界带. The plate that
	 * motivates this is a hand-drawn elevation, so it fixes only the *structure* (there is a
	 * block band, a plaster field, and a horizontal break between them); nothing on it can be
	 * measured, so every size below is [自定] for this project:
	 *
	 *   block 1.1 D x 0.38 D, joint width 0.02 D, joint depth 0.012 D (the bed the blocks stand
	 *   proud of), 分界带 height DadoTopTrim * D projecting 0.06 D, plaster patches 1.5 D square
	 *   with up to 0.008 D of relief.
	 *
	 * The joints are real geometry: each course sits on a bed recessed by the joint depth, so the
	 * mortar lines are grooves rather than strips laid on a plane, and every block protrudes to
	 * the nominal wall face. Vertical joints stagger by half a block on alternate courses and the
	 * end blocks take up the remainder by clipping to the bay, so no joint is forced onto the bay
	 * end and no course is left short. The plaster above is emitted as patches rather than one
	 * pristine box, because the plate's other requirement is that the plaster field is not a bare
	 * plane: the patches differ in depth (deterministically, off their grid index) and pick up the
	 * accumulator's per-piece mottle, which is what gives the hand-troweled reading.
	 */
	void AddZonedWallBay(
		const BuildingSpec& Spec,
		const Vector3& Centre,
		bool bAlongX,
		float HalfLength,
		float HalfThickness,
		float WallHeight,
		float SolidHeight,
		MeshAccumulator& Mesh)
	{
		const auto Extent = [bAlongX](float Along, float Up, float Thick) -> Vector3
		{
			return bAlongX ? Vector3(Along, Up, Thick) : Vector3(Thick, Up, Along);
		};
		const auto Offset = [bAlongX](float Along, float Up) -> Vector3
		{
			return bAlongX ? Vector3(Along, Up, 0.0f) : Vector3(0.0f, Up, Along);
		};

		// Every zone of a wall bay — 下碱砌块带, 上部抹灰带 and 分界带 — is wall face, whatever
		// stone tint a zone borrows: the slot is the surface a material lands on, and all three
		// are the same plastered / coursed wall plane.
		Mesh.SetSlot(EMaterialSlot::Wall);

		const float D = Spec.Module;
		// The band never grows past the solid 槛墙: a 下碱 taller than the wall below the window
		// has nothing to stand on, and the window opening is not ours to move.
		const float Band = std::fmin(std::fmax(Spec.DadoHeightRatio, 0.0f) * WallHeight, SolidHeight);
		// The joint depth is what makes a block band read as masonry instead of as louvres: past
		// a centimetre or so each course starts casting a bright lip on top of the course below.
		// R2 asks for a recess of a few millimetres and forbids a drawn-on strip; 8 mm is the
		// smallest value that still resolves at the fixed M1 view, where a pixel is about 7 mm.
		const float Joint = std::fmin(D * 0.012f, HalfThickness * 0.2f);
		const float Gap = D * 0.02f;
		const float BlockLength = D * 1.1f;
		const float CourseHeight = D * 0.38f;

		if (Band > BUILD_EPSILON)
		{
			// --- 下碱砌块带 ---
			const int32_t Courses = std::max(int32_t(std::lround(Band / CourseHeight)), 1);
			const float Course = Band / float(Courses);
			Mesh.AddBox(
				Centre + Offset(0.0f, Band * 0.5f),
				Extent(HalfLength, Band * 0.5f, HalfThickness - Joint),
				Spec.StoneColor * 0.74f);

			const int32_t Cols = std::max(int32_t(std::lround(HalfLength * 2.0f / BlockLength)), 1);
			const float Block = HalfLength * 2.0f / float(Cols);
			for (int32_t C = 0; C < Courses; ++C)
			{
				const float Y = Course * (float(C) + 0.5f);
				const float Shift = (C % 2) ? -Block * 0.5f : 0.0f;
				// One extra slot each side covers the half bats the stagger leaves at the ends.
				for (int32_t K = -1; K <= Cols; ++K)
				{
					const float From = std::fmax(-HalfLength, Shift + Block * float(K));
					const float To = std::fmin(HalfLength, Shift + Block * float(K + 1));
					const float Half = (To - From) * 0.5f - Gap * 0.5f;
					if (Half <= BUILD_EPSILON)
					{
						continue;
					}
					Mesh.AddBox(
						Centre + Offset((From + To) * 0.5f, Y),
						Extent(Half, Course * 0.5f - Gap * 0.5f, HalfThickness),
						Spec.StoneColor * 1.04f);
				}
			}
		}

		const float PlasterLow = Band;
		const float PlasterHigh = SolidHeight;
		if (PlasterHigh - PlasterLow > BUILD_EPSILON)
		{
			// --- 上部抹灰带: patched, not one pristine plane ---
			const float Patch = D * 1.5f;
			const int32_t Cols = std::max(int32_t(std::lround(HalfLength * 2.0f / Patch)), 1);
			const int32_t Rows = std::max(int32_t(std::lround((PlasterHigh - PlasterLow) / Patch)), 1);
			const float CellAlong = HalfLength * 2.0f / float(Cols);
			const float CellUp = (PlasterHigh - PlasterLow) / float(Rows);
			const float Relief = std::fmin(D * 0.008f, (PlasterHigh - PlasterLow) * 0.2f);

			for (int32_t I = 0; I < Cols; ++I)
			{
				for (int32_t J = 0; J < Rows; ++J)
				{
					// Deterministic off the grid index, so a rebuild reproduces the same face.
					// The jitter never reaches zero, so no patch is left exactly on the nominal
					// wall plane — which is the point of the zone not reading as a bare plane.
					const uint32_t Hash = uint32_t(I) * 73856093u ^ uint32_t(J) * 19349663u;
					const float T = 0.35f + 0.65f * (float(Hash % 256u) / 255.0f);
					Mesh.AddBox(
						Centre + Offset(-HalfLength + CellAlong * (float(I) + 0.5f),
							PlasterLow + CellUp * (float(J) + 0.5f)),
						Extent(CellAlong * 0.5f, CellUp * 0.5f, HalfThickness + Relief * T),
						Spec.PlasterColor);
				}
			}
		}

		// --- 分界带: a moulded band capping the block band, proud of the wall face ---
		// Never taller than the band it caps, so an over-large DadoTopTrim cannot push the
		// trim down into the platform.
		const float Trim = std::fmin(Spec.DadoTopTrim * D, std::fmin(SolidHeight - Band, Band));
		if (Band > BUILD_EPSILON && Trim > BUILD_EPSILON)
		{
			Mesh.AddBox(
				Centre + Offset(0.0f, Band - Trim * 0.5f),
				Extent(HalfLength, Trim * 0.5f, HalfThickness + D * 0.06f),
				Spec.StoneColor * 1.08f);
		}
	}

	/**
	 * One wall bay: either a door or a 槛墙 dado with a lattice window above it. Axis-aligned,
	 * with bAlongX selecting whether the bay runs along X (a front/back wall) or Z (an end wall).
	 *
	 * The lattice is a grid of thin bars rather than a texture, which is the only option that
	 * stays consistent with this module being asset-free.
	 */
	void AddWallBay(
		const BuildingSpec& Spec,
		const Vector3& Centre,
		bool bAlongX,
		float HalfLength,
		float HalfThickness,
		float Height,
		bool bIsDoor,
		MeshAccumulator& Mesh)
	{
		if (HalfLength <= 0.0f || Height <= 0.0f)
		{
			return;
		}

		// Extent helper: swaps which axis carries the bay length.
		const auto Extent = [bAlongX, HalfThickness](float Along, float Up) -> Vector3
		{
			return bAlongX ? Vector3(Along, Up, HalfThickness) : Vector3(HalfThickness, Up, Along);
		};
		const auto Offset = [bAlongX](float Along, float Up) -> Vector3
		{
			return bAlongX ? Vector3(Along, Up, 0.0f) : Vector3(0.0f, Up, Along);
		};

		const float FrameHalf = Spec.Module * 0.13f;

		if (bIsDoor)
		{
			// 板门: two leaves, jambs and lintel — joinery, so timber, not wall.
			Mesh.SetSlot(EMaterialSlot::Timber);
			const float LeafHalf = (HalfLength - FrameHalf * 2.0f) * 0.5f;
			if (LeafHalf <= 0.0f)
			{
				return;
			}

			// Jambs.
			for (int32_t Side = -1; Side <= 1; Side += 2)
			{
				Mesh.AddBox(
					Centre + Offset((HalfLength - FrameHalf) * float(Side), Height * 0.5f),
					Extent(FrameHalf, Height * 0.5f),
					Spec.TimberColor);
			}
			// Lintel.
			Mesh.AddBox(
				Centre + Offset(0.0f, Height - FrameHalf),
				Extent(HalfLength, FrameHalf),
				Spec.TimberColor);
			// Two leaves, set back so the frame reads as a frame.
			for (int32_t Side = -1; Side <= 1; Side += 2)
			{
				Mesh.AddBox(
					Centre + Offset(LeafHalf * float(Side), (Height - FrameHalf) * 0.5f),
					Extent(LeafHalf * 0.94f, (Height - FrameHalf) * 0.5f) * Vector3(1, 1, 0.55f)
						+ Vector3(bAlongX ? 0.0f : 0.0f, 0.0f, 0.0f),
					Spec.TimberColor * 0.82f);
			}

			return;
		}

		// 槛墙 dado, then the window opening above it. DadoHeightRatio = 0 keeps the single
		// unzoned box this bay has always been (R15); above 0 the same solid is built as a
		// 下碱砌块带 with a plaster band over it.
		const float DadoHeight = Height * 0.46f;
		Mesh.SetSlot(EMaterialSlot::Wall);
		if (Spec.DadoHeightRatio > 0.0f)
		{
			AddZonedWallBay(Spec, Centre, bAlongX, HalfLength, HalfThickness, Height, DadoHeight, Mesh);
		}
		else
		{
			Mesh.AddBox(
				Centre + Offset(0.0f, DadoHeight * 0.5f),
				Extent(HalfLength, DadoHeight * 0.5f),
				Spec.PlasterColor);
		}

		// Everything above the sill is window joinery: frame, sill rail, head and 棂条 lattice.
		Mesh.SetSlot(EMaterialSlot::Timber);
		const float SillY = DadoHeight;
		const float WindowHeight = Height - DadoHeight;

		// Frame around the opening.
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			Mesh.AddBox(
				Centre + Offset((HalfLength - FrameHalf) * float(Side), SillY + WindowHeight * 0.5f),
				Extent(FrameHalf, WindowHeight * 0.5f),
				Spec.TimberColor);
		}
		Mesh.AddBox(
			Centre + Offset(0.0f, SillY + FrameHalf),
			Extent(HalfLength, FrameHalf),
			Spec.TimberColor);
		Mesh.AddBox(
			Centre + Offset(0.0f, Height - FrameHalf),
			Extent(HalfLength, FrameHalf),
			Spec.TimberColor);

		// 棂条 lattice: a bar grid spanning the opening.
		const float InnerHalf = HalfLength - FrameHalf * 2.0f;
		const float InnerLow = SillY + FrameHalf * 2.0f;
		const float InnerHigh = Height - FrameHalf * 2.0f;
		if (InnerHalf <= 0.0f || InnerHigh <= InnerLow)
		{
			return;
		}

		const float BarHalf = Spec.Module * 0.022f;
		const float BarSpacing = Spec.Module * 0.95f;
		const float LatticeThickness = HalfThickness * 0.45f;

		const int32_t Verticals = std::max(int32_t(InnerHalf * 2.0f / BarSpacing), 1);
		for (int32_t Index = 0; Index <= Verticals; ++Index)
		{
			const float Along = -InnerHalf + (InnerHalf * 2.0f) * float(Index) / float(Verticals);
			Mesh.AddBox(
				Centre + Offset(Along, (InnerLow + InnerHigh) * 0.5f),
				bAlongX
					? Vector3(BarHalf, (InnerHigh - InnerLow) * 0.5f, LatticeThickness)
					: Vector3(LatticeThickness, (InnerHigh - InnerLow) * 0.5f, BarHalf),
				Spec.TimberColor * 0.9f);
		}

		const int32_t Horizontals = std::max(int32_t((InnerHigh - InnerLow) / BarSpacing), 1);
		for (int32_t Index = 0; Index <= Horizontals; ++Index)
		{
			const float Up = InnerLow + (InnerHigh - InnerLow) * float(Index) / float(Horizontals);
			Mesh.AddBox(
				Centre + Offset(0.0f, Up),
				bAlongX
					? Vector3(InnerHalf, BarHalf, LatticeThickness)
					: Vector3(LatticeThickness, BarHalf, InnerHalf),
				Spec.TimberColor * 0.9f);
		}
	}

	/**
	 * A 斗拱 bracket set: a 座斗 block carrying tiers of crossing 拱 arms, each tier stepping
	 * wider and higher, with small 升 blocks at the arm ends. Boxes only — the real joinery is
	 * far beyond what a greybox needs, but the stepped corbelling is the silhouette that reads.
	 */
	void AddBracketSet(
		const BuildingSpec& Spec,
		const Vector3& Base,
		bool bAlongX,
		MeshAccumulator& Mesh)
	{
		const float Height = Spec.BracketHeight;
		if (Height <= 0.0f)
		{
			return;
		}

		// 斗拱 is joinery in timber, however blocky the greybox stands in for it.
		Mesh.SetSlot(EMaterialSlot::Timber);

		const float Unit = Spec.ColumnRadius;
		const int32_t Tiers = 2;
		const float TierHeight = Height / float(Tiers + 1);

		// 座斗, the block the whole set stands on.
		Mesh.AddBox(
			Base + Vector3(0.0f, TierHeight * 0.5f, 0.0f),
			Vector3(Unit * 0.85f, TierHeight * 0.5f, Unit * 0.85f),
			Spec.BracketColor);

		for (int32_t Tier = 0; Tier < Tiers; ++Tier)
		{
			const float Y = TierHeight * (float(Tier) + 1.0f);
			const float Reach = Unit * (1.15f + 0.5f * float(Tier));
			const float ArmHalf = TierHeight * 0.32f;

			// Crossing arms: one along the wall, one projecting out from it.
			Mesh.AddBox(
				Base + Vector3(0.0f, Y + ArmHalf, 0.0f),
				bAlongX ? Vector3(Reach, ArmHalf, Unit * 0.4f) : Vector3(Unit * 0.4f, ArmHalf, Reach),
				Spec.BracketColor * (1.0f + 0.06f * float(Tier)));
			Mesh.AddBox(
				Base + Vector3(0.0f, Y + ArmHalf, 0.0f),
				bAlongX ? Vector3(Unit * 0.4f, ArmHalf, Reach) : Vector3(Reach, ArmHalf, Unit * 0.4f),
				Spec.BracketColor * (1.0f + 0.06f * float(Tier)));

			// 升 blocks capping the arm ends.
			for (int32_t Side = -1; Side <= 1; Side += 2)
			{
				const Vector3 Along = bAlongX
					? Vector3(Reach * float(Side), 0.0f, 0.0f)
					: Vector3(0.0f, 0.0f, Reach * float(Side));
				const Vector3 Across = bAlongX
					? Vector3(0.0f, 0.0f, Reach * float(Side))
					: Vector3(Reach * float(Side), 0.0f, 0.0f);

				for (const Vector3& At : { Along, Across })
				{
					Mesh.AddBox(
						Base + At + Vector3(0.0f, Y + ArmHalf * 2.0f + TierHeight * 0.18f, 0.0f),
						Vector3(Unit * 0.32f, TierHeight * 0.16f, Unit * 0.32f),
						Spec.BracketColor * 1.15f);
				}
			}
		}
	}

	void BuildPlatform(const BuildingSpec& Spec, MeshAccumulator& Mesh)
	{
		if (Spec.PlatformHeight <= 0.0f)
		{
			return;
		}

		// 台基 body, 阶条石 cap, 沿口, 顶面接缝 and 方砖铺地 are all dressed stone.
		Mesh.SetSlot(EMaterialSlot::Stone);

		// 阶条石 cap: a slightly wider slab on top of the body, which reads as dressed stone.
		const float CapHeight = std::fmin(Spec.PlatformHeight * 0.22f, Spec.Module * 0.5f);
		const float BodyHeight = Spec.PlatformHeight - CapHeight;
		const float Inset = Spec.Module * 0.12f;

		Mesh.AddBox(
			Vector3(0.0f, BodyHeight * 0.5f, 0.0f),
			Vector3(Spec.PlatformHalfWidth - Inset, BodyHeight * 0.5f, Spec.PlatformHalfDepth - Inset),
			Spec.StoneColor);

		// 沿口 (60_台基地面 R6): a moulded band standing proud of the cap, so the edge steps
		// out instead of dropping straight to the body. [自定] Height and projection: the plate
		// shows that the edge is not a straight drop, but a hand-drawn elevation is not
		// measurable, so both are project choices.
		const float LipHeight = Spec.bPlatformEdgeLip
			? std::fmin(CapHeight * 0.6f, Spec.Module * 0.25f)
			: 0.0f;
		const float LipOut = Spec.Module * 0.10f;
		if (LipHeight > BUILD_EPSILON)
		{
			Mesh.AddBox(
				Vector3(0.0f, BodyHeight + LipHeight * 0.5f, 0.0f),
				Vector3(Spec.PlatformHalfWidth + LipOut, LipHeight * 0.5f, Spec.PlatformHalfDepth + LipOut),
				Spec.StoneColor * 1.02f);
		}

		// 顶面接缝网格 (60_台基地面 R6): the cap top is cut into a slab grid standing on a bed
		// that is recessed by the joint depth, so the joints are real grooves rather than a
		// colour pattern. With the flag off every term below collapses onto the legacy single
		// cap box, arithmetic included.
		const float JointDepth = Spec.bPlatformTopJoints
			? std::fmin(Spec.Module * 0.03f, CapHeight * 0.25f)
			: 0.0f;
		const float CapThickness = CapHeight - LipHeight - JointDepth;
		// The bed keeps the platform's exact footprint: the slabs stop half a joint short of it,
		// so bed and slabs never share a plane and the plan extent does not move with the flag.
		Mesh.AddBox(
			Vector3(0.0f, BodyHeight + LipHeight + CapThickness * 0.5f, 0.0f),
			Vector3(Spec.PlatformHalfWidth, CapThickness * 0.5f, Spec.PlatformHalfDepth),
			Spec.StoneColor * 1.06f);

		if (JointDepth > BUILD_EPSILON)
		{
			// [自定] Slab pitch and joint width: the plate shows a regular grid with visible
			// joints but is not measurable, so the grid is a project choice.
			const float Pitch = Spec.Module * 1.5f;
			const int32_t Cols = std::max(int32_t(std::lround(2.0f * Spec.PlatformHalfWidth / Pitch)), 1);
			const int32_t Rows = std::max(int32_t(std::lround(2.0f * Spec.PlatformHalfDepth / Pitch)), 1);
			const float CellX = 2.0f * Spec.PlatformHalfWidth / float(Cols);
			const float CellZ = 2.0f * Spec.PlatformHalfDepth / float(Rows);
			const float Gap = std::fmin(Spec.Module * 0.05f, std::fmin(CellX, CellZ) * 0.35f);

			for (int32_t I = 0; I < Cols; ++I)
			{
				const float X = -Spec.PlatformHalfWidth + CellX * (float(I) + 0.5f);
				for (int32_t J = 0; J < Rows; ++J)
				{
					const float Z = -Spec.PlatformHalfDepth + CellZ * (float(J) + 0.5f);
					Mesh.AddBox(
						Vector3(X, Spec.PlatformHeight - JointDepth * 0.5f, Z),
						Vector3(CellX * 0.5f - Gap * 0.5f, JointDepth * 0.5f, CellZ * 0.5f - Gap * 0.5f),
						Spec.StoneColor * 1.02f);
				}
			}
		}

		// 方砖铺地 (60_台基地面 R9): a flagstone apron on the ground around the platform. The
		// legacy build has no paving at all, so the whole field sits behind the flag.
		// [自定] Extent, pitch, thickness, joint width and the weathered tint: the plate shows
		// large rectangular slabs laid to a grid with visible joints (and a finer grid further
		// out), but a hand-drawn elevation is not measurable, so every value here is a project
		// choice. The tint is deliberately below the platform's so the apron and the platform
		// top do not merge into one tiled plane at the acceptance distances.
		// The field is a slab standing on the ground plane rather than a coplanar decal, so it
		// cannot z-fight with a scene floor; it is not clipped out from under the platform,
		// which hides it anyway.
		if (Spec.bPaving)
		{
			const float Margin = std::fmax(Spec.StepRunDepth, Spec.Module * 1.5f) + Spec.Module * 0.5f;
			const float HalfX = Spec.PlatformHalfWidth + Margin;
			const float HalfZ = Spec.PlatformHalfDepth + Margin;
			const float Thickness = Spec.Module * 0.10f;
			const float Groove = Spec.bPavingJointGeometry
				? std::fmin(Spec.Module * 0.045f, Thickness * 0.6f)
				: 0.0f;
			// With joints the tiles are a thin course on a recessed bed; without, they butt into
			// the full-thickness slab themselves and the bed is not needed.
			const float BedHeight = Thickness - Groove;
			const float TileLow = (Groove > BUILD_EPSILON) ? BedHeight : 0.0f;
			const float TileHalfY = (Thickness - TileLow) * 0.5f;
			const float Pitch = Spec.Module * 1.6f;
			const int32_t Cols = std::max(int32_t(std::lround(2.0f * HalfX / Pitch)), 1);
			const int32_t Rows = std::max(int32_t(std::lround(2.0f * HalfZ / Pitch)), 1);
			const float CellX = 2.0f * HalfX / float(Cols);
			const float CellZ = 2.0f * HalfZ / float(Rows);

			if (TileLow > BUILD_EPSILON)
			{
				// The tiles stop half a joint short of the field's edge, so the bed can keep the
				// exact footprint without ever sharing a plane with them.
				Mesh.AddBox(Vector3(0.0f, BedHeight * 0.5f, 0.0f), Vector3(HalfX, BedHeight * 0.5f, HalfZ),
					Spec.StoneColor * 0.72f);
			}

			for (int32_t I = 0; I < Cols; ++I)
			{
				const float X = -HalfX + CellX * (float(I) + 0.5f);
				for (int32_t J = 0; J < Rows; ++J)
				{
					const float Z = -HalfZ + CellZ * (float(J) + 0.5f);
					Mesh.AddBox(
						Vector3(X, TileLow + TileHalfY, Z),
						Vector3(CellX * 0.5f - Groove * 0.5f, TileHalfY, CellZ * 0.5f - Groove * 0.5f),
						Spec.StoneColor * 0.94f);
				}
			}
		}
	}

	/**
	 * Box whose plan axes are an arbitrary (Along, Side) frame instead of the world axes.
	 *
	 * The stair runs rotate with lambda, so their side cheeks cannot be an AddBox — at
	 * RunCount = 8 the run sits at 45 degrees. Faces are wound with AddQuadOriented against
	 * their outward direction, the same way the roof panels and the balustrade rails are.
	 */
	void AddOrientedBox(const Vector3& Centre, const Vector3& Along, const Vector3& Side,
		float HalfAlong, float HalfSide, float HalfUp, const Color& Tint, MeshAccumulator& Mesh)
	{
		if (HalfAlong <= 0.0f || HalfSide <= 0.0f || HalfUp <= 0.0f)
		{
			return;
		}

		const Vector3 A = Along * HalfAlong;
		const Vector3 S = Side * HalfSide;
		const Vector3 U(0.0f, HalfUp, 0.0f);
		const Color Col = Mesh.MottleColor(Tint);
		const Vector3 P000 = Centre - A - S - U;
		const Vector3 P100 = Centre + A - S - U;
		const Vector3 P110 = Centre + A + S - U;
		const Vector3 P010 = Centre - A + S - U;
		const Vector3 P001 = Centre - A - S + U;
		const Vector3 P101 = Centre + A - S + U;
		const Vector3 P111 = Centre + A + S + U;
		const Vector3 P011 = Centre - A + S + U;

		Mesh.AddQuadOriented(P001, P101, P111, P011, U, Col);
		Mesh.AddQuadOriented(P000, P100, P110, P010, -U, Col);
		Mesh.AddQuadOriented(P100, P101, P111, P110, Along, Col);
		Mesh.AddQuadOriented(P000, P001, P011, P010, -Along, Col);
		Mesh.AddQuadOriented(P010, P011, P111, P110, Side, Col);
		Mesh.AddQuadOriented(P000, P001, P101, P100, -Side, Col);
	}

	/**
	 * One stair run, built the way the paper intends: a swept block whose *top* is stepped by
	 * the direction constraint of equations 3-5. delta is derived from the block's own
	 * proportions so the two upper contour corners are selected and the lower two are not —
	 * the paper leaves delta to the user, which is fragile for a block this flat.
	 */
	void BuildStepRun(const BuildingSpec& Spec, float Angle, MeshAccumulator& Mesh)
	{
		const int32_t Steps = std::max(Spec.StepCount, 1);
		const float RunWidth = Spec.FenceGapWidth;
		const float BlockHeight = Spec.PlatformHeight;
		if (RunWidth <= 0.0f || BlockHeight <= 0.0f || Spec.StepRunDepth <= 0.0f)
		{
			return;
		}

		// 踏跺, treads and side cheeks alike: stone throughout.
		Mesh.SetSlot(EMaterialSlot::Stone);

		// Outward direction for this run, and the point where it crosses the platform boundary.
		//
		// F08: the run starts where its own ray leaves the platform, not on whichever single
		// half-extent happens to face it. For a rectangular platform of half width A and half
		// depth B and a unit outward direction d = (dx, dz) the crossing is
		// t_hit = min(A/|dx|, B/|dz|) with a zero component's term taken as infinity, so the
		// start is t_hit * d. The old "(|dz| > 0.5) ? B : A" test agrees with that for the four
		// axis directions and only for them: RunCount 1/2/4 (lambda <= 2) emit the axes and stay
		// bit-identical, while RunCount 8 (lambda = 3) adds the 45-degree runs, which the old
		// test started *inside* the platform (A = B = 5 gives 7.0711 against the old 5).
		const Vector3 Outward(std::sin(Angle), 0.0f, std::cos(Angle));
		const float AbsX = std::abs(Outward.x);
		const float AbsZ = std::abs(Outward.z);
		const float EdgeX = (AbsX > BUILD_EPSILON)
			? Spec.PlatformHalfWidth / AbsX
			: std::numeric_limits<float>::infinity();
		const float EdgeZ = (AbsZ > BUILD_EPSILON)
			? Spec.PlatformHalfDepth / AbsZ
			: std::numeric_limits<float>::infinity();
		const float EdgeDistance = std::fmin(EdgeX, EdgeZ);

		const float TreadDepth = Spec.StepRunDepth / float(Steps);
		const float RiserOffset = TreadDepth * 0.04f;

		// Two knots per tread: flat across the tread, then a near-vertical jump to the next.
		std::vector<Vector3> Knots;
		std::vector<float> Samples;
		for (int32_t Index = 0; Index < Steps; ++Index)
		{
			const float Drop = -BlockHeight * float(Index) / float(Steps);
			const float Near = EdgeDistance + float(Index) * TreadDepth + RiserOffset;
			const float Far = EdgeDistance + float(Index + 1) * TreadDepth;

			Knots.push_back(Outward * Near + Vector3(0.0f, BlockHeight * 0.5f, 0.0f));
			Samples.push_back(Drop);
			Knots.push_back(Outward * Far + Vector3(0.0f, BlockHeight * 0.5f, 0.0f));
			Samples.push_back(Drop);
		}

		SweepSettings Settings;
		Settings.Contour.push_back(Vector2(-RunWidth * 0.5f, -BlockHeight * 0.5f));
		Settings.Contour.push_back(Vector2(RunWidth * 0.5f, -BlockHeight * 0.5f));
		Settings.Contour.push_back(Vector2(RunWidth * 0.5f, BlockHeight * 0.5f));
		Settings.Contour.push_back(Vector2(-RunWidth * 0.5f, BlockHeight * 0.5f));
		Settings.bClosedContour = true;
		Settings.bGenerateCaps = true;
		Settings.DisplacementScale = 1.0f;
		Settings.DisplacementSamples = Samples;

		// Corner half-angle from the bitangent, plus a margin, keeps the selection unambiguous.
		const float CornerAngle = std::atan2(RunWidth * 0.5f, BlockHeight * 0.5f) * (180.0f / BUILD_PI);
		Settings.ConstraintAngleDegrees = std::fmin(CornerAngle + 6.0f, 89.0f);

		SweepResult Sweep;
		if (BuildSweep(Knots, Settings, Sweep))
		{
			Mesh.AddSweep(Sweep, Spec.StoneColor);
		}

		// 两侧简单侧挡 (60_台基地面 R8): one block per tread on each side of the run, flush with
		// the run's own riser lines, so the staircase reads as 等宽分级 between two cheeks
		// instead of as a bare block. [自定] Thickness and form have no source; the plate shows
		// only that the dwelling stair is the simple variant.
		if (Spec.bStepSideCheek)
		{
			const Vector3 Side(std::cos(Angle), 0.0f, -std::sin(Angle));
			const float CheekHalf = Spec.Module * 0.09f;
			for (int32_t Index = 0; Index < Steps; ++Index)
			{
				const float Top = BlockHeight * (1.0f - float(Index) / float(Steps));
				if (Top <= BUILD_EPSILON)
				{
					continue;
				}
				const float Near = EdgeDistance + float(Index) * TreadDepth;
				const float Far = EdgeDistance + float(Index + 1) * TreadDepth;
				for (int32_t Sign = -1; Sign <= 1; Sign += 2)
				{
					// Overlap the run by a hair rather than kissing it: two coincident faces
					// would z-fight, and nothing resolves a 0.3 mm interpenetration.
					const Vector3 At = Outward * ((Near + Far) * 0.5f)
						+ Side * ((RunWidth * 0.5f + CheekHalf * 0.98f) * float(Sign));
					AddOrientedBox(At + Vector3(0.0f, Top * 0.5f, 0.0f), Outward, Side,
						(Far - Near) * 0.5f, CheekHalf, Top * 0.5f, Spec.StoneColor * 0.98f, Mesh);
				}
			}
		}
	}

	/** Balustrade posts, panels and a swept rail, broken by a gap at each culling angle. */
	void BuildFence(const BuildingSpec& Spec, MeshAccumulator& Mesh)
	{
		// 栏杆 is a stone balustrade here — rail, posts and panels all take its tint, so they all
		// belong on the stone surface.
		Mesh.SetSlot(EMaterialSlot::Stone);

		const float PostHalf = Spec.Module * 0.16f;
		const float RailHeight = Spec.PlatformHeight + Spec.FenceHeight;
		const float HalfGap = Spec.FenceGapWidth * 0.5f;
		const std::vector<float> Angles = CullingAngles(Spec.StepRunCount);

		// Walk the platform rim as four runs so gaps can be punched per side.
		struct Side
		{
			Vector3 From;
			Vector3 To;
			float Angle;
		};

		const float HW = Spec.PlatformHalfWidth;
		const float HD = Spec.PlatformHalfDepth;
		const Side Sides[4] = {
			{ Vector3(-HW, 0, HD), Vector3(HW, 0, HD), 0.0f },                 // front, +Z
			{ Vector3(HW, 0, HD), Vector3(HW, 0, -HD), BUILD_PI * 0.5f },      // +X
			{ Vector3(HW, 0, -HD), Vector3(-HW, 0, -HD), BUILD_PI },           // back, -Z
			{ Vector3(-HW, 0, -HD), Vector3(-HW, 0, HD), BUILD_PI * 1.5f },    // -X
		};

		for (const Side& Run : Sides)
		{
			// Radial culling: this side is broken only if one of the culling angles points along it.
			bool bHasGap = false;
			for (const float Angle : Angles)
			{
				if (std::abs(std::cos(Angle - Run.Angle)) > 0.99f && std::cos(Angle - Run.Angle) > 0.0f)
				{
					bHasGap = true;
					break;
				}
			}

			const Vector3 Delta = Run.To - Run.From;
			const float Length = Delta.length();
			if (Length < BUILD_EPSILON)
			{
				continue;
			}
			const Vector3 Direction = Delta / Length;

			// Two sub-runs when broken, one otherwise. Each becomes its own swept rail.
			struct SubRun
			{
				float Start;
				float End;
			};
			std::vector<SubRun> SubRuns;
			if (bHasGap && HalfGap > 0.0f && HalfGap < Length * 0.5f)
			{
				SubRuns.push_back({ 0.0f, Length * 0.5f - HalfGap });
				SubRuns.push_back({ Length * 0.5f + HalfGap, Length });
			}
			else
			{
				SubRuns.push_back({ 0.0f, Length });
			}

			for (const SubRun& Part : SubRuns)
			{
				const float Span = Part.End - Part.Start;
				if (Span < Spec.Module * 0.5f)
				{
					continue;
				}

				const Vector3 Start = Run.From + Direction * Part.Start;
				const Vector3 End = Run.From + Direction * Part.End;

				// Rail, swept so it miters correctly if the rim is ever made curved.
				std::vector<Vector3> Knots;
				Knots.push_back(Start + Vector3(0.0f, RailHeight, 0.0f));
				Knots.push_back(End + Vector3(0.0f, RailHeight, 0.0f));

				SweepSettings Settings;
				const float RailHalf = Spec.Module * 0.2f;
				Settings.Contour.push_back(Vector2(-RailHalf, -RailHalf * 0.5f));
				Settings.Contour.push_back(Vector2(RailHalf, -RailHalf * 0.5f));
				Settings.Contour.push_back(Vector2(RailHalf, RailHalf * 0.5f));
				Settings.Contour.push_back(Vector2(-RailHalf, RailHalf * 0.5f));
				Settings.bClosedContour = true;

				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					Mesh.AddSweep(Sweep, Spec.StoneColor * 1.04f);
				}

				// Posts at both ends and every module or so between.
				const int32_t PostCount = std::max(int32_t(Span / (Spec.Module * 2.4f)), 1);
				for (int32_t Index = 0; Index <= PostCount; ++Index)
				{
					const Vector3 At = Start + (End - Start) * (float(Index) / float(PostCount));
					Mesh.AddBox(
						At + Vector3(0.0f, Spec.PlatformHeight + Spec.FenceHeight * 0.5f, 0.0f),
						Vector3(PostHalf, Spec.FenceHeight * 0.5f, PostHalf),
						Spec.StoneColor * 0.96f);
				}

				// Panel below the rail.
				const Vector3 Mid = (Start + End) * 0.5f;
				const Vector3 PanelHalf = Vector3(
					std::abs(Direction.x) > 0.5f ? Span * 0.5f - PostHalf : PostHalf * 0.45f,
					Spec.FenceHeight * 0.28f,
					std::abs(Direction.z) > 0.5f ? Span * 0.5f - PostHalf : PostHalf * 0.45f);
				Mesh.AddBox(
					Mid + Vector3(0.0f, Spec.PlatformHeight + Spec.FenceHeight * 0.45f, 0.0f),
					PanelHalf,
					Spec.StoneColor * 0.92f);
			}
		}
	}

	void BuildBody(const BuildingSpec& Spec, MeshAccumulator& Mesh)
	{
		const float HalfWidth = Spec.Width * 0.5f;
		const float HalfDepth = Spec.Depth * 0.5f;
		const float Base = Spec.PlatformHeight;
		const float ColumnTop = Base + Spec.ColumnHeight;

		const std::vector<float> XPositions = BayPositions(HalfWidth, Spec.BaysX);
		const std::vector<float> ZPositions = BayPositions(HalfDepth, Spec.BaysZ);

		// Columns stand on the bay grid's perimeter — the paper's "connection line of the
		// column's pivot" used as the body frame.
		if (Spec.bGenerateColumns)
		{
			for (size_t XI = 0; XI < XPositions.size(); ++XI)
			{
				const float X = XPositions[XI];
				for (size_t ZI = 0; ZI < ZPositions.size(); ++ZI)
				{
					const float Z = ZPositions[ZI];
					const bool bOnPerimeter =
						std::abs(std::abs(X) - HalfWidth) < BUILD_EPSILON ||
						std::abs(std::abs(Z) - HalfDepth) < BUILD_EPSILON;
					if (!bOnPerimeter)
					{
						continue;
					}

					AddBuildingColumn(Spec, Mesh, Vector3(X, Base, Z),
						0x100000u + uint32_t(XI * ZPositions.size() + ZI));
				}
			}
		}

		// Walls fill the perimeter bays, except the bay a stair run arrives at.
		if (Spec.bGenerateWalls)
		{
			// AddWallBay picks its own slot per part: the wall zones go on Wall, the door and
			// window joinery on Timber. This is only the entry state, so a bay that emits nothing
			// still leaves the accumulator somewhere sensible.
			Mesh.SetSlot(EMaterialSlot::Wall);

			const float WallHalf = Spec.ColumnRadius * 0.72f;
			const float WallHeight = Spec.ColumnHeight * 0.94f;
			const std::vector<float> Angles = CullingAngles(Spec.StepRunCount);

			const auto HasOpening = [&Angles](float Angle) -> bool
			{
				for (const float Culling : Angles)
				{
					if (std::abs(std::cos(Culling - Angle)) > 0.99f && std::cos(Culling - Angle) > 0.0f)
					{
						return true;
					}
				}

				return false;
			};

			// Front (+Z) and back (-Z) runs, split by bay.
			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				const float Z = HalfDepth * float(Sign);
				const float Angle = (Sign > 0) ? 0.0f : BUILD_PI;
				const bool bOpen = HasOpening(Angle);
				const int32_t Centre = Spec.BaysX / 2;

				for (int32_t Bay = 0; Bay < Spec.BaysX; ++Bay)
				{
					const float From = XPositions[size_t(Bay)];
					const float To = XPositions[size_t(Bay) + 1];
					// The centre bay of an approached side is the doorway; the rest get windows.
					const bool bIsDoor = bOpen && Bay == Centre;

					AddWallBay(
						Spec,
						Vector3((From + To) * 0.5f, Base, Z),
						true,
						std::abs(To - From) * 0.5f - Spec.ColumnRadius,
						WallHalf,
						WallHeight,
						bIsDoor,
						Mesh);
				}
			}

			// Side (+/-X) runs.
			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				const float X = HalfWidth * float(Sign);
				const float Angle = (Sign > 0) ? BUILD_PI * 0.5f : BUILD_PI * 1.5f;
				const bool bOpen = HasOpening(Angle);
				const int32_t Centre = Spec.BaysZ / 2;

				for (int32_t Bay = 0; Bay < Spec.BaysZ; ++Bay)
				{
					const float From = ZPositions[size_t(Bay)];
					const float To = ZPositions[size_t(Bay) + 1];
					const bool bIsDoor = bOpen && Bay == Centre;

					AddWallBay(
						Spec,
						Vector3(X, Base, (From + To) * 0.5f),
						false,
						std::abs(To - From) * 0.5f - Spec.ColumnRadius,
						WallHalf,
						WallHeight,
						bIsDoor,
						Mesh);
				}
			}
		}

		// Bracket band: 阑额 architrave plus a 斗 block over each column. Stands in for a real
		// 斗拱 set until phase 3.
		if (Spec.BracketHeight > 0.0f)
		{
			// 阑额 band and the 铺作 on it: timber, like the sets they stand in for.
			Mesh.SetSlot(EMaterialSlot::Timber);

			const float BandHalf = Spec.BracketHeight * 0.42f;
			const float Overhang = Spec.ColumnRadius * 1.5f;

			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				Mesh.AddBox(
					Vector3(0.0f, ColumnTop + BandHalf, HalfDepth * float(Sign)),
					Vector3(HalfWidth + Overhang, BandHalf, Overhang * 0.6f),
					Spec.BracketColor);
				Mesh.AddBox(
					Vector3(HalfWidth * float(Sign), ColumnTop + BandHalf, 0.0f),
					Vector3(Overhang * 0.6f, BandHalf, HalfDepth),
					Spec.BracketColor);
			}

			// 柱头铺作 over each column, and one 补间铺作 between them — the alternation is what
			// makes a bracket band read as 斗拱 rather than as a cornice.
			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				for (size_t Index = 0; Index < XPositions.size(); ++Index)
				{
					AddBracketSet(Spec, Vector3(XPositions[Index], ColumnTop, HalfDepth * float(Sign)), true, Mesh);
				}

				for (size_t Index = 0; Index + 1 < ZPositions.size(); ++Index)
				{
					const float Mid = (ZPositions[Index] + ZPositions[Index + 1]) * 0.5f;
					AddBracketSet(Spec, Vector3(HalfWidth * float(Sign), ColumnTop, Mid), false, Mesh);
				}
			}
		}
	}

	static void AddGableSlopeRafters(
		MeshAccumulator& Mesh, const BuildingSpec& Spec, const std::vector<Vector2>& Profile,
		float Sign, float HalfWidth, float RoofBase);

	/**
	 * One flush-gable slope: boarding, tile courses swept along per-course sub-splines, and
	 * the eave drip course. Follows section 7's construction — a surface between ridge curves,
	 * resampled into sub-splines, with tiles instanced along each.
	 */
	void BuildGableSlope(
		const BuildingSpec& Spec,
		const std::vector<Vector2>& Profile,
		float Sign,
		float HalfWidth,
		MeshAccumulator& Mesh)
	{
		const float RoofBase = Spec.RoofBase;
		const float HalfSpan = Profile.front().x;
		const float Thickness = GetBoardThickness(Spec);
		const Color BoardColor = Spec.TileColor * 0.7f;
		const Color SoffitColor = Spec.TimberColor * 1.15f;

		// 望板: the boarding deck on the rafters, top face and soffit together. It is tinted like
		// the tiles it carries, but it is the timber under them, so it takes the timber slot.
		Mesh.SetSlot(EMaterialSlot::Timber);

		// Boarding, as one quad strip per rafter course, each a sandwich with its 望板 soffit.
		for (size_t Index = 0; Index + 1 < Profile.size(); ++Index)
		{
			const Vector2& Low = Profile[Index];
			const Vector2& High = Profile[Index + 1];

			const Vector3 A(-HalfWidth, RoofBase + Low.y, Sign * Low.x);
			const Vector3 B(HalfWidth, RoofBase + Low.y, Sign * Low.x);
			const Vector3 C(HalfWidth, RoofBase + High.y, Sign * High.x);
			const Vector3 D(-HalfWidth, RoofBase + High.y, Sign * High.x);

			Vector3 Normal;
			Vector3 VertexNormals[4];
			if (Spec.RoofCurveMode == 1)
			{
				// The analytic curve normal sampled at each band's two span coordinates and tilted
				// into this slope's face. Adjacent bands share their boundary values, so the 望板
				// shades as one continuous surface instead of stepping per course.
				const Vector2 N2Low = RoofCurveNormalAtX(Spec, HalfSpan, Spec.RoofHeight, Low.x);
				const Vector2 N2High = RoofCurveNormalAtX(Spec, HalfSpan, Spec.RoofHeight, High.x);
				VertexNormals[0] = Vector3(0.0f, N2Low.y, Sign * N2Low.x);
				VertexNormals[1] = VertexNormals[0];
				VertexNormals[2] = Vector3(0.0f, N2High.y, Sign * N2High.x);
				VertexNormals[3] = VertexNormals[2];
				Normal = (VertexNormals[0] + VertexNormals[2]).normalized();
			}
			else
			{
				// Up-slope means z decreases, so the surface normal is up and outward along Sign.
				const Vector3 Up = Vector3(0.0f, High.y - Low.y, Sign * (High.x - Low.x));
				Normal = Vector3(1.0f, 0.0f, 0.0f).cross(Up).normalized();
				if (Normal.y < 0.0f)
				{
					Normal = -Normal;
				}
			}

			AddRoofPanel(
				Mesh, A, B, C, D, Normal, Thickness,
				Index == 0 ? ERoofPanelEdges::Lower : ERoofPanelEdges::None,
				BoardColor, SoffitColor,
				Spec.RoofCurveMode == 1 ? VertexNormals : nullptr);
		}

		// Tile skin. Cr measures coverage down from the ridge, so drop the lower knots.
		AddGableSlopeRafters(Mesh, Spec, Profile, Sign, HalfWidth, RoofBase);
		const int32_t Courses = std::max(int32_t((HalfWidth * 2.0f) / std::fmax(Spec.TileCourseWidth, 0.02f)), 1);
		const float Pitch = (HalfWidth * 2.0f) / float(Courses);
		const size_t KeepFrom = RoofCoverageStart(Spec, Profile, HalfSpan);
		Vector2 CoverageBoundary;
		const bool bCoverageBoundary = RoofCoverageBoundary(Spec, Profile, HalfSpan, KeepFrom, CoverageBoundary);

		{
			// 包络 (R14.1): both ends of this band are the 垂脊 down the gable verges, so the courses
			// are cut inside their footprints at both ends instead of running to whichever edge the
			// layout happened to stop on.
			const float VergeRidge = Spec.Module * 0.85f * Spec.RidgeScale;
			const TileCourseLayout Band = TileBandFor(
				Spec, -HalfWidth, HalfWidth, Pitch, Courses, VergeRidge, VergeRidge);

			std::vector<TileSkinColumn> Columns;
			LayTileCourses(
				Spec.TileDetail,
				Band,
				Courses,
				[&Profile, RoofBase, Sign, KeepFrom, bCoverageBoundary, &CoverageBoundary](float X) -> std::vector<Vector3>
				{
					std::vector<Vector3> Points;
					Points.reserve(Profile.size() - KeepFrom + 1);
					// The exact coverage boundary sits on the curve, not on a sample, so the bare
					// edge of the roof stays put however the sampling density changes.
					if (bCoverageBoundary)
					{
						Points.push_back(Vector3(X, RoofBase + CoverageBoundary.y, Sign * CoverageBoundary.x));
					}
					for (size_t Index = KeepFrom; Index < Profile.size(); ++Index)
					{
						Points.push_back(Vector3(X, RoofBase + Profile[Index].y, Sign * Profile[Index].x));
					}

					return Points;
				},
				Columns);

			// Cr below 1 bares the roof from the eave upward, which leaves column 0 partway up the
			// slope with no eave to dress.
			Mesh.SetSlot(EMaterialSlot::Tile);
			BuildTileSkin(
				Columns,
				ETileSkinLoop::Open,
				KeepFrom == 0 ? ETileEaves::AtStart : ETileEaves::None,
				TileSkinSettingsFor(Spec),
				Spec.TileColor,
				Mesh);
		}

		// 连檐 board along the bottom edge, tucked under the eave tiles.
		{
			Mesh.SetSlot(EMaterialSlot::Timber);
			const Vector2& Eave = Profile.front();
			std::vector<Vector3> Knots;
			Knots.push_back(TuckedEaveKnot(
				Spec, Vector3(-HalfWidth, RoofBase + Eave.y, Sign * Eave.x), Sign, 0.0f));
			Knots.push_back(TuckedEaveKnot(
				Spec, Vector3(HalfWidth, RoofBase + Eave.y, Sign * Eave.x), Sign, 0.0f));

			SweepSettings Settings;
			Settings.Contour = MakeEaveContour(EaveBoardScale(Spec));
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 垂脊 down the gable edge on this slope.
		// Detailed verges are emitted as one eave-to-eave run by BuildGabledRoof.
		if (Spec.RidgeDetail >= 1) { return; }
		Mesh.SetSlot(EMaterialSlot::Ridge);
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			std::vector<Vector3> Knots;
			for (const Vector2& Point : Profile)
			{
				Knots.push_back(Vector3(HalfWidth * float(Side), RoofBase + Point.y, Sign * Point.x));
			}
			// 高度链 (R17): the 垂脊 bears on the tiled face, so it comes up with it.
			LiftAlongKnotNormals(Knots, RoofBeddingLift(Spec));

			const float VergeScale = Spec.Module * 0.85f * Spec.RidgeScale;

			SweepSettings Settings;
			ConfigureRidgeSweep(Settings, Spec, VergeScale, true);
			Settings.bClosedContour = true;
			Settings.UpReference = Vector3(0, 1, 0);

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}

			// 垂兽 + 走兽 at the eave end. The knots run eave first, so that end is bit 0.
			SeatRidgeBeasts(Mesh, Spec, Knots, VergeScale, 0x1);
		}
	}

	// Eave rafter heads under this slope, from the shared eave section: even spacing along
	// the eave line, each head a straight timber running back up the slope along the eave
	// tangent.
	static void AddGableSlopeRafters(
		MeshAccumulator& Mesh, const BuildingSpec& Spec, const std::vector<Vector2>& Profile,
		float Sign, float HalfWidth, float RoofBase)
	{
		const float TangentSlope = (Profile[1].y - Profile[0].y)
			/ std::fmax(Profile[0].x - Profile[1].x, BUILD_EPSILON);
		const float Pitch = Spec.Module * 0.7f;
		const int32_t Count = std::max(int32_t((HalfWidth * 2.0f) / std::fmax(Pitch, 0.05f)), 1);
		const float Step = (HalfWidth * 2.0f) / float(Count);

		std::vector<Vector3> Points;
		std::vector<Vector2> Inward;
		Points.reserve(size_t(Count));
		Inward.reserve(size_t(Count));
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			const float X = -HalfWidth + (float(Index) + 0.5f) * Step;
			Points.push_back(Vector3(X, RoofBase, Sign * Profile.front().x));
			Inward.push_back(Vector2(0.0f, -Sign));
		}

		AddEaveRafterHeads(Mesh, Spec, Points, Inward, TangentSlope, nullptr, Spec.TimberColor * 1.28f);
	}

/** How far the hipped shell has closed in at a profile distance: 0 at the eave, 1 at the break. */
	float SkirtInsetFraction(float Distance, float Inset)
	{
		return 1.0f - std::fmin(Distance / std::fmax(Inset, BUILD_EPSILON), 1.0f);
	}

	/**
	 * One face of a hipped skirt, and the source of that face's tile skin columns.
	 *
	 * A face narrows as the shell rises, so a column laid at a fixed offset along the eave
	 * eventually runs off the side and has to stop on the hip line. Holding the offset — rather
	 * than a fraction of the narrowing width — is what keeps the courses parallel and square to
	 * the eave, as real 瓦垄 are.
	 */
	struct SkirtFace
	{
		const std::vector<Vector2>* Profile = nullptr;
		const CornerFlip* Flip = nullptr;

		/** Whether the face's eave runs along X; otherwise along Z. */
		bool bAlongX = true;

		/** Which of the two opposing faces this is. */
		float Sign = 1.0f;

		/** Half-extent along the eave, and the distance out to the eave across it. */
		float Extent = 0.0f;
		float OppositeExtent = 0.0f;

		float Inset = 0.0f;
		float RoofBase = 0.0f;

		Vector3 PointAt(float Along, float Fraction, float Y) const
		{
			const float Out = OppositeExtent - Inset * Fraction;
			const Vector3 Point = bAlongX
				? Vector3(Along, Y, Sign * Out)
				: Vector3(Sign * Out, Y, Along);

			return Flip->Apply(Point);
		}

		std::vector<Vector3> Column(float Along) const
		{
			const std::vector<Vector2>& Steps = *Profile;

			std::vector<Vector3> Points;
			Points.reserve(Steps.size());

			for (size_t Step = 0; Step < Steps.size(); ++Step)
			{
				const float Fraction = SkirtInsetFraction(Steps[Step].x, Inset);
				// The face narrows as the shell rises. Once it passes this column, the column has
				// reached the hip.
				const float Cross = Extent - Inset * Fraction;
				if (std::abs(Along) > Cross)
				{
					// 翼角切瓦: land the column exactly on the hip line rather than stopping at the
					// previous ring, which left a serrated edge the 戗脊 could not fully hide.
					// Real corners use cut tiles for this.
					if (Step > 0)
					{
						const float PrevFraction = SkirtInsetFraction(Steps[Step - 1].x, Inset);
						const float PrevCross = Extent - Inset * PrevFraction;
						const float Span = std::fmax(PrevCross - Cross, BUILD_EPSILON);
						const float T = (PrevCross - std::abs(Along)) / Span;

						const float HitFraction = PrevFraction + (Fraction - PrevFraction) * T;
						const float HitY = RoofBase + Steps[Step - 1].y
							+ (Steps[Step].y - Steps[Step - 1].y) * T;
						Points.push_back(PointAt(Along, HitFraction, HitY));
					}
					break;
				}

				Points.push_back(PointAt(Along, Fraction, RoofBase + Steps[Step].y));
			}

			return Points;
		}
	};

	/**
	 * The hipped family, 歇山 and 庑殿 in one construction.
	 *
	 * A hipped shell is lofted between the eave rectangle and an inner rectangle inset by the
	 * same distance in X and Z, which is what puts the hip ridges at 45 degrees in plan and so
	 * makes all four slopes share one pitch. Lofting rings also yields the four faces *and* the
	 * four corner wedges in one pass, with no corner special-casing.
	 *
	 * Three terminations share it, which is why the whole family costs so little:
	 *  - 歇山 stops at the 收山 break and puts a gabled tier with 山花 above.
	 *  - 庑殿 takes the shell all the way in, so the inner rectangle collapses to a line, and
	 *    that line *is* the main ridge. 歇山's 戗脊 become 庑殿's 垂脊 unchanged.
	 *  - 盝顶 stops early and caps the opening with a flat platform ringed by a 围脊.
	 *
	 * A square plan taken to full hip degenerates the ridge to a point — correctly, since that
	 * is a 攒尖 pyramid — so the main ridge is skipped when it has no length.
	 */
	void BuildHippedRoof(const BuildingSpec& Spec, EHipTop Top, MeshAccumulator& Mesh)
	{
		const bool bFullHip = Top == HIP_TOP_RIDGE;
		const bool bFlatTop = Top == HIP_TOP_FLAT;
		const float RoofBase = Spec.RoofBase;
		const float HalfWidthEave = Spec.Width * 0.5f + Spec.EaveOverhang;
		const float HalfDepthEave = Spec.Depth * 0.5f + Spec.EaveOverhang;

		// How far the shell closes in: all the way for 庑殿, to the 收山 break for 歇山, and
		// only as far as the flat platform for 盝顶.
		const float TopRatio = bFlatTop
			? (1.0f - std::fmin(std::fmax(Spec.FlatTopRatio, 0.05f), 0.9f))
			: std::fmin(std::fmax(Spec.GableRatio, 0.05f), 0.9f);
		const float Inset = bFullHip
			? HalfDepthEave
			: std::fmin(
				HalfDepthEave * TopRatio,
				std::fmin(HalfWidthEave, HalfDepthEave) * 0.9f);
		const float HalfWidthBreak = HalfWidthEave - Inset;
		const float HalfDepthBreak = HalfDepthEave - Inset;

		CornerFlip Flip;
		Flip.Rise = Spec.CornerRise;
		Flip.Extend = Spec.CornerExtend;
		Flip.Span = Spec.CornerSpan;
		Flip.HalfWidth = HalfWidthEave;
		Flip.HalfDepth = HalfDepthEave;

		// One curve over the full depth, so the skirt and the tier stay continuous.
		const std::vector<Vector2> Full = BuildRoofProfile(Spec, HalfDepthEave);

		const auto HeightAt = [&Full](float Distance) -> float
		{
			for (size_t Index = 0; Index + 1 < Full.size(); ++Index)
			{
				const float High = Full[Index].x;
				const float Low = Full[Index + 1].x;
				if (Distance <= High && Distance >= Low)
				{
					const float Span = std::fmax(High - Low, BUILD_EPSILON);
					const float T = (High - Distance) / Span;
					return Full[Index].y + (Full[Index + 1].y - Full[Index].y) * T;
				}
			}

			return Full.back().y;
		};

		const float BreakHeight = HeightAt(HalfDepthBreak);
		const std::vector<Vector2> SkirtProfile = BuildRoofProfileScaled(Spec, Inset, BreakHeight);

		// Enough samples per side that the corner lift curves instead of kinking.
		const int32_t SamplesX = std::max(int32_t(HalfWidthEave * 2.0f / std::fmax(Spec.Module * 0.7f, 0.05f)), 4);
		const int32_t SamplesZ = std::max(int32_t(HalfDepthEave * 2.0f / std::fmax(Spec.Module * 0.7f, 0.05f)), 4);

		// A closed ring of plan positions, walking the +Z, +X, -Z then -X sides.
		const auto BuildRing = [&](float HalfW, float HalfD) -> std::vector<Vector2>
		{
			std::vector<Vector2> Ring;
			for (int32_t Index = 0; Index < SamplesX; ++Index)
			{
				const float T = float(Index) / float(SamplesX);
				Ring.push_back(Vector2(-HalfW + 2.0f * HalfW * T, HalfD));
			}
			for (int32_t Index = 0; Index < SamplesZ; ++Index)
			{
				const float T = float(Index) / float(SamplesZ);
				Ring.push_back(Vector2(HalfW, HalfD - 2.0f * HalfD * T));
			}
			for (int32_t Index = 0; Index < SamplesX; ++Index)
			{
				const float T = float(Index) / float(SamplesX);
				Ring.push_back(Vector2(HalfW - 2.0f * HalfW * T, -HalfD));
			}
			for (int32_t Index = 0; Index < SamplesZ; ++Index)
			{
				const float T = float(Index) / float(SamplesZ);
				Ring.push_back(Vector2(-HalfW, -HalfD + 2.0f * HalfD * T));
			}

			return Ring;
		};

		// Skirt.x runs from Inset down to 0, so this is 0 at the eave and 1 at the break.
		const auto InsetFraction = [Inset](float Distance) -> float
		{
			return SkirtInsetFraction(Distance, Inset);
		};

		// ---- Hipped skirt, lofted between the eave and break rectangles ----

		const float Thickness = GetBoardThickness(Spec);
		const Color BoardColor = Spec.TileColor * 0.7f;
		const Color SoffitColor = Spec.TimberColor * 1.15f;

		// 望板 over the whole skirt, as in the gabled family.
		Mesh.SetSlot(EMaterialSlot::Timber);

		std::vector<std::vector<Vector3>> Rings;
		Rings.reserve(SkirtProfile.size());
		for (const Vector2& Step : SkirtProfile)
		{
			const float Fraction = InsetFraction(Step.x);
			const std::vector<Vector2> Plan = BuildRing(
				HalfWidthEave - Inset * Fraction, HalfDepthEave - Inset * Fraction);

			std::vector<Vector3> Ring;
			Ring.reserve(Plan.size());
			for (const Vector2& Point : Plan)
			{
				Ring.push_back(Flip.Apply(Vector3(Point.x, RoofBase + Step.y, Point.y)));
			}
			Rings.push_back(Ring);
		}

		for (size_t Level = 0; Level + 1 < Rings.size(); ++Level)
		{
			const std::vector<Vector3>& Low = Rings[Level];
			const std::vector<Vector3>& High = Rings[Level + 1];
			const size_t Count = Low.size();

			for (size_t Index = 0; Index < Count; ++Index)
			{
				const size_t Next = (Index + 1) % Count;

				Vector3 Normal;
				Vector3 VertexNormals[4];
				if (Spec.RoofCurveMode == 1)
				{
					// The analytic profile normal at each ring's span coordinate, tilted outward by
					// each corner's own azimuth. Adjacent bands share their boundary values, so the
					// skirt shades continuously instead of stepping per course. The corner wedges
					// get the same tilt with their diagonal azimuth — an approximation, since their
					// true normal also picks up the ring curvature, but the 戗脊 covers the seam.
					const Vector2 N2Low = RoofCurveNormalAtX(Spec, Inset, BreakHeight, SkirtProfile[Level].x);
					const Vector2 N2High = RoofCurveNormalAtX(
						Spec, Inset, BreakHeight, SkirtProfile[Level + 1].x);
					const Vector3 Corners[4] = { Low[Index], Low[Next], High[Next], High[Index] };
					const Vector2* LevelNormals[4] = { &N2Low, &N2Low, &N2High, &N2High };
					for (int32_t K = 0; K < 4; ++K)
					{
						const Vector2 P(Corners[K].x, Corners[K].z);
						if (P.length_squared() > 1e-12f)
						{
							const Vector2 Dir = P.normalized();
							VertexNormals[K] = Vector3(
								Dir.x * LevelNormals[K]->x, LevelNormals[K]->y, Dir.y * LevelNormals[K]->x);
						}
						else
						{
							VertexNormals[K] = Vector3(0.0f, 1.0f, 0.0f);
						}
					}
					Normal = (VertexNormals[0] + VertexNormals[1] + VertexNormals[2] + VertexNormals[3]).normalized();
				}
				else
				{
					Normal = (Low[Next] - Low[Index]).cross(High[Index] - Low[Index]);
					if (Normal.length_squared() < 1e-12f)
					{
						continue;
					}
					Normal = Normal.normalized();
					// A roof surface always faces upward.
					if (Normal.y < 0.0f)
					{
						Normal = -Normal;
					}
				}

				AddRoofPanel(
					Mesh,
					Low[Index], Low[Next], High[Next], High[Index],
					Normal, Thickness,
					Level == 0 ? ERoofPanelEdges::Lower : ERoofPanelEdges::None,
					BoardColor, SoffitColor,
					Spec.RoofCurveMode == 1 ? VertexNormals : nullptr);
			}
		}

		// Tile skin on the four skirt faces. The corner wedges keep boarding only and the
		// 戗脊 sits over that seam, which is how a real corner hides its fan of cut tiles.
		{
			struct Face
			{
				bool bAlongX;
				float Sign;
			};
			const Face Faces[4] = { { true, 1.0f }, { true, -1.0f }, { false, 1.0f }, { false, -1.0f } };

			for (const Face& Current : Faces)
			{
				// Space courses over the eave edge and let each column clip itself where it runs
				// off the face. That tiles the corner wedges too, and on 庑殿 it tiles the
				// triangular hip ends, which a constant top extent would have left bare.
				SkirtFace Skirt;
				Skirt.Profile = &SkirtProfile;
				Skirt.Flip = &Flip;
				Skirt.bAlongX = Current.bAlongX;
				Skirt.Sign = Current.Sign;
				Skirt.Extent = Current.bAlongX ? HalfWidthEave : HalfDepthEave;
				Skirt.OppositeExtent = Current.bAlongX ? HalfDepthEave : HalfWidthEave;
				Skirt.Inset = Inset;
				Skirt.RoofBase = RoofBase;

				const int32_t Courses = std::max(
					int32_t(Skirt.Extent * 2.0f / std::fmax(Spec.TileCourseWidth, 0.05f)), 1);
				const float Pitch = Skirt.Extent * 2.0f / float(Courses);

				// 包络: the band ends under the two diagonal ridges that carry this face's corners —
				// 戗脊 on 歇山, 垂脊 on 庑殿 — at the section those are swept with.
				const float DiagonalRidge = Spec.Module * 0.85f * Spec.RidgeScale;
				const TileCourseLayout Band = TileBandFor(
					Spec, -Skirt.Extent, Skirt.Extent, Pitch, Courses, DiagonalRidge, DiagonalRidge);

				std::vector<TileSkinColumn> Columns;
				LayTileCourses(
					Spec.TileDetail,
					Band,
					Courses,
					[&Skirt](float Along) -> std::vector<Vector3> { return Skirt.Column(Along); },
					Columns);

				Mesh.SetSlot(EMaterialSlot::Tile);
				BuildTileSkin(
					Columns, ETileSkinLoop::Open, ETileEaves::AtStart, TileSkinSettingsFor(Spec),
					Spec.TileColor, Mesh);
			}
		}

		// ---- Gabled tier above the break ----

		const float TierBase = RoofBase + BreakHeight;
		const float TierRise = std::fmax(Spec.RoofHeight - BreakHeight, 0.01f);
		const std::vector<Vector2> TierProfile = (Top == HIP_TOP_GABLED_TIER)
			? BuildRoofProfileScaled(Spec, HalfDepthBreak, TierRise)
			: std::vector<Vector2>();

		for (int32_t Sign = -1; Sign <= 1 && Top == HIP_TOP_GABLED_TIER; Sign += 2)
		{
			Mesh.SetSlot(EMaterialSlot::Timber);
			for (size_t Index = 0; Index + 1 < TierProfile.size(); ++Index)
			{
				const Vector2& Low = TierProfile[Index];
				const Vector2& High = TierProfile[Index + 1];

				const Vector3 A(-HalfWidthBreak, TierBase + Low.y, float(Sign) * Low.x);
				const Vector3 B(HalfWidthBreak, TierBase + Low.y, float(Sign) * Low.x);
				const Vector3 C(HalfWidthBreak, TierBase + High.y, float(Sign) * High.x);
				const Vector3 D(-HalfWidthBreak, TierBase + High.y, float(Sign) * High.x);

				// Continuous mode smooth-shades the tier with the analytic curve normal sampled at
				// each band's two span coordinates. Legacy shade came from the per-band geometric
				// facets, so the tier read as a stack of flat courses.
				Vector3 Normal;
				Vector3 VertexNormals[4];
				if (Spec.RoofCurveMode == 1)
				{
					const Vector2 N2Low = RoofCurveNormalAtX(Spec, HalfDepthBreak, TierRise, Low.x);
					const Vector2 N2High = RoofCurveNormalAtX(Spec, HalfDepthBreak, TierRise, High.x);
					VertexNormals[0] = Vector3(0.0f, N2Low.y, float(Sign) * N2Low.x);
					VertexNormals[1] = VertexNormals[0];
					VertexNormals[2] = Vector3(0.0f, N2High.y, float(Sign) * N2High.x);
					VertexNormals[3] = VertexNormals[2];
					Normal = (VertexNormals[0] + VertexNormals[2]).normalized();
				}
				else
				{
					Normal = Vector3(0.0f, 1.0f, float(Sign)).normalized();
				}

				// The tier's lower edge lands on the skirt below it, so there is no open eave to cap.
				AddRoofPanel(
					Mesh, A, B, C, D, Normal,
					Thickness, ERoofPanelEdges::None, BoardColor, SoffitColor,
					Spec.RoofCurveMode == 1 ? VertexNormals : nullptr);
			}

			const int32_t Courses = std::max(
				int32_t(HalfWidthBreak * 2.0f / std::fmax(Spec.TileCourseWidth, 0.05f)), 1);
			const float Pitch = HalfWidthBreak * 2.0f / float(Courses);
			const size_t KeepFrom = RoofCoverageStart(Spec, TierProfile, HalfDepthBreak);
			Vector2 CoverageBoundary;
			const bool bCoverageBoundary = RoofCoverageBoundary(
				Spec, TierProfile, HalfDepthBreak, KeepFrom, CoverageBoundary);

			// 包络: the tier's two ends are the 垂脊 that run down its gable verges; the 山花板 sits
			// behind them at the same X, so the courses are cut inside the 垂脊 footprints.
			const float TierVergeRidge = Spec.Module * 0.8f * Spec.RidgeScale;
			const TileCourseLayout Band = TileBandFor(Spec, -HalfWidthBreak, HalfWidthBreak,
				Pitch, Courses, TierVergeRidge, TierVergeRidge);

			std::vector<TileSkinColumn> Columns;
			LayTileCourses(
				Spec.TileDetail,
				Band,
				Courses,
				[&TierProfile, TierBase, Sign, KeepFrom, bCoverageBoundary, &CoverageBoundary](float X) -> std::vector<Vector3>
				{
					std::vector<Vector3> Points;
					Points.reserve(TierProfile.size() - KeepFrom + 1);
					if (bCoverageBoundary)
					{
						Points.push_back(Vector3(
							X, TierBase + CoverageBoundary.y, float(Sign) * CoverageBoundary.x));
					}
					for (size_t Index = KeepFrom; Index < TierProfile.size(); ++Index)
					{
						Points.push_back(Vector3(
							X, TierBase + TierProfile[Index].y, float(Sign) * TierProfile[Index].x));
					}

					return Points;
				},
				Columns);

			// The tier's lower edge is the 收山 break sitting on the skirt below it, closed by a
			// 博脊 — not an eave, so it gets no 瓦当 or 滴水.
			Mesh.SetSlot(EMaterialSlot::Tile);
			BuildTileSkin(Columns, ETileSkinLoop::Open, ETileEaves::None, TileSkinSettingsFor(Spec),
				Spec.TileColor, Mesh);
		}

		// 山花, the vertical tympanum closing each end of the tier. Only 歇山 has one.
		Mesh.SetSlot(EMaterialSlot::Gable);
		for (int32_t Side = -1; Side <= 1 && Top == HIP_TOP_GABLED_TIER; Side += 2)
		{
			const float X = HalfWidthBreak * float(Side);
			std::vector<Vector3> Points;
			for (size_t Index = 0; Index < TierProfile.size(); ++Index)
			{
				Points.push_back(Vector3(X, TierBase + TierProfile[Index].y, TierProfile[Index].x));
			}
			for (size_t Index = TierProfile.size(); Index-- > 0;)
			{
				Points.push_back(Vector3(X, TierBase + TierProfile[Index].y, -TierProfile[Index].x));
			}
			Mesh.AddPolygon(Points, Vector3(float(Side), 0.0f, 0.0f), Spec.PlasterColor * 0.9f);
		}

		// ---- Ridges ----

		const float Apex = (Top == HIP_TOP_GABLED_TIER) ? (TierBase + TierProfile.back().y) : TierBase;
		// 高度链 (R17): every ridge on this roof bears on the tiled faces, so they all come up with
		// the 泥背 layer instead of sinking into it. 0 unless the tier builds a bedded skin.
		const float Bedding = RoofBeddingLift(Spec);

		// 盝顶: cap the opening with a flat platform and ring it with a 围脊.
		if (bFlatTop)
		{
			// The 盝顶 deck is the roof's own weathering surface, tinted with the tiles, so it
			// goes on the tile slot; only the 围脊 ringed round it is ridge.
			Mesh.SetSlot(EMaterialSlot::Tile);
			std::vector<Vector3> Cap;
			const std::vector<Vector2> Plan = BuildRing(HalfWidthBreak, HalfDepthBreak);
			for (const Vector2& Point : Plan)
			{
				Cap.push_back(Vector3(Point.x, Apex + Bedding, Point.y));
			}
			Mesh.AddPolygon(Cap, Vector3(0, 1, 0), Spec.TileColor * 0.8f);

			Mesh.SetSlot(EMaterialSlot::Ridge);
			std::vector<Vector3> Knots;
			// Tile-layout samples can be closer to a corner than the ridge's half-width.
			// Sweeping through those samples folds the inner miter back on itself. A rectangular
			// surround needs only its four actual corners, irrespective of tile sampling density.
			const std::vector<Vector2> RidgePlan = Spec.RidgeDetail >= 1
				? std::vector<Vector2>{ Vector2(-HalfWidthBreak, HalfDepthBreak),
					Vector2(HalfWidthBreak, HalfDepthBreak), Vector2(HalfWidthBreak, -HalfDepthBreak),
					Vector2(-HalfWidthBreak, -HalfDepthBreak) }
				: Plan;
			for (const Vector2& Point : RidgePlan)
			{
				Knots.push_back(Vector3(Point.x, Apex + Bedding, Point.y));
			}
			Knots.push_back(Knots.front());

			SweepSettings Settings;
			ConfigureRidgeSweep(Settings, Spec, Spec.Module * 1.05f * Spec.RidgeScale);
			Settings.bClosedContour = true;
			Settings.bGenerateCaps = false;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 正脊 along the apex. On a square 庑殿 plan the ridge has no length — that is a 攒尖
		// pyramid, and the four diagonal ridges already meet at the point.
		Mesh.SetSlot(EMaterialSlot::Ridge);
		if (!bFlatTop && HalfWidthBreak > Spec.Module * 0.15f)
		{
			std::vector<Vector3> Knots;
			Knots.push_back(Vector3(-HalfWidthBreak, Apex, 0.0f));
			Knots.push_back(Vector3(HalfWidthBreak, Apex, 0.0f));
			if (Spec.RidgeDetail >= 1 && Top == HIP_TOP_GABLED_TIER)
			{
				const float HeadOverhang = Spec.Module * 0.8f * Spec.RidgeScale * 0.5f;
				Knots.front().x -= HeadOverhang;
				Knots.back().x += HeadOverhang;
			}
			LiftVertically(Knots, Bedding);

			const float HipMainScale = Spec.Module * 1.35f * Spec.RidgeScale;

			SweepSettings Settings;
			ConfigureRidgeSweep(Settings, Spec, HipMainScale);
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}

			// 正吻 at both ends — the 庑殿's 正脊 is the one this class is for.
			SeatRidgeFinials(Mesh, Spec, Knots, HipMainScale);
		}

		// 垂脊 down each edge of the gabled tier. Only 歇山 has one.
		for (int32_t Side = -1; Side <= 1 && Top == HIP_TOP_GABLED_TIER; Side += 2)
		{
			for (int32_t Sign = -1; Sign <= 1; Sign += 2)
			{
				if (Spec.RidgeDetail >= 1 && Sign > 0) { continue; }
				std::vector<Vector3> Knots;
				for (size_t Index = TierProfile.size(); Index-- > 0;)
				{
					Knots.push_back(Vector3(
						HalfWidthBreak * float(Side),
						TierBase + TierProfile[Index].y,
						float(Sign) * TierProfile[Index].x));
				}
				if (Spec.RidgeDetail >= 1)
				{
					std::reverse(Knots.begin(), Knots.end());
					for (size_t I = TierProfile.size() - 1; I-- > 0;)
					{
						Knots.push_back(Vector3(HalfWidthBreak * float(Side),
							TierBase + TierProfile[I].y, TierProfile[I].x));
					}
				}
				LiftAlongKnotNormals(Knots, Bedding);

				const float TierVergeScale = Spec.Module * 0.8f * Spec.RidgeScale;

				SweepSettings Settings;
				ConfigureRidgeSweep(Settings, Spec, TierVergeScale, true);
				Settings.bClosedContour = true;
				Settings.UpReference = Vector3(0, 1, 0);

				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					Mesh.AddSweep(Sweep, Spec.RidgeColor);
				}

				// The knots run backwards from the tier's break line, so the end that dies into the
				// 戗脊 is the last one. 歇山 puts its 垂兽 there, where the two ridges meet.
				SeatRidgeBeasts(Mesh, Spec, Knots, TierVergeScale, Spec.RidgeDetail >= 1 ? 0x3 : 0x2);
			}
		}

		// The diagonal ridges: 戗脊 on 歇山, 垂脊 on 庑殿. Same curve either way — from the inner
		// rectangle corner out and down to the flipped eave corner.
		for (int32_t SideX = -1; SideX <= 1; SideX += 2)
		{
			for (int32_t SideZ = -1; SideZ <= 1; SideZ += 2)
			{
				std::vector<Vector3> Knots;
				for (size_t Index = SkirtProfile.size(); Index-- > 0;)
				{
					const float Fraction = InsetFraction(SkirtProfile[Index].x);
					const Vector3 Point(
						float(SideX) * (HalfWidthEave - Inset * Fraction),
						RoofBase + SkirtProfile[Index].y,
						float(SideZ) * (HalfDepthEave - Inset * Fraction));
					Knots.push_back(Flip.Apply(Point));
				}
				// Two faces meet along a diagonal, so it follows the plane they share rather than
				// either one of them.
				LiftAlongKnotNormals(Knots, Bedding);

				const float DiagonalScale = Spec.Module * 0.85f * Spec.RidgeScale;

				SweepSettings Settings;
				ConfigureRidgeSweep(Settings, Spec, DiagonalScale, true);
				Settings.bClosedContour = true;
				Settings.UpReference = Vector3(0, 1, 0);

				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					Mesh.AddSweep(Sweep, Spec.RidgeColor);
				}

				// 戗脊 (歇山) / 垂脊 (庑殿): built from the skirt profile backwards, so its eave corner
				// is the last knot. That corner is where the 垂兽 goes and the 走兽 walk up from.
				SeatRidgeBeasts(Mesh, Spec, Knots, DiagonalScale, 0x2);
			}
		}

		// 连檐 board all the way round the flipped eave, tucked under the eave tiles. Closing the
		// loop is what makes the last corner miter against the first side rather than butt-ending.
		{
			Mesh.SetSlot(EMaterialSlot::Timber);
			// Inset and dropped rather than sitting on the eave line, where at full size it used to
			// stand in front of the tile ends and hide every 瓦当 and 滴水 behind it.
			const float Back = Spec.Module * 0.26f;

			const std::vector<Vector2> Plan = BuildRing(HalfWidthEave - Back, HalfDepthEave - Back);
			std::vector<Vector3> Knots;
			for (const Vector2& Point : Plan)
			{
				Knots.push_back(Flip.Apply(Vector3(Point.x, RoofBase - Spec.Module * 0.20f, Point.y)));
			}
			Knots.push_back(Knots.front());

			SweepSettings Settings;
			Settings.Contour = MakeEaveContour(EaveBoardScale(Spec));
			Settings.bClosedContour = true;
			Settings.bGenerateCaps = false;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// Eave rafter heads under the eave ring, at the ring sample midpoints so the corners
		// fan like real corner rafters; the ridge sweeps cover the seam where the fan converges.
		{
			const std::vector<Vector2> Ring = BuildRing(HalfWidthEave, HalfDepthEave);
			const float TangentSlope = (SkirtProfile[1].y - SkirtProfile[0].y)
				/ std::fmax(SkirtProfile[0].x - SkirtProfile[1].x, BUILD_EPSILON);

			std::vector<Vector3> Points;
			std::vector<Vector2> Inward;
			for (size_t Index = 0; Index + 1 < Ring.size(); ++Index)
			{
				const Vector2 Mid = (Ring[Index] + Ring[Index + 1]) * 0.5f;
				if (Mid.length_squared() < 1e-12f)
				{
					continue;
				}
				Points.push_back(Vector3(Mid.x, RoofBase, Mid.y));
				Inward.push_back(-Mid.normalized());
			}

			AddEaveRafterHeads(Mesh, Spec, Points, Inward, TangentSlope, &Flip, Spec.TimberColor * 1.28f);
		}
	}

	/**
	 * 卷棚. One continuous profile from the +Z eave, over a rolled ridge of radius RollRadius,
	 * down to the -Z eave, so a single tile course spans the whole roof and the ridge needs no
	 * separate 正脊. The slopes are built to the tangent point and joined by a semicircle.
	 */
	void BuildRolledGable(const BuildingSpec& Spec, float HalfWidth, float HalfSpan, MeshAccumulator& Mesh)
	{
		const float RoofBase = Spec.RoofBase;
		const float Roll = std::fmin(Spec.RollRadius, std::fmin(HalfSpan * 0.5f, Spec.RoofHeight * 0.6f));
		const float SlopeRise = std::fmax(Spec.RoofHeight - Roll, 0.01f);

		// Slope from the eave up to where the roll begins.
		const std::vector<Vector2> Slope = BuildRoofProfileScaled(Spec, HalfSpan - Roll, SlopeRise);

		// Continuous profile: +Z slope, the roll, then the mirrored -Z slope.
		std::vector<Vector2> Profile;
		for (const Vector2& Step : Slope)
		{
			Profile.push_back(Vector2(Step.x + Roll, Step.y));
		}

		const float RollCentre = Spec.RoofHeight - Roll;
		const int32_t RollSteps = 8;
		for (int32_t Index = 1; Index < RollSteps; ++Index)
		{
			const float Angle = BUILD_PI * float(Index) / float(RollSteps);
			Profile.push_back(Vector2(Roll * std::cos(Angle), RollCentre + Roll * std::sin(Angle)));
		}

		for (size_t Index = Slope.size(); Index-- > 0;)
		{
			Profile.push_back(Vector2(-(Slope[Index].x + Roll), Slope[Index].y));
		}

		// Boarding. The profile runs eave to eave over the roll, so both ends are open edges.
		const float Thickness = GetBoardThickness(Spec);
		const Color BoardColor = Spec.TileColor * 0.7f;
		const Color SoffitColor = Spec.TimberColor * 1.15f;

		Mesh.SetSlot(EMaterialSlot::Timber);
		for (size_t Index = 0; Index + 1 < Profile.size(); ++Index)
		{
			const Vector2& From = Profile[Index];
			const Vector2& To = Profile[Index + 1];

			const Vector3 A(-HalfWidth, RoofBase + From.y, From.x);
			const Vector3 B(HalfWidth, RoofBase + From.y, From.x);
			const Vector3 C(HalfWidth, RoofBase + To.y, To.x);
			const Vector3 D(-HalfWidth, RoofBase + To.y, To.x);

			// Outward is up and away from the centreline, which flips sign over the roll.
			Vector3 Outward;
			Vector3 VertexNormals[4];
			if (Spec.RoofCurveMode == 1)
			{
				// One normal per corner, sampled at the corner's own span: the slope region uses
				// the curve normal on the unshifted slope span (the mirrored -Z side shares the
				// span coordinate; the sign rides on X), the roll region uses the semicircle's
				// radial normal.
				const auto CornerNormal = [&](float X, float Y) -> Vector3
				{
					if (std::abs(X) >= Roll)
					{
						const Vector2 N2 = RoofCurveNormalAtX(Spec, HalfSpan - Roll, SlopeRise, std::abs(X) - Roll);
						return Vector3(0.0f, N2.y, (X >= 0.0f ? N2.x : -N2.x));
					}

					const Vector2 N2 = Vector2(
						X / std::fmax(Roll, BUILD_EPSILON),
						(Y - RollCentre) / std::fmax(Roll, BUILD_EPSILON)).normalized();
					return Vector3(0.0f, N2.y, N2.x);
				};
				VertexNormals[0] = CornerNormal(From.x, From.y);
				VertexNormals[1] = VertexNormals[0];
				VertexNormals[2] = CornerNormal(To.x, To.y);
				VertexNormals[3] = VertexNormals[2];
				Outward = (VertexNormals[0] + VertexNormals[2]).normalized();
			}
			else
			{
				Outward = Vector3(0.0f, 1.0f, (From.x + To.x) * 0.5f).normalized();
			}

			ERoofPanelEdges Edges = ERoofPanelEdges::None;
			if (Index == 0)
			{
				Edges = Edges | ERoofPanelEdges::Lower;
			}
			if (Index + 2 == Profile.size())
			{
				Edges = Edges | ERoofPanelEdges::Upper;
			}

			AddRoofPanel(
				Mesh, A, B, C, D, Outward, Thickness, Edges, BoardColor, SoffitColor,
				Spec.RoofCurveMode == 1 ? VertexNormals : nullptr);
		}

		// Tile skin running the full span, eave to eave over the roll.
		{
			const int32_t Courses = std::max(int32_t(HalfWidth * 2.0f / std::fmax(Spec.TileCourseWidth, 0.05f)), 1);
			const float Pitch = HalfWidth * 2.0f / float(Courses);

			// 包络: a 卷棚 runs eave to eave over the roll, so both ends of the band are gable
			// verges carrying a 垂脊, exactly as on the ridged roofs.
			const float RollVergeRidge = Spec.Module * 0.85f * Spec.RidgeScale;
			const TileCourseLayout Band = TileBandFor(
				Spec, -HalfWidth, HalfWidth, Pitch, Courses, RollVergeRidge, RollVergeRidge);

			std::vector<TileSkinColumn> Columns;
			LayTileCourses(
				Spec.TileDetail,
				Band,
				Courses,
				[&Profile, RoofBase](float X) -> std::vector<Vector3>
				{
					std::vector<Vector3> Points;
					Points.reserve(Profile.size());
					for (const Vector2& Step : Profile)
					{
						Points.push_back(Vector3(X, RoofBase + Step.y, Step.x));
					}

					return Points;
				},
				Columns);

			// 卷棚's profile runs eave to eave over the roll, so both ends want dressing.
			Mesh.SetSlot(EMaterialSlot::Tile);
			BuildTileSkin(
				Columns, ETileSkinLoop::Open, ETileEaves::AtBothEnds, TileSkinSettingsFor(Spec),
				Spec.TileColor, Mesh);
		}

		// 垂脊 along both gable edges, following the whole rolled profile.
		Mesh.SetSlot(EMaterialSlot::Ridge);
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			std::vector<Vector3> Knots;
			for (const Vector2& Step : Profile)
			{
				Knots.push_back(Vector3(HalfWidth * float(Side), RoofBase + Step.y, Step.x));
			}
			// 高度链 (R17): the 垂脊 bears on the rolled tile surface, so it comes up with it.
			LiftAlongKnotNormals(Knots, RoofBeddingLift(Spec));

			const float RollVergeScale = Spec.Module * 0.85f * Spec.RidgeScale;

			SweepSettings Settings;
			ConfigureRidgeSweep(Settings, Spec, RollVergeScale, true);
			Settings.bClosedContour = true;
			Settings.UpReference = Vector3(0, 1, 0);

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}

			// A 卷棚 runs its 垂脊 eave to eave over the roll: both ends are 檐口 ends, so both get a
			// 垂兽 and a row of 走兽.
			SeatRidgeBeasts(Mesh, Spec, Knots, RollVergeScale, 0x3);
		}

		// 连檐 board on both sides, tucked under the eave tiles.
		Mesh.SetSlot(EMaterialSlot::Timber);
		for (int32_t Sign = -1; Sign <= 1; Sign += 2)
		{
			std::vector<Vector3> Knots;
			Knots.push_back(TuckedEaveKnot(
				Spec, Vector3(-HalfWidth, RoofBase, float(Sign) * HalfSpan), float(Sign), 0.0f));
			Knots.push_back(TuckedEaveKnot(
				Spec, Vector3(HalfWidth, RoofBase, float(Sign) * HalfSpan), float(Sign), 0.0f));

			SweepSettings Settings;
			Settings.Contour = MakeEaveContour(EaveBoardScale(Spec));
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}
		}

		// 山墙 following the wall line, closing the rolled silhouette.
		Mesh.SetSlot(EMaterialSlot::Gable);
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			const float X = Spec.Width * 0.5f * float(Side);
			std::vector<Vector3> Points;
			for (const Vector2& Step : Profile)
			{
				Points.push_back(Vector3(X, RoofBase + Step.y, Step.x));
			}
			Mesh.AddPolygon(Points, Vector3(float(Side), 0.0f, 0.0f), Spec.PlasterColor * 0.94f);
		}

		// Eave rafter heads under both eaves, sharing the section and spacing of every roof.
		for (int32_t Sign = -1; Sign <= 1; Sign += 2)
		{
			const float TangentSlope = (Profile[1].y - Profile[0].y)
				/ std::fmax(Profile[0].x - Profile[1].x, BUILD_EPSILON);
			const float Pitch = Spec.Module * 0.7f;
			const int32_t Count = std::max(int32_t((HalfWidth * 2.0f) / std::fmax(Pitch, 0.05f)), 1);
			const float Step = (HalfWidth * 2.0f) / float(Count);

			std::vector<Vector3> Points;
			std::vector<Vector2> Inward;
			Points.reserve(size_t(Count));
			Inward.reserve(size_t(Count));
			for (int32_t Index = 0; Index < Count; ++Index)
			{
				const float X = -HalfWidth + (float(Index) + 0.5f) * Step;
				Points.push_back(Vector3(X, RoofBase, float(Sign) * HalfSpan));
				Inward.push_back(Vector2(0.0f, -float(Sign)));
			}

			AddEaveRafterHeads(Mesh, Spec, Points, Inward, TangentSlope, nullptr, Spec.TimberColor * 1.28f);
		}
	}

	/**
	 * The gabled family: 硬山, 悬山 and 卷棚.
	 *
	 * 硬山 stops the roof at the end walls; 悬山 overhangs past them. 卷棚 replaces the sharp
	 * ridge with a roll, and because the tiles then run continuously from one eave over the top
	 * to the other, it is built from a single unbroken profile rather than two slopes — which is
	 * also physically what a 卷棚 roof does.
	 */
	void BuildGabledRoof(const BuildingSpec& Spec, bool bOverhang, bool bRolled, MeshAccumulator& Mesh)
	{
		const float HalfWidth = Spec.Width * 0.5f + (bOverhang ? Spec.GableOverhang : 0.0f);
		const float HalfSpan = Spec.Depth * 0.5f + Spec.EaveOverhang;

		if (bRolled)
		{
			BuildRolledGable(Spec, HalfWidth, HalfSpan, Mesh);
			return;
		}

		const std::vector<Vector2> Profile = BuildRoofProfile(Spec, HalfSpan);

		BuildGableSlope(Spec, Profile, 1.0f, HalfWidth, Mesh);
		BuildGableSlope(Spec, Profile, -1.0f, HalfWidth, Mesh);

		if (Spec.RidgeDetail >= 1)
		{
			// One shared miter ring at the gable apex, no overlapping internal end caps.
			Mesh.SetSlot(EMaterialSlot::Ridge);
			for (int32_t Side = -1; Side <= 1; Side += 2)
			{
				std::vector<Vector3> Knots;
				for (const Vector2& P : Profile)
				{
					Knots.push_back(Vector3(HalfWidth * float(Side), Spec.RoofBase + P.y, P.x));
				}
				for (size_t I = Profile.size() - 1; I-- > 0;)
				{
					Knots.push_back(Vector3(HalfWidth * float(Side), Spec.RoofBase + Profile[I].y, -Profile[I].x));
				}
				LiftAlongKnotNormals(Knots, RoofBeddingLift(Spec));
				const float Scale = Spec.Module * 0.85f * Spec.RidgeScale;
				SweepSettings Settings;
				ConfigureRidgeSweep(Settings, Spec, Scale, true);
				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep)) { Mesh.AddSweep(Sweep, Spec.RidgeColor); }
				SeatRidgeBeasts(Mesh, Spec, Knots, Scale, 0x3);
			}
		}

		// 正脊 along the apex.
		{
			Mesh.SetSlot(EMaterialSlot::Ridge);
			const float Apex = Spec.RoofBase + Profile.back().y;
			std::vector<Vector3> Knots;
			Knots.push_back(Vector3(-HalfWidth, Apex, 0.0f));
			Knots.push_back(Vector3(HalfWidth, Apex, 0.0f));
			if (Spec.RidgeDetail >= 1)
			{
				// The head covers the outer face of the verge foot, rather than ending at its centre.
				const float HeadOverhang = Spec.Module * 0.85f * Spec.RidgeScale * 0.5f;
				Knots.front().x -= HeadOverhang;
				Knots.back().x += HeadOverhang;
			}
			// 高度链 (R17): both slopes rise into the 正脊, so it comes up with them.
			LiftVertically(Knots, RoofBeddingLift(Spec));

			const float MainRidgeScale = Spec.Module * 1.35f * Spec.RidgeScale;

			SweepSettings Settings;
			ConfigureRidgeSweep(Settings, Spec, MainRidgeScale);
			Settings.bClosedContour = true;

			SweepResult Sweep;
			if (BuildSweep(Knots, Settings, Sweep))
			{
				Mesh.AddSweep(Sweep, Spec.RidgeColor);
			}

			SeatRidgeFinials(Mesh, Spec, Knots, MainRidgeScale);
		}

		// 山墙 gable tympanum. It closes the wall line, not the roof edge, so on 悬山 the roof
		// correctly overhangs a wall that stops short of it.
		Mesh.SetSlot(EMaterialSlot::Gable);
		for (int32_t Side = -1; Side <= 1; Side += 2)
		{
			const float X = Spec.Width * 0.5f * float(Side);
			std::vector<Vector3> Points;

			// Up the +Z slope, over the apex, back down the -Z slope.
			for (size_t Index = 0; Index < Profile.size(); ++Index)
			{
				Points.push_back(Vector3(X, Spec.RoofBase + Profile[Index].y, Profile[Index].x));
			}
			for (size_t Index = Profile.size(); Index-- > 0;)
			{
				Points.push_back(Vector3(X, Spec.RoofBase + Profile[Index].y, -Profile[Index].x));
			}

			Mesh.AddPolygon(Points, Vector3(float(Side), 0.0f, 0.0f), Spec.PlasterColor * 0.94f);
		}
	}
} // namespace

// ==================== 脊/瓦 高度链 (R17) ====================
//
// The roof layer owns these two because it is the layer that places ridges, 山花 members and the
// 宝顶; the 瓦作 layer owns RoofBeddingLift, which is what they are lifting against.

void BuildingGen::LiftVertically(std::vector<Vector3>& Knots, float Lift)
{
	if (!(Lift > 0.0f))
	{
		return;
	}

	for (Vector3& Knot : Knots)
	{
		Knot.y += Lift;
	}
}

void BuildingGen::LiftAlongKnotNormals(std::vector<Vector3>& Knots, float Lift)
{
	if (!(Lift > 0.0f) || Knots.empty())
	{
		return;
	}

	// Normals first: the loop below moves the very knots their neighbours read.
	std::vector<Vector3> Normals;
	Normals.reserve(Knots.size());
	for (size_t Index = 0; Index < Knots.size(); ++Index)
	{
		Normals.push_back(KnotNormalUp(Knots, Index));
	}
	for (size_t Index = 0; Index < Knots.size(); ++Index)
	{
		Knots[Index] += Normals[Index] * Lift;
	}
}

// ==================== 脊断面 / 脊饰 (50_脊饰) ====================

std::vector<Vector2> BuildingGen::RidgeContourFor(const BuildingSpec& Spec, float Scale, bool bVerge)
{
	if (Spec.RidgeDetail < 1)
	{
		return Spec.LODLevel >= 2 ? MakeRidgeContourBlock(Scale) : MakeRidgeContour(Scale);
	}

	std::vector<Vector2> Unit;
	if (Spec.LODLevel <= 0)
	{
		Unit = MakeRidgeContourTiered();
	}
	else if (Spec.LODLevel == 1)
	{
		Unit = MakeRidgeContourTieredMid();
	}
	else
	{
		Unit = MakeRidgeContourTieredBlock();
	}

	for (Vector2& Point : Unit)
	{
		if (bVerge && Point.y > 0.0f) { Point.y *= 0.70f; }
		Point *= Scale;
	}

	return Unit;
}

void BuildingGen::ConfigureRidgeSweep(SweepSettings& Settings, const BuildingSpec& Spec, float Scale, bool bVerge)
{
	Settings.Contour = RidgeContourFor(Spec, Scale, bVerge);
	if (Spec.RidgeDetail >= 1)
	{
		Settings.Mode = ESweepMode::LocalMiter;
		Settings.bTriangulateCaps = true;
		Settings.bCorrectSurfaceNormals = true;
		Settings.SmoothContourMinY = Scale * 0.45f * (bVerge ? 0.70f : 1.0f);
	}
}

void MeshAccumulator::AddOrientedBox(const Vector3& Origin, const Vector3& AxisX, const Vector3& AxisY,
	const Vector3& AxisZ, const Vector3& HalfExtents, const Color& Tint)
{
	const Vector3& H = HalfExtents;
	if (H.x <= 0.0f || H.y <= 0.0f || H.z <= 0.0f)
	{
		return;
	}

	// One mottle per component, as AddBox does: the piece reads as one material piece.
	const Color Col = MottleColor(Tint);

	Vector3 Corner[8];
	for (int32_t Index = 0; Index < 8; ++Index)
	{
		const float SX = (Index & 1) ? H.x : -H.x;
		const float SY = (Index & 2) ? H.y : -H.y;
		const float SZ = (Index & 4) ? H.z : -H.z;
		Corner[Index] = Origin + AxisX * SX + AxisY * SY + AxisZ * SZ;
	}

	// 000 100 110 010 / 001 101 111 011, with the face normals along the frame's own axes.
	AddQuadOriented(Corner[4], Corner[6], Corner[7], Corner[5], AxisZ, Col);
	AddQuadOriented(Corner[1], Corner[3], Corner[2], Corner[0], -AxisZ, Col);
	AddQuadOriented(Corner[5], Corner[7], Corner[3], Corner[1], AxisX, Col);
	AddQuadOriented(Corner[0], Corner[2], Corner[6], Corner[4], -AxisX, Col);
	AddQuadOriented(Corner[2], Corner[3], Corner[7], Corner[6], AxisY, Col);
	AddQuadOriented(Corner[0], Corner[4], Corner[5], Corner[1], -AxisY, Col);
}

void MeshAccumulator::AddPlacedTriangles(
	const std::vector<Vector3>& SourceVertices, const std::vector<Vector3>& SourceNormals,
	const std::vector<Vector2>& SourceUVs, const std::vector<int32_t>& SourceIndices,
	const Vector3& Origin, const Vector3& AxisX, const Vector3& AxisY, const Vector3& AxisZ,
	float Scale, const Color& Tint)
{
	if (SourceIndices.size() < 3 || SourceVertices.empty() || !(Scale > 0.0f))
	{
		return;
	}

	// Seat by the soup's own base, not by wherever its origin happens to be: the placement records
	// the point that rests on the ridge, so the mesh's bottom-face centre is moved there. An
	// authored 鸱吻 therefore lands the same way the placeholder cube does, whatever its pivot.
	Vector3 Min = SourceVertices.front();
	Vector3 Max = SourceVertices.front();
	for (const Vector3& Source : SourceVertices)
	{
		Min = Vector3(std::fmin(Min.x, Source.x), std::fmin(Min.y, Source.y), std::fmin(Min.z, Source.z));
		Max = Vector3(std::fmax(Max.x, Source.x), std::fmax(Max.y, Source.y), std::fmax(Max.z, Source.z));
	}
	const Vector3 Anchor((Min.x + Max.x) * 0.5f, Min.y, (Min.z + Max.z) * 0.5f);

	const Color Col = MottleColor(Tint);
	const int32_t First = int32_t(Vertices.size());
	const size_t SourceCount = SourceVertices.size();
	for (size_t Index = 0; Index < SourceCount; ++Index)
	{
		const Vector3 Local = SourceVertices[Index] - Anchor;
		Vertices.push_back(Origin
			+ AxisX * (Local.x * Scale) + AxisY * (Local.y * Scale) + AxisZ * (Local.z * Scale));

		Vector3 Normal;
		if (Index < SourceNormals.size() && SourceNormals[Index].length_squared() > 1e-12f)
		{
			Normal = (AxisX * SourceNormals[Index].x + AxisY * SourceNormals[Index].y
				+ AxisZ * SourceNormals[Index].z).normalized();
		}
		else
		{
			Normal = AxisY;
		}
		Normals.push_back(Normal);
		UVs.push_back(Index < SourceUVs.size() ? SourceUVs[Index] : Vector2());
		Colors.push_back(Col);
	}

	for (size_t Index = 0; Index + 2 < SourceIndices.size(); Index += 3)
	{
		const int32_t A = SourceIndices[Index];
		const int32_t B = SourceIndices[Index + 1];
		const int32_t C = SourceIndices[Index + 2];
		if (A < 0 || B < 0 || C < 0 || size_t(A) >= SourceCount || size_t(B) >= SourceCount
			|| size_t(C) >= SourceCount)
		{
			continue;
		}

		PushTriangle(First + A, First + B, First + C);
	}
}

namespace
{
	/**
	 * A ridge run's local frame at a station measured in arc length from one of its ends.
	 *
	 * Up comes from the polyline itself (KnotNormalUp), so it is the same direction the 高度链 lift
	 * used; Outward runs downhill, past the end the station was measured from, which is the way a
	 * 垂兽 / 走兽 faces.
	 */
	struct RidgeStation
	{
		Vector3 Position;
		Vector3 Up = Vector3(0, 1, 0);
		Vector3 Outward = Vector3(0, 0, 1);
		bool bValid = false;
	};

	RidgeStation StationAlongRidge(const std::vector<Vector3>& Knots, int32_t End, float Distance)
	{
		RidgeStation Station;
		if (Knots.size() < 2 || Distance < 0.0f)
		{
			return Station;
		}

		// Walk from the requested end. Index pairs are (From, To) in walk order.
		const bool bFromFirst = (End == 0);
		const size_t Start = bFromFirst ? 0 : Knots.size() - 1;
		const size_t Stop = bFromFirst ? Knots.size() - 1 : 0;

		float Remaining = Distance;
		for (size_t Index = Start; Index != Stop;)
		{
			const size_t Next = bFromFirst ? Index + 1 : Index - 1;
			const Vector3& From = Knots[Index];
			const Vector3& To = Knots[Next];
			const Vector3 Segment = To - From;
			const float Length = Segment.length();
			if (Length > BUILD_EPSILON && Remaining <= Length)
			{
				const float T = Remaining / Length;
				const Vector3 UpFrom = KnotNormalUp(Knots, Index);
				const Vector3 UpTo = KnotNormalUp(Knots, Next);
				const Vector3 Up = UpFrom + (UpTo - UpFrom) * T;
				const Vector3 Outward = -Segment / Length;

				Station.Outward = Outward;
				// Keep the frame orthonormal: the interpolated up is only orthogonal to Outward where
				// the ridge is straight, and a tilted ornament would leave the crown on one corner.
				const Vector3 Orthogonal = Up - Outward * Up.dot(Outward);
				Station.Up = (Orthogonal.length_squared() > BUILD_EPSILON)
					? Orthogonal.normalized()
					: Vector3(0, 1, 0);
				Station.Position = From + Segment * T;
				Station.bValid = true;

				return Station;
			}

			Remaining -= Length;
			Index = Next;
		}

		// Past the far end: no station, so nothing is seated beyond the ridge.
		return Station;
	}

	float RidgeRunLength(const std::vector<Vector3>& Knots)
	{
		float Total = 0.0f;
		for (size_t Index = 0; Index + 1 < Knots.size(); ++Index)
		{
			Total += Knots[Index].distance_to(Knots[Index + 1]);
		}

		return Total;
	}


	/**
	 * Seats one 脊饰 (50_脊饰 J1 / 卡片 R3: the piece sits ON the ridge, a separate support
	 * relationship — not interpenetrating, not a veneer).
	 *
	 * The bottom face lands on the higher of the two surfaces at the piece's own half-width: the
	 * finest section of this RidgeDetail, and the one this tier actually swept. Both are convex
	 * enough that the surface rises inward from the piece's corners, so the whole bottom face is in
	 * contact — no hover anywhere under it — and the only penetration is the crown rising inside the
	 * piece, which is what "seated" means. Taking the design section as one half of that pair is what
	 * keeps the seat still when the tier switches: the coarser tiers are built outside the finer one.
	 */
	void SeatOrnament(MeshAccumulator& Mesh, const BuildingSpec& Spec, const std::vector<Vector3>& Knots,
		float RidgeScale, int32_t End, float Distance, ERidgeOrnamentKind Kind)
	{
		const float Size = OrnamentSizeFor(Spec, Kind);
		if (!(Size > 0.0f) || !(RidgeScale > 0.0f))
		{
			return;
		}

		const RidgeStation Station = StationAlongRidge(Knots, End, Distance + Size * 0.5f);
		if (!Station.bValid)
		{
			return;
		}

		const float HalfWidth = (Size * 0.5f) / RidgeScale;
		const bool bVerge = Kind != ERidgeOrnamentKind::Finial;
		const float SeatDesign = RidgeSurfaceAt(RidgeDetailContour(Spec, bVerge), HalfWidth);
		const float SeatTier = RidgeSurfaceAt(RidgeContourFor(Spec, 1.0f, bVerge), HalfWidth);
		const float Seat = std::fmin(SeatDesign, SeatTier) * RidgeScale;

		const Vector3 AxisY = Station.Up;
		const Vector3 AxisZ = Station.Outward;
		Vector3 AxisX = AxisY.cross(AxisZ);
		if (AxisX.length_squared() < BUILD_EPSILON)
		{
			return;
		}
		AxisX = AxisX.normalized();

		RidgeOrnamentPlacement Placement;
		Placement.Kind = Kind;
		Placement.Origin = Station.Position + AxisY * Seat;
		Placement.AxisX = AxisX;
		Placement.AxisY = AxisY;
		Placement.AxisZ = AxisZ;
		Placement.Size = Size;
		Mesh.RidgeOrnaments.push_back(Placement);

		// A caller supplying its own mesh for this class places it from the record instead.
		if (Spec.RidgeOrnamentMeshMask & RidgeOrnamentBit(Kind))
		{
			return;
		}

		Mesh.AddOrientedBox(Placement.Origin, AxisX, AxisY, AxisZ,
			Vector3(Size * 0.5f, Size * 0.5f, Size * 0.5f), RidgeOrnamentColor(Spec));
	}
} // namespace

Color BuildingGen::RidgeOrnamentColor(const BuildingSpec& Spec)
{
	return Spec.RidgeColor * 1.12f;
}


void BuildingGen::SeatRidgeFinials(MeshAccumulator& Mesh, const BuildingSpec& Spec,
	const std::vector<Vector3>& Knots, float RidgeScale)
{
	if (!Spec.bRidgeOrnaments)
	{
		return;
	}

	// Both ends of a 正脊: the 正吻 caps the ridge rather than overhanging it, so its outer face is
	// flush with the ridge's own end face and its whole bottom face has ridge under it.
	SeatOrnament(Mesh, Spec, Knots, RidgeScale, 0, 0.0f, ERidgeOrnamentKind::Finial);
	SeatOrnament(Mesh, Spec, Knots, RidgeScale, 1, 0.0f, ERidgeOrnamentKind::Finial);
}

void BuildingGen::SeatRidgeBeasts(MeshAccumulator& Mesh, const BuildingSpec& Spec,
	const std::vector<Vector3>& Knots, float RidgeScale, int32_t EaveEnds)
{
	if (!Spec.bRidgeOrnaments)
	{
		return;
	}

	const float Beast = OrnamentSizeFor(Spec, ERidgeOrnamentKind::Beast);
	const float Walker = OrnamentSizeFor(Spec, ERidgeOrnamentKind::Walker);
	// J2's row pitch: 兽身宽 + 间隙. The gap is [待定标] (0.005–0.01 D), so the middle is used.
	const float Pitch = Walker + Spec.Module * 0.0075f;
	const int32_t Count = std::max(Spec.RidgeWalkerCount, 0);
	const float Run = RidgeRunLength(Knots);

	for (int32_t End = 0; End <= 1; ++End)
	{
		if (!(EaveEnds & (1 << End)))
		{
			continue;
		}

		SeatOrnament(Mesh, Spec, Knots, RidgeScale, End, 0.0f, ERidgeOrnamentKind::Beast);

		// 走兽 walk up behind the 垂兽. A row longer than its ridge simply stops short of the top
		// instead of marching off the far end.
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			const float Distance = Beast + (float(Index) + 0.5f) * Pitch;
			if (Distance + Walker > Run)
			{
				break;
			}
			SeatOrnament(Mesh, Spec, Knots, RidgeScale, End, Distance, ERidgeOrnamentKind::Walker);
		}
	}
}

// ==================== Entry point ====================

void BuildingGen::BuildBuilding(const BuildingSpec& Spec, MeshAccumulator& OutMesh)
{
	const bool bCentralRoof = Spec.RoofType == ROOF_PYRAMIDAL
		|| Spec.RoofType == ROOF_ROUND
		|| Spec.RoofType == ROOF_HELMET;
	const ECentralProfile CentralProfile =
		(Spec.RoofType == ROOF_HELMET) ? CENTRAL_HELMET : CENTRAL_STRAIGHT;

	// Equation 8: a non-rectangular plan must be regular, and only a centralised roof can sit
	// on one. Both conditions route to the polygonal generator.
	if (Spec.Sides != 4)
	{
		BuildPolygonalBuilding(Spec, CentralProfile, OutMesh);
		return;
	}

	// 台基 optional (地基). With no platform the base is at ground level, so the stairs and the
	// balustrade — both of which only make sense on a raised base — are skipped with it.
	// (CollectSpec has already zeroed PlatformHeight in that case.)
	if (Spec.bGeneratePlatform)
	{
		BuildPlatform(Spec, OutMesh);

		if (Spec.bGenerateFence)
		{
			BuildFence(Spec, OutMesh);
		}

		if (Spec.bGenerateSteps)
		{
			for (const float Angle : CullingAngles(Spec.StepRunCount))
			{
				BuildStepRun(Spec, Angle, OutMesh);
			}
		}
	}

	BuildBody(Spec, OutMesh);

	// Only the ridged family is implemented so far; the centralised family (攒尖/盔顶/盝顶)
	// is a separate generator, per the Eq 8 split.
	// A centralised roof on a square plan is legal — that is exactly the 攒尖 a square 庑殿
	// already degenerates into, just built deliberately and with a finial.
	if (bCentralRoof)
	{
		BuildCentralisedRoof(Spec, CentralProfile, OutMesh);
		return;
	}

	switch (Spec.RoofType)
	{
		case ROOF_HIP:
			BuildHippedRoof(Spec, HIP_TOP_RIDGE, OutMesh);
			break;
		case ROOF_GABLE_AND_HIP:
			BuildHippedRoof(Spec, HIP_TOP_GABLED_TIER, OutMesh);
			break;
		case ROOF_HOLLOW:
			BuildHippedRoof(Spec, HIP_TOP_FLAT, OutMesh);
			break;
		case ROOF_OVERHANGING_GABLE:
			BuildGabledRoof(Spec, true, false, OutMesh);
			break;
		case ROOF_ROUND_RIDGE:
			BuildGabledRoof(Spec, true, true, OutMesh);
			break;
		default:
			BuildGabledRoof(Spec, false, false, OutMesh);
			break;
	}
}
