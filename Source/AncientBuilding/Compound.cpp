#include "AncientBuilding/Compound.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <utility>

using namespace BuildingGen;

namespace
{
	const float COMPOUND_EPSILON = 1e-6f;
	/** Sub-triangles are refined down to this edge length where a junction crosses them. */
	const float CLIP_MAX_EDGE = 1.0f;
	const int32_t CLIP_MAX_DEPTH = 12;
	/** Height-field cell. Boarding panels are long, so a coarse grid keeps lookups cheap. */
	const float FIELD_CELL = 0.75f;
	/** Valley tracing grid step. */
	const float VALLEY_STEP = 0.15f;
	/** "Outside the other roof": keep, with a small positive value so cuts land on the edge. */
	const float OUTSIDE_VALUE = 0.05f;

	struct Vertex
	{
		Vector3 Position;
		Vector3 Normal;
		Vector2 UV;
		Color Tint;
	};

	using Triangle = std::array<Vertex, 3>;

	Vertex Lerp(const Vertex& A, const Vertex& B, float T)
	{
		Vertex Out;
		Out.Position = A.Position + (B.Position - A.Position) * T;
		Out.Normal = (A.Normal + (B.Normal - A.Normal) * T);
		if (Out.Normal.length_squared() > COMPOUND_EPSILON)
		{
			Out.Normal = Out.Normal.normalized();
		}
		Out.UV = A.UV + (B.UV - A.UV) * T;
		Out.Tint = A.Tint.lerp(B.Tint, T);
		return Out;
	}

	/** The region of Tri where Value(p) >= 0, as 0..2 triangles, winding preserved. */
	void ClipTriangle(const Triangle& Tri, const std::function<float(const Vector3&)>& Value,
		std::vector<Triangle>& Out)
	{
		float F[3];
		int32_t Inside = 0;
		for (int32_t Index = 0; Index < 3; ++Index)
		{
			F[Index] = Value(Tri[size_t(Index)].Position);
			if (F[Index] >= 0.0f)
			{
				++Inside;
			}
		}
		if (Inside == 3)
		{
			Out.push_back(Tri);
			return;
		}
		if (Inside == 0)
		{
			return;
		}

		// Sutherland–Hodgman against one linear function.
		std::vector<Vertex> Polygon;
		for (int32_t Index = 0; Index < 3; ++Index)
		{
			const int32_t Next = (Index + 1) % 3;
			const Vertex& A = Tri[size_t(Index)];
			const Vertex& B = Tri[size_t(Next)];
			if (F[Index] >= 0.0f)
			{
				Polygon.push_back(A);
			}
			if ((F[Index] >= 0.0f) != (F[Next] >= 0.0f))
			{
				const float T = F[Index] / (F[Index] - F[Next]);
				Polygon.push_back(Lerp(A, B, std::clamp(T, 0.0f, 1.0f)));
			}
		}
		for (size_t Index = 1; Index + 1 < Polygon.size(); ++Index)
		{
			const Triangle Piece = { Polygon[0], Polygon[Index], Polygon[Index + 1] };
			const Vector3 Cross = (Piece[1].Position - Piece[0].Position).cross(Piece[2].Position - Piece[0].Position);
			if (Cross.length_squared() > 1e-12f)
			{
				Out.push_back(Piece);
			}
		}
	}

	/** What one other wing forbids: its roof shell and its body. */
	struct Obstacle
	{
		const RoofHeightField* Field = nullptr;
		/** Roof shell depth below the surface. */
		float Shell = 0.3f;
		/** Surface above the boarding field: the tiles and bedding stand on it. */
		float SurfaceLift = 0.1f;
		bool bHasBody = true;
		Vector3 Offset;
		int32_t Turns = 0;
		float BodyHalfX = 0.0f;
		float BodyHalfZ = 0.0f;
		float BodyBottom = 0.0f;

		float Height(const Vector3& P) const
		{
			const float H = Field->Sample(P.x, P.z);
			return (H <= RoofHeightField::NONE * 0.5f) ? H : H + SurfaceLift;
		}

		/** Above the other roof's surface: always allowed. */
		float Above(const Vector3& P) const
		{
			const float H = Height(P);
			return (H <= RoofHeightField::NONE * 0.5f) ? OUTSIDE_VALUE : P.y - H;
		}

