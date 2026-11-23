// Tactical map part of ASquadHUD (GDD §13.1): the map maximised with the team's plan drawn on it, its side panel
// (tools, team roles and planning rights, order requests), the plan marks on the minimap, and the plan notices.

#include "SquadHUD.h"

#include "CanvasItem.h"
#include "GridManager.h"
#include "SquadHUDDraw.h"
#include "SquadPlayerController.h"
#include "SquadPlayerState.h"
#include "TacticalPlan.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "GameFramework/GameStateBase.h"

using namespace SquadHUDDraw;

namespace TacMap
{
	// Sizes at 1080p, scaled with the screen height.
	constexpr float Margin = 24.f;
	constexpr float PanelWidth = 310.f;
	constexpr float Gap = 14.f;
	constexpr float PanelPad = 14.f;
	constexpr float RowHeight = 22.f;
	constexpr float ToolButtonHeight = 32.f;
	/** Minimap cells are about this many pixels at 1080p; Detail = the tactical map's cell size over this. */
	constexpr float MinimapCellPx = 4.7f;
	constexpr float MaxDetail = 2.6f;
	/** Plan marks at Detail 1 (the minimap). */
	constexpr float IconRadius = 5.f;
	constexpr float LineWidth = 2.2f;
	constexpr float NoticeLifetime = 6.f;
	constexpr int32 MaxNotices = 4;

	const FLinearColor Backdrop(0.f, 0.f, 0.f, 0.6f);
	const FLinearColor FrameBg(0.02f, 0.025f, 0.02f, 0.92f);
	const FLinearColor PanelBg(0.03f, 0.035f, 0.03f, 0.94f);
	const FLinearColor Border(1.f, 1.f, 1.f, 0.16f);
	const FLinearColor Header(1.f, 0.85f, 0.4f);
	const FLinearColor Text(0.9f, 0.9f, 0.9f);
	const FLinearColor Dim(0.55f, 0.55f, 0.55f);
	const FLinearColor Button(0.22f, 0.27f, 0.16f, 1.f);
	const FLinearColor ButtonHover(0.36f, 0.45f, 0.22f, 1.f);
	const FLinearColor ButtonActive(0.55f, 0.45f, 0.15f, 1.f);
	const FLinearColor ButtonDisabled(0.15f, 0.15f, 0.15f, 1.f);
	const FLinearColor Back(0.f, 0.f, 0.f, 0.65f);
	const FLinearColor Request(1.f, 0.7f, 0.1f, 1.f);
	const FLinearColor Acknowledged(0.45f, 0.9f, 0.45f, 1.f);

	FLinearColor RoleColor(ETacticalRole Role)
	{
		switch (Role)
		{
		case ETacticalRole::CO: return FLinearColor(1.f, 0.8f, 0.3f);
		case ETacticalRole::XO: return FLinearColor(0.95f, 0.65f, 0.3f);
		case ETacticalRole::Planner: return FLinearColor(0.55f, 0.8f, 1.f);
		default: return Dim;
		}
	}

	FLinearColor MarkerColor(ETacticalMarkerType Type)
	{
		switch (Type)
		{
		case ETacticalMarkerType::Push:     return FLinearColor(0.3f, 0.72f, 1.f);
		case ETacticalMarkerType::HoldLine: return FLinearColor(0.35f, 0.85f, 0.45f);
		case ETacticalMarkerType::Route:    return FLinearColor(0.95f, 0.92f, 0.55f);
		case ETacticalMarkerType::Defend:   return FLinearColor(0.35f, 0.85f, 0.45f);
		case ETacticalMarkerType::Attack:   return FLinearColor(1.f, 0.55f, 0.15f);
		case ETacticalMarkerType::Rally:    return FLinearColor(1.f, 0.85f, 0.4f);
		default:                            return FLinearColor(1.f, 0.25f, 0.2f);
		}
	}

	const TCHAR* ToolLabel(ETacticalTool Tool)
	{
		switch (Tool)
		{
		case ETacticalTool::View: return TEXT("View");
		case ETacticalTool::Push: return TEXT("Push");
		case ETacticalTool::HoldLine: return TEXT("Hold");
		case ETacticalTool::Route: return TEXT("Route");
		case ETacticalTool::Defend: return TEXT("Defend");
		case ETacticalTool::Attack: return TEXT("Attack");
		case ETacticalTool::Rally: return TEXT("Rally");
		case ETacticalTool::Enemy: return TEXT("Enemy");
		default: return TEXT("Erase");
		}
	}

	const TCHAR* ToolHint(ETacticalTool Tool)
	{
		switch (Tool)
		{
		case ETacticalTool::View: return TEXT("Click / drag: move the camera");
		case ETacticalTool::Push: return TEXT("Drag: arrow - push this way");
		case ETacticalTool::HoldLine: return TEXT("Drag: line to hold");
		case ETacticalTool::Route: return TEXT("Drag: draw the route to take");
		case ETacticalTool::Defend: return TEXT("Click: defend this position");
		case ETacticalTool::Attack: return TEXT("Click: attack this position");
		case ETacticalTool::Rally: return TEXT("Click: rally / regroup here");
		case ETacticalTool::Enemy: return TEXT("Click: enemy reported here");
		default: return TEXT("Click a mark to remove it");
		}
	}

	void AddArrowHead(TArray<FCanvasUVTri>& T, const FVector2D& Tip, const FVector2D& Dir, float Length, float HalfWidth, const FLinearColor& Color)
	{
		const FVector2D Side(-Dir.Y, Dir.X);
		AddTri(T, Tip, Tip - Dir * Length + Side * HalfWidth, Tip - Dir * Length - Side * HalfWidth, Color);
	}

