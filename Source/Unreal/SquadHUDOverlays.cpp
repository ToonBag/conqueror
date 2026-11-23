// Battlefield overlays for the player's own soldiers: move range, cover-direction preview and cover vision cones.
// Canvas drawing projected onto the ground, like the plan marks (SquadHUDTacticalMap.cpp). Cosmetic, client only.

#include "SquadHUD.h"
#include "SquadHUDDraw.h"
#include "SquadPlayerController.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "GridManager.h"
#include "CoverData.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "HAL/IConsoleManager.h"

using namespace SquadHUDDraw;

namespace Overlays
{
	// Move range outline (GDD §4.3): faint, so it reads as a boundary rather than a marking.
	const FLinearColor RangeColor(0.85f, 0.9f, 1.f, 0.22f);
	constexpr float RangeLineWidth = 1.6f;
	constexpr float RangeRefreshInterval = 0.2f;
	constexpr int32 RangeCircleSegments = 160;

	// Cover preview (GDD §9.2): ring sectors round the hovered cell, one per adjacent piece of cover.
	const FLinearColor CoverLeanColor(0.45f, 0.95f, 0.5f, 1.f);
	const FLinearColor CoverBackColor(1.f, 0.78f, 0.3f, 1.f);
	constexpr float CoverInnerMetres = 0.55f;
	constexpr float CoverOuterMetres = 2.6f;
	constexpr int32 CoverArcSegments = 12;

	// Vision cone (GDD §9.3).
	const FLinearColor VisionColor(0.8f, 0.92f, 1.f, 1.f);
	constexpr float VisionFillAlpha = 0.11f;
	constexpr float VisionEdgeAlpha = 0.4f;
	constexpr int32 VisionRays = 49;
	constexpr float VisionRayStepMetres = 0.5f;
	constexpr float VisionFadeIn = 0.2f;
	constexpr float VisionFadeOut = 0.9f;
	/** The soldier's facing must hold within this many degrees for VisionSettleTime before the cone flashes. */
	constexpr float VisionSettleTolerance = 0.5f;
	constexpr float VisionSettleTime = 0.12f;

	TAutoConsoleVariable<FString> CVarDebugHoverCell(
		TEXT("squad.DebugHoverCell"), TEXT(""),
		TEXT("\"X Y\": draw the cover preview as if the cursor were on that cell (tests). Empty: use the cursor."),
		ECVF_Cheat);

	FVector2D YawDir(float YawDeg)
	{
		const float R = FMath::DegreesToRadians(YawDeg);
		return FVector2D(FMath::Cos(R), FMath::Sin(R));
	}

	float SideYaw(FIntPoint Side)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Side.Y), static_cast<float>(Side.X)));
	}
}

void ASquadHUD::DrawBattlefieldOverlays(ASquadPlayerController& PC)
{
	if (!Canvas || !Canvas->DefaultTexture || !AGridManager::Find(this))
	{
		return;
	}
	UpdateVisionFlashes(PC);

	TArray<FCanvasUVTri> Tris;
	TArray<TPair<FString, FVector2D>> Labels;
	TArray<FLinearColor> LabelColors;
	AddVisionFlashes(PC, Tris);
	AddMoveRange(PC, Tris);
	AddCoverPreview(PC, Tris, Labels, LabelColors);

	if (Tris.Num() > 0)
	{
		FCanvasTriangleItem Item(Tris, Canvas->DefaultTexture->GetResource());
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float S = LastScale;
	for (int32 I = 0; I < Labels.Num(); ++I)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Labels[I].Key, TW, TH, Font, 0.85f * S);
		const FVector2D At = Labels[I].Value - FVector2D(TW * 0.5f, TH * 0.5f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), At.X - 3.f * S, At.Y - 1.f * S, TW + 6.f * S, TH + 2.f * S);
		DrawText(Labels[I].Key, LabelColors[I], At.X, At.Y, Font, 0.85f * S);
	}
}

// ------------------------------------------------------------------------------------------------ Move range