		/** Below the other roof's soffit: allowed unless also inside its body. */
		float BelowShell(const Vector3& P) const
		{
			const float H = Height(P);
			return (H <= RoofHeightField::NONE * 0.5f) ? OUTSIDE_VALUE : (H - Shell) - P.y;
		}

		/** Signed distance outside the other wing's wall line (negative inside). */
		float OutsideBody(const Vector3& P) const
		{
			if (!bHasBody || P.y < BodyBottom)
			{
				return OUTSIDE_VALUE;
			}
			const Vector3 Local = RotateQuarterTurns(P - Offset, -Turns);
			const float DX = std::abs(Local.x) - BodyHalfX;
			const float DZ = std::abs(Local.z) - BodyHalfZ;
			return (DX > 0.0f || DZ > 0.0f)
				? Vector2(std::fmax(DX, 0.0f), std::fmax(DZ, 0.0f)).length()
				: std::fmax(DX, DZ);
		}

		bool Allows(const Vector3& P) const
		{
			return Above(P) >= 0.0f || (BelowShell(P) >= 0.0f && OutsideBody(P) >= 0.0f);
		}
	};

	float MaxEdge(const Triangle& Tri)
	{
		return std::fmax(Tri[0].Position.distance_to(Tri[1].Position),
			std::fmax(Tri[1].Position.distance_to(Tri[2].Position), Tri[2].Position.distance_to(Tri[0].Position)));
	}

	/**
	 * Allowed / forbidden / mixed, over a barycentric grid dense enough (about 0.6 m) that a
	 * junction crossing a long boarding panel cannot slip between samples.
	 */
	int32_t Classify(const Triangle& Tri, const Obstacle& Other)
	{
		const Vector3& A = Tri[0].Position;
		const Vector3& B = Tri[1].Position;
		const Vector3& C = Tri[2].Position;
		const int32_t Steps = std::clamp(int32_t(std::ceil(MaxEdge(Tri) / 0.6f)), 2, 32);
		int32_t Allowed = 0;
		int32_t Total = 0;
		for (int32_t I = 0; I <= Steps; ++I)
		{
			for (int32_t J = 0; I + J <= Steps; ++J)
			{
				const float U = float(I) / float(Steps);
				const float V = float(J) / float(Steps);
				if (Other.Allows(A * (1.0f - U - V) + B * U + C * V))
				{
					++Allowed;
				}
				++Total;
			}
		}
		return (Allowed == Total) ? 1 : (Allowed == 0) ? -1 : 0;
	}

	void ClipAgainst(const Triangle& Tri, const Obstacle& Other, int32_t Depth, std::vector<Triangle>& Out)
	{
		const int32_t Class = Classify(Tri, Other);
		if (Class == 1)
		{
			Out.push_back(Tri);
			return;
		}
		if (Class == -1)
		{
			return;
		}
		const float Edge = MaxEdge(Tri);
		if (Edge > CLIP_MAX_EDGE && Depth < CLIP_MAX_DEPTH)
		{
			// Bisect the longest edge: the height field is only piecewise linear, so a cut is only
			// as good as the triangle it is taken across — but the tile skin is all slivers, and a
			// four-way split of a sliver multiplies triangles without shortening the long edge any
			// faster.
			const float E01 = Tri[0].Position.distance_to(Tri[1].Position);
			const float E12 = Tri[1].Position.distance_to(Tri[2].Position);
			const float E20 = Tri[2].Position.distance_to(Tri[0].Position);
			const int32_t Longest = (E01 >= E12 && E01 >= E20) ? 0 : (E12 >= E20) ? 1 : 2;
			const Vertex& P0 = Tri[size_t(Longest)];
			const Vertex& P1 = Tri[size_t((Longest + 1) % 3)];
			const Vertex& P2 = Tri[size_t((Longest + 2) % 3)];
			const Vertex Mid = Lerp(P0, P1, 0.5f);
			ClipAgainst(Triangle{ P0, Mid, P2 }, Other, Depth + 1, Out);
			ClipAgainst(Triangle{ Mid, P1, P2 }, Other, Depth + 1, Out);
			return;
		}

		// Allowed = Above ∪ (BelowShell ∩ OutsideBody); the two parts are disjoint.
		ClipTriangle(Tri, [&Other](const Vector3& P) { return Other.Above(P); }, Out);
		std::vector<Triangle> Under;
		ClipTriangle(Tri, [&Other](const Vector3& P) { return -Other.Above(P) - 1e-5f; }, Under);
		for (const Triangle& Piece : Under)
		{
			std::vector<Triangle> Low;
			ClipTriangle(Piece, [&Other](const Vector3& P) { return Other.BelowShell(P); }, Low);
			for (const Triangle& LowPiece : Low)
			{
				ClipTriangle(LowPiece, [&Other](const Vector3& P) { return Other.OutsideBody(P); }, Out);
			}
		}
	}