	/**
	 * Triangles for one plan mark. P: the centre for point marks, start and end for Push / HoldLine, the path for
	 * Route (screen pixels). R: icon radius, W: line width. Drawn over a dark backing so it reads on any ground.
	 */
	void AddMarker(TArray<FCanvasUVTri>& T, ETacticalMarkerType Type, const TArray<FVector2D>& P, float R, float W, const FLinearColor& Color)
	{
		if (P.Num() == 0)
		{
			return;
		}
		const float B = FMath::Max(1.5f, W * 0.4f); // Backing margin.
		const FLinearColor Bk(0.f, 0.f, 0.f, 0.6f * Color.A);
		const FVector2D C = P[0];
		switch (Type)
		{
		case ETacticalMarkerType::Push:
		{
			const FVector2D A = P[0], E = P.Last();
			FVector2D Dir = E - A;
			const float Len = Dir.Size();
			if (!Dir.Normalize())
			{
				return;
			}
			const float H = FMath::Min(W * 3.4f, Len * 0.6f);
			const FVector2D ShaftEnd = E - Dir * H * 0.7f;
			AddSegment(T, A, ShaftEnd, W + 2.f * B, Bk);
			AddArrowHead(T, E + Dir * B * 1.6f, Dir, H + B * 2.4f, H * 0.62f + B * 1.6f, Bk);
			AddSegment(T, A, ShaftEnd, W, Color);
			AddArrowHead(T, E, Dir, H, H * 0.62f, Color);
			break;
		}
		case ETacticalMarkerType::HoldLine:
		{
			const FVector2D A = P[0], E = P.Last();
			FVector2D Dir = E - A;
			const float Len = Dir.Size();
			if (!Dir.Normalize())
			{
				return;
			}
			// Ticks point up the screen: the enemy's side (their deployment zone is at the top).
			FVector2D Up(-Dir.Y, Dir.X);
			if (Up.Y > 0.f)
			{
				Up = -Up;
			}
			const FVector2D Across(-Dir.Y, Dir.X);
			const int32 Ticks = FMath::Max(1, FMath::FloorToInt(Len / (W * 4.5f)));
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const float Extra = Pass == 0 ? 2.f * B : 0.f;
				const FLinearColor Col = Pass == 0 ? Bk : Color;
				AddSegment(T, A, E, W + Extra, Col);
				AddSegment(T, A - Across * W * 1.6f, A + Across * W * 1.6f, W * 0.8f + Extra, Col);
				AddSegment(T, E - Across * W * 1.6f, E + Across * W * 1.6f, W * 0.8f + Extra, Col);
				for (int32 I = 0; I < Ticks; ++I)
				{
					const FVector2D Q = A + Dir * (Len * (I + 0.5f) / Ticks);
					AddSegment(T, Q, Q + Up * W * 2.2f, W * 0.6f + Extra, Col);
				}
			}
			break;
		}
		case ETacticalMarkerType::Route:
		{
			if (P.Num() < 2)
			{
				return;
			}
			const float RW = W * 0.75f;
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const float Extra = Pass == 0 ? 2.f * B : 0.f;
				const FLinearColor Col = Pass == 0 ? Bk : Color;
				AddDisc(T, P[0], RW * 1.2f + Extra * 0.5f, Col, 8);
				for (int32 I = 1; I < P.Num(); ++I)
				{
					AddSegment(T, P[I - 1], P[I], RW + Extra, Col);
				}
				// Arrowhead along the last stretch at least a few pixels long.
				const FVector2D Tip = P.Last();
				FVector2D From = P[0];
				for (int32 I = P.Num() - 2; I >= 0; --I)
				{
					if (FVector2D::Distance(P[I], Tip) >= RW * 3.f)
					{
						From = P[I];
						break;
					}
				}
				FVector2D Dir = Tip - From;
				if (Dir.Normalize())
				{
					const float H = RW * 3.6f;
					AddArrowHead(T, Tip + Dir * (Extra * 0.8f), Dir, H + Extra * 1.2f, H * 0.6f + Extra * 0.8f, Col);
				}
			}
			break;
		}
		case ETacticalMarkerType::Defend:
		{
			auto Shield = [&C](float Rad)
			{
				return TArray<FVector2D>{ C + FVector2D(-Rad, -Rad * 0.9f), C + FVector2D(Rad, -Rad * 0.9f), C + FVector2D(Rad, Rad * 0.1f),
					C + FVector2D(0.f, Rad * 1.15f), C + FVector2D(-Rad, Rad * 0.1f) };
			};
			AddPolygon(T, Shield(R + B * 1.4f), Bk);
			AddPolygon(T, Shield(R), Color);
			// Darker band across the top, like a heraldic chief.
			AddPolygon(T, { C + FVector2D(-R * 0.7f, -R * 0.62f), C + FVector2D(R * 0.7f, -R * 0.62f), C + FVector2D(R * 0.7f, -R * 0.3f), C + FVector2D(-R * 0.7f, -R * 0.3f) },
				FLinearColor(0.f, 0.f, 0.f, 0.3f * Color.A));
			break;
		}
		case ETacticalMarkerType::Attack:
		{
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const float Extra = Pass == 0 ? B : 0.f;
				const FLinearColor Col = Pass == 0 ? Bk : Color;
				const float Th = FMath::Max(1.5f, R * 0.22f);
				AddRing(T, C, R * 0.62f - Th * 0.5f - Extra, R * 0.62f + Th * 0.5f + Extra, Col, 18);
				const FVector2D Dirs[4] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
				for (const FVector2D& D : Dirs)
				{
					AddSegment(T, C + D * R * 0.3f, C + D * R * 1.25f, Th + 2.f * Extra, Col, false);
				}
				AddDisc(T, C, Th * 0.7f + Extra, Col, 8);
			}
			break;
		}
		case ETacticalMarkerType::Rally:
		{
			const float Th = FMath::Max(1.5f, R * 0.2f);
			const FVector2D Foot = C + FVector2D(-R * 0.45f, R), Top = C + FVector2D(-R * 0.45f, -R);
			const TArray<FVector2D> Flag = { Top, C + FVector2D(R * 0.95f, -R * 0.52f), C + FVector2D(-R * 0.45f, -R * 0.05f) };
			AddSegment(T, Foot, Top, Th + 2.f * B, Bk);
			AddPolygon(T, { Flag[0] + FVector2D(-B, -B * 1.5f), Flag[1] + FVector2D(B * 2.f, 0.f), Flag[2] + FVector2D(-B, B * 1.5f) }, Bk);
			AddDisc(T, Foot, Th * 1.6f + B, Bk, 8);
			AddSegment(T, Foot, Top, Th, Color);
			AddPolygon(T, Flag, Color);
			AddDisc(T, Foot, Th * 1.6f, Color, 8);
			break;
		}
		case ETacticalMarkerType::Enemy:
		{
			auto Diamond = [&C](float Rad)
			{
				return TArray<FVector2D>{ C + FVector2D(0.f, -Rad), C + FVector2D(Rad, 0.f), C + FVector2D(0.f, Rad), C + FVector2D(-Rad, 0.f) };
			};
			AddPolygon(T, Diamond(R * 1.1f + B * 1.4f), Bk);
			AddPolygon(T, Diamond(R * 1.1f), Color);
			const float X = R * 0.38f, Th = FMath::Max(1.2f, R * 0.18f);
			const FLinearColor Mark(0.f, 0.f, 0.f, 0.75f * Color.A);
			AddSegment(T, C + FVector2D(-X, -X), C + FVector2D(X, X), Th, Mark);
			AddSegment(T, C + FVector2D(-X, X), C + FVector2D(X, -X), Th, Mark);
			break;
		}
		}
	}

	/** Screen points of a mark (see AddMarker). */
	TArray<FVector2D> MarkerScreenPoints(const ASquadHUD::FMapXf& Xf, const FTacticalMarker& M)
	{
		TArray<FVector2D> Out;
		if (M.Type == ETacticalMarkerType::Route)
		{
			for (const FVector2f& P : M.Points)
			{
				Out.Add(Xf.ToScreen(FVector2D(P)));
			}
		}
		else
		{
			Out.Add(Xf.ToScreen(FVector2D(M.Start)));
			if (M.IsLine())
			{
				Out.Add(Xf.ToScreen(FVector2D(M.End)));
			}
		}
		return Out;
	}

	/** Pixel distance from Mouse to the mark as drawn. */
	float DistanceToMarker(const TArray<FVector2D>& P, const FTacticalMarker& M, const FVector2D& Mouse, float R)
	{
		if (P.Num() == 0)
		{
			return FLT_MAX;
		}
		if (M.IsPoint())
		{
			return FMath::Max(0.f, FVector2D::Distance(P[0], Mouse) - R);
		}
		float Best = FLT_MAX;
		for (int32 I = 1; I < P.Num(); ++I)
		{
			Best = FMath::Min(Best, DistToSegment(Mouse, P[I - 1], P[I]));
		}
		return Best;
	}

	FString MarkerTooltip(const FTacticalMarker& M)
	{
		const TCHAR* RoleStr = TacticalPlan::RoleTag(M.AuthorRole);
		return FString::Printf(TEXT("%s - %s%s%s"), TacticalPlan::MarkerName(M.Type), RoleStr, *RoleStr ? TEXT(" ") : TEXT(""), *M.AuthorName);
	}

	// ---- Command wheel and pings (GDD §13.2). Sizes at 1080p.

	/** Plan marks drawn on the battlefield. */
	constexpr float WorldIconRadius = 11.f;
	constexpr float WorldLineWidth = 4.5f;
	constexpr float RadialDeadzone = 26.f;
	constexpr float RadialInner = 34.f;
	constexpr float RadialOuter = 122.f;
	/** Off-screen ping arrows sit this far in from the screen edge. */
	constexpr float EdgeInset = 34.f;

	// Opacity of what is drawn over the battlefield, kept low so the play area stays readable (the minimap and
	// tactical map draw at full strength). Marks being erased or placed are drawn stronger.
	constexpr float WorldMarkAlpha = 0.4f;
	constexpr float WorldDraftAlpha = 0.65f;
	constexpr float WorldPingAlpha = 0.7f;
	constexpr float WorldRequestAlpha = 0.55f;
	constexpr float WorldLabelAlpha = 0.75f;

	FLinearColor PingColor(ETacticalPingType Type)
	{
		return Type == ETacticalPingType::Spot ? FLinearColor(1.f, 0.3f, 0.2f) : FLinearColor(0.35f, 0.9f, 1.f);
	}

	/** Spot: a warning triangle with "!"; Ping: a ring with a dot. */
	void AddPingIcon(TArray<FCanvasUVTri>& T, ETacticalPingType Type, const FVector2D& C, float R, const FLinearColor& Color)
	{
		const FLinearColor Bk(0.f, 0.f, 0.f, 0.6f * Color.A);
		const float B = FMath::Max(1.5f, R * 0.18f);
		if (Type == ETacticalPingType::Spot)
		{
			auto Tri = [&C](float Rad) { return TArray<FVector2D>{ C + FVector2D(0.f, -Rad * 1.1f), C + FVector2D(Rad, Rad * 0.75f), C + FVector2D(-Rad, Rad * 0.75f) }; };
			AddPolygon(T, Tri(R + B * 1.6f), Bk);
			AddPolygon(T, Tri(R), Color);
			const FLinearColor Mark(0.f, 0.f, 0.f, 0.85f * Color.A);
			AddSegment(T, C + FVector2D(0.f, -R * 0.45f), C + FVector2D(0.f, R * 0.12f), FMath::Max(1.5f, R * 0.2f), Mark, false);
			AddDisc(T, C + FVector2D(0.f, R * 0.42f), FMath::Max(1.f, R * 0.12f), Mark, 6);
		}
		else
		{
			const float Th = FMath::Max(1.5f, R * 0.22f);
			AddRing(T, C, R * 0.7f - B, R * 0.7f + Th + B, Bk, 18);
			AddRing(T, C, R * 0.7f, R * 0.7f + Th, Color, 18);
			AddDisc(T, C, R * 0.25f + B * 0.5f, Bk, 8);
			AddDisc(T, C, R * 0.25f, Color, 8);
		}
	}

	/** Small icon of a mark type, centred in a box of radius R (tool buttons, the command wheel). */
	void AddMarkerIcon(TArray<FCanvasUVTri>& T, ETacticalMarkerType Type, const FVector2D& C, float R, float W, const FLinearColor& Color)
	{
		TArray<FVector2D> P;
		switch (Type)
		{
		case ETacticalMarkerType::Push: P = { C + FVector2D(-R * 1.1f, R * 0.7f), C + FVector2D(R * 1.1f, -R * 0.7f) }; break;
		case ETacticalMarkerType::HoldLine: P = { C + FVector2D(-R * 1.2f, R * 0.3f), C + FVector2D(R * 1.2f, R * 0.3f) }; break;
		case ETacticalMarkerType::Route:
			P = { C + FVector2D(-R * 1.2f, R * 0.8f), C + FVector2D(-R * 0.3f, -R * 0.4f), C + FVector2D(R * 0.4f, R * 0.4f), C + FVector2D(R * 1.2f, -R * 0.8f) };
			break;
		default: P = { C }; break;
		}
		AddMarker(T, Type, P, R, W, Color);
	}

	/** An X (erase). */
	void AddCross(TArray<FCanvasUVTri>& T, const FVector2D& C, float R, float W, const FLinearColor& Color)
	{
		AddSegment(T, C + FVector2D(-R * 0.8f, -R * 0.8f), C + FVector2D(R * 0.8f, R * 0.8f), W, Color);
		AddSegment(T, C + FVector2D(-R * 0.8f, R * 0.8f), C + FVector2D(R * 0.8f, -R * 0.8f), W, Color);
	}

	/** Expanding, fading ring under a ping (0..1 phase repeating). */
	void AddPingPulse(TArray<FCanvasUVTri>& T, const FVector2D& C, float R, float Time, const FLinearColor& Color)
	{
		const float Phase = FMath::Fmod(Time * 1.2f, 1.f);
		const float Rad = R * (0.6f + 1.8f * Phase);
		AddRing(T, C, Rad, Rad + FMath::Max(1.5f, R * 0.18f), WithAlpha(Color, Color.A * (1.f - Phase)), 22);
	}

	/** World points of a mark being drawn: the route as traced, or a line's ends snapped to cell centres as the server will store them. */
	TArray<FVector2D> DraftWorldPoints(const ASquadPlayerController::FPlanDraft& Draft, const AGridManager* Grid)
	{
		TArray<FVector2D> Out;
		if (Draft.Type == ETacticalMarkerType::Route)
		{
			Out = Draft.Points;
			Out.Add(Draft.End);
			return Out;
		}
		FVector2D A = Draft.Start, E = Draft.End;
		if (Grid)
		{
			FIntPoint Cell;
			const float Z = Grid->GetActorLocation().Z;
			if (Grid->WorldToCell(FVector(A, Z), Cell)) { A = FVector2D(Grid->CellToWorld(Cell)); }
			if (Grid->WorldToCell(FVector(E, Z), Cell)) { E = FVector2D(Grid->CellToWorld(Cell)); }
		}
		Out = { A, E };
		return Out;
	}

	/** Seconds a ping has left (from the replicated server times). */
	float PingTimeLeft(const FTacticalPing& Ping, float ServerNow) { return Ping.Lifetime - (ServerNow - Ping.Time); }
}