void ASquadHUD::AddMoveRange(ASquadPlayerController& PC, TArray<FCanvasUVTri>& Tris)
{
	using namespace Overlays;

	AGridManager* Grid = AGridManager::Find(this);
	const TArray<ASoldier*> Selected = PC.GetSelectedSoldiers();
	if (!Grid || Selected.Num() == 0)
	{
		MoveRangeEdges.Reset();
		return;
	}

	const float Now = GetWorld()->GetRealTimeSeconds();
	if (MoveRangeRefreshTime < 0.f || Now - MoveRangeRefreshTime >= RangeRefreshInterval)
	{
		MoveRangeRefreshTime = Now;
		MoveRangeEdges.Reset();

		// Mirror ASquadPlayerController::IssueMove: one soldier goes where it's sent; a group keeps its formation
		// round the lowest-numbered selected soldier, and only the soldiers able to act move.
		TArray<ASoldier*> Movers;
		for (ASoldier* Soldier : Selected)
		{
			if (Soldier && !Soldier->IsDead() && (Selected.Num() == 1 || Soldier->CanAct()))
			{
				Movers.Add(Soldier);
			}
		}
		// Nothing limits the move when every living squad-mate goes with it (dragged soldiers follow their dragger).
		bool bLimited = false;
		for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
		{
			const ASoldier* Mate = Weak.Get();
			if (Movers.Num() > 0 && Mate && !Mate->IsDead() && Mate->Team == Movers[0]->Team && !Movers.Contains(Mate)
				&& !Movers.Contains(Mate->GetDraggedBy()))
			{
				bLimited = true;
				break;
			}
		}
		if (bLimited && Movers.Num() > 0 && Movers[0]->GetSoldierData()->SquadCohesionLimit > 0.f)
		{
			// GDD §4.3 (as ASoldier::IsCohesiveDestination judges it): each mover must end up within the limit of every
			// squad-mate staying behind, or no further from it than it already is. For the lead soldier's cell that is a
			// circle per (mover, mate) pair, centred on the mate shifted by the mover's place in the formation. The
			// range is where all the circles overlap, so its outline is the parts of each circle inside all the others.
			const float Limit = Movers[0]->GetSoldierData()->SquadCohesionLimit;
			const FIntPoint Anchor = Selected[0]->GetDestinationCell();
			struct FCircle { FVector2D Centre; float Radius; };
			TArray<FCircle> Circles; // In cells.
			for (const ASoldier* Mover : Movers)
			{
				const FIntPoint Offset = Selected.Num() == 1 ? FIntPoint::ZeroValue : Mover->GetDestinationCell() - Anchor;
				const FVector2D Here(Mover->GetCohesionCell());
				for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
				{
					const ASoldier* Mate = Weak.Get();
					if (!Mate || Mate->IsDead() || !Mover->IsSquadMate(Mate) || Movers.Contains(Mate) || Movers.Contains(Mate->GetDraggedBy()))
					{
						continue;
					}
					const FVector2D MateCell(Mate->GetCohesionCell());
					Circles.Add({ MateCell - FVector2D(Offset), FMath::Max(Limit, static_cast<float>(FVector2D::Distance(Here, MateCell))) });
				}
			}

			const FVector2D Cell0(Grid->CellToWorld(FIntPoint::ZeroValue));
			const float Size = Grid->CellSize;
			const FBox2D MapBox(Cell0 - FVector2D(Size * 0.5f), Cell0 + FVector2D(Grid->GridSizeX - 0.5f, Grid->GridSizeY - 0.5f) * Size);
			auto ToWorld = [&](const FVector2D& C) { return Cell0 + C * Size; };
			for (int32 I = 0; I < Circles.Num(); ++I)
			{
				const FCircle& C = Circles[I];
				for (int32 K = 0; K < RangeCircleSegments; ++K)
				{
					const float A0 = 2.f * PI * K / RangeCircleSegments, A1 = 2.f * PI * (K + 1) / RangeCircleSegments;
					const FVector2D P0 = C.Centre + C.Radius * FVector2D(FMath::Cos(A0), FMath::Sin(A0));
					const FVector2D P1 = C.Centre + C.Radius * FVector2D(FMath::Cos(A1), FMath::Sin(A1));
					const FVector2D Mid = (P0 + P1) * 0.5f;
					bool bInside = MapBox.IsInside(ToWorld(Mid)); // The map's own edge isn't drawn.
					for (int32 J = 0; J < Circles.Num() && bInside; ++J)
					{
						bInside = J == I || FVector2D::Distance(Mid, Circles[J].Centre) <= Circles[J].Radius + 0.01f;
					}
					if (bInside)
					{
						MoveRangeEdges.Emplace(ToWorld(P0), ToWorld(P1));
					}
				}
			}
		}
	}

	const float W = FMath::Max(1.f, RangeLineWidth * LastScale);
	for (const TPair<FVector2D, FVector2D>& E : MoveRangeEdges)
	{
		FVector2D A, B;
		if (ProjectGround(PC, E.Key, A) && ProjectGround(PC, E.Value, B))
		{
			AddSegment(Tris, A, B, W, RangeColor);
		}
	}
}