	/** A wing's geometry moved into the compound frame, slots and tags kept. */
	void TransformInto(const MeshAccumulator& Source, const CompoundWing& Wing, MeshAccumulator& Out)
	{
		const int32_t Count = Source.GetTriangleCount();
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			Vector3 Positions[3];
			Vector3 Normals[3];
			Vector2 Coords[3];
			Color Tints[3];
			for (int32_t K = 0; K < 3; ++K)
			{
				const int32_t V = Source.Indices[size_t(Index * 3 + K)];
				Positions[K] = RotateQuarterTurns(Source.Vertices[size_t(V)], Wing.Turns) + Wing.Offset;
				Normals[K] = RotateQuarterTurns(Source.Normals[size_t(V)], Wing.Turns);
				Coords[K] = Source.UVs[size_t(V)];
				Tints[K] = Source.Colors[size_t(V)];
			}
			Out.SetSlot(Source.GetTriangleSlot(Index));
			Out.SetTag(Source.GetTriangleTag(Index));
			Out.AddRawTriangle(Positions, Normals, Coords, Tints);
		}
	}

	/** Eave rectangle half extents of a wing (polygonal plans: the square around the apothem). */
	Vector2 EaveHalfExtents(const BuildingSpec& Spec)
	{
		if (Spec.Sides != 4)
		{
			const float R = Spec.PlanApothem + Spec.EaveOverhang;
			return Vector2(R, R);
		}
		return Vector2(Spec.Width * 0.5f + Spec.EaveOverhang, Spec.Depth * 0.5f + Spec.EaveOverhang);
	}

	/**
	 * 翼角 that would rise inside a neighbour's roof are cleared: at a junction the eave dies
	 * into a valley, and a flipped corner there would stand up through the other roof.
	 */
	uint32_t CornerMaskFor(const std::vector<CompoundWing>& Wings, size_t Self)
	{
		const CompoundWing& Wing = Wings[Self];
		if (Wing.Spec.Sides != 4)
		{
			return 0xFu;
		}
		const Vector2 Half = EaveHalfExtents(Wing.Spec);
		uint32_t Mask = 0xFu;
		for (uint32_t Bit = 0; Bit < 4; ++Bit)
		{
			const Vector3 Corner((Bit & 1u) ? -Half.x : Half.x, 0.0f, (Bit & 2u) ? -Half.y : Half.y);
			const Vector3 World = RotateQuarterTurns(Corner, Wing.Turns) + Wing.Offset;
			for (size_t Other = 0; Other < Wings.size(); ++Other)
			{
				if (Other == Self)
				{
					continue;
				}
				const Vector3 Local = RotateQuarterTurns(World - Wings[Other].Offset, -Wings[Other].Turns);
				const Vector2 OtherHalf = EaveHalfExtents(Wings[Other].Spec);
				if (std::abs(Local.x) < OtherHalf.x + 0.05f && std::abs(Local.z) < OtherHalf.y + 0.05f)
				{
					Mask &= ~(1u << Bit);
				}
			}
		}
		return Mask;
	}

	// ---- Valley tracing ----

	using PointKey = std::pair<int64_t, int64_t>;

	PointKey KeyOf(const Vector2& P)
	{
		return { int64_t(std::llround(P.x * 1000.0f)), int64_t(std::llround(P.y * 1000.0f)) };
	}

	/**
	 * Where two roof surfaces are equal inside both footprints, as plan polylines. Marching
	 * squares over the difference field, then segments chained end to end.
	 */
	std::vector<std::vector<Vector2>> TraceValleys(const Obstacle& A, const Obstacle& B)
	{
		std::vector<std::vector<Vector2>> Lines;
		const float MinX = std::fmax(A.Field->MinX, B.Field->MinX);
		const float MaxX = std::fmin(A.Field->MaxX, B.Field->MaxX);
		const float MinZ = std::fmax(A.Field->MinZ, B.Field->MinZ);
		const float MaxZ = std::fmin(A.Field->MaxZ, B.Field->MaxZ);
		if (MaxX - MinX < VALLEY_STEP * 2.0f || MaxZ - MinZ < VALLEY_STEP * 2.0f)
		{
			return Lines;
		}

		const int32_t NX = int32_t((MaxX - MinX) / VALLEY_STEP) + 1;
		const int32_t NZ = int32_t((MaxZ - MinZ) / VALLEY_STEP) + 1;
		std::vector<float> D(size_t(NX) * size_t(NZ), 0.0f);
		std::vector<uint8_t> Valid(size_t(NX) * size_t(NZ), 0);
		for (int32_t J = 0; J < NZ; ++J)
		{
			for (int32_t I = 0; I < NX; ++I)
			{
				const Vector3 P(MinX + float(I) * VALLEY_STEP, 0.0f, MinZ + float(J) * VALLEY_STEP);
				const float HA = A.Height(P);
				const float HB = B.Height(P);
				if (HA > RoofHeightField::NONE * 0.5f && HB > RoofHeightField::NONE * 0.5f)
				{
					D[size_t(J) * size_t(NX) + size_t(I)] = HA - HB;
					Valid[size_t(J) * size_t(NX) + size_t(I)] = 1;
				}
			}
		}

		// Segments per cell, joined by their shared edge crossings.
		std::vector<std::pair<Vector2, Vector2>> Segments;
		const auto At = [&D, NX](int32_t I, int32_t J) { return D[size_t(J) * size_t(NX) + size_t(I)]; };
		const auto Cross = [MinX, MinZ](int32_t I0, int32_t J0, float D0, int32_t I1, int32_t J1, float D1)
		{
			const float T = D0 / (D0 - D1);
			return Vector2(MinX + (float(I0) + (float(I1 - I0)) * T) * VALLEY_STEP,
				MinZ + (float(J0) + (float(J1 - J0)) * T) * VALLEY_STEP);
		};
		for (int32_t J = 0; J + 1 < NZ; ++J)
		{
			for (int32_t I = 0; I + 1 < NX; ++I)
			{
				const size_t Base = size_t(J) * size_t(NX) + size_t(I);
				if (!Valid[Base] || !Valid[Base + 1] || !Valid[Base + size_t(NX)] || !Valid[Base + size_t(NX) + 1])
				{
					continue;
				}
				const int32_t Corners[4][2] = { { I, J }, { I + 1, J }, { I + 1, J + 1 }, { I, J + 1 } };
				std::vector<Vector2> Hits;
				for (int32_t E = 0; E < 4; ++E)
				{
					const int32_t* P0 = Corners[E];
					const int32_t* P1 = Corners[(E + 1) % 4];
					const float D0 = At(P0[0], P0[1]);
					const float D1 = At(P1[0], P1[1]);
					if ((D0 >= 0.0f) != (D1 >= 0.0f))
					{
						Hits.push_back(Cross(P0[0], P0[1], D0, P1[0], P1[1], D1));
					}
				}
				if (Hits.size() == 2)
				{
					Segments.push_back({ Hits[0], Hits[1] });
				}
				else if (Hits.size() == 4)
				{
					// Saddle: pair the crossings by the centre value, as marching squares does.
					Segments.push_back({ Hits[0], Hits[1] });
					Segments.push_back({ Hits[2], Hits[3] });
				}
			}
		}

		// Chain.
		std::map<PointKey, std::vector<size_t>> Ends;
		for (size_t Index = 0; Index < Segments.size(); ++Index)
		{
			Ends[KeyOf(Segments[Index].first)].push_back(Index);
			Ends[KeyOf(Segments[Index].second)].push_back(Index);
		}
		std::vector<uint8_t> Used(Segments.size(), 0);
		for (size_t Start = 0; Start < Segments.size(); ++Start)
		{
			if (Used[Start])
			{
				continue;
			}
			Used[Start] = 1;
			std::vector<Vector2> Line = { Segments[Start].first, Segments[Start].second };
			for (int32_t Direction = 0; Direction < 2; ++Direction)
			{
				bool bGrew = true;
				while (bGrew)
				{
					bGrew = false;
					const Vector2 Tip = Direction == 0 ? Line.back() : Line.front();
					for (const size_t Next : Ends[KeyOf(Tip)])
					{
						if (Used[Next])
						{
							continue;
						}
						Used[Next] = 1;
						const Vector2 Far = (KeyOf(Segments[Next].first) == KeyOf(Tip))
							? Segments[Next].second : Segments[Next].first;
						if (Direction == 0)
						{
							Line.push_back(Far);
						}
						else
						{
							Line.insert(Line.begin(), Far);
						}
						bGrew = true;
						break;
					}
				}
			}
			Lines.push_back(Line);
		}
		return Lines;
	}

	/** Resample a polyline at a fixed spacing and relax it, so tile ripples do not show. */
	std::vector<Vector2> SmoothLine(const std::vector<Vector2>& Line, float Spacing)
	{
		std::vector<Vector2> Out;
		if (Line.size() < 2)
		{
			return Out;
		}
		Out.push_back(Line.front());
		float Carry = 0.0f;
		for (size_t Index = 1; Index < Line.size(); ++Index)
		{
			const Vector2 A = Line[Index - 1];
			const Vector2 B = Line[Index];
			const float Length = A.distance_to(B);
			float T = Spacing - Carry;
			while (T <= Length)
			{
				Out.push_back(A + (B - A) * (T / std::fmax(Length, COMPOUND_EPSILON)));
				T += Spacing;
			}
			Carry = Length - (T - Spacing);
		}
		if (Out.back().distance_to(Line.back()) > Spacing * 0.3f)
		{
			Out.push_back(Line.back());
		}
		for (int32_t Pass = 0; Pass < 2; ++Pass)
		{
			std::vector<Vector2> Relaxed = Out;
			for (size_t Index = 1; Index + 1 < Out.size(); ++Index)
			{
				Relaxed[Index] = (Out[Index - 1] + Out[Index] * 2.0f + Out[Index + 1]) * 0.25f;
			}
			Out = Relaxed;
		}
		return Out;
	}

	float LineLength(const std::vector<Vector2>& Line)
	{
		float Length = 0.0f;
		for (size_t Index = 1; Index < Line.size(); ++Index)
		{
			Length += Line[Index - 1].distance_to(Line[Index]);
		}
		return Length;
	}
} // namespace

