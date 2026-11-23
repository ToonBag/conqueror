// Minimap part of ASquadHUD (GDD §13): bottom left of the screen, drawn on the HUD canvas. The map drawing itself
// (DrawMapBase) is shared with the tactical map (GDD §13.1, SquadHUDTacticalMap.cpp).

#include "SquadHUD.h"

#include "CanvasItem.h"
#include "CoverData.h"
#include "DeploymentZone.h"
#include "EngineUtils.h"
#include "GridManager.h"
#include "MapZoneColors.h"
#include "Soldier.h"
#include "SquadHUDDraw.h"
#include "SquadPlayerController.h"
#include "SquadPlayerState.h"
#include "TacticalPlan.h"
#include "VictoryPoint.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

using namespace SquadHUDDraw;

namespace Minimap
{
	// Sizes at 1080p; scaled with the screen height like the rest of the HUD.
	/** Longest edge of the map area. Shrinks if it would run into the squad bar. */
	constexpr float MaxSize = 300.f;
	constexpr float MinSize = 150.f;
	constexpr float Margin = 12.f;
	constexpr float Pad = 6.f;
	constexpr float ExpandSize = 16.f;
	/** Faint reference lines every this many cells. */
	constexpr int32 GridLineStep = 10;

	constexpr float ConeLength = 24.f;
	constexpr float ConeHalfAngle = 32.f;
	constexpr float OwnRadius = 4.f;
	constexpr float AllyRadius = 3.f;
	constexpr float EnemyRadius = 3.5f;

	const FLinearColor FrameBg(0.02f, 0.025f, 0.02f, 0.7f);
	const FLinearColor MapBg(0.01f, 0.012f, 0.01f, 0.82f);
	const FLinearColor Border(1.f, 1.f, 1.f, 0.14f);
	const FLinearColor GridLine(1.f, 1.f, 1.f, 0.05f);
	const FLinearColor CameraView(1.f, 1.f, 1.f, 0.35f);
	const FLinearColor Outline(0.f, 0.f, 0.f, 0.7f);
	const FLinearColor Gold(1.f, 0.85f, 0.4f, 1.f);
	const FLinearColor Dead(0.55f, 0.55f, 0.55f, 0.7f);
	const FLinearColor Enemy(1.f, 0.25f, 0.2f, 1.f);
	const FLinearColor Request(1.f, 0.7f, 0.1f, 1.f);

	// Cover, by type: light shades on the dark map, like a sketch plan.
	const FLinearColor Hay(0.85f, 0.72f, 0.35f, 0.9f);
	const FLinearColor LowWall(0.72f, 0.72f, 0.68f, 0.9f);
	const FLinearColor Wall(0.95f, 0.95f, 0.92f, 1.f);
	const FLinearColor Window(0.55f, 0.78f, 1.f, 1.f);
	const FLinearColor Tree(0.3f, 0.6f, 0.28f, 0.9f);
	/** Rubble of a breached wall (GDD §9.4): low cover you can walk over, drawn warmer than a low wall. */
	const FLinearColor Rubble(0.78f, 0.6f, 0.42f, 0.9f);
}

bool ASquadHUD::MinimapToWorld(const FVector2D& ScreenPosition, FVector& OutWorld) const
{
	if (!bMinimapVisible || MinimapXf.PxPerCm <= 0.f)
	{
		return false;
	}
	const FBox2D& Box = MinimapXf.MapBox;
	const FVector2D Clamped(FMath::Clamp(ScreenPosition.X, Box.Min.X, Box.Max.X), FMath::Clamp(ScreenPosition.Y, Box.Min.Y, Box.Max.Y));
	const FVector2D World = MinimapXf.ToWorld(Clamped);
	OutWorld = FVector(World.X, World.Y, MinimapXf.GroundZ);
	return true;
}

void ASquadHUD::GetMapOrientation(const ASquadPlayerController& PC, const AGridManager& Grid, FMapXf& Xf) const
{
	Xf.WorldCenter = FVector2D(Grid.GetActorLocation());
	Xf.GroundZ = Grid.GetActorLocation().Z;
	Xf.Down = FVector2D(-1.f, 0.f);
	if (const ADeploymentZone* OwnZone = ADeploymentZone::FindForTeam(this, PC.PlayerTeam))
	{
		const FIntPoint Min = OwnZone->GetMinCell();
		const FVector ZoneCenter = (Grid.CellToWorld(Min) + Grid.CellToWorld(Min + FIntPoint(OwnZone->SizeX - 1, OwnZone->SizeY - 1))) * 0.5f;
		const FVector2D ToZone = FVector2D(ZoneCenter) - Xf.WorldCenter;
		if (!ToZone.IsNearlyZero())
		{
			Xf.Down = FMath::Abs(ToZone.X) >= FMath::Abs(ToZone.Y)
				? FVector2D(FMath::Sign(ToZone.X), 0.f)
				: FVector2D(0.f, FMath::Sign(ToZone.Y));
		}
	}
	Xf.Right = FVector2D(Xf.Down.Y, -Xf.Down.X); // A turn, never a mirror image.
}