// ------------------------------------------------------------------------------------------------ Hit testing

bool ASquadHUD::TacticalMapToWorld(const FVector2D& ScreenPosition, FVector2D& OutWorld, bool bClamp) const
{
	if (!bTacMapVisible || TacMapXf.PxPerCm <= 0.f)
	{
		return false;
	}
	const FBox2D& Box = TacMapXf.MapBox;
	if (!bClamp && !Box.IsInside(ScreenPosition))
	{
		return false;
	}
	const FVector2D Clamped(FMath::Clamp(ScreenPosition.X, Box.Min.X, Box.Max.X - 0.01f), FMath::Clamp(ScreenPosition.Y, Box.Min.Y, Box.Max.Y - 0.01f));
	OutWorld = TacMapXf.ToWorld(Clamped);
	return true;
}

bool ASquadHUD::GetTacticalButtonAt(const FVector2D& ScreenPosition, FTacMapButton& OutButton) const
{
	for (const FTacMapButton& Button : TacButtons)
	{
		if (Button.bEnabled && Button.Box.IsInside(ScreenPosition))
		{
			OutButton = Button;
			return true;
		}
	}
	return false;
}

int32 ASquadHUD::FindTacticalMarkerAt(const FVector2D& ScreenPosition) const
{
	const ASquadPlayerController* PC = Cast<ASquadPlayerController>(GetOwningPlayerController());
	const ATacticalPlan* Plan = PC ? PC->GetTacticalPlan() : nullptr;
	if (!bTacMapVisible || !Plan || !TacMapXf.MapBox.IsInside(ScreenPosition))
	{
		return INDEX_NONE;
	}
	const float Reach = FMath::Max(6.f, TacMapLineWidth * 1.5f);
	int32 BestId = INDEX_NONE;
	float BestDist = Reach;
	// Newest first: it is drawn on top.
	const TArray<FTacticalMarker>& Markers = Plan->GetMarkers();
	for (int32 I = Markers.Num() - 1; I >= 0; --I)
	{
		const FTacticalMarker& M = Markers[I];
		const float Dist = TacMap::DistanceToMarker(TacMap::MarkerScreenPoints(TacMapXf, M), M, ScreenPosition, TacMapIconRadius);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestId = M.Id;
		}
	}
	return BestId;
}

// ------------------------------------------------------------------------------------------------ Plan overlay