Vector3 BuildingGen::RotateQuarterTurns(const Vector3& Point, int32_t Turns)
{
	switch (((Turns % 4) + 4) % 4)
	{
		case 1: return Vector3(Point.z, Point.y, -Point.x);
		case 2: return Vector3(-Point.x, Point.y, -Point.z);
		case 3: return Vector3(-Point.z, Point.y, Point.x);
		default: return Point;
	}
}

void RoofHeightField::Build(const MeshAccumulator& Mesh, float MinY, float CellSize)
{
	Triangles.clear();
	Cells.clear();
	Cell = std::fmax(CellSize, 0.05f);

	const int32_t Count = Mesh.GetTriangleCount();
	for (int32_t Index = 0; Index < Count; ++Index)
	{
		if (Mesh.GetTriangleTag(Index) == MeshAccumulator::TAG_FIXED)
		{
			continue;
		}
		// Boarding and ridges: the tile skin is thousands of triangles riding a fixed lift above
		// the boarding, so it is represented by Obstacle::SurfaceLift instead of sampled.
		const EMaterialSlot Slot = Mesh.GetTriangleSlot(Index);
		if (Slot != EMaterialSlot::Timber && Slot != EMaterialSlot::Ridge)
		{
			continue;
		}
		const Vector3& A = Mesh.Vertices[size_t(Mesh.Indices[size_t(Index * 3)])];
		const Vector3& B = Mesh.Vertices[size_t(Mesh.Indices[size_t(Index * 3 + 1)])];
		const Vector3& C = Mesh.Vertices[size_t(Mesh.Indices[size_t(Index * 3 + 2)])];
		if ((A.y + B.y + C.y) / 3.0f < MinY)
		{
			continue;
		}
		const Vector3 Normal = Mesh.Normals[size_t(Mesh.Indices[size_t(Index * 3)])]
			+ Mesh.Normals[size_t(Mesh.Indices[size_t(Index * 3 + 1)])]
			+ Mesh.Normals[size_t(Mesh.Indices[size_t(Index * 3 + 2)])];
		if (Normal.y < 0.25f * Normal.length())
		{
			continue;
		}
		Triangles.push_back({ A, B, C });
	}
	if (Triangles.empty())
	{
		return;
	}

	MinX = MinZ = 1e30f;
	MaxX = MaxZ = -1e30f;
	for (const FieldTriangle& Tri : Triangles)
	{
		for (const Vector3* P : { &Tri.A, &Tri.B, &Tri.C })
		{
			MinX = std::fmin(MinX, P->x);
			MaxX = std::fmax(MaxX, P->x);
			MinZ = std::fmin(MinZ, P->z);
			MaxZ = std::fmax(MaxZ, P->z);
		}
	}
	Columns = std::max(int32_t((MaxX - MinX) / Cell) + 1, 1);
	Rows = std::max(int32_t((MaxZ - MinZ) / Cell) + 1, 1);
	Cells.assign(size_t(Columns) * size_t(Rows), {});
	for (size_t Index = 0; Index < Triangles.size(); ++Index)
	{
		const FieldTriangle& Tri = Triangles[Index];
		const float X0 = std::fmin(Tri.A.x, std::fmin(Tri.B.x, Tri.C.x));
		const float X1 = std::fmax(Tri.A.x, std::fmax(Tri.B.x, Tri.C.x));
		const float Z0 = std::fmin(Tri.A.z, std::fmin(Tri.B.z, Tri.C.z));
		const float Z1 = std::fmax(Tri.A.z, std::fmax(Tri.B.z, Tri.C.z));
		const int32_t I0 = std::clamp(int32_t((X0 - MinX) / Cell), 0, Columns - 1);
		const int32_t I1 = std::clamp(int32_t((X1 - MinX) / Cell), 0, Columns - 1);
		const int32_t J0 = std::clamp(int32_t((Z0 - MinZ) / Cell), 0, Rows - 1);
		const int32_t J1 = std::clamp(int32_t((Z1 - MinZ) / Cell), 0, Rows - 1);
		for (int32_t J = J0; J <= J1; ++J)
		{
			for (int32_t I = I0; I <= I1; ++I)
			{
				Cells[size_t(J) * size_t(Columns) + size_t(I)].push_back(int32_t(Index));
			}
		}
	}
}

