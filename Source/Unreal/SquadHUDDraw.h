#pragma once

// Canvas drawing helpers shared by the HUD's .cpp files (minimap, tactical map). Private to the HUD.

#include "CoreMinimal.h"
#include "CanvasItem.h"

namespace SquadHUDDraw
{
	inline FLinearColor WithAlpha(FLinearColor C, float A) { C.A = A; return C; }

	inline void AddTri(TArray<FCanvasUVTri>& Tris, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
	{
		FCanvasUVTri& Tri = Tris.AddDefaulted_GetRef();
		Tri.V0_Pos = A; Tri.V1_Pos = B; Tri.V2_Pos = C;
		Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = Color;
	}

	inline void AddDisc(TArray<FCanvasUVTri>& Tris, const FVector2D& Center, float Radius, const FLinearColor& Color, int32 Segments = 10)
	{
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A0 = 2.f * PI * I / Segments, A1 = 2.f * PI * (I + 1) / Segments;
			AddTri(Tris, Center, Center + Radius * FVector2D(FMath::Cos(A0), FMath::Sin(A0)),
				Center + Radius * FVector2D(FMath::Cos(A1), FMath::Sin(A1)), Color);
		}
	}

	/** A ring between two radii. */
	inline void AddRing(TArray<FCanvasUVTri>& Tris, const FVector2D& Center, float Inner, float Outer, const FLinearColor& Color, int32 Segments = 20)
	{
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A0 = 2.f * PI * I / Segments, A1 = 2.f * PI * (I + 1) / Segments;
			const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0)), D1(FMath::Cos(A1), FMath::Sin(A1));
			AddTri(Tris, Center + D0 * Inner, Center + D0 * Outer, Center + D1 * Outer, Color);
			AddTri(Tris, Center + D0 * Inner, Center + D1 * Outer, Center + D1 * Inner, Color);
		}
	}

	/** A thick segment as a quad (with square ends extended by half the width so joints overlap). */
	inline void AddSegment(TArray<FCanvasUVTri>& Tris, const FVector2D& A, const FVector2D& B, float Width, const FLinearColor& Color, bool bExtendEnds = true)
	{
		FVector2D Dir = B - A;
		if (!Dir.Normalize())
		{
			return;
		}
		const FVector2D Side = FVector2D(-Dir.Y, Dir.X) * (Width * 0.5f);
		const FVector2D Ext = bExtendEnds ? Dir * (Width * 0.5f) : FVector2D::ZeroVector;
		const FVector2D A0 = A - Ext, B0 = B + Ext;
		AddTri(Tris, A0 + Side, B0 + Side, B0 - Side, Color);
		AddTri(Tris, A0 + Side, B0 - Side, A0 - Side, Color);
	}

	/** Convex polygon as a fan. */
	inline void AddPolygon(TArray<FCanvasUVTri>& Tris, const TArray<FVector2D>& Points, const FLinearColor& Color)
	{
		for (int32 I = 1; I + 1 < Points.Num(); ++I)
		{
			AddTri(Tris, Points[0], Points[I], Points[I + 1], Color);
		}
	}

	/** Closed outline of a polygon. */
	inline void AddOutline(TArray<FCanvasUVTri>& Tris, const TArray<FVector2D>& Points, float Width, const FLinearColor& Color)
	{
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			AddSegment(Tris, Points[I], Points[(I + 1) % Points.Num()], Width, Color);
		}
	}

	/** Facing cone: a fan from Apex, Dir is the screen-space facing (unit). */
	inline void AddCone(TArray<FCanvasUVTri>& Tris, const FVector2D& Apex, const FVector2D& Dir, float Length, float HalfAngleDeg, const FLinearColor& Color, int32 Segments = 6)
	{
		const float Facing = FMath::Atan2(Dir.Y, Dir.X);
		const float Half = FMath::DegreesToRadians(HalfAngleDeg);
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A0 = Facing - Half + 2.f * Half * I / Segments;
			const float A1 = Facing - Half + 2.f * Half * (I + 1) / Segments;
			AddTri(Tris, Apex, Apex + Length * FVector2D(FMath::Cos(A0), FMath::Sin(A0)),
				Apex + Length * FVector2D(FMath::Cos(A1), FMath::Sin(A1)), Color);
		}
	}

	/** Liang-Barsky: clips the segment A-B to Box. False if nothing of it is inside. */
	inline bool ClipSegment(FVector2D& A, FVector2D& B, const FBox2D& Box)
	{
		const FVector2D D = B - A;
		float T0 = 0.f, T1 = 1.f;
		const float P[4] = { static_cast<float>(-D.X), static_cast<float>(D.X), static_cast<float>(-D.Y), static_cast<float>(D.Y) };
		const float Q[4] = { static_cast<float>(A.X - Box.Min.X), static_cast<float>(Box.Max.X - A.X), static_cast<float>(A.Y - Box.Min.Y), static_cast<float>(Box.Max.Y - A.Y) };
		for (int32 I = 0; I < 4; ++I)
		{
			if (FMath::IsNearlyZero(P[I]))
			{
				if (Q[I] < 0.f)
				{
					return false;
				}
				continue;
			}
			const float T = Q[I] / P[I];
			if (P[I] < 0.f)
			{
				T0 = FMath::Max(T0, T);
			}
			else
			{
				T1 = FMath::Min(T1, T);
			}
			if (T0 > T1)
			{
				return false;
			}
		}
		const FVector2D Start = A;
		A = Start + D * T0;
		B = Start + D * T1;
		return true;
	}

	/** Distance from P to the segment A-B. */
	inline float DistToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		return static_cast<float>(FMath::Sqrt(FMath::PointDistToSegmentSquared(FVector(P, 0.f), FVector(A, 0.f), FVector(B, 0.f))));
	}
}