void ASquadHUD::DrawPlanOverlay(ASquadPlayerController& PC, const FMapXf& Xf, float S, float Detail, int32 HighlightId, bool bTacticalMap)
{
	using namespace TacMap;

	const ATacticalPlan* Plan = PC.GetTacticalPlan();
	if (!Canvas->DefaultTexture)
	{
		return;
	}
	const FTexture* White = Canvas->DefaultTexture->GetResource();
	const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	const float R = IconRadius * S * Detail;
	const float W = LineWidth * S * Detail;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;

	TArray<FCanvasUVTri> Tris;
	if (Plan)
	{
		for (const FTacticalMarker& M : Plan->GetMarkers())
		{
			const bool bHi = M.Id == HighlightId;
			FLinearColor Color = MarkerColor(M.Type);
			Color.A = bTacticalMap ? (HighlightId == INDEX_NONE || bHi ? 1.f : 0.75f) : 0.9f;
			if (bHi)
			{
				Color = FLinearColor::LerpUsingHSV(Color, FLinearColor::White, 0.35f);
			}
			AddMarker(Tris, M.Type, MarkerScreenPoints(Xf, M), bHi ? R * 1.2f : R, bHi ? W * 1.25f : W, Color);
		}

		// Order requests: a pulsing amber ring where the squad asking is; green once acknowledged.
		for (const FTacticalOrderRequest& Req : Plan->GetRequests())
		{
			const FVector2D P = Xf.ToScreen(FVector2D(Req.Location));
			if (Req.bAcknowledged)
			{
				AddRing(Tris, P, R * 0.9f, R * 0.9f + FMath::Max(1.5f, W * 0.6f), WithAlpha(Acknowledged, 0.7f));
			}
			else
			{
				const float Pulse = FMath::Fmod(Time * 0.9f, 1.f);
				const float Rad = R * (0.8f + 1.4f * Pulse);
				AddRing(Tris, P, Rad, Rad + FMath::Max(1.5f, W * 0.7f), WithAlpha(Request, 1.f - Pulse));
				AddRing(Tris, P, R * 0.75f, R * 0.75f + FMath::Max(1.5f, W * 0.7f), Request);
			}
		}
	}

	// Pings (GDD §13.2): pulsing, fading out over their last second.
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float ServerNow = GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : 0.f;
	if (Plan)
	{
		for (const FTacticalPing& Ping : Plan->GetPings())
		{
			const float Fade = FMath::Clamp(PingTimeLeft(Ping, ServerNow), 0.f, 1.f);
			if (Fade <= 0.f)
			{
				continue;
			}
			const FVector2D P = Xf.ToScreen(FVector2D(Ping.Location));
			const FLinearColor Color = WithAlpha(PingColor(Ping.Type), Fade);
			AddPingPulse(Tris, P, R * 1.3f, Time, Color);
			AddPingIcon(Tris, Ping.Type, P, R * 0.9f, Color);
		}
	}

	// The stroke being drawn (on the tactical map, or placed from the command wheel), and a ghost of the point mark
	// under the cursor on the tactical map.
	const ASquadPlayerController::FPlanDraft* Draft = PC.GetPlanDraft().bActive ? &PC.GetPlanDraft()
		: PC.GetCommandMode() == ASquadPlayerController::ECommandMode::Drawing ? &PC.GetCommandDraft() : nullptr;
	if (Draft)
	{
		TArray<FVector2D> P;
		for (const FVector2D& Q : DraftWorldPoints(*Draft, AGridManager::Find(this)))
		{
			P.Add(Xf.ToScreen(Q));
		}
		AddMarker(Tris, Draft->Type, P, R, W, WithAlpha(MarkerColor(Draft->Type), 0.7f));
	}
	if (bTacticalMap)
	{
		if (!Draft && PC.GetCommandMode() == ASquadPlayerController::ECommandMode::None)
		{
			ETacticalMarkerType Type;
			float MX = 0.f, MY = 0.f;
			FVector2D World;
			if (TacticalPlan::ToolMarker(PC.GetTacticalTool(), Type) && PC.GetMousePosition(MX, MY) && TacticalMapToWorld(FVector2D(MX, MY), World, false))
			{
				const AGridManager* Grid = AGridManager::Find(this);
				FIntPoint Cell;
				if (Grid && Grid->WorldToCell(FVector(World, Xf.GroundZ), Cell))
				{
					World = FVector2D(Grid->CellToWorld(Cell));
				}
				if (Type == ETacticalMarkerType::Push || Type == ETacticalMarkerType::HoldLine || Type == ETacticalMarkerType::Route)
				{
					AddDisc(Tris, Xf.ToScreen(World), W * 1.2f, WithAlpha(MarkerColor(Type), 0.6f), 10);
				}
				else
				{
					AddMarker(Tris, Type, { Xf.ToScreen(World) }, R, W, WithAlpha(MarkerColor(Type), 0.45f));
				}
			}
		}
	}

	if (Tris.Num() > 0)
	{
		FCanvasTriangleItem Item(Tris, White);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	// Tactical map: who is asking for orders, beside each request ring.
	if (bTacticalMap && Plan)
	{
		for (const FTacticalOrderRequest& Req : Plan->GetRequests())
		{
			const FVector2D P = Xf.ToScreen(FVector2D(Req.Location));
			const FString Label = Req.bAcknowledged ? FString::Printf(TEXT("%s (ack)"), *Req.RequesterLabel) : FString::Printf(TEXT("%s ?"), *Req.RequesterLabel);
			float TW = 0.f, TH = 0.f;
			GetTextSize(Label, TW, TH, Font, 0.85f * S);
			const float X = P.X + R * 1.3f, Y = P.Y - TH * 0.5f;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f), X - 2.f * S, Y - 1.f * S, TW + 4.f * S, TH + 2.f * S);
			DrawText(Label, Req.bAcknowledged ? Acknowledged : Request, X, Y, Font, 0.85f * S);
		}
		// Who pinged.
		for (const FTacticalPing& Ping : Plan->GetPings())
		{
			const float Fade = FMath::Clamp(PingTimeLeft(Ping, ServerNow), 0.f, 1.f);
			if (Fade > 0.f)
			{
				const FVector2D P = Xf.ToScreen(FVector2D(Ping.Location));
				DrawText(Ping.AuthorLabel, WithAlpha(PingColor(Ping.Type), Fade), P.X + R * 1.2f, P.Y + R * 0.4f, Font, 0.8f * S);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------ Tactical map

void ASquadHUD::DrawTacticalMap(ASquadPlayerController& PC)
{
	using namespace TacMap;

	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid || !Canvas->DefaultTexture)
	{
		return;
	}
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.75f, 1.5f);
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;

	FMapXf Xf;
	GetMapOrientation(PC, *Grid, Xf);
	const float WidthCm = (Xf.XIsHorizontal() ? Grid->GridSizeX : Grid->GridSizeY) * Grid->CellSize;
	const float HeightCm = (Xf.XIsHorizontal() ? Grid->GridSizeY : Grid->GridSizeX) * Grid->CellSize;

	// ---- Layout: the map as big as fits, the side panel to its right, both centred.
	const float M = Margin * S, PanelW = PanelWidth * S, G = Gap * S;
	const float AvailW = Canvas->ClipX - 2.f * M - PanelW - G;
	const float AvailH = Canvas->ClipY - 2.f * M;
	const float Size = FMath::Max(100.f, FMath::Min(AvailW, AvailH));
	Xf.PxPerCm = Size / FMath::Max(WidthCm, HeightCm);
	const float MapW = WidthCm * Xf.PxPerCm, MapH = HeightCm * Xf.PxPerCm;
	const float X0 = FMath::Max(M, (Canvas->ClipX - (MapW + G + PanelW)) * 0.5f);
	const float Y0 = (Canvas->ClipY - MapH) * 0.5f;
	Xf.MapBox = FBox2D(FVector2D(X0, Y0), FVector2D(X0 + MapW, Y0 + MapH));
	const FBox2D SideBox(FVector2D(X0 + MapW + G, M), FVector2D(X0 + MapW + G + PanelW, Canvas->ClipY - M));

	const float Detail = FMath::Clamp(Grid->CellSize * Xf.PxPerCm / (MinimapCellPx * S), 1.f, MaxDetail);
	TacMapXf = Xf;
	bTacMapVisible = true;
	TacMapIconRadius = IconRadius * S * Detail;
	TacMapLineWidth = LineWidth * S * Detail;

	// ---- Backdrop, frame, the map, then the plan.
	DrawRect(Backdrop, 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	const float FramePad = 6.f * S;
	DrawRect(FrameBg, Xf.MapBox.Min.X - FramePad, Xf.MapBox.Min.Y - FramePad, MapW + 2.f * FramePad, MapH + 2.f * FramePad);
	DrawMapBase(PC, *Grid, Xf, S, Detail);

	float MX = 0.f, MY = 0.f;
	const bool bHaveMouse = PC.GetMousePosition(MX, MY);
	const FVector2D Mouse(MX, MY);
	const int32 HoverId = bHaveMouse && !PC.GetPlanDraft().bActive ? FindTacticalMarkerAt(Mouse) : INDEX_NONE;
	DrawPlanOverlay(PC, Xf, S, Detail, HoverId, true);

	// Which way is which: the enemy's side is always at the top.
	{
		// Small chips in the top-left and bottom-left corners, on a dark backing so edge walls don't hide them.
		auto Chip = [&](const FString& Str, const FLinearColor& Color, bool bTop)
		{
			float TW = 0.f, TH = 0.f;
			GetTextSize(Str, TW, TH, Font, 0.85f * S);
			const float CX = Xf.MapBox.Min.X + 8.f * S;
			const float CY = bTop ? Xf.MapBox.Min.Y + 8.f * S : Xf.MapBox.Max.Y - TH - 12.f * S;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.75f), CX, CY, TW + 10.f * S, TH + 4.f * S);
			DrawRect(Color, CX, CY, 2.f * S, TH + 4.f * S);
			DrawText(Str, Color, CX + 6.f * S, CY + 2.f * S, Font, 0.85f * S);
		};
		Chip(TEXT("ENEMY SIDE"), FLinearColor(1.f, 0.45f, 0.4f, 0.9f), true);
		Chip(TEXT("OUR DEPLOYMENT"), FLinearColor(0.5f, 0.7f, 1.f, 0.9f), false);
	}

	// Hovered mark: what it is and who drew it.
	if (HoverId != INDEX_NONE)
	{
		if (const ATacticalPlan* Plan = PC.GetTacticalPlan())
		{
			if (const FTacticalMarker* Mark = Plan->GetMarkers().FindByPredicate([HoverId](const FTacticalMarker& Q) { return Q.Id == HoverId; }))
			{
				FString Tip = MarkerTooltip(*Mark);
				const ASquadPlayerState* PS = PC.GetSquadPlayerState();
				if (PS && PS->CanPlan() && (PS->HasCommandRole() || Mark->Author == PS))
				{
					Tip += TEXT("   [right click: erase]");
				}
				float TW = 0.f, TH = 0.f;
				GetTextSize(Tip, TW, TH, Font, 0.9f * S);
				const float TX = FMath::Min(Mouse.X + 16.f * S, Xf.MapBox.Max.X - TW - 6.f * S), TY = Mouse.Y + 14.f * S;
				DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.8f), TX - 4.f * S, TY - 2.f * S, TW + 8.f * S, TH + 4.f * S);
				DrawText(Tip, FLinearColor::White, TX, TY, Font, 0.9f * S);
			}
		}
	}

	const FBox2D Frame(Xf.MapBox.Min - FVector2D(FramePad), Xf.MapBox.Max + FVector2D(FramePad));
	DrawRect(Border, Frame.Min.X, Frame.Min.Y, Frame.GetSize().X, 1.f);
	DrawRect(Border, Frame.Min.X, Frame.Max.Y - 1.f, Frame.GetSize().X, 1.f);
	DrawRect(Border, Frame.Min.X, Frame.Min.Y, 1.f, Frame.GetSize().Y);
	DrawRect(Border, Frame.Max.X - 1.f, Frame.Min.Y, 1.f, Frame.GetSize().Y);

	DrawTacticalPanel(PC, SideBox, S, HoverId);
}

void ASquadHUD::DrawTacButton(const FTacMapButton& Button, const FString& Label, const FVector2D& Mouse, float S, bool bActive, ETacticalTool IconTool)
{
	using namespace TacMap;

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const FVector2D Size = Button.Box.GetSize();
	const bool bHover = Button.bEnabled && Button.Box.IsInside(Mouse);
	DrawRect(!Button.bEnabled ? ButtonDisabled : bActive ? ButtonActive : (bHover ? ButtonHover : TacMap::Button), Button.Box.Min.X, Button.Box.Min.Y, Size.X, Size.Y);

	float TextX = Button.Box.Min.X + 8.f * S;
	bool bCentre = true;
	ETacticalMarkerType Type;
	if (Button.Action == ETacMapAction::SelectTool)
	{
		// Tool buttons: a small icon of the mark on the left.
		bCentre = false;
		const float IconBox = Size.Y;
		const FVector2D C(Button.Box.Min.X + IconBox * 0.5f, Button.Box.GetCenter().Y);
		const float R = IconBox * 0.24f, W = FMath::Max(1.5f, IconBox * 0.09f);
		TArray<FCanvasUVTri> Tris;
		if (TacticalPlan::ToolMarker(IconTool, Type))
		{
			TArray<FVector2D> P;
			if (Type == ETacticalMarkerType::Push || Type == ETacticalMarkerType::HoldLine)
			{
				P = { C + FVector2D(-R * 1.1f, R * 0.7f), C + FVector2D(R * 1.1f, -R * 0.7f) };
				if (Type == ETacticalMarkerType::HoldLine)
				{
					P = { C + FVector2D(-R * 1.2f, R * 0.3f), C + FVector2D(R * 1.2f, R * 0.3f) };
				}
			}
			else if (Type == ETacticalMarkerType::Route)
			{
				P = { C + FVector2D(-R * 1.2f, R * 0.8f), C + FVector2D(-R * 0.3f, -R * 0.4f), C + FVector2D(R * 0.4f, R * 0.4f), C + FVector2D(R * 1.2f, -R * 0.8f) };
			}
			else
			{
				P = { C };
			}
			AddMarker(Tris, Type, P, R, W, MarkerColor(Type));
		}
		else if (IconTool == ETacticalTool::View)
		{
			// A camera frame.
			AddOutline(Tris, { C + FVector2D(-R, -R * 0.7f), C + FVector2D(R, -R * 0.7f), C + FVector2D(R, R * 0.7f), C + FVector2D(-R, R * 0.7f) }, W, FLinearColor(0.9f, 0.9f, 0.9f));
		}
		else
		{
			// Erase: an X.
			AddSegment(Tris, C + FVector2D(-R * 0.8f, -R * 0.8f), C + FVector2D(R * 0.8f, R * 0.8f), W * 1.3f, FLinearColor(0.9f, 0.5f, 0.45f));
			AddSegment(Tris, C + FVector2D(-R * 0.8f, R * 0.8f), C + FVector2D(R * 0.8f, -R * 0.8f), W * 1.3f, FLinearColor(0.9f, 0.5f, 0.45f));
		}
		if (Tris.Num() > 0)
		{
			FCanvasTriangleItem Item(Tris, Canvas->DefaultTexture->GetResource());
			Item.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Item);
		}
		TextX = Button.Box.Min.X + IconBox;
	}

	float TW = 0.f, TH = 0.f;
	GetTextSize(Label, TW, TH, Font, 0.95f * S);
	DrawText(Label, Button.bEnabled ? Text : Dim, bCentre ? Button.Box.Min.X + (Size.X - TW) * 0.5f : TextX,
		Button.Box.Min.Y + (Size.Y - TH) * 0.5f, Font, 0.95f * S);
	TacButtons.Add(Button);
}