float RoofHeightField::Sample(float X, float Z) const
{
	if (Triangles.empty() || X < MinX || X > MaxX || Z < MinZ || Z > MaxZ)
	{
		return NONE;
	}
	const int32_t I = std::clamp(int32_t((X - MinX) / Cell), 0, Columns - 1);
	const int32_t J = std::clamp(int32_t((Z - MinZ) / Cell), 0, Rows - 1);
	float Best = NONE;
	for (const int32_t Index : Cells[size_t(J) * size_t(Columns) + size_t(I)])
	{
		const FieldTriangle& Tri = Triangles[size_t(Index)];
		const float D = (Tri.B.z - Tri.C.z) * (Tri.A.x - Tri.C.x) + (Tri.C.x - Tri.B.x) * (Tri.A.z - Tri.C.z);
		if (std::abs(D) < 1e-9f)
		{
			continue;
		}
		const float U = ((Tri.B.z - Tri.C.z) * (X - Tri.C.x) + (Tri.C.x - Tri.B.x) * (Z - Tri.C.z)) / D;
		const float V = ((Tri.C.z - Tri.A.z) * (X - Tri.C.x) + (Tri.A.x - Tri.C.x) * (Z - Tri.C.z)) / D;
		const float W = 1.0f - U - V;
		const float Tolerance = -1e-4f;
		if (U < Tolerance || V < Tolerance || W < Tolerance)
		{
			continue;
		}
		Best = std::fmax(Best, U * Tri.A.y + V * Tri.B.y + W * Tri.C.y);
	}
	return Best;
}