// ------------------------------------------------------------------------------------------------ Cover preview

void ASquadHUD::AddCoverPreview(ASquadPlayerController& PC, TArray<FCanvasUVTri>& Tris, TArray<TPair<FString, FVector2D>>& Labels, TArray<FLinearColor>& LabelColors)
{
	using namespace Overlays;

	AGridManager* Grid = AGridManager::Find(this);
	ASoldier* Soldier = PC.GetSelectedSoldier();
	FIntPoint Cell = FIntPoint::ZeroValue;
	bool bHover = Grid && Grid->GetHoveredCell(Cell);
	const FString DebugCell = CVarDebugHoverCell.GetValueOnGameThread();
	FString XStr, YStr;
	if (Grid && DebugCell.TrimStartAndEnd().Split(TEXT(" "), &XStr, &YStr))
	{
		Cell = FIntPoint(FCString::Atoi(*XStr), FCString::Atoi(*YStr));
		bHover = Grid->IsInBounds(Cell);
	}
	if (!Grid || !Soldier || !Soldier->CanAct() || !bHover)
	{
		return;
	}
	// Plain hover only: Shift (face / attack) and Alt (grenade) clicks don't move the soldier there.
	if (PC.IsInputKeyDown(EKeys::LeftShift) || PC.IsInputKeyDown(EKeys::RightShift)
		|| PC.IsInputKeyDown(EKeys::LeftAlt) || PC.IsInputKeyDown(EKeys::RightAlt))
	{
		return;
	}

	const FIntPoint Sides[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	bool bAnyCover = false;
	for (const FIntPoint& Side : Sides)
	{
		bAnyCover |= Grid->IsCoverPiece(Cell, Side); // Not the middle of a full wall's face.
	}
	if (!bAnyCover)
	{
		return;
	}

	// Which way the soldier will face on arrival: the direction of its last step (it then leans on the cover
	// nearest that, GDD §9.2). Already standing there: the way it faces now.
	const FIntPoint From = Soldier->GetCurrentCell();
	if (CoverPreviewSoldier.Get() != Soldier || CoverPreviewFrom != From || CoverPreviewCell != Cell
		|| CoverPreviewRevision != Grid->GetCoverRevision())
	{
		CoverPreviewRevision = Grid->GetCoverRevision(); // A breached wall (GDD §9.4) can open new paths.
		CoverPreviewSoldier = Soldier;
		CoverPreviewFrom = From;
		CoverPreviewCell = Cell;
		bCoverPreviewReachable = false;
		if (Cell == From)
		{
			bCoverPreviewReachable = true;
			CoverPreviewArrivalYaw = Soldier->GetActorRotation().Yaw;
		}
		else if (Grid->IsCellFreeAsDestination(Cell, Soldier) && Soldier->IsCohesiveDestination(Cell))
		{
			TArray<FIntPoint> Path;
			if (Grid->FindPath(From, Cell, Soldier, Path) && Path.Num() > 0)
			{
				const FIntPoint Prev = Path.Num() >= 2 ? Path[Path.Num() - 2] : From;
				CoverPreviewArrivalYaw = SideYaw(Cell - Prev);
				bCoverPreviewReachable = true;
			}
		}
	}
	if (!bCoverPreviewReachable)
	{
		return;
	}
	const float ArrivalYaw = Cell == From ? Soldier->GetActorRotation().Yaw : CoverPreviewArrivalYaw;
	FIntPoint LeanSide = FIntPoint::ZeroValue;
	const bool bLeans = Grid->FindCoverToLeanOn(Cell, ArrivalYaw, LeanSide);

	const UCoverData* Data = Grid->GetCoverData();
	const FVector2D Centre(Grid->CellToWorld(Cell));
	const float Metre = Grid->CellSize;
	const float Half = Data->CoverArcHalfAngle;
	const float Pulse = 0.85f + 0.15f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 4.f);
	const float S = LastScale;

	for (const FIntPoint& Side : Sides)
	{
		const FGridCoverCell* Cover = Grid->GetCoverCell(Cell + Side);
		if (!Cover || !Grid->IsCoverPiece(Cell, Side))
		{
			continue;
		}
		const bool bLean = bLeans && Side == LeanSide;
		const FLinearColor Base = bLean ? CoverLeanColor : CoverBackColor;
		const float FillA = bLean ? 0.3f * Pulse : 0.12f;
		const float EdgeA = bLean ? 0.85f : 0.35f;
		const float Yaw = SideYaw(Side);

		// Ring sector from the inner to the outer radius across the cover's arc (GDD §9.2: fire from within it is covered).
		TArray<FVector2D> Inner, Outer;
		bool bOk = true;
		for (int32 I = 0; I <= CoverArcSegments && bOk; ++I)
		{
			const FVector2D Dir = YawDir(Yaw - Half + 2.f * Half * I / CoverArcSegments);
			FVector2D PI0, PO0;
			bOk = ProjectGround(PC, Centre + Dir * CoverInnerMetres * Metre, PI0) && ProjectGround(PC, Centre + Dir * CoverOuterMetres * Metre, PO0);
			Inner.Add(PI0);
			Outer.Add(PO0);
		}
		if (!bOk)
		{
			continue;
		}
		const FLinearColor Fill = WithAlpha(Base, FillA);
		const FLinearColor Edge = WithAlpha(Base, EdgeA);
		const float W = FMath::Max(1.f, (bLean ? 2.f : 1.2f) * S);
		for (int32 I = 0; I < CoverArcSegments; ++I)
		{
			AddTri(Tris, Inner[I], Outer[I], Outer[I + 1], Fill);
			AddTri(Tris, Inner[I], Outer[I + 1], Inner[I + 1], Fill);
			AddSegment(Tris, Outer[I], Outer[I + 1], W, Edge, false);
		}
		AddSegment(Tris, Inner[0], Outer[0], W, Edge, false);
		AddSegment(Tris, Inner.Last(), Outer.Last(), W, Edge, false);

		// "-50 LOW WALL" on the leaned-on cover; the reduced value (back turned) on the rest.
		const float Reduction = Data->GetReduction(Cover->Type) * (bLean ? 1.f : Data->NotLeaningCoverMultiplier);
		FVector2D LabelAt;
		if (ProjectGround(PC, Centre + YawDir(Yaw) * (CoverOuterMetres + 0.45f) * Metre, LabelAt))
		{
			Labels.Emplace(FString::Printf(TEXT("-%.0f %s%s"), Reduction, UCoverData::GetCoverName(Cover->Type), bLean ? TEXT("") : TEXT(" (BACK)")), LabelAt);
			LabelColors.Add(WithAlpha(Base, bLean ? 0.95f : 0.6f));
		}
	}
}