void ASquadHUD::DrawTacticalPanel(ASquadPlayerController& PC, const FBox2D& SideBox, float S, int32 HoverMarkerId)
{
	using namespace TacMap;

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const ASquadPlayerState* PS = PC.GetSquadPlayerState();
	const ATacticalPlan* Plan = PC.GetTacticalPlan();
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float Now = GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : 0.f;
	const ETacticalRole MyRole = PS ? PS->TacticalRole : ETacticalRole::Soldier;
	const bool bCanPlan = PS && PS->CanPlan();
	const bool bCommand = PS && PS->HasCommandRole();

	float MX = 0.f, MY = 0.f;
	PC.GetMousePosition(MX, MY);
	const FVector2D Mouse(MX, MY);

	DrawRect(PanelBg, SideBox.Min.X, SideBox.Min.Y, SideBox.GetSize().X, SideBox.GetSize().Y);
	const float Pad = PanelPad * S;
	const float X = SideBox.Min.X + Pad, Right = SideBox.Max.X - Pad, InnerW = Right - X;
	const float RowH = RowHeight * S;
	const float FooterTop = SideBox.Max.Y - Pad - 2.f * RowH;
	float Y = SideBox.Min.Y + Pad;

	auto Line = [&](const FString& Str, const FLinearColor& Color, float Scale = 0.95f, float Indent = 0.f)
	{
		DrawText(Str, Color, X + Indent, Y, Font, Scale * S);
		Y += RowH;
	};
	auto Section = [&](const FString& Title)
	{
		Y += 6.f * S;
		DrawText(Title, Header, X, Y, Font, 1.0f * S);
		Y += RowH;
		DrawRect(FLinearColor(1.f, 0.85f, 0.4f, 0.25f), X, Y - 4.f * S, InnerW, 1.f);
		Y += 2.f * S;
	};
	auto MakeButton = [&](float BX, float BY, float BW, float BH, ETacMapAction Action)
	{
		FTacMapButton B;
		B.Box = FBox2D(FVector2D(BX, BY), FVector2D(BX + BW, BY + BH));
		B.Action = Action;
		return B;
	};

	// ---- Title, close button, the player's role.
	DrawText(TEXT("TACTICAL MAP"), Header, X, Y, Font, 1.35f * S);
	{
		const float B = 24.f * S;
		DrawTacButton(MakeButton(Right - B, Y, B, B, ETacMapAction::Close), TEXT("X"), Mouse, S, false);
	}
	Y += 32.f * S;
	Line(FString::Printf(TEXT("You: %s"), TacticalPlan::RoleName(MyRole)), RoleColor(MyRole));

	// ---- Plan tools.
	Section(TEXT("PLAN"));
	if (bCanPlan)
	{
		static const ETacticalTool Tools[] = {
			ETacticalTool::View, ETacticalTool::Push, ETacticalTool::HoldLine,
			ETacticalTool::Route, ETacticalTool::Defend, ETacticalTool::Attack,
			ETacticalTool::Rally, ETacticalTool::Enemy, ETacticalTool::Erase };
		constexpr int32 Cols = 3;
		const float BG = 4.f * S;
		const float BW = (InnerW - BG * (Cols - 1)) / Cols, BH = ToolButtonHeight * S;
		for (int32 I = 0; I < UE_ARRAY_COUNT(Tools); ++I)
		{
			const int32 Col = I % Cols, Row = I / Cols;
			FTacMapButton B = MakeButton(X + Col * (BW + BG), Y + Row * (BH + BG), BW, BH, ETacMapAction::SelectTool);
			B.Tool = Tools[I];
			DrawTacButton(B, ToolLabel(Tools[I]), Mouse, S, PC.GetTacticalTool() == Tools[I], Tools[I]);
		}
		Y += 3 * (BH + BG) + 2.f * S;
		Line(ToolHint(PC.GetTacticalTool()), Dim, 0.9f);
		if (bCommand)
		{
			FTacMapButton B = MakeButton(X, Y, InnerW, 24.f * S, ETacMapAction::ClearPlan);
			B.bEnabled = Plan && Plan->GetMarkers().Num() > 0;
			DrawTacButton(B, TEXT("Clear whole plan"), Mouse, S, false);
			Y += 28.f * S;
		}
	}
	else
	{
		Line(TEXT("View only. Your CO or XO can"), Dim, 0.9f);
		Line(TEXT("give you planning rights."), Dim, 0.9f);
	}
	Line(FString::Printf(TEXT("%d mark%s on the plan"), Plan ? Plan->GetMarkers().Num() : 0, Plan && Plan->GetMarkers().Num() == 1 ? TEXT("") : TEXT("s")), Dim, 0.85f);

	// ---- Team: roles, and CO / XO give or take back planning rights.
	Section(TEXT("TEAM"));
	TArray<const ASquadPlayerState*> Team;
	if (GS)
	{
		for (const APlayerState* Other : GS->PlayerArray)
		{
			const ASquadPlayerState* SPS = Cast<ASquadPlayerState>(Other);
			if (SPS && SPS->Team == PC.PlayerTeam)
			{
				Team.Add(SPS);
			}
		}
	}
	Team.Sort([](const ASquadPlayerState& A, const ASquadPlayerState& B)
	{
		return A.TacticalRole != B.TacticalRole ? A.TacticalRole > B.TacticalRole : A.Elo > B.Elo;
	});
	const float TagW = 36.f * S, DelegateW = 70.f * S;
	const float RequestsReserve = 5.f * RowH; // Keep room for the requests section below.
	int32 Shown = 0;
	for (const ASquadPlayerState* Member : Team)
	{
		if (Y + RowH > FooterTop - RequestsReserve && Shown < Team.Num() - 1)
		{
			Line(FString::Printf(TEXT("+%d more"), Team.Num() - Shown), Dim, 0.85f);
			break;
		}
		++Shown;
		const FLinearColor RC = RoleColor(Member->TacticalRole);
		const TCHAR* Tag = TacticalPlan::RoleTag(Member->TacticalRole);
		if (*Tag)
		{
			DrawRect(WithAlpha(RC, 0.25f), X, Y + 1.f * S, TagW - 4.f * S, RowH - 3.f * S);
			float TW = 0.f, TH = 0.f;
			GetTextSize(Tag, TW, TH, Font, 0.8f * S);
			DrawText(Tag, RC, X + (TagW - 4.f * S - TW) * 0.5f, Y + (RowH - TH) * 0.5f - 1.f * S, Font, 0.8f * S);
		}
		const FString Name = FString::Printf(TEXT("%s %s%s"), *Member->GetRankAbbreviation(), *Member->GetPlayerName(), Member == PS ? TEXT(" (you)") : TEXT(""));
		DrawText(Name, Member == PS ? Text : FLinearColor(0.8f, 0.8f, 0.8f), X + TagW, Y + 2.f * S, Font, 0.9f * S);
		if (bCommand && Member != PS && !Member->HasCommandRole())
		{
			FTacMapButton B = MakeButton(Right - DelegateW, Y + 1.f * S, DelegateW, RowH - 3.f * S, ETacMapAction::ToggleDelegate);
			B.Player = const_cast<ASquadPlayerState*>(Member);
			DrawTacButton(B, Member->bPlanningDelegated ? TEXT("Revoke") : TEXT("Delegate"), Mouse, S, false);
		}
		Y += RowH;
	}
	if (Team.Num() == 1)
	{
		Line(TEXT("No team-mates in this match."), Dim, 0.85f);
	}

	// ---- Order requests: CO / XO answer them, everyone else asks.
	Section(TEXT("ORDER REQUESTS"));
	if (bCommand)
	{
		int32 Listed = 0;
		if (Plan)
		{
			for (const FTacticalOrderRequest& Req : Plan->GetRequests())
			{
				if (Y + RowH * 2.f > FooterTop)
				{
					break;
				}
				++Listed;
				const int32 Age = FMath::Max(0, FMath::FloorToInt(Now - Req.RequestTime));
				DrawText(FString::Printf(TEXT("%s  %ds"), *Req.RequesterLabel, Age), Req.bAcknowledged ? Dim : Request, X, Y + 2.f * S, Font, 0.9f * S);
				Y += RowH;
				const float BW = 60.f * S, BH = RowH - 3.f * S;
				FTacMapButton Go = MakeButton(X + 12.f * S, Y, BW, BH, ETacMapAction::FocusRequest);
				Go.Location = FVector2D(Req.Location);
				DrawTacButton(Go, TEXT("Go to"), Mouse, S, false);
				if (Req.bAcknowledged)
				{
					DrawText(FString::Printf(TEXT("Ack: %s"), *Req.AcknowledgedBy), Acknowledged, X + 20.f * S + BW, Y + 2.f * S, Font, 0.85f * S);
				}
				else
				{
					FTacMapButton Ack = MakeButton(X + 18.f * S + BW, Y, BW + 20.f * S, BH, ETacMapAction::Acknowledge);
					Ack.Param = Req.Id;
					DrawTacButton(Ack, TEXT("Acknowledge"), Mouse, S, false);
				}
				Y += RowH + 2.f * S;
			}
		}
		if (Listed == 0)
		{
			Line(TEXT("No requests."), Dim, 0.9f);
		}
	}
	else
	{
		const float Cooldown = Plan && PS ? Plan->GetRequestCooldownLeft(PS) : 0.f;
		FTacMapButton B = MakeButton(X, Y, InnerW, 30.f * S, ETacMapAction::RequestOrders);
		B.bEnabled = Plan && PS && Cooldown <= 0.f;
		DrawTacButton(B, Cooldown > 0.f ? FString::Printf(TEXT("Request orders (%ds)"), FMath::CeilToInt(Cooldown)) : FString(TEXT("Request orders")), Mouse, S, false);
		Y += 36.f * S;
		const FTacticalOrderRequest* Mine = nullptr;
		if (Plan && PS)
		{
			for (const FTacticalOrderRequest& Req : Plan->GetRequests())
			{
				if (Req.Requester == PS)
				{
					Mine = &Req;
				}
			}
		}
		if (Mine && Mine->bAcknowledged)
		{
			Line(FString::Printf(TEXT("Acknowledged by %s"), *Mine->AcknowledgedBy), Acknowledged, 0.9f);
		}
		else if (Mine)
		{
			Line(FString::Printf(TEXT("Waiting for the CO / XO (%ds)"), FMath::Max(0, FMath::FloorToInt(Now - Mine->RequestTime))), Request, 0.9f);
		}
		else
		{
			Line(TEXT("Lets your CO and XO know you"), Dim, 0.9f);
			Line(TEXT("need orders, marked at your squad."), Dim, 0.9f);
		}
	}

	// ---- Footer.
	Y = FooterTop;
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.1f), X, Y - 4.f * S, InnerW, 1.f);
	Line(TEXT("M / Esc   close the map"), Dim, 0.85f);
	Line(bCanPlan ? TEXT("Right click   erase a mark / cancel") : TEXT("Click the map   move the camera"), Dim, 0.85f);

	const FVector2D Sz = SideBox.GetSize();
	DrawRect(Border, SideBox.Min.X, SideBox.Min.Y, Sz.X, 1.f);
	DrawRect(Border, SideBox.Min.X, SideBox.Max.Y - 1.f, Sz.X, 1.f);
	DrawRect(Border, SideBox.Min.X, SideBox.Min.Y, 1.f, Sz.Y);
	DrawRect(Border, SideBox.Max.X - 1.f, SideBox.Min.Y, 1.f, Sz.Y);
}