void BuildingGen::BuildCompound(const std::vector<CompoundWing>& InWings, MeshAccumulator& OutMesh,
	std::vector<MeshAccumulator>* OutWingMeshes, std::vector<RoofHeightField>* OutFields,
	CompoundReport* OutReport)
{
	CompoundReport Report;

	// Corners that die into a neighbour lose their 翼角 before anything is built.
	std::vector<CompoundWing> Wings = InWings;
	for (size_t Index = 0; Index < Wings.size(); ++Index)
	{
		Wings[Index].Spec.CornerFlipMask = CornerMaskFor(InWings, Index);
	}

	// 1. Every wing on its own, moved into the compound frame.
	std::vector<MeshAccumulator> World(Wings.size());
	for (size_t Index = 0; Index < Wings.size(); ++Index)
	{
		MeshAccumulator Local;
		Local.SetMottle(Wings[Index].Spec.ColorMottle);
		BuildBuilding(Wings[Index].Spec, Local);
		TransformInto(Local, Wings[Index], World[Index]);
		Report.TrianglesIn += World[Index].GetTriangleCount();
	}

	// 2. Each wing's roof, as a height field over the plan.
	std::vector<RoofHeightField> Fields(Wings.size());
	std::vector<Obstacle> Obstacles(Wings.size());
	for (size_t Index = 0; Index < Wings.size(); ++Index)
	{
		const BuildingSpec& Spec = Wings[Index].Spec;
		Fields[Index].Build(World[Index], Wings[Index].Offset.y + Spec.EaveHeight - Spec.Module * 0.1f, FIELD_CELL);

		Obstacle& Other = Obstacles[Index];
		Other.Field = &Fields[Index];
		// [自定] Shell: boarding, bedding and tiles. SurfaceLift: the skin above the boarding.
		Other.Shell = std::clamp(Spec.Module * 0.45f, 0.18f, 0.7f);
		Other.SurfaceLift = std::clamp(Spec.Module * 0.12f, 0.04f, 0.25f);
		Other.bHasBody = !Spec.bRoofOnly;
		Other.Offset = Wings[Index].Offset;
		Other.Turns = Wings[Index].Turns;
		const float Pad = Spec.ColumnRadius * 1.2f;
		Other.BodyHalfX = ((Spec.Sides != 4) ? Spec.PlanApothem : Spec.Width * 0.5f) + Pad;
		Other.BodyHalfZ = ((Spec.Sides != 4) ? Spec.PlanApothem : Spec.Depth * 0.5f) + Pad;
		Other.BodyBottom = Wings[Index].Offset.y + Spec.PlatformHeight + Spec.Module * 0.05f;
	}

	// 3. The junction rule, wing against every other wing.
	if (OutWingMeshes != nullptr)
	{
		OutWingMeshes->assign(Wings.size(), MeshAccumulator());
	}
	for (size_t Self = 0; Self < Wings.size(); ++Self)
	{
		const MeshAccumulator& Source = World[Self];
		const int32_t Count = Source.GetTriangleCount();
		for (int32_t Index = 0; Index < Count; ++Index)
		{
			Triangle Tri;
			for (int32_t K = 0; K < 3; ++K)
			{
				const int32_t V = Source.Indices[size_t(Index * 3 + K)];
				Tri[size_t(K)] = { Source.Vertices[size_t(V)], Source.Normals[size_t(V)],
					Source.UVs[size_t(V)], Source.Colors[size_t(V)] };
			}

			std::vector<Triangle> Pieces = { Tri };
			if (Source.GetTriangleTag(Index) != MeshAccumulator::TAG_FIXED)
			{
				for (size_t Other = 0; Other < Wings.size() && !Pieces.empty(); ++Other)
				{
					if (Other == Self || Fields[Other].IsEmpty())
					{
						continue;
					}
					// Cheap reject: nowhere near the other roof.
					const RoofHeightField& Field = Fields[Other];
					const float X0 = std::fmin(Tri[0].Position.x, std::fmin(Tri[1].Position.x, Tri[2].Position.x));
					const float X1 = std::fmax(Tri[0].Position.x, std::fmax(Tri[1].Position.x, Tri[2].Position.x));
					const float Z0 = std::fmin(Tri[0].Position.z, std::fmin(Tri[1].Position.z, Tri[2].Position.z));
					const float Z1 = std::fmax(Tri[0].Position.z, std::fmax(Tri[1].Position.z, Tri[2].Position.z));
					if (X1 < Field.MinX || X0 > Field.MaxX || Z1 < Field.MinZ || Z0 > Field.MaxZ)
					{
						continue;
					}
					std::vector<Triangle> Next;
					for (const Triangle& Piece : Pieces)
					{
						ClipAgainst(Piece, Obstacles[Other], 0, Next);
					}
					Pieces.swap(Next);
				}
			}

			for (const Triangle& Piece : Pieces)
			{
				const Vector3 Positions[3] = { Piece[0].Position, Piece[1].Position, Piece[2].Position };
				const Vector3 Normals[3] = { Piece[0].Normal, Piece[1].Normal, Piece[2].Normal };
				const Vector2 Coords[3] = { Piece[0].UV, Piece[1].UV, Piece[2].UV };
				const Color Tints[3] = { Piece[0].Tint, Piece[1].Tint, Piece[2].Tint };
				OutMesh.SetSlot(Source.GetTriangleSlot(Index));
				OutMesh.SetTag(Source.GetTriangleTag(Index));
				OutMesh.AddRawTriangle(Positions, Normals, Coords, Tints);
				if (OutWingMeshes != nullptr)
				{
					MeshAccumulator& WingOut = (*OutWingMeshes)[Self];
					WingOut.SetSlot(Source.GetTriangleSlot(Index));
					WingOut.SetTag(Source.GetTriangleTag(Index));
					WingOut.AddRawTriangle(Positions, Normals, Coords, Tints);
				}
			}
		}
	}

	// 4. 窝角沟 wherever two roofs meet, laid over the cut.
	OutMesh.SetSlot(EMaterialSlot::Ridge);
	OutMesh.SetTag(MeshAccumulator::TAG_NONE);
	for (size_t A = 0; A < Wings.size(); ++A)
	{
		for (size_t B = A + 1; B < Wings.size(); ++B)
		{
			if (Fields[A].IsEmpty() || Fields[B].IsEmpty())
			{
				continue;
			}
			const BuildingSpec& Lower = (Wings[A].Spec.Module < Wings[B].Spec.Module) ? Wings[A].Spec : Wings[B].Spec;
			// [自定] Gutter: a 0.55 D channel standing just proud of the tiles.
			const float Half = Lower.Module * 0.55f;
			const float Lift = std::fmax(Obstacles[A].SurfaceLift, Obstacles[B].SurfaceLift) * 0.6f;
			for (const std::vector<Vector2>& Raw : TraceValleys(Obstacles[A], Obstacles[B]))
			{
				if (LineLength(Raw) < Lower.Module * 1.5f)
				{
					continue;
				}
				const std::vector<Vector2> Line = SmoothLine(Raw, std::fmax(Lower.Module * 0.6f, 0.3f));
				std::vector<Vector3> Knots;
				for (const Vector2& P : Line)
				{
					const float H = std::fmax(Obstacles[A].Height(Vector3(P.x, 0.0f, P.y)),
						Obstacles[B].Height(Vector3(P.x, 0.0f, P.y)));
					Knots.push_back(Vector3(P.x, H + Lift, P.y));
				}

				SweepSettings Settings;
				Settings.Contour = { Vector2(-Half, -Half * 0.3f), Vector2(Half, -Half * 0.3f),
					Vector2(Half, Half * 0.12f), Vector2(Half * 0.7f, Half * 0.02f),
					Vector2(-Half * 0.7f, Half * 0.02f), Vector2(-Half, Half * 0.12f) };
				Settings.bClosedContour = true;
				Settings.bGenerateCaps = true;
				Settings.UpReference = Vector3(0, 1, 0);
				SweepResult Sweep;
				if (BuildSweep(Knots, Settings, Sweep))
				{
					OutMesh.AddSweep(Sweep, Lower.TileColor * 0.85f);
					++Report.ValleyCount;
					Report.ValleyLength += LineLength(Line);
				}
			}
		}
	}

	Report.TrianglesOut = OutMesh.GetTriangleCount();
	if (OutFields != nullptr)
	{
		*OutFields = Fields;
	}
	if (OutReport != nullptr)
	{
		*OutReport = Report;
	}
}