// ------------------------------------------------------------------------------------------------ Vision cones

void ASquadHUD::UpdateVisionFlashes(ASquadPlayerController& PC)
{
	using namespace Overlays;

	AGridManager* Grid = AGridManager::Find(this);
	const float Now = GetWorld()->GetTimeSeconds();
	VisionFlashes.RemoveAll([&](const FVisionFlash& F) { return !F.Soldier.IsValid() || F.Soldier->IsDead() || Now - F.StartTime > VisionFlashDuration; });
	if (!Grid)
	{
		return;
	}

	const UCoverData* Data = Grid->GetCoverData();
	const float MaxSight = Data->MaxSightRange > 0.f ? Data->MaxSightRange : 40.f;
	const float TargetHeight = Data->GetSightHeight(ESoldierStance::Standing);

	for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
	{
		ASoldier* Soldier = Weak.Get();
		if (!Soldier || Soldier->Team != PC.PlayerTeam || Soldier->IsDead())
		{
			continue;
		}
		// Only once the soldier has finished turning into the lean: arriving at cover it is still facing the way it
		// ran for a moment (already within the leaning angle), and the cone must show where it ends up looking.
		const float Yaw = Soldier->GetActorRotation().Yaw;
		FFacingSettle& Settle = VisionFacing.FindOrAdd(Soldier);
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(Settle.Yaw, Yaw)) > VisionSettleTolerance)
		{
			Settle.Since = Now;
		}
		Settle.Yaw = Yaw;
		if (Soldier->IsMoving())
		{
			VisionFlashCells.Remove(Soldier); // Off to new cover: flash again when it settles in.
			continue;
		}
		const FIntPoint Cell = Soldier->GetCurrentCell();
		const FIntPoint* Flashed = VisionFlashCells.Find(Soldier);
		if (!Soldier->IsLeaningOnCover() || (Flashed && *Flashed == Cell) || Now - Settle.Since < VisionSettleTime)
		{
			continue; // Turning away from the cover and back doesn't flash it again.
		}
		FIntPoint LeanSide;
		if (!Grid->FindCoverToLeanOn(Cell, Yaw, LeanSide) || !Grid->IsCoverPiece(Cell, LeanSide))
		{
			continue; // Up against the middle of a full wall: it blocks sight but isn't a cover piece.
		}
		VisionFlashCells.Add(Soldier, Cell);

		// The cone's far edge, ray by ray: how far a standing enemy could be seen (GDD §9.3: cover blocks sight,
		// a soldier leaning out past a wall's end sees from beside it). Cover cells themselves are skipped.
		FVisionFlash& Flash = VisionFlashes.AddDefaulted_GetRef();
		Flash.Soldier = Soldier;
		Flash.StartTime = Now;
		Flash.Apex = FVector2D(Grid->CellToWorld(Cell));
		float FirstYaw = Soldier->GetActorRotation().Yaw - VisionFlashHalfAngle;
		float SpanYaw = 2.f * VisionFlashHalfAngle;
		float MinReach = 0.5f;
		FWallPeek Peek;
		if (Soldier->GetActivePeek(Peek))
		{
			// Leaning out past a wall's edge: the cone fans out from the wall's corner over the open side only, from
			// straight along the wall to VisionFlashHalfAngle off it. The apex sits just outside the corner, so the
			// ray along the wall's face doesn't clip the wall cells.
			const FVector2D Toward(Peek.Toward.X, Peek.Toward.Y);
			const FVector2D Edge(Peek.Edge.X, Peek.Edge.Y);
			Flash.Apex = FVector2D(Grid->CellToWorld(Peek.WallCell)) + (Edge * 0.55f - Toward * 0.5f) * Grid->CellSize;
			const float TowardYaw = FMath::RadiansToDegrees(FMath::Atan2(Toward.Y, Toward.X));
			const bool bEdgeIsPositive = Peek.Edge == FIntPoint(-Peek.Toward.Y, Peek.Toward.X); // +90° yaw from Toward.
			FirstYaw = bEdgeIsPositive ? TowardYaw : TowardYaw - VisionFlashHalfAngle;
			SpanYaw = VisionFlashHalfAngle;
			MinReach = 0.f;
		}
		const float Height = Soldier->GetSightHeight();
		TArray<FIntPoint, TInlineAllocator<3>> Origins;
		for (int32 R = 0; R < VisionRays; ++R)
		{
			const FVector2D Dir = YawDir(FirstYaw + SpanYaw * R / (VisionRays - 1));
			float Reach = 0.f;
			FIntPoint LastCell = Cell;
			bool bLastVisible = true;
			for (float D = VisionRayStepMetres; D <= MaxSight + KINDA_SMALL_NUMBER; D += VisionRayStepMetres)
			{
				FIntPoint Probe;
				if (!Grid->WorldToCell(FVector(Flash.Apex + Dir * D * Grid->CellSize, Grid->GetActorLocation().Z), Probe))
				{
					break; // Off the map.
				}
				if (Probe != LastCell)
				{
					LastCell = Probe;
					// Rubble (GDD §9.4) can be stood on, so it is judged like open ground.
					if (const FGridCoverCell* Cover = Grid->GetCoverCell(Probe); Cover && !Cover->bPassable)
					{
						if (Cover->bBlocksLineOfSight)
						{
							break; // Full walls end the ray: nothing is seen through or along the top of one.
						}
						continue; // Judge the cells beyond low cover, not the cover itself.
					}
					Soldier->GetSightOrigins(Probe, Origins);
					bLastVisible = false;
					for (const FIntPoint& Origin : Origins)
					{
						if (Grid->HasLineOfSight(Origin, Probe, Height, TargetHeight))
						{
							bLastVisible = true;
							break;
						}
					}
				}
				if (!bLastVisible)
				{
					break;
				}
				Reach = D;
			}
			Flash.Edge.Add(Flash.Apex + Dir * FMath::Max(Reach, MinReach) * Grid->CellSize);
		}
	}
}