void ASquadHUD::DrawMapBase(ASquadPlayerController& PC, const AGridManager& Grid, const FMapXf& Xf, float S, float Detail)
{
	using namespace Minimap;

	const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const FTexture* White = Canvas->DefaultTexture->GetResource();
	const FBox2D& MapBox = Xf.MapBox;
	const float MapW = MapBox.GetSize().X, MapH = MapBox.GetSize().Y;
	const float DS = S * Detail;

	auto RectOutline = [this](const FBox2D& Box, const FLinearColor& Color, float T)
	{
		const FVector2D Sz = Box.GetSize();
		DrawRect(Color, Box.Min.X, Box.Min.Y, Sz.X, T);
		DrawRect(Color, Box.Min.X, Box.Max.Y - T, Sz.X, T);
		DrawRect(Color, Box.Min.X, Box.Min.Y + T, T, Sz.Y - 2.f * T);
		DrawRect(Color, Box.Max.X - T, Box.Min.Y + T, T, Sz.Y - 2.f * T);
	};
	auto DrawTris = [this, White](const TArray<FCanvasUVTri>& Tris)
	{
		if (Tris.Num() > 0)
		{
			FCanvasTriangleItem Item(Tris, White);
			Item.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Item);
		}
	};
	/** Screen box covered by a block of cells. */
	auto CellsBox = [&](FIntPoint Min, FIntPoint Max)
	{
		const FVector Half(Grid.CellSize * 0.5f, Grid.CellSize * 0.5f, 0.f);
		const FVector2D A = Xf.ToScreen(Grid.CellToWorld(Min) - Half);
		const FVector2D B = Xf.ToScreen(Grid.CellToWorld(Max) + Half);
		return FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
	};

	// ---- Background and reference grid
	DrawRect(MapBg, MapBox.Min.X, MapBox.Min.Y, MapW, MapH);
	const float CellPx = Grid.CellSize * Xf.PxPerCm;
	for (int32 I = GridLineStep; I * CellPx < MapW - 1.f; I += GridLineStep)
	{
		DrawRect(GridLine, MapBox.Min.X + I * CellPx, MapBox.Min.Y, 1.f, MapH);
	}
	for (int32 I = GridLineStep; I * CellPx < MapH - 1.f; I += GridLineStep)
	{
		DrawRect(GridLine, MapBox.Min.X, MapBox.Min.Y + I * CellPx, MapW, 1.f);
	}

	// ---- Deployment zones (faint) and victory points
	for (TActorIterator<ADeploymentZone> It(GetWorld()); It; ++It)
	{
		const FBox2D Box = CellsBox(It->GetMinCell(), It->GetMinCell() + FIntPoint(It->SizeX - 1, It->SizeY - 1));
		const FLinearColor Color = MapZoneColors::ForTeam(It->Team);
		DrawRect(WithAlpha(Color, 0.08f), Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
		RectOutline(Box, WithAlpha(Color, 0.35f), 1.f);
	}
	for (TActorIterator<AVictoryPoint> It(GetWorld()); It; ++It)
	{
		const FBox2D Box = CellsBox(It->GetMinCell(), It->GetMinCell() + FIntPoint(It->SizeX - 1, It->SizeY - 1));
		FLinearColor Color = It->IsOwned() ? MapZoneColors::ForTeam(It->GetOwningTeam()) : MapZoneColors::Neutral;
		float Alpha = 0.8f;
		if (It->IsContested())
		{
			Color = MapZoneColors::Contested;
			Alpha = 0.5f + 0.5f * (0.5f + 0.5f * FMath::Sin(Time * 6.f));
		}
		DrawRect(WithAlpha(Color, 0.14f), Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
		RectOutline(Box, WithAlpha(Color, Alpha), FMath::Max(1.f, S * FMath::Min(Detail, 1.6f)));
		const FString Label = It->PointName.ToString();
		const float LabelScale = 0.9f * S * FMath::Min(Detail, 1.8f);
		float TW = 0.f, TH = 0.f;
		GetTextSize(Label, TW, TH, Font, LabelScale);
		DrawText(Label, WithAlpha(Color, 0.9f), Box.GetCenter().X - TW * 0.5f, Box.GetCenter().Y - TH * 0.5f, Font, LabelScale);
	}

	// ---- Cover: walls and windows as slabs along their run, hay and low walls thicker and tinted, trees as dots.
	{
		TArray<FCanvasUVTri> TreeTris;
		const bool bXHorizontal = Xf.XIsHorizontal();
		for (const TPair<FIntPoint, FGridCoverCell>& Pair : Grid.GetCoverCells())
		{
			const FGridCoverCell& Cover = Pair.Value;
			const FVector2D P = Xf.ToScreen(Grid.CellToWorld(Pair.Key));
			if (Cover.Type == ECoverType::Tree)
			{
				AddDisc(TreeTris, P, FMath::Max(2.f * S, CellPx * 0.45f), Tree);
				continue;
			}
			FLinearColor Color = Wall;
			switch (Cover.Type)
			{
			case ECoverType::Soft:    Color = Hay; break;
			case ECoverType::LowWall: Color = LowWall; break;
			case ECoverType::Window:  Color = Window; break;
			default: break;
			}
			if (Cover.bPassable)
			{
				Color = Rubble;
			}
			const float Long = CellPx + 0.5f; // Slight overlap so runs of cells join up.
			const float Thick = Cover.bFillsCell ? Long : FMath::Max(1.5f, 2.f * UCoverData::GetGrenadeSlabHalfThickness(Cover.Type) / 100.f * CellPx);
			const bool bHorizontal = Cover.bRunsAlongX == bXHorizontal;
			const float W = bHorizontal ? Long : Thick, H = bHorizontal ? Thick : Long;
			DrawRect(Color, P.X - W * 0.5f, P.Y - H * 0.5f, W, H);
		}
		DrawTris(TreeTris);
	}

	// ---- The camera's view on the ground, clipped to the map.
	{
		const FVector2D Corners[4] = { FVector2D(0.f, 0.f), FVector2D(Canvas->ClipX, 0.f), FVector2D(Canvas->ClipX, Canvas->ClipY), FVector2D(0.f, Canvas->ClipY) };
		FVector2D Ground[4];
		bool bValid = true;
		const float GroundZ = Xf.GroundZ;
		constexpr float MaxReach = 100000.f; // 1 km: rays at or above the horizon stop here.
		for (int32 I = 0; I < 4 && bValid; ++I)
		{
			FVector Origin, Dir;
			bValid = PC.DeprojectScreenPositionToWorld(Corners[I].X, Corners[I].Y, Origin, Dir);
			const FVector Hit = Dir.Z < -KINDA_SMALL_NUMBER
				? Origin + Dir * ((GroundZ - Origin.Z) / Dir.Z)
				: Origin + Dir.GetSafeNormal2D() * MaxReach;
			const FVector2D Reach = FVector2D(Hit - Origin).GetClampedToMaxSize(MaxReach);
			Ground[I] = Xf.ToScreen(FVector2D(Origin.X + Reach.X, Origin.Y + Reach.Y));
		}
		if (bValid)
		{
			for (int32 I = 0; I < 4; ++I)
			{
				FVector2D A = Ground[I], B = Ground[(I + 1) % 4];
				if (ClipSegment(A, B, MapBox))
				{
					DrawLine(A.X, A.Y, B.X, B.Y, CameraView, Detail > 1.2f ? 1.5f : 1.f);
				}
			}
		}
	}

	// ---- Soldiers: allies and visible enemies, then the player's own squad on top with facing cones.
	TArray<FCanvasUVTri> OthersTris;
	TArray<FCanvasUVTri> ConeTris;
	TArray<const ASoldier*, TInlineAllocator<4>> Own;
	for (const ASoldier* Soldier : PC.GetSquad())
	{
		if (Soldier)
		{
			Own.Add(Soldier);
		}
	}
	for (const TWeakObjectPtr<ASoldier>& Weak : Grid.GetSoldiers())
	{
		const ASoldier* Soldier = Weak.Get();
		if (!Soldier || Soldier->IsDead() || Own.Contains(Soldier))
		{
			continue;
		}
		const FVector2D P = Xf.ToScreen(Soldier->GetActorLocation());
		if (Soldier->Team == PC.PlayerTeam)
		{
			const FLinearColor Ally = FLinearColor::LerpUsingHSV(MapZoneColors::ForTeam(Soldier->Team), FLinearColor::White, 0.35f);
			AddDisc(OthersTris, P, (AllyRadius + 1.f) * DS, Outline);
			AddDisc(OthersTris, P, AllyRadius * DS, Ally);
		}
		else if (!Soldier->IsHidden()) // Only enemies the team can see (GDD §9.3); hidden locally by the server's verdict.
		{
			AddDisc(OthersTris, P, (EnemyRadius + 1.f) * DS, Outline);
			AddDisc(OthersTris, P, EnemyRadius * DS, Enemy);
		}
	}
	DrawTris(OthersTris);

	// Own soldiers: a dot in the soldier's squad-bar colour with a cone showing which way it faces.
	TArray<FCanvasUVTri> OwnTris;
	for (const ASoldier* Soldier : Own)
	{
		const FVector2D P = Xf.ToScreen(Soldier->GetActorLocation());
		if (Soldier->IsDead())
		{
			const float R = OwnRadius * DS;
			DrawLine(P.X - R, P.Y - R, P.X + R, P.Y + R, Dead, 1.5f * S);
			DrawLine(P.X - R, P.Y + R, P.X + R, P.Y - R, Dead, 1.5f * S);
			continue;
		}
		const float Yaw = FMath::DegreesToRadians(Soldier->GetActorRotation().Yaw);
		const FVector2D Dir = Xf.DirToScreen(FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw))).GetSafeNormal();
		AddCone(ConeTris, P, Dir, ConeLength * DS, ConeHalfAngle, WithAlpha(Soldier->SoldierColor, Soldier->IsSelected() ? 0.45f : 0.3f));
		if (Soldier->IsSelected())
		{
			AddDisc(OwnTris, P, (OwnRadius + 2.5f) * DS, Gold);
		}
		AddDisc(OwnTris, P, (OwnRadius + 1.f) * DS, Outline);
		AddDisc(OwnTris, P, OwnRadius * DS, WithAlpha(Soldier->SoldierColor, 1.f));
	}
	DrawTris(ConeTris);
	DrawTris(OwnTris);

	if (Detail > 1.4f)
	{
		// Big map: each living soldier's number beside its dot.
		for (const ASoldier* Soldier : Own)
		{
			if (!Soldier->IsDead())
			{
				const FVector2D P = Xf.ToScreen(Soldier->GetActorLocation());
				DrawText(FString::FromInt(Soldier->GetSquadNumber()), FLinearColor::White, P.X + (OwnRadius + 2.f) * DS, P.Y - (OwnRadius + 8.f) * DS, Font, 0.8f * S);
			}
		}
	}
}