// ------------------------------------------------------------------------------------------------ Notices

void ASquadHUD::UpdatePlanNotices(ASquadPlayerController& PC)
{
	using namespace TacMap;

	const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	Notices.RemoveAll([Time](const FNotice& N) { return Time - N.Time > NoticeLifetime; });

	const ASquadPlayerState* PS = PC.GetSquadPlayerState();
	const ATacticalPlan* Plan = PC.GetTacticalPlan();
	if (!PS || !Plan)
	{
		return;
	}
	auto Add = [&](const FString& Str, const FLinearColor& Color)
	{
		Notices.Add({ Str, Color, Time });
		if (Notices.Num() > MaxNotices)
		{
			Notices.RemoveAt(0);
		}
	};

	TSet<int32> MarkerIds, RequestIds, AckIds;
	for (const FTacticalMarker& Mk : Plan->GetMarkers())
	{
		MarkerIds.Add(Mk.Id);
	}
	for (const FTacticalOrderRequest& Req : Plan->GetRequests())
	{
		RequestIds.Add(Req.Id);
		if (Req.bAcknowledged)
		{
			AckIds.Add(Req.Id);
		}
	}
	if (!bNoticesPrimed)
	{
		// First look: what's already there isn't news.
		bNoticesPrimed = true;
		SeenMarkerIds = MoveTemp(MarkerIds);
		SeenRequestIds = MoveTemp(RequestIds);
		SeenAcknowledgedIds = MoveTemp(AckIds);
		SeenRole = PS->TacticalRole;
		if (PS->HasCommandRole())
		{
			Add(FString::Printf(TEXT("You are the %s - press M to plan"), TacticalPlan::RoleName(PS->TacticalRole)), RoleColor(PS->TacticalRole));
		}
		return;
	}

	// Role changes.
	if (PS->TacticalRole != SeenRole)
	{
		const ETacticalRole NewRole = PS->TacticalRole;
		if (TacticalPlan::IsCommandRole(NewRole))
		{
			Add(FString::Printf(TEXT("You are now the %s - press M to plan"), TacticalPlan::RoleName(NewRole)), RoleColor(NewRole));
		}
		else if (NewRole == ETacticalRole::Planner)
		{
			Add(TEXT("You have been given planning rights - press M"), RoleColor(NewRole));
		}
		else if (SeenRole == ETacticalRole::Planner)
		{
			Add(TEXT("Your planning rights were taken back"), Dim);
		}
		SeenRole = NewRole;
	}

	// New marks by someone else: one notice per author this frame.
	TMap<FString, int32> NewByAuthor;
	FString LastNewType;
	for (const FTacticalMarker& Mk : Plan->GetMarkers())
	{
		if (!SeenMarkerIds.Contains(Mk.Id) && Mk.Author != PS)
		{
			const TCHAR* RoleStr = TacticalPlan::RoleTag(Mk.AuthorRole);
			NewByAuthor.FindOrAdd(FString::Printf(TEXT("%s%s%s"), RoleStr, *RoleStr ? TEXT(" ") : TEXT(""), *Mk.AuthorName))++;
			LastNewType = TacticalPlan::MarkerName(Mk.Type);
		}
	}
	for (const TPair<FString, int32>& Pair : NewByAuthor)
	{
		Add(Pair.Value == 1 ? FString::Printf(TEXT("New orders from %s: %s  (M)"), *Pair.Key, *LastNewType)
			: FString::Printf(TEXT("%d new orders from %s  (M)"), Pair.Value, *Pair.Key), MarkerColor(ETacticalMarkerType::Push));
	}

	// Requests: CO / XO hear about new ones; the requester hears when theirs is acknowledged.
	for (const FTacticalOrderRequest& Req : Plan->GetRequests())
	{
		if (PS->HasCommandRole() && !Req.bAcknowledged && !SeenRequestIds.Contains(Req.Id) && Req.Requester != PS)
		{
			Add(FString::Printf(TEXT("%s requests orders  (M)"), *Req.RequesterLabel), Request);
		}
		if (Req.Requester == PS && Req.bAcknowledged && !SeenAcknowledgedIds.Contains(Req.Id))
		{
			Add(FString::Printf(TEXT("%s acknowledged your request for orders"), *Req.AcknowledgedBy), Acknowledged);
		}
	}

	SeenMarkerIds = MoveTemp(MarkerIds);
	SeenRequestIds = MoveTemp(RequestIds);
	SeenAcknowledgedIds = MoveTemp(AckIds);
}

void ASquadHUD::DrawPlanNotices()
{
	using namespace TacMap;

	if (Notices.Num() == 0)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);
	const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	float Y = 58.f * S; // Below the VP hold timer.
	for (int32 I = Notices.Num() - 1; I >= 0; --I)
	{
		const FNotice& N = Notices[I];
		const float Age = Time - N.Time;
		const float Alpha = FMath::Clamp((NoticeLifetime - Age) / 1.f, 0.f, 1.f) * FMath::Clamp(Age / 0.15f, 0.f, 1.f);
		float TW = 0.f, TH = 0.f;
		GetTextSize(N.Text, TW, TH, Font, 1.05f * S);
		const float Pad = 8.f * S;
		const float X = (Canvas->ClipX - TW) * 0.5f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.72f * Alpha), X - Pad, Y, TW + 2.f * Pad, TH + Pad);
		DrawRect(WithAlpha(N.Color, Alpha), X - Pad, Y, 3.f * S, TH + Pad);
		DrawText(N.Text, WithAlpha(N.Color, Alpha), X, Y + Pad * 0.5f, Font, 1.05f * S);
		Y += TH + Pad + 4.f * S;
	}
}