void ASquadHUD::AddVisionFlashes(ASquadPlayerController& PC, TArray<FCanvasUVTri>& Tris)
{
	using namespace Overlays;

	const float Now = GetWorld()->GetTimeSeconds();
	const float W = FMath::Max(1.f, 1.5f * LastScale);
	for (const FVisionFlash& Flash : VisionFlashes)
	{
		const float T = Now - Flash.StartTime;
		const float Envelope = FMath::Clamp(FMath::Min(T / VisionFadeIn, (VisionFlashDuration - T) / VisionFadeOut), 0.f, 1.f);
		if (Envelope <= 0.f || Flash.Edge.Num() < 2)
		{
			continue;
		}
		FVector2D Apex;
		if (!ProjectGround(PC, Flash.Apex, Apex))
		{
			continue;
		}
		TArray<FVector2D> P;
		P.SetNum(Flash.Edge.Num());
		TBitArray<> Ok(false, Flash.Edge.Num());
		for (int32 I = 0; I < Flash.Edge.Num(); ++I)
		{
			Ok[I] = ProjectGround(PC, Flash.Edge[I], P[I]);
		}
		const FLinearColor Fill = WithAlpha(VisionColor, VisionFillAlpha * Envelope);
		const FLinearColor Edge = WithAlpha(VisionColor, VisionEdgeAlpha * Envelope);
		for (int32 I = 0; I + 1 < P.Num(); ++I)
		{
			if (Ok[I] && Ok[I + 1])
			{
				AddTri(Tris, Apex, P[I], P[I + 1], Fill);
				AddSegment(Tris, P[I], P[I + 1], W, Edge, false);
			}
		}
		if (Ok[0])
		{
			AddSegment(Tris, Apex, P[0], W, Edge, false);
		}
		if (Ok[P.Num() - 1])
		{
			AddSegment(Tris, Apex, P.Last(), W, Edge, false);
		}
	}
}