void ASquadHUD::DrawMinimap(ASquadPlayerController& PC)
{
	using namespace Minimap;

	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid || !Canvas->DefaultTexture)
	{
		return;
	}
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.9f, 1.5f);
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;

	// ---- Orientation: the player's own deployment zone at the bottom, the enemy's at the top.
	FMapXf Xf;
	GetMapOrientation(PC, *Grid, Xf);

	// ---- Size and place: bottom left, kept clear of the squad bar.
	const float WidthCm = (Xf.XIsHorizontal() ? Grid->GridSizeX : Grid->GridSizeY) * Grid->CellSize;
	const float HeightCm = (Xf.XIsHorizontal() ? Grid->GridSizeY : Grid->GridSizeX) * Grid->CellSize;
	float Size = MaxSize * S;
	if (bSquadBarVisible)
	{
		Size = FMath::Min(Size, SquadBarBox.Min.X - 2.f * (Margin + Pad) * S);
	}
	Size = FMath::Max(Size, MinSize * S);
	Xf.PxPerCm = Size / FMath::Max(WidthCm, HeightCm);
	const float MapW = WidthCm * Xf.PxPerCm, MapH = HeightCm * Xf.PxPerCm;
	const float FrameX = Margin * S, FrameY = Canvas->ClipY - Margin * S - MapH - 2.f * Pad * S;
	Xf.MapBox = FBox2D(FVector2D(FrameX + Pad * S, FrameY + Pad * S), FVector2D(FrameX + Pad * S + MapW, FrameY + Pad * S + MapH));
	MinimapBox = FBox2D(FVector2D(FrameX, FrameY), FVector2D(FrameX + MapW + 2.f * Pad * S, FrameY + MapH + 2.f * Pad * S));
	bMinimapVisible = true;
	MinimapXf = Xf;

	DrawRect(FrameBg, MinimapBox.Min.X, MinimapBox.Min.Y, MinimapBox.GetSize().X, MinimapBox.GetSize().Y);
	DrawMapBase(PC, *Grid, Xf, S, 1.f);
	DrawPlanOverlay(PC, Xf, S, 1.f, INDEX_NONE, false);

	float MX = 0.f, MY = 0.f;
	const bool bHaveMouse = PC.GetMousePosition(MX, MY);

	// ---- Expand button (top right corner): opens the tactical map (GDD §13.1).
	{
		const float B = ExpandSize * S;
		const FVector2D Min(Xf.MapBox.Max.X - B - 2.f * S, Xf.MapBox.Min.Y + 2.f * S);
		MinimapExpandBox = FBox2D(Min, Min + FVector2D(B, B));
		const bool bHover = bHaveMouse && MinimapExpandBox.IsInside(FVector2D(MX, MY));
		DrawRect(FLinearColor(0.f, 0.f, 0.f, bHover ? 0.85f : 0.6f), Min.X, Min.Y, B, B);
		const FLinearColor IconColor(1.f, 1.f, 1.f, bHover ? 1.f : 0.75f);
		const float I = B * 0.22f, L = B * 0.28f;
		// Two corner brackets pointing out: "maximise".
		DrawLine(Min.X + I, Min.Y + I, Min.X + I + L, Min.Y + I, IconColor, 1.5f);
		DrawLine(Min.X + I, Min.Y + I, Min.X + I, Min.Y + I + L, IconColor, 1.5f);
		DrawLine(Min.X + B - I, Min.Y + B - I, Min.X + B - I - L, Min.Y + B - I, IconColor, 1.5f);
		DrawLine(Min.X + B - I, Min.Y + B - I, Min.X + B - I, Min.Y + B - I - L, IconColor, 1.5f);
		if (bHover)
		{
			const FString Tip = TEXT("Tactical map (M)");
			float TW = 0.f, TH = 0.f;
			GetTextSize(Tip, TW, TH, Font, 0.85f * S);
			const float TX = Min.X + B - TW - 6.f * S, TY = Min.Y + B + 4.f * S;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.75f), TX, TY, TW + 6.f * S, TH + 4.f * S);
			DrawText(Tip, FLinearColor::White, TX + 3.f * S, TY + 2.f * S, Font, 0.85f * S);
		}
	}

	// ---- CO / XO: how many order requests are waiting, just above the minimap.
	const ASquadPlayerState* PS = PC.GetSquadPlayerState();
	const ATacticalPlan* Plan = PC.GetTacticalPlan();
	if (PS && PS->HasCommandRole() && Plan)
	{
		int32 Open = 0;
		for (const FTacticalOrderRequest& R : Plan->GetRequests())
		{
			Open += R.bAcknowledged ? 0 : 1;
		}
		if (Open > 0)
		{
			const float Time = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
			const FString Str = FString::Printf(TEXT("%d ORDER REQUEST%s  (M)"), Open, Open == 1 ? TEXT("") : TEXT("S"));
			float TW = 0.f, TH = 0.f;
			GetTextSize(Str, TW, TH, Font, 0.95f * S);
			const float X = MinimapBox.Min.X, Y = MinimapBox.Min.Y - TH - 10.f * S;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), X, Y, TW + 14.f * S, TH + 6.f * S);
			DrawRect(WithAlpha(Request, 0.6f + 0.4f * FMath::Sin(Time * 5.f)), X, Y, 3.f * S, TH + 6.f * S);
			DrawText(Str, Request, X + 9.f * S, Y + 3.f * S, Font, 0.95f * S);
		}
	}

	const FVector2D Sz = MinimapBox.GetSize();
	DrawRect(Border, MinimapBox.Min.X, MinimapBox.Min.Y, Sz.X, 1.f);
	DrawRect(Border, MinimapBox.Min.X, MinimapBox.Max.Y - 1.f, Sz.X, 1.f);
	DrawRect(Border, MinimapBox.Min.X, MinimapBox.Min.Y + 1.f, 1.f, Sz.Y - 2.f);
	DrawRect(Border, MinimapBox.Max.X - 1.f, MinimapBox.Min.Y + 1.f, 1.f, Sz.Y - 2.f);
}