// ------------------------------------------------------------------------------------------------ Battlefield plan & command wheel (GDD §13.2)

bool ASquadHUD::ProjectGround(const ASquadPlayerController& PC, const FVector2D& World, FVector2D& OutScreen) const
{
	const AGridManager* Grid = AGridManager::Find(this);
	const float Z = (Grid ? Grid->GetActorLocation().Z : 0.f) + 2.f; // Just above the ground.
	return PC.ProjectWorldLocationToScreen(FVector(World, Z), OutScreen, false);
}

int32 ASquadHUD::GetRadialItemAt(const FVector2D& Center, const FVector2D& Mouse, int32 NumItems) const
{
	const FVector2D D = Mouse - Center;
	if (NumItems <= 0 || D.Size() < TacMap::RadialDeadzone * LastScale)
	{
		return INDEX_NONE;
	}
	// Slice 0 is straight up; the rest run clockwise (screen Y points down).
	const float Step = 360.f / NumItems;
	float Angle = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) + 90.f;
	Angle = FMath::Fmod(Angle + 360.f + Step * 0.5f, 360.f);
	return FMath::Clamp(FMath::FloorToInt(Angle / Step), 0, NumItems - 1);
}

namespace TacMap
{
	/** World points of a plan mark: the route, a line's two ends, or a point mark's cell. */
	TArray<FVector2D> MarkerWorldPoints(const FTacticalMarker& M)
	{
		TArray<FVector2D> Out;
		if (M.Type == ETacticalMarkerType::Route)
		{
			for (const FVector2f& Q : M.Points)
			{
				Out.Add(FVector2D(Q));
			}
		}
		else
		{
			Out.Add(FVector2D(M.Start));
			if (M.IsLine())
			{
				Out.Add(FVector2D(M.End));
			}
		}
		return Out;
	}
}

int32 ASquadHUD::FindWorldMarkerAt(const FVector2D& ScreenPosition) const
{
	const ASquadPlayerController* PC = Cast<ASquadPlayerController>(GetOwningPlayerController());
	const ATacticalPlan* Plan = PC ? PC->GetTacticalPlan() : nullptr;
	if (!Plan)
	{
		return INDEX_NONE;
	}
	const float R = TacMap::WorldIconRadius * LastScale;
	int32 BestId = INDEX_NONE;
	float BestDist = FMath::Max(8.f, TacMap::WorldLineWidth * LastScale * 1.5f);
	const TArray<FTacticalMarker>& Markers = Plan->GetMarkers();
	for (int32 I = Markers.Num() - 1; I >= 0; --I)
	{
		const FTacticalMarker& M = Markers[I];
		TArray<FVector2D> P;
		bool bOk = true;
		for (const FVector2D& Q : TacMap::MarkerWorldPoints(M))
		{
			FVector2D Screen;
			bOk = bOk && ProjectGround(*PC, Q, Screen);
			P.Add(Screen);
		}
		if (!bOk || P.Num() == 0)
		{
			continue;
		}
		if (M.IsPoint())
		{
			P[0].Y -= R; // Point marks stand just above their cell.
		}
		const float Dist = TacMap::DistanceToMarker(P, M, ScreenPosition, R);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestId = M.Id;
		}
	}
	return BestId;
}

void ASquadHUD::DrawWorldPlan(ASquadPlayerController& PC)
{
	using namespace TacMap;

	const ATacticalPlan* Plan = PC.GetTacticalPlan();
	if (!Canvas->DefaultTexture)
	{
		return;
	}
	const float S = LastScale;
	const float R = WorldIconRadius * S, W = WorldLineWidth * S;
	const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float ServerNow = GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : 0.f;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const ASquadPlayerState* PS = PC.GetSquadPlayerState();
	const bool bErasing = PC.GetCommandMode() == ASquadPlayerController::ECommandMode::Erasing;

	float MX = 0.f, MY = 0.f;
	const bool bHaveMouse = PC.GetMousePosition(MX, MY);
	const int32 HoverId = bErasing && bHaveMouse ? FindWorldMarkerAt(FVector2D(MX, MY)) : INDEX_NONE;

	auto ProjectAll = [&](const TArray<FVector2D>& World, TArray<FVector2D>& Out)
	{
		Out.Reset();
		for (const FVector2D& Q : World)
		{
			FVector2D P;
			if (!ProjectGround(PC, Q, P))
			{
				return false;
			}
			Out.Add(P);
		}
		return Out.Num() > 0;
	};

	TArray<FCanvasUVTri> Tris;
	struct FLabel { FString Text; FVector2D At; FLinearColor Color; };
	TArray<FLabel> Labels;

	// ---- Plan marks, on the ground where they were drawn. Point marks stand just above their cell.
	if (Plan)
	{
		for (const FTacticalMarker& M : Plan->GetMarkers())
		{
			TArray<FVector2D> P;
			if (!ProjectAll(MarkerWorldPoints(M), P))
			{
				continue;
			}
			const bool bHi = M.Id == HoverId;
			const FLinearColor Color = bHi ? FLinearColor(1.f, 0.35f, 0.3f, 1.f) : WithAlpha(MarkerColor(M.Type), WorldMarkAlpha); // Red: about to be erased.
			if (M.IsPoint())
			{
				AddDisc(Tris, P[0], 2.5f * S, WithAlpha(Color, 0.9f), 8);
				P[0].Y -= R;
			}
			AddMarker(Tris, M.Type, P, bHi ? R * 1.2f : R, bHi ? W * 1.2f : W, Color);
		}

		// ---- Open order requests, for the CO / XO who answer them.
		if (PS && PS->HasCommandRole())
		{
			for (const FTacticalOrderRequest& Req : Plan->GetRequests())
			{
				FVector2D P;
				if (!Req.bAcknowledged && ProjectGround(PC, FVector2D(Req.Location), P))
				{
					const FLinearColor ReqColor = WithAlpha(Request, WorldRequestAlpha);
					AddPingPulse(Tris, P, R * 1.4f, Time * 0.8f, ReqColor);
					AddRing(Tris, P, R * 0.8f, R * 0.8f + 2.f * S, ReqColor, 18);
					Labels.Add({ FString::Printf(TEXT("%s needs orders"), *Req.RequesterLabel), P + FVector2D(R * 1.3f, -R * 0.5f), ReqColor });
				}
			}
		}
	}

	// ---- The line being placed from the command wheel.
	if (PC.GetCommandMode() == ASquadPlayerController::ECommandMode::Drawing)
	{
		const ASquadPlayerController::FPlanDraft& Draft = PC.GetCommandDraft();
		TArray<FVector2D> P;
		if (ProjectAll(DraftWorldPoints(Draft, AGridManager::Find(this)), P))
		{
			AddMarker(Tris, Draft.Type, P, R, W, WithAlpha(MarkerColor(Draft.Type), WorldDraftAlpha));
		}
	}

	// ---- Pings: a pulse on the ground and the icon above it; an arrow at the screen edge if it is off screen.
	if (Plan)
	{
		const FBox2D Inset(FVector2D(EdgeInset * S, EdgeInset * S), FVector2D(Canvas->ClipX - EdgeInset * S, Canvas->ClipY - EdgeInset * S));
		const FVector2D Centre(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
		for (const FTacticalPing& Ping : Plan->GetPings())
		{
			const float Fade = FMath::Clamp(PingTimeLeft(Ping, ServerNow), 0.f, 1.f);
			if (Fade <= 0.f)
			{
				continue;
			}
			const FLinearColor Color = WithAlpha(PingColor(Ping.Type), Fade * WorldPingAlpha);
			FVector2D P;
			if (!ProjectGround(PC, FVector2D(Ping.Location), P))
			{
				continue;
			}
			if (Inset.IsInside(P))
			{
				AddPingPulse(Tris, P, R * 1.6f, Time, Color);
				AddDisc(Tris, P, 2.5f * S, Color, 8);
				const FVector2D Icon = P - FVector2D(0.f, R * 2.2f);
				AddSegment(Tris, P, Icon + FVector2D(0.f, R * 0.6f), FMath::Max(1.f, S), WithAlpha(Color, 0.6f * Fade), false);
				AddPingIcon(Tris, Ping.Type, Icon, R, Color);
				const FString PingText = Ping.Type == ETacticalPingType::Spot ? FString::Printf(TEXT("ENEMY - %s"), *Ping.AuthorLabel) : Ping.AuthorLabel;
				Labels.Add({ PingText, Icon + FVector2D(R * 1.4f, -R * 0.6f), Color });
			}
			else
			{
				// Off screen: an arrow on the inset edge pointing at it, with the icon just inside.
				FVector2D Dir = P - Centre;
				if (!Dir.Normalize())
				{
					continue;
				}
				const float TX = !FMath::IsNearlyZero(Dir.X) ? ((Dir.X > 0.f ? Inset.Max.X : Inset.Min.X) - Centre.X) / Dir.X : FLT_MAX;
				const float TY = !FMath::IsNearlyZero(Dir.Y) ? ((Dir.Y > 0.f ? Inset.Max.Y : Inset.Min.Y) - Centre.Y) / Dir.Y : FLT_MAX;
				const FVector2D Edge = Centre + Dir * FMath::Min(TX, TY);
				AddArrowHead(Tris, Edge + Dir * R * 0.9f, Dir, R * 1.2f, R * 0.8f, Color);
				AddPingIcon(Tris, Ping.Type, Edge - Dir * R * 0.9f, R * 0.8f, Color);
			}
		}
	}

	if (Tris.Num() > 0)
	{
		FCanvasTriangleItem Item(Tris, Canvas->DefaultTexture->GetResource());
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
	for (const FLabel& L : Labels)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(L.Text, TW, TH, Font, 0.85f * S);
		const FLinearColor TextColor = WithAlpha(L.Color, FMath::Min(1.f, L.Color.A / WorldPingAlpha) * WorldLabelAlpha);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * TextColor.A), L.At.X - 3.f * S, L.At.Y - 1.f * S, TW + 6.f * S, TH + 2.f * S);
		DrawText(L.Text, TextColor, L.At.X, L.At.Y, Font, 0.85f * S);
	}
}

void ASquadHUD::DrawCommandUI(ASquadPlayerController& PC)
{
	using namespace TacMap;

	const ASquadPlayerController::ECommandMode Mode = PC.GetCommandMode();
	if (Mode == ASquadPlayerController::ECommandMode::None || !Canvas->DefaultTexture)
	{
		return;
	}
	const float S = LastScale;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;

	// ---- Hint while placing a line or erasing.
	if (Mode != ASquadPlayerController::ECommandMode::Radial)
	{
		FString Hint;
		FLinearColor Color(1.f, 0.4f, 0.35f);
		if (Mode == ASquadPlayerController::ECommandMode::Erasing)
		{
			Hint = TEXT("ERASE  -  left click a mark to remove it    right click / Esc: done");
		}
		else
		{
			const ETacticalMarkerType Type = PC.GetCommandDraft().Type;
			Color = MarkerColor(Type);
			Hint = Type == ETacticalMarkerType::Route
				? FString(TEXT("ROUTE  -  trace the route with the cursor, left click to finish    right click / Esc: cancel"))
				: FString::Printf(TEXT("%s  -  move to the end point, left click to place    right click / Esc: cancel"), Type == ETacticalMarkerType::Push ? TEXT("PUSH") : TEXT("HOLD LINE"));
		}
		float TW = 0.f, TH = 0.f;
		GetTextSize(Hint, TW, TH, Font, 1.f * S);
		const float Bottom = PC.IsTacticalMapOpen() ? Canvas->ClipY - 8.f * S : (bSquadBarVisible ? SquadBarBox.Min.Y : Canvas->ClipY) - 12.f * S;
		const float X = (Canvas->ClipX - TW) * 0.5f, Y = Bottom - TH - 8.f * S;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.75f), X - 10.f * S, Y, TW + 20.f * S, TH + 8.f * S);
		DrawRect(Color, X - 10.f * S, Y, 3.f * S, TH + 8.f * S);
		DrawText(Hint, FLinearColor(0.92f, 0.92f, 0.92f), X, Y + 4.f * S, Font, 1.f * S);
		return;
	}

	// ---- The wheel.
	const ASquadPlayerController::FRadialMenu& Menu = PC.GetRadialMenu();
	if (Menu.bBuildMenu)
	{
		DrawBuildWheel(PC); // GDD §13.3 sub-wheel, same look as this one.
		return;
	}
	const int32 N = Menu.Items.Num();
	if (N == 0)
	{
		return;
	}
	const FVector2D C = Menu.ScreenCenter;
	const float Inner = RadialInner * S, Outer = RadialOuter * S;
	const float Step = 2.f * PI / N;
	TArray<FCanvasUVTri> Tris;
	for (int32 I = 0; I < N; ++I)
	{
		const bool bHover = I == Menu.Hover;
		const float Mid = -0.5f * PI + I * Step;
		const float A0 = Mid - Step * 0.5f + 0.02f, A1 = Mid + Step * 0.5f - 0.02f; // Small gaps between slices.
		const FLinearColor Fill = bHover ? FLinearColor(0.42f, 0.5f, 0.26f, 0.92f) : FLinearColor(0.04f, 0.05f, 0.04f, 0.82f);
		const float O = bHover ? Outer * 1.06f : Outer;
		constexpr int32 Sub = 8;
		for (int32 K = 0; K < Sub; ++K)
		{
			const float B0 = FMath::Lerp(A0, A1, static_cast<float>(K) / Sub), B1 = FMath::Lerp(A0, A1, static_cast<float>(K + 1) / Sub);
			const FVector2D D0(FMath::Cos(B0), FMath::Sin(B0)), D1(FMath::Cos(B1), FMath::Sin(B1));
			AddTri(Tris, C + D0 * Inner, C + D0 * O, C + D1 * O, Fill);
			AddTri(Tris, C + D0 * Inner, C + D1 * O, C + D1 * Inner, Fill);
		}
		// Icon in the middle of the slice.
		const FVector2D IconC = C + FVector2D(FMath::Cos(Mid), FMath::Sin(Mid)) * (Inner + Outer) * 0.5f - FVector2D(0.f, 6.f * S);
		const float IR = 10.f * S, IW = 3.f * S;
		const ERadialAction Action = Menu.Items[I];
		ETacticalMarkerType Type;
		if (Action == ERadialAction::Spot || Action == ERadialAction::Ping)
		{
			const ETacticalPingType PingType = Action == ERadialAction::Spot ? ETacticalPingType::Spot : ETacticalPingType::Ping;
			AddPingIcon(Tris, PingType, IconC, IR, PingColor(PingType));
		}
		else if (TacticalPlan::RadialMarker(Action, Type))
		{
			AddMarkerIcon(Tris, Type, IconC, IR * 0.85f, IW, MarkerColor(Type));
		}
		else if (Action == ERadialAction::Erase)
		{
			AddCross(Tris, IconC, IR * 0.8f, IW, FLinearColor(0.95f, 0.5f, 0.45f));
		}
		else if (Action == ERadialAction::Build)
		{
			// A brick: three courses of a wall (GDD §13.3).
			const FLinearColor Brick(0.85f, 0.55f, 0.3f);
			for (int32 Row = 0; Row < 3; ++Row)
			{
				const float Y = IconC.Y + (Row - 1) * IR * 0.62f;
				const float Shift = (Row % 2) * IR * 0.35f;
				for (int32 K = 0; K < 2; ++K)
				{
					const float X0 = IconC.X - IR + Shift + K * IR * 0.95f;
					const float X1 = FMath::Min(X0 + IR * 0.85f, IconC.X + IR + Shift * 0.f);
					AddTri(Tris, FVector2D(X0, Y - IR * 0.25f), FVector2D(X1, Y - IR * 0.25f), FVector2D(X1, Y + IR * 0.25f), Brick);
					AddTri(Tris, FVector2D(X0, Y - IR * 0.25f), FVector2D(X1, Y + IR * 0.25f), FVector2D(X0, Y + IR * 0.25f), Brick);
				}
			}
		}
		else // Request orders: an amber ring with "?" in it (text below).
		{
			AddRing(Tris, IconC, IR * 0.75f, IR * 0.75f + IW * 0.8f, Request, 18);
		}
	}
	// Centre: dead zone (a quick tap there is a Ping).
	AddDisc(Tris, C, RadialDeadzone * S, FLinearColor(0.f, 0.f, 0.f, 0.55f), 20);
	AddRing(Tris, C, RadialDeadzone * S - 1.5f * S, RadialDeadzone * S, FLinearColor(1.f, 1.f, 1.f, 0.3f), 20);
	{
		FCanvasTriangleItem Item(Tris, Canvas->DefaultTexture->GetResource());
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	// Labels under each icon, the request "?" glyph, and the chosen slice below the wheel.
	for (int32 I = 0; I < N; ++I)
	{
		const float Mid = -0.5f * PI + I * Step;
		const FVector2D Slice = C + FVector2D(FMath::Cos(Mid), FMath::Sin(Mid)) * (Inner + Outer) * 0.5f;
		FString Label = TacticalPlan::RadialName(Menu.Items[I]);
		if (Menu.Items[I] == ERadialAction::RequestOrders)
		{
			Label = TEXT("Request");
			float QW = 0.f, QH = 0.f;
			GetTextSize(TEXT("?"), QW, QH, Font, 1.f * S);
			DrawText(TEXT("?"), Request, Slice.X - QW * 0.5f, Slice.Y - 6.f * S - QH * 0.5f, Font, 1.f * S);
		}
		else if (Menu.Items[I] == ERadialAction::Spot)
		{
			Label = TEXT("Spot");
		}
		float TW = 0.f, TH = 0.f;
		GetTextSize(Label, TW, TH, Font, 0.85f * S);
		DrawText(Label, I == Menu.Hover ? FLinearColor::White : FLinearColor(0.8f, 0.8f, 0.8f), Slice.X - TW * 0.5f, Slice.Y + 8.f * S, Font, 0.85f * S);
	}
	const FString Chosen = Menu.Items.IsValidIndex(Menu.Hover) ? FString(TacticalPlan::RadialName(Menu.Items[Menu.Hover])) : FString(TEXT("tap: Ping"));
	float TW = 0.f, TH = 0.f;
	GetTextSize(Chosen, TW, TH, Font, 0.9f * S);
	const float CY = C.Y + Outer + 10.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), C.X - TW * 0.5f - 6.f * S, CY, TW + 12.f * S, TH + 4.f * S);
	DrawText(Chosen, FLinearColor::White, C.X - TW * 0.5f, CY + 2.f * S, Font, 0.9f * S);
}
