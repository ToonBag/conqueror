#include "SquadPlayerController.h"

#include "GridManager.h"
#include "LootSystem.h"
#include "Soldier.h"
#include "SoldierData.h"
#include "SquadCameraPawn.h"
#include "SquadHUD.h"
#include "SquadPlayerState.h"
#include "TacticalPlan.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

ASquadPlayerController::ASquadPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ASquadPlayerController::BeginPlay()
{
	Super::BeginPlay();

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	if (IsLocalController())
	{
		LoadAudioSettings();
	}
	Grid = AGridManager::Find(this);
	if (IsLocalController())
	{
		// The server's copy of a remote player gets its squad from AssignTeam (game mode, on login); this
		// client's copy re-gathers when PlayerTeam arrives (OnRep_PlayerTeam).
		GatherSquad();
		SelectBySquadNumber(1);
	}
}

void ASquadPlayerController::GatherSquad()
{
	Squad.Reset();
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		if (IsOwnSquad(*It))
		{
			Squad.Add(*It);
			if (HasAuthority())
			{
				It->SetOwningPlayer(PlayerState); // Kill feed shows this player's rank, regiment and name.
			}
		}
	}
	Squad.Sort([](const ASoldier& A, const ASoldier& B) { return A.SquadIndex < B.SquadIndex; });
}

// ------------------------------------------------------------------------------------------------ Selection

void ASquadPlayerController::SetSelection(const TArray<ASoldier*>& NewSelection)
{
	for (ASoldier* Soldier : Selected)
	{
		if (Soldier)
		{
			Soldier->SetSelected(false);
		}
	}
	Selected.Reset();
	for (ASoldier* Soldier : NewSelection)
	{
		if (Soldier && !Soldier->IsDead() && IsOwnSquad(Soldier))
		{
			Selected.AddUnique(Soldier);
			Soldier->SetSelected(true);
		}
	}
	Selected.Sort([](const ASoldier& A, const ASoldier& B) { return A.SquadIndex < B.SquadIndex; });
}

void ASquadPlayerController::SelectSoldier(ASoldier* Soldier)
{
	TArray<ASoldier*> NewSelection;
	if (Soldier)
	{
		NewSelection.Add(Soldier);
	}
	SetSelection(NewSelection);
}

void ASquadPlayerController::SelectAll()
{
	TArray<ASoldier*> NewSelection;
	for (ASoldier* Soldier : Squad)
	{
		NewSelection.Add(Soldier);
	}
	SetSelection(NewSelection);
}

void ASquadPlayerController::SelectBySquadNumber(int32 Number)
{
	// Match the number shown on the soldier, not array position, so key and label always agree.
	for (ASoldier* Soldier : Squad)
	{
		if (Soldier && Soldier->GetSquadNumber() == Number && !Soldier->IsDead())
		{
			SelectSoldier(Soldier);
			return;
		}
	}
}

void ASquadPlayerController::CentreCameraOnSquadNumber(int32 Number)
{
	APawn* Cam = GetPawn();
	if (!Cam)
	{
		return;
	}
	for (const ASoldier* Soldier : Squad)
	{
		if (Soldier && Soldier->GetSquadNumber() == Number)
		{
			const FVector Target = Soldier->GetActorLocation();
			Cam->SetActorLocation(FVector(Target.X, Target.Y, Cam->GetActorLocation().Z));
			return;
		}
	}
}

void ASquadPlayerController::PruneSelection()
{
	Selected.RemoveAll([](const TObjectPtr<ASoldier>& Soldier) { return !Soldier || Soldier->IsDead(); });
}

ASoldier* ASquadPlayerController::GetSelectedSoldier() const
{
	return Selected.Num() > 0 ? Selected[0].Get() : nullptr;
}

TArray<ASoldier*> ASquadPlayerController::GetSelectedSoldiers() const
{
	TArray<ASoldier*> Result;
	for (ASoldier* Soldier : Selected)
	{
		Result.Add(Soldier);
	}
	return Result;
}

// ------------------------------------------------------------------------------------------------ Input

void ASquadPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Server calls queued by this player (soldier orders, loot, build, plan, debug) go out now.
	TArray<TFunction<void()>> Calls = MoveTemp(PendingServerCalls);
	PendingServerCalls.Reset();
	for (TFunction<void()>& Call : Calls)
	{
		Call();
	}

	// Start the camera over this player's own squad once it is known (the map's player start is on the Allied side).
	if (!bCameraOnOwnSquad && IsLocalController() && GetPawn() && Squad.Num() > 0)
	{
		CentreCameraOnSquadNumber(1);
		bCameraOnOwnSquad = true;
	}

	// Real (undilated) time: the camera keeps its speed when the debug menu slows or speeds up the game.
	HandleCamera(FApp::GetDeltaTime());
	PruneSelection();
	UpdatePendingMoves(DeltaTime);
	RefreshContextMenu();
	RebuildDebugMenu();
	HandleKeys();
	HandleMouse();
}

void ASquadPlayerController::HandleKeys()
{
	const FKey NumberKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	for (int32 i = 0; i < UE_ARRAY_COUNT(NumberKeys); ++i)
	{
		if (WasInputKeyJustPressed(NumberKeys[i]))
		{
			const int32 Number = i + 1;
			SelectBySquadNumber(Number);
			// GDD §13: a second tap of the same number within the window centres the camera on that soldier.
			if (Number == LastNumberTapped && Now - LastNumberTapTime <= DoubleTapWindow)
			{
				CentreCameraOnSquadNumber(Number);
				LastNumberTapped = 0; // A third tap starts a new double tap.
			}
			else
			{
				LastNumberTapped = Number;
				LastNumberTapTime = Now;
			}
		}
	}

	// Backtick. UE names the `/~ key "Tilde".
	if (WasInputKeyJustPressed(EKeys::Tilde))
	{
		SelectAll();
	}

	// GDD §5.5 / §13: Tab opens the loot & trade menu for the selected soldier. It stays open while the
	// player keeps commanding (number keys switch which soldier is looting).
	if (WasInputKeyJustPressed(EKeys::Tab))
	{
		bLootMenuOpen = !bLootMenuOpen;
	}

#if !UE_BUILD_SHIPPING
	// Debug menu (SquadPlayerControllerDebug.cpp). F6: F1-F5 / F9 are engine debug bindings, F10 the console.
	if (WasInputKeyJustPressed(EKeys::F6))
	{
		SetDebugMenuOpen(!bDebugMenuOpen);
	}
	if (DebugClickTool != EDebugClickTool::None && WasInputKeyJustPressed(EKeys::Escape))
	{
		DebugClickTool = EDebugClickTool::None; // Esc drops a debug click tool before anything else.
		return;
	}
#endif

	// GDD §13.1: M opens / closes the tactical map. Esc cancels a stroke being drawn, then closes the map;
	// with the map shut it closes the loot menu.
	if (WasInputKeyJustPressed(EKeys::M))
	{
		SetTacticalMapOpen(!bTacticalMapOpen);
	}
	else if (WasInputKeyJustPressed(EKeys::Escape))
	{
		if (ContextMenu.bOpen)
		{
			CloseContextMenu(); // The right-click menu goes first: it sits on top of everything.
		}
		else if (CommandMode != ECommandMode::None)
		{
			CancelCommand(); // GDD §13.2: Esc first cancels the command wheel / live drawing.
		}
		else if (bTacticalMapOpen)
		{
			if (PlanDraft.bActive)
			{
				PlanDraft = FPlanDraft();
			}
			else
			{
				SetTacticalMapOpen(false);
			}
		}
		else if (bLootMenuOpen)
		{
			bLootMenuOpen = false;
		}
	}

	if (WasInputKeyJustPressed(EKeys::C))
	{
		IssueStanceCycle();
	}

	if (WasInputKeyJustPressed(EKeys::H))
	{
		IssueHoldFireToggle();
	}
}

bool ASquadPlayerController::HandleLootMenuMouse()
{
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!bLootMenuOpen || !SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	const FVector2D Mouse(MouseX, MouseY);
	if (!SquadHUD->IsOverLootMenu(Mouse))
	{
		return false;
	}
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		const bool bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
		FLootAction Action;
		if (SquadHUD->GetLootActionAt(Mouse, bShift, Action))
		{
			RequestLootAction(Action);
		}
	}
	return true;
}

bool ASquadPlayerController::HandleSquadBarMouse()
{
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	const FVector2D Mouse(MouseX, MouseY);
	if (!SquadHUD->IsOverSquadBar(Mouse))
	{
		return false;
	}
	if (WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		// Right click an injured soldier's card: the same context menu as on the battlefield.
		if (ASoldier* Clicked = SquadHUD->GetSquadCardAt(Mouse))
		{
			OpenContextMenu(Clicked, Mouse);
		}
	}
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		ASoldier* Clicked = SquadHUD->GetSquadCardAt(Mouse);
		if (Clicked && !Clicked->IsDead())
		{
			const bool bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
			if (bShift)
			{
				TArray<ASoldier*> NewSelection = GetSelectedSoldiers();
				if (NewSelection.Contains(Clicked))
				{
					NewSelection.Remove(Clicked);
				}
				else
				{
					NewSelection.Add(Clicked);
				}
				SetSelection(NewSelection);
			}
			else
			{
				SelectSoldier(Clicked);
			}
		}
	}
	return true;
}

void ASquadPlayerController::RequestLootAction(const FLootAction& Action)
{
	// Looting is done by the first selected soldier (the one named in the menu title), on the server.
	if (ASoldier* Looter = GetSelectedSoldier())
	{
		CallServer([=, this]() { ServerLootAction(Looter, Action); });
	}
}

void ASquadPlayerController::ServerLootAction_Implementation(ASoldier* Looter, const FLootAction& Action)
{
	if (IsOwnSquad(Looter) && !Looter->IsDead())
	{
		SquadLoot::Execute(Looter, Action); // Checks reach and what the source holds itself.
		Looter->ForceNetUpdate();
	}
}

void ASquadPlayerController::ServerSoldierOrder_Implementation(const FSoldierOrder& Order)
{
	if (!HasAuthority())
	{
		return; // Never run a client's order anywhere but the server.
	}
	// Only this player's own living soldiers take orders from it (GDD §4.1: one squad per player).
	ASoldier* Soldier = Order.Soldier.Get();
	if (!IsOwnSquad(Soldier) || Soldier->IsDead()
		|| (Soldier->GetOwningPlayer() && Soldier->GetOwningPlayer() != PlayerState))
	{
		return;
	}
	Soldier->ExecuteOrder(Order);
}

void ASquadPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASquadPlayerController, PlayerTeam);
	DOREPLIFETIME(ASquadPlayerController, PlayerSquadId);
}

void ASquadPlayerController::AssignTeam(ESquadTeam Team, int32 SquadId)
{
	check(HasAuthority());
	PlayerTeam = Team;
	PlayerSquadId = SquadId;
	if (ASquadPlayerState* PS = GetPlayerState<ASquadPlayerState>())
	{
		PS->Team = Team;
	}
	GatherSquad(); // Server: also makes this player the squad's owner (kill feed, orders).
	SelectBySquadNumber(1);
}

void ASquadPlayerController::OnRep_PlayerTeam()
{
	GatherSquad();
	SelectBySquadNumber(1);
	bCameraOnOwnSquad = false; // Team or squad arrived: PlayerTick moves the camera to it.
}

bool ASquadPlayerController::HandleMinimapMouse()
{
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		bMinimapDragging = false;
		return false;
	}
	const FVector2D Mouse(MouseX, MouseY);
	if (!bMinimapDragging && WasInputKeyJustPressed(EKeys::LeftMouseButton) && SquadHUD->IsOverMinimapExpandButton(Mouse))
	{
		SetTacticalMapOpen(true); // GDD §13.1: the expand button in the minimap's corner.
		return true;
	}
	if (!IsInputKeyDown(EKeys::LeftMouseButton))
	{
		bMinimapDragging = false;
	}
	else if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && SquadHUD->IsOverMinimap(Mouse))
	{
		bMinimapDragging = true;
	}
	if (bMinimapDragging)
	{
		FVector Focus;
		if (APawn* Cam = GetPawn(); Cam && SquadHUD->MinimapToWorld(Mouse, Focus))
		{
			Cam->SetActorLocation(FVector(Focus.X, Focus.Y, Cam->GetActorLocation().Z));
		}
		return true;
	}
	return SquadHUD->IsOverMinimap(Mouse);
}

void ASquadPlayerController::HandleMouse()
{
	// The tactical map takes everything while open. Then the minimap: a camera drag started on it keeps going
	// over the other panels.
	// The command wheel and what it starts (live drawing / erasing) come first: they own the mouse while active.
	// Then the right-click context menu, which sits on top of the other panels.
	if (WasInputKeyJustPressed(EKeys::MiddleMouseButton))
	{
		CloseContextMenu(); // The command wheel replaces it.
	}
	if (HandleDebugMouse() || HandleAudioMouse() || HandleCommandMouse() || HandleTacticalMapMouse() || HandleContextMenuMouse() || HandleMinimapMouse() || HandleLootMenuMouse() || HandleSquadBarMouse())
	{
		if (Grid)
		{
			Grid->SetHoveredCell(false);
		}
		return; // The cursor is on the loot menu, squad bar or minimap: nothing reaches the map.
	}

	FHitResult Hit;
	const bool bHit = GetHitResultUnderCursor(ECC_Visibility, false, Hit);
	ASoldier* HitSoldier = bHit ? Cast<ASoldier>(Hit.GetActor()) : nullptr;

	FIntPoint HoverCell = FIntPoint::ZeroValue;
	bool bValidCell = false;
	if (Grid && bHit)
	{
		bValidCell = Grid->WorldToCell(HitSoldier ? HitSoldier->GetActorLocation() : Hit.ImpactPoint, HoverCell);
	}
	if (Grid)
	{
		Grid->SetHoveredCell(bValidCell, HoverCell);
	}
	ASoldier* const ClickedSoldier = HitSoldier; // Includes corpses (they can be dragged).
	if (HitSoldier && HitSoldier->IsDead())
	{
		HitSoldier = nullptr; // Otherwise corpses don't block cells; treat as ground.
	}

	// Hovering an enemy: show each selected soldier's hit chance against it, cover included (GDD §9).
	if (HitSoldier && HitSoldier->Team != PlayerTeam && Selected.Num() > 0)
	{
		FString Preview;
		for (const ASoldier* Soldier : Selected)
		{
			if (Soldier && !Soldier->IsDead())
			{
				Preview += FString::Printf(TEXT("%s%d: %s"), Preview.IsEmpty() ? TEXT("") : TEXT("\n"),
					Soldier->GetSquadNumber(), *Soldier->GetHitChanceAgainst(HitSoldier).Describe());
			}
		}
		if (!Preview.IsEmpty())
		{
			HitSoldier->ShowHitPreview(Preview);
		}
	}

	// Right click opens the context menu on an injured ally (GDD §13); it never moves soldiers.
	if (WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		ASoldier* Target = ClickedSoldier;
		if (!Target && bValidCell && Grid)
		{
			// Injured soldiers lie flat and are a small target: the cell they are on counts too.
			for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
			{
				ASoldier* Soldier = Weak.Get();
				if (Soldier && Soldier->IsInjured() && Soldier->Team == PlayerTeam && Soldier->GetCurrentCell() == HoverCell)
				{
					Target = Soldier;
					break;
				}
			}
		}
		float MouseX = 0.f, MouseY = 0.f;
		if (Target && GetMousePosition(MouseX, MouseY))
		{
			OpenContextMenu(Target, FVector2D(MouseX, MouseY));
		}
		return;
	}

	const bool bLeft = WasInputKeyJustPressed(EKeys::LeftMouseButton);
	if (!bLeft)
	{
		return;
	}

	const bool bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	const bool bAlt = IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt);
	const bool bCtrl = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
	const bool bEnemy = HitSoldier && HitSoldier->Team != PlayerTeam;
	const bool bFriendly = HitSoldier && HitSoldier->Team == PlayerTeam;

	if (bLeft && bAlt)
	{
		if (bValidCell)
		{
			IssueGrenade(HoverCell);
		}
	}
	else if (bLeft && bShift)
	{
		if (bEnemy)
		{
			IssueAttack(HitSoldier);
		}
		else if (!HitSoldier && bValidCell)
		{
			IssueFace(HoverCell);
		}
	}
	else if (bCtrl)
	{
		// GDD §5.4: Ctrl + Left click a downed ally to start dragging it, or the dragged soldier to let go.
		if (bLeft && ClickedSoldier && ClickedSoldier->Team == PlayerTeam)
		{
			IssueDragToggle(ClickedSoldier);
		}
	}
	else if (bLeft && bFriendly)
	{
		SelectSoldier(HitSoldier);
	}
	else if (!HitSoldier && bValidCell)
	{
		IssueMove(HoverCell);
	}
}

// ------------------------------------------------------------------------------------------------ Orders

void ASquadPlayerController::IssueMove(FIntPoint Goal)
{
	PruneSelection();
	if (!Grid || Selected.Num() == 0)
	{
		return;
	}
	CancelPendingMoves();
	if (Selected.Num() == 1)
	{
		Selected[0]->MoveToCell(Goal);
		return;
	}
	// Group move keeps the current formation, anchored on the lowest-numbered living soldier in the
	// selection (Selected is sorted by number and never holds the dead): that soldier goes to the
	// clicked cell and everyone else keeps their offset from it. Offsets come from where each soldier
	// is heading (its current cell when idle). If a slot is blocked or off the grid, that soldier takes
	// the nearest free cell. A stunned anchor stays put but the others still form up on the click.
	const FIntPoint AnchorCell = Selected[0]->GetDestinationCell();
	TArray<ASoldier*> Movers;
	for (ASoldier* Soldier : Selected)
	{
		if (Soldier->CanAct())
		{
			Movers.Add(Soldier);
		}
	}
	if (Movers.Num() == 0)
	{
		return;
	}

	TArray<FIntPoint> Offsets;
	for (ASoldier* Soldier : Movers)
	{
		Offsets.Add(Soldier->GetDestinationCell() - AnchorCell);
	}

	// GDD §4.3: a group moving away from squad-mates left behind stops where the cohesion limit allows.
	const FIntPoint GroupGoal = ClampGroupGoal(Goal, AnchorCell, Movers, Offsets);
	if (GroupGoal != Goal)
	{
		Movers[0]->ShowMessage(TEXT("SQUAD LIMIT"));
	}

	TArray<FIntPoint> Slots;
	TMap<const ASoldier*, FIntPoint> Plan; // Where each mover is going, so they're judged against each other's new cells.
	for (int32 i = 0; i < Movers.Num(); ++i)
	{
		Slots.Add(GroupGoal + Offsets[i]);
		Plan.Add(Movers[i], Slots[i]);
	}

	for (int32 i = 0; i < Movers.Num(); ++i)
	{
		ASoldier* Soldier = Movers[i];
		const FIntPoint Slot = Slots[i];
		if (Grid->IsInBounds(Slot) && IsHeldBySquadMate(Slot, Soldier, Movers))
		{
			// A squad-mate is standing on (or heading to) this slot but is moving off it as part of
			// the same order: wait for it to clear rather than breaking formation.
			PendingMoves.Add({ Soldier, Slot, PendingMoveTimeout });
			continue;
		}
		FIntPoint Dest = Slot;
		if (!Grid->IsCellFreeAsDestination(Slot, Soldier) && !FindFallbackCell(Slot, Soldier, Slots, Dest))
		{
			continue;
		}
		Plan.Add(Soldier, Dest);
		Soldier->MoveToCellInGroup(Dest, Plan);
	}
}

FIntPoint ASquadPlayerController::ClampGroupGoal(FIntPoint Goal, FIntPoint AnchorCell, const TArray<ASoldier*>& Movers, const TArray<FIntPoint>& Offsets) const
{
	TMap<const ASoldier*, FIntPoint> Plan;
	auto Fits = [&](FIntPoint Candidate)
	{
		Plan.Reset();
		for (int32 i = 0; i < Movers.Num(); ++i)
		{
			Plan.Add(Movers[i], Candidate + Offsets[i]);
		}
		for (int32 i = 0; i < Movers.Num(); ++i)
		{
			if (!Movers[i]->IsCohesiveDestination(Candidate + Offsets[i], &Plan))
			{
				return false;
			}
		}
		return true;
	};
	if (Fits(Goal))
	{
		return Goal;
	}

	// Staying put (anchor on its own cell) always fits, so the answer is no further from Goal than that.
	const int32 MaxRadius = FMath::CeilToInt(FVector2D::Distance(FVector2D(Goal), FVector2D(AnchorCell))) + 1;
	FIntPoint Best = AnchorCell;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Radius = 1; Radius <= MaxRadius && Radius <= BestDistance; ++Radius)
	{
		for (int32 DY = -Radius; DY <= Radius; ++DY)
		{
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Radius)
				{
					continue;
				}
				const FIntPoint Candidate = Goal + FIntPoint(DX, DY);
				const float Distance = FVector2D::Distance(FVector2D(Candidate), FVector2D(Goal));
				if (Distance < BestDistance && Grid->IsInBounds(Candidate) && Fits(Candidate))
				{
					Best = Candidate;
					BestDistance = Distance;
				}
			}
		}
	}
	return Best;
}

bool ASquadPlayerController::IsHeldBySquadMate(FIntPoint Cell, const ASoldier* Soldier, const TArray<ASoldier*>& Movers) const
{
	for (const ASoldier* Mate : Movers)
	{
		if (Mate != Soldier && (Mate->OccupiesCell(Cell) || Mate->GetDestinationCell() == Cell))
		{
			return true;
		}
	}
	return false;
}

bool ASquadPlayerController::FindFallbackCell(FIntPoint Slot, const ASoldier* Soldier, const TArray<FIntPoint>& Reserved, FIntPoint& OutCell) const
{
	// Nearest free cell to the slot that isn't another squad-mate's slot.
	for (int32 Radius = 1; Radius <= 4; ++Radius)
	{
		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			for (int32 DY = -Radius; DY <= Radius; ++DY)
			{
				if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Radius)
				{
					continue;
				}
				const FIntPoint Candidate = Slot + FIntPoint(DX, DY);
				if (!Reserved.Contains(Candidate) && Grid->IsCellFreeAsDestination(Candidate, Soldier))
				{
					OutCell = Candidate;
					return true;
				}
			}
		}
	}
	return Grid->FindNearestFreeCell(Slot, Soldier, OutCell);
}

void ASquadPlayerController::UpdatePendingMoves(float DeltaTime)
{
	for (int32 i = PendingMoves.Num() - 1; i >= 0; --i)
	{
		FPendingMove& Pending = PendingMoves[i];
		ASoldier* Soldier = Pending.Soldier.Get();
		if (!Soldier || !Soldier->CanAct())
		{
			PendingMoves.RemoveAtSwap(i);
			continue;
		}
		if (Soldier->MoveToCell(Pending.Slot))
		{
			PendingMoves.RemoveAtSwap(i);
			continue;
		}
		Pending.TimeLeft -= DeltaTime;
		if (Pending.TimeLeft <= 0.f)
		{
			// The squad-mate never cleared the slot (blocked or re-ordered): settle for the nearest free cell.
			FIntPoint Dest;
			if (Grid && Grid->FindNearestFreeCell(Pending.Slot, Soldier, Dest))
			{
				Soldier->MoveToCell(Dest);
			}
			PendingMoves.RemoveAtSwap(i);
		}
	}
}

void ASquadPlayerController::CancelPendingMoves()
{
	// Any new order to the selection replaces a move that was still waiting for its slot.
	PendingMoves.RemoveAll([this](const FPendingMove& Pending)
	{
		return !Pending.Soldier.IsValid() || Selected.Contains(Pending.Soldier.Get());
	});
}

void ASquadPlayerController::IssueFace(FIntPoint Cell)
{
	CancelPendingMoves();
	for (ASoldier* Soldier : Selected)
	{
		Soldier->FaceCell(Cell);
	}
}

void ASquadPlayerController::IssueAttack(ASoldier* Target)
{
	CancelPendingMoves();
	for (ASoldier* Soldier : Selected)
	{
		Soldier->OrderAttack(Target);
	}
}

void ASquadPlayerController::IssueGrenade(FIntPoint Cell)
{
	if (!Grid)
	{
		return;
	}
	// Every selected soldier throws one grenade at the cell (each reports its own "NO GRENADES" / "TOO FAR").
	for (ASoldier* Soldier : Selected)
	{
		Soldier->ThrowGrenadeAt(Cell);
	}
}

void ASquadPlayerController::IssueDragToggle(ASoldier* Target)
{
	// Clicking the soldier being dragged releases it.
	if (ASoldier* Dragger = Target->GetDraggedBy())
	{
		Dragger->StopDragging();
		return;
	}
	if (!Target->IsDowned())
	{
		return;
	}

	// Otherwise the closest selected soldier able to drag it takes hold.
	TArray<ASoldier*> Candidates = GetSelectedSoldiers();
	Candidates.Remove(Target);
	if (Candidates.Num() == 0)
	{
		return;
	}
	const FVector TargetLocation = Target->GetActorLocation();
	Candidates.Sort([&TargetLocation](const ASoldier& A, const ASoldier& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), TargetLocation) < FVector::DistSquared2D(B.GetActorLocation(), TargetLocation);
	});
	for (ASoldier* Soldier : Candidates)
	{
		if (Soldier->CanStartDragging(Target))
		{
			CancelPendingMoves();
			Soldier->StartDragging(Target);
			return;
		}
	}
	// Nobody can: let the closest one say why ("TOO FAR TO DRAG" etc.).
	Candidates[0]->StartDragging(Target);
}

// ------------------------------------------------------------------------------------------------ Context menu

bool ASquadPlayerController::OpenContextMenu(ASoldier* Target, FVector2D ScreenPosition)
{
	CloseContextMenu();
	// For now only injured allies have anything on the menu (GDD §5.2: Use medkit).
	if (!Target || Target->Team != PlayerTeam || !Target->IsInjured() || bTacticalMapOpen)
	{
		return false;
	}
	ContextMenu.bOpen = true;
	ContextMenu.ScreenPosition = ScreenPosition;
	ContextMenu.Target = Target;
	RefreshContextMenu();
	return ContextMenu.bOpen;
}

void ASquadPlayerController::CloseContextMenu()
{
	ContextMenu = FContextMenu();
}

void ASquadPlayerController::RefreshContextMenu()
{
	if (!ContextMenu.bOpen)
	{
		return;
	}
	ASoldier* Target = ContextMenu.Target.Get();
	if (!Target || !Target->IsInjured())
	{
		CloseContextMenu(); // Treated, killed or gone: nothing left to offer.
		return;
	}

	ContextMenu.Items.Reset();
	FContextMenuItem& Medkit = ContextMenu.Items.AddDefaulted_GetRef();
	Medkit.Action = EContextAction::UseMedkit;
	Medkit.Label = TEXT("Use medkit");
	if (const ASoldier* CurrentMedic = Target->GetTreatedBy())
	{
		Medkit.Detail = FString::Printf(TEXT("Soldier %d is on it"), CurrentMedic->GetSquadNumber());
	}
	else
	{
		FString Reason;
		if (const ASoldier* Medic = FindMedicFor(Target, &Reason))
		{
			Medkit.bEnabled = true;
			const float Seconds = Medic->GetSoldierData() ? Medic->GetSoldierData()->MedkitChannelTime : 0.f;
			Medkit.Detail = Medic->IsInMedkitReach(Target)
				? FString::Printf(TEXT("Soldier %d treats  (%.0f s)"), Medic->GetSquadNumber(), Seconds)
				: FString::Printf(TEXT("Soldier %d goes over  (%.0f s)"), Medic->GetSquadNumber(), Seconds);
		}
		else
		{
			Medkit.Detail = Reason;
		}
	}
}

ASoldier* ASquadPlayerController::FindMedicFor(const ASoldier* Patient, FString* OutReason) const
{
	if (!Patient)
	{
		return nullptr;
	}
	auto Nearest = [Patient](const TArray<ASoldier*>& Candidates, FString& OutFirstReason) -> ASoldier*
	{
		ASoldier* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (ASoldier* Soldier : Candidates)
		{
			if (!Soldier || Soldier == Patient || Soldier->IsDead())
			{
				continue;
			}
			const FString Reason = Soldier->GetMedkitBlockReason(Patient);
			if (!Reason.IsEmpty())
			{
				if (OutFirstReason.IsEmpty())
				{
					OutFirstReason = FString::Printf(TEXT("Soldier %d: %s"), Soldier->GetSquadNumber(), *Reason.ToLower());
				}
				continue;
			}
			const float Distance = FVector::DistSquared2D(Soldier->GetActorLocation(), Patient->GetActorLocation());
			if (Distance < BestDistance)
			{
				Best = Soldier;
				BestDistance = Distance;
			}
		}
		return Best;
	};

	// The selected soldiers first: that is who the player is commanding. Otherwise anyone in the squad.
	FString SelectedReason;
	if (ASoldier* Medic = Nearest(GetSelectedSoldiers(), SelectedReason))
	{
		return Medic;
	}
	FString SquadReason;
	TArray<ASoldier*> SquadList;
	for (ASoldier* Soldier : Squad)
	{
		SquadList.Add(Soldier);
	}
	if (ASoldier* Medic = Nearest(SquadList, SquadReason))
	{
		return Medic;
	}
	if (OutReason)
	{
		*OutReason = !SelectedReason.IsEmpty() ? SelectedReason : !SquadReason.IsEmpty() ? SquadReason : FString(TEXT("Nobody can treat"));
		// Everyone out of medkits reads better as one line than one soldier's reason.
		bool bAllOut = true;
		for (const ASoldier* Soldier : Squad)
		{
			if (Soldier && Soldier != Patient && !Soldier->IsDead() && Soldier->GetMedkits() > 0)
			{
				bAllOut = false;
			}
		}
		if (bAllOut)
		{
			*OutReason = TEXT("No medkits left in the squad");
		}
	}
	return nullptr;
}

bool ASquadPlayerController::OrderMedkitOn(ASoldier* Patient)
{
	ASoldier* Medic = FindMedicFor(Patient);
	if (!Medic)
	{
		return false;
	}
	// Once soldiers replicate this becomes a Server RPC; the soldier re-checks everything before acting.
	PendingMoves.RemoveAll([Medic](const FPendingMove& Pending) { return Pending.Soldier.Get() == Medic; });
	return Medic->OrderUseMedkit(Patient);
}

void ASquadPlayerController::ExecuteContextAction(EContextAction Action, ASoldier* Target)
{
	switch (Action)
	{
	case EContextAction::UseMedkit:
		OrderMedkitOn(Target);
		break;
	}
}

bool ASquadPlayerController::HandleContextMenuMouse()
{
	if (!ContextMenu.bOpen)
	{
		return false;
	}
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	const FVector2D Mouse(MouseX, MouseY);
	const bool bLeft = WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bRight = WasInputKeyJustPressed(EKeys::RightMouseButton);
	if (SquadHUD->IsOverContextMenu(Mouse))
	{
		if (bLeft)
		{
			const int32 Index = SquadHUD->GetContextMenuItemAt(Mouse);
			if (ContextMenu.Items.IsValidIndex(Index) && ContextMenu.Items[Index].bEnabled)
			{
				const EContextAction Action = ContextMenu.Items[Index].Action;
				ASoldier* Target = ContextMenu.Target.Get();
				CloseContextMenu();
				ExecuteContextAction(Action, Target);
			}
		}
		return true; // Nothing under the menu reacts.
	}
	if (bLeft)
	{
		CloseContextMenu();
		return true; // A click off the menu only closes it.
	}
	if (bRight)
	{
		CloseContextMenu(); // Let the right click through: it may open the menu on another soldier.
	}
	return false;
}

void ASquadPlayerController::IssueStanceCycle()
{
	// Everyone selected moves to the same next stance, based on the first soldier able to change.
	for (ASoldier* Leader : Selected)
	{
		if (Leader->CanAct() && !Leader->IsInjured())
		{
			const ESoldierStance Next = Leader->GetNextStance();
			for (ASoldier* Soldier : Selected)
			{
				Soldier->OrderStance(Next);
			}
			return;
		}
	}
}

void ASquadPlayerController::IssueHoldFireToggle()
{
	// Mixed selection: everyone holds. Only when all of them already hold does H release them.
	bool bAllHolding = true;
	bool bAnyAlive = false;
	for (const ASoldier* Soldier : Selected)
	{
		if (!Soldier->IsDead())
		{
			bAnyAlive = true;
			bAllHolding &= Soldier->IsHoldingFire();
		}
	}
	if (!bAnyAlive)
	{
		return;
	}
	for (ASoldier* Soldier : Selected)
	{
		Soldier->OrderHoldFire(!bAllHolding);
	}
}

// ------------------------------------------------------------------------------------------------ Camera

void ASquadPlayerController::HandleCamera(float DeltaTime)
{
	ASquadCameraPawn* Cam = Cast<ASquadCameraPawn>(GetPawn());
	if (!Cam)
	{
		return;
	}

	FVector2D PanInput = FVector2D::ZeroVector;
	if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up))    { PanInput.X += 1.f; }
	if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down))  { PanInput.X -= 1.f; }
	if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right)) { PanInput.Y += 1.f; }
	if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left))  { PanInput.Y -= 1.f; }
	Cam->Pan(PanInput, DeltaTime);

	float RotateInput = 0.f;
	if (IsInputKeyDown(EKeys::Q)) { RotateInput -= 1.f; }
	if (IsInputKeyDown(EKeys::E)) { RotateInput += 1.f; }
	Cam->Rotate(RotateInput, DeltaTime);

	float Wheel = 0.f;
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp))   { Wheel += 1.f; }
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown)) { Wheel -= 1.f; }
	Cam->Zoom(Wheel);
}

// ------------------------------------------------------------------------------------------------ Tactical map (GDD §13.1)

namespace
{
	/** Freehand routes record a point each time the cursor has moved this far on the ground (cm). */
	constexpr float RouteRecordStep = 50.f;
}

ASquadPlayerState* ASquadPlayerController::GetSquadPlayerState() const
{
	return GetPlayerState<ASquadPlayerState>();
}

ATacticalPlan* ASquadPlayerController::GetTacticalPlan() const
{
	return ATacticalPlan::FindForTeam(this, PlayerTeam);
}

void ASquadPlayerController::SetTacticalMapOpen(bool bOpen)
{
	bTacticalMapOpen = bOpen;
	PlanDraft = FPlanDraft();
	CancelCommand();
	CloseContextMenu();
	bTacticalMapPanning = false;
	bMinimapDragging = false;
	if (bOpen && Grid)
	{
		Grid->SetHoveredCell(false);
	}
}

void ASquadPlayerController::SetTacticalTool(ETacticalTool Tool)
{
	const ASquadPlayerState* PS = GetSquadPlayerState();
	TacticalTool = (PS && PS->CanPlan()) ? Tool : ETacticalTool::View;
	PlanDraft = FPlanDraft();
}

bool ASquadPlayerController::HandleTacticalMapMouse()
{
	if (!bTacticalMapOpen)
	{
		PlanDraft = FPlanDraft();
		bTacticalMapPanning = false;
		return false;
	}
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		return true;
	}
	const FVector2D Mouse(MouseX, MouseY);
	const bool bLeftDown = IsInputKeyDown(EKeys::LeftMouseButton);
	const bool bLeftPressed = WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bRightPressed = WasInputKeyJustPressed(EKeys::RightMouseButton);

	ASquadPlayerState* PS = GetSquadPlayerState();
	const bool bCanPlan = PS && PS->CanPlan();
	if (!bCanPlan && TacticalTool != ETacticalTool::View)
	{
		TacticalTool = ETacticalTool::View; // Planning rights were taken away.
		PlanDraft = FPlanDraft();
	}

	auto MoveCamera = [this](const FVector2D& World)
	{
		if (APawn* Cam = GetPawn())
		{
			Cam->SetActorLocation(FVector(World.X, World.Y, Cam->GetActorLocation().Z));
		}
	};

	// ---- A stroke being drawn: follows the cursor (clamped to the map) until the button is let go.
	if (PlanDraft.bActive)
	{
		FVector2D World;
		if (SquadHUD->TacticalMapToWorld(Mouse, World, true))
		{
			if (PlanDraft.Type == ETacticalMarkerType::Route && FVector2D::Distance(PlanDraft.Points.Last(), World) >= RouteRecordStep)
			{
				PlanDraft.Points.Add(World);
			}
			PlanDraft.End = World;
		}
		if (bRightPressed)
		{
			PlanDraft = FPlanDraft(); // Cancel.
		}
		else if (!bLeftDown)
		{
			FinishPlanDraft(PlanDraft);
		}
		return true;
	}
	if (bTacticalMapPanning)
	{
		FVector2D World;
		if (!bLeftDown)
		{
			bTacticalMapPanning = false;
		}
		else if (SquadHUD->TacticalMapToWorld(Mouse, World, true))
		{
			MoveCamera(World);
		}
		return true;
	}

	if (bLeftPressed)
	{
		// ---- Side panel buttons.
		ASquadHUD::FTacMapButton Button;
		if (SquadHUD->GetTacticalButtonAt(Mouse, Button))
		{
			switch (Button.Action)
			{
			case ASquadHUD::ETacMapAction::Close: SetTacticalMapOpen(false); break;
			case ASquadHUD::ETacMapAction::SelectTool: SetTacticalTool(Button.Tool); break;
			case ASquadHUD::ETacMapAction::ClearPlan: CallServer([this]() { ServerClearTacticalMarkers(); }); break;
			case ASquadHUD::ETacMapAction::RequestOrders: CallServer([this]() { ServerRequestOrders(); }); break;
			case ASquadHUD::ETacMapAction::Acknowledge: CallServer([this, Id = Button.Param]() { ServerAcknowledgeOrderRequest(Id); }); break;
			case ASquadHUD::ETacMapAction::FocusRequest: MoveCamera(Button.Location); break;
			case ASquadHUD::ETacMapAction::ToggleDelegate:
				if (ASquadPlayerState* Target = Button.Player.Get())
				{
					CallServer([this, Target = TObjectPtr<ASquadPlayerState>(Target), bDelegate = !Target->bPlanningDelegated]() { ServerSetPlanningDelegated(Target, bDelegate); });
				}
				break;
			}
			return true;
		}

		// ---- The map itself.
		FVector2D World;
		if (SquadHUD->TacticalMapToWorld(Mouse, World, false))
		{
			ETacticalMarkerType Type;
			if (TacticalTool == ETacticalTool::Erase)
			{
				const int32 Id = SquadHUD->FindTacticalMarkerAt(Mouse);
				if (Id != INDEX_NONE)
				{
					CallServer([=, this]() { ServerRemoveTacticalMarker(Id); });
				}
			}
			else if (TacticalPlan::ToolMarker(TacticalTool, Type))
			{
				if (Type == ETacticalMarkerType::Push || Type == ETacticalMarkerType::HoldLine || Type == ETacticalMarkerType::Route)
				{
					PlanDraft.bActive = true;
					PlanDraft.Type = Type;
					PlanDraft.Start = PlanDraft.End = World;
					PlanDraft.Points = { World };
				}
				else
				{
					FTacticalMarker Marker;
					Marker.Type = Type;
					Marker.Start = FVector2f(World);
					CallServer([=, this]() { ServerAddTacticalMarker(Marker); });
				}
			}
			else
			{
				bTacticalMapPanning = true;
				MoveCamera(World);
			}
		}
	}
	else if (bRightPressed && bCanPlan)
	{
		const int32 Id = SquadHUD->FindTacticalMarkerAt(Mouse);
		if (Id != INDEX_NONE)
		{
			CallServer([=, this]() { ServerRemoveTacticalMarker(Id); });
		}
	}
	return true;
}

void ASquadPlayerController::FinishPlanDraft(FPlanDraft& Draft)
{
	FTacticalMarker Marker;
	Marker.Type = Draft.Type;
	Marker.Start = FVector2f(Draft.Start);
	Marker.End = FVector2f(Draft.End);
	if (Draft.Type == ETacticalMarkerType::Route)
	{
		if (Draft.Points.Num() == 0 || FVector2D::Distance(Draft.Points.Last(), Draft.End) > 1.f)
		{
			Draft.Points.Add(Draft.End);
		}
		for (const FVector2D& P : Draft.Points)
		{
			Marker.Points.Add(FVector2f(P));
		}
	}
	Draft = FPlanDraft();
	CallServer([=, this]() { ServerAddTacticalMarker(Marker); }); // The server drops strokes that are too short.
}

// ------------------------------------------------------------------------------------------------ Command wheel (GDD §13.2)

TArray<ERadialAction> ASquadPlayerController::GetRadialActions() const
{
	// Spot sits at the top of the wheel, the rest run clockwise.
	TArray<ERadialAction> Items = { ERadialAction::Spot, ERadialAction::Ping };
	const ASquadPlayerState* PS = GetSquadPlayerState();
	if (PS && PS->CanPlan())
	{
		Items.Append({ ERadialAction::Push, ERadialAction::HoldLine, ERadialAction::Route, ERadialAction::Defend,
			ERadialAction::Attack, ERadialAction::Rally, ERadialAction::Erase });
	}
	// GDD §13.3: everyone gets Build, which opens the Build sub-wheel (always a sub-wheel, so it looks and works the
	// same whether the main wheel is short or a planner's full one).
	Items.Add(ERadialAction::Build);
	if (!PS || !PS->HasCommandRole())
	{
		Items.Add(ERadialAction::RequestOrders);
	}
	return Items;
}

bool ASquadPlayerController::GetCursorWorld2D(FVector2D& OutWorld) const
{
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	const FVector2D Mouse(MouseX, MouseY);
	if (bTacticalMapOpen)
	{
		return SquadHUD->TacticalMapToWorld(Mouse, OutWorld, false);
	}
	if (SquadHUD->IsOverMinimap(Mouse))
	{
		FVector World;
		if (!SquadHUD->IsOverMinimapExpandButton(Mouse) && SquadHUD->MinimapToWorld(Mouse, World))
		{
			OutWorld = FVector2D(World);
			return true;
		}
		return false;
	}
	if (SquadHUD->IsOverSquadBar(Mouse) || (bLootMenuOpen && SquadHUD->IsOverLootMenu(Mouse)) || SquadHUD->IsOverDebugMenu(Mouse)
		|| SquadHUD->IsOverAudioControls(Mouse))
	{
		return false;
	}
	// The battlefield: where the cursor's ray meets the ground plane, if that is on the map.
	FVector Origin, Dir;
	if (!DeprojectScreenPositionToWorld(MouseX, MouseY, Origin, Dir) || Dir.Z > -KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float GroundZ = Grid ? Grid->GetActorLocation().Z : 0.f;
	const FVector Ground = Origin + Dir * ((GroundZ - Origin.Z) / Dir.Z);
	FIntPoint Cell;
	if (Grid && !Grid->WorldToCell(Ground, Cell))
	{
		return false;
	}
	OutWorld = FVector2D(Ground);
	return true;
}

void ASquadPlayerController::CancelCommand()
{
	CommandMode = ECommandMode::None;
	CommandDraft = FPlanDraft();
	Radial.Hover = INDEX_NONE;
	Radial.bBuildMenu = false;
	Radial.BuildItems.Reset();
}

bool ASquadPlayerController::HandleCommandMouse()
{
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		if (CommandMode == ECommandMode::Radial)
		{
			CancelCommand();
		}
		return CommandMode != ECommandMode::None;
	}
	const FVector2D Mouse(MouseX, MouseY);
	const bool bMidPressed = WasInputKeyJustPressed(EKeys::MiddleMouseButton);
	const bool bMidDown = IsInputKeyDown(EKeys::MiddleMouseButton);
	const bool bLeftPressed = WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bRightPressed = WasInputKeyJustPressed(EKeys::RightMouseButton);
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;

	// Planning rights taken away mid-stroke: drop the line / erase mode (the server would refuse them anyway).
	if (CommandMode == ECommandMode::Drawing || CommandMode == ECommandMode::Erasing)
	{
		const ASquadPlayerState* PS = GetSquadPlayerState();
		if (!PS || !PS->CanPlan())
		{
			CancelCommand();
			return false;
		}
	}

	switch (CommandMode)
	{
	case ECommandMode::None:
	{
		FVector2D World;
		if (bMidPressed && !PlanDraft.bActive && !bTacticalMapPanning && !bMinimapDragging && GetCursorWorld2D(World))
		{
			Radial.ScreenCenter = Mouse;
			Radial.WorldOrigin = World;
			Radial.Items = GetRadialActions();
			Radial.Hover = INDEX_NONE;
			Radial.OpenTime = Now;
			CommandMode = ECommandMode::Radial;
			return true;
		}
		return false;
	}
	case ECommandMode::Radial:
	{
		if (Radial.bBuildMenu)
		{
			// GDD §13.3: the Build sub-wheel stays open after the middle mouse is let go. Left click (or a middle press
			// and release) over an entry chooses it; in the centre it just closes; right click / Esc cancels.
			Radial.BuildItems = GetBuildWheelItems(Radial.WorldOrigin);
			Radial.Hover = SquadHUD->GetRadialItemAt(Radial.ScreenCenter, Mouse, Radial.BuildItems.Num());
			if (bRightPressed)
			{
				CancelCommand();
			}
			else if (bLeftPressed || WasInputKeyJustReleased(EKeys::MiddleMouseButton))
			{
				const FVector2D Origin = Radial.WorldOrigin;
				const bool bValid = Radial.BuildItems.IsValidIndex(Radial.Hover);
				const FBuildWheelItem Item = bValid ? Radial.BuildItems[Radial.Hover] : FBuildWheelItem();
				CancelCommand();
				if (bValid)
				{
					if (Item.bEnabled)
					{
						OrderBuildSelected(Item.Kind, Origin);
					}
					else
					{
						ShowBuildFeedback(Item.Detail, Origin);
					}
				}
			}
			return true;
		}
		Radial.Hover = SquadHUD->GetRadialItemAt(Radial.ScreenCenter, Mouse, Radial.Items.Num());
		if (bRightPressed)
		{
			CancelCommand();
		}
		else if (!bMidDown || bLeftPressed)
		{
			// Let go over a slice (or click one) to choose it; a quick tap without choosing is a Ping.
			const int32 Hover = Radial.Hover;
			const bool bTap = Hover == INDEX_NONE && Now - Radial.OpenTime <= QuickTapTime;
			CancelCommand();
			if (Radial.Items.IsValidIndex(Hover))
			{
				ExecuteRadialAction(Radial.Items[Hover], Radial.WorldOrigin);
			}
			else if (bTap)
			{
				ExecuteRadialAction(ERadialAction::Ping, Radial.WorldOrigin);
			}
		}
		return true;
	}
	case ECommandMode::Drawing:
	{
		FVector2D World;
		if (GetCursorWorld2D(World))
		{
			if (CommandDraft.Type == ETacticalMarkerType::Route && FVector2D::Distance(CommandDraft.Points.Last(), World) >= RouteRecordStep)
			{
				CommandDraft.Points.Add(World);
			}
			CommandDraft.End = World;
		}
		if (bRightPressed || bMidPressed)
		{
			CancelCommand();
		}
		else if (bLeftPressed)
		{
			FinishPlanDraft(CommandDraft);
			CancelCommand();
		}
		return true;
	}
	case ECommandMode::Erasing:
	{
		if (bRightPressed || bMidPressed)
		{
			CancelCommand();
		}
		else if (bLeftPressed)
		{
			const int32 Id = bTacticalMapOpen ? SquadHUD->FindTacticalMarkerAt(Mouse) : SquadHUD->FindWorldMarkerAt(Mouse);
			if (Id != INDEX_NONE)
			{
				CallServer([=, this]() { ServerRemoveTacticalMarker(Id); });
			}
		}
		return true;
	}
	}
	return false;
}

void ASquadPlayerController::ExecuteRadialAction(ERadialAction Action, const FVector2D& Origin)
{
	// Plan tools are for the CO, XO and delegated planners only (GDD §13.1); everyone may Spot, Ping and request
	// orders. The wheel only offers them to planners, but rights can change while it is open.
	ETacticalMarkerType Type;
	const ASquadPlayerState* PS = GetSquadPlayerState();
	if ((TacticalPlan::RadialMarker(Action, Type) || Action == ERadialAction::Erase) && (!PS || !PS->CanPlan()))
	{
		return;
	}
	switch (Action)
	{
	case ERadialAction::Spot:
	{
		// Snap onto the nearest enemy the team can see near the point.
		FVector2D Where = Origin;
		float Best = SpotSnapDistance;
		if (Grid)
		{
			for (const TWeakObjectPtr<ASoldier>& Weak : Grid->GetSoldiers())
			{
				const ASoldier* Soldier = Weak.Get();
				if (Soldier && Soldier->Team != PlayerTeam && !Soldier->IsDead() && !Soldier->IsHidden())
				{
					const float Dist = FVector2D::Distance(FVector2D(Soldier->GetActorLocation()), Origin);
					if (Dist < Best)
					{
						Best = Dist;
						Where = FVector2D(Soldier->GetActorLocation());
					}
				}
			}
		}
		CallServer([=, this]() { ServerAddPing(ETacticalPingType::Spot, Where); });
		break;
	}
	case ERadialAction::Ping:
		CallServer([=, this]() { ServerAddPing(ETacticalPingType::Ping, Origin); });
		break;
	case ERadialAction::RequestOrders:
		CallServer([=, this]() { ServerRequestOrders(); });
		break;
	case ERadialAction::Erase:
		CommandMode = ECommandMode::Erasing;
		break;
	case ERadialAction::Build:
		// GDD §13.3: reopen in place as the Build sub-wheel (same centre and ground point).
		Radial.bBuildMenu = true;
		Radial.Hover = INDEX_NONE;
		Radial.BuildItems = GetBuildWheelItems(Origin);
		CommandMode = ECommandMode::Radial;
		break;
	default:
		if (TacticalPlan::RadialMarker(Action, Type))
		{
			if (Type == ETacticalMarkerType::Push || Type == ETacticalMarkerType::HoldLine || Type == ETacticalMarkerType::Route)
			{
				// Lines start where the wheel was opened and follow the cursor until a left click places them.
				CommandDraft = FPlanDraft();
				CommandDraft.bActive = true;
				CommandDraft.Type = Type;
				CommandDraft.Start = CommandDraft.End = Origin;
				CommandDraft.Points = { Origin };
				CommandMode = ECommandMode::Drawing;
			}
			else
			{
				FTacticalMarker Marker;
				Marker.Type = Type;
				Marker.Start = FVector2f(Origin);
				CallServer([=, this]() { ServerAddTacticalMarker(Marker); });
			}
		}
		break;
	}
}

void ASquadPlayerController::ServerAddPing_Implementation(ETacticalPingType Type, FVector2D Location)
{
	if (ATacticalPlan* Plan = GetTacticalPlan())
	{
		Plan->AddPing(GetSquadPlayerState(), Type, Location);
	}
}

int32 ASquadPlayerController::GetPingCount() const
{
	const ATacticalPlan* Plan = GetTacticalPlan();
	return Plan ? Plan->GetPings().Num() : -1;
}

void ASquadPlayerController::ServerAddTacticalMarker_Implementation(const FTacticalMarker& Marker)
{
	if (ATacticalPlan* Plan = GetTacticalPlan())
	{
		Plan->AddMarker(GetSquadPlayerState(), Marker);
	}
}

void ASquadPlayerController::ServerRemoveTacticalMarker_Implementation(int32 MarkerId)
{
	if (ATacticalPlan* Plan = GetTacticalPlan())
	{
		Plan->RemoveMarker(GetSquadPlayerState(), MarkerId);
	}
}

void ASquadPlayerController::ServerClearTacticalMarkers_Implementation()
{
	if (ATacticalPlan* Plan = GetTacticalPlan())
	{
		Plan->ClearMarkers(GetSquadPlayerState());
	}
}

void ASquadPlayerController::ServerRequestOrders_Implementation()
{
	ATacticalPlan* Plan = GetTacticalPlan();
	if (!Plan)
	{
		return;
	}
	// Mark the request where the player's squad is (centre of its living soldiers), else where they're looking.
	FVector2D Sum = FVector2D::ZeroVector;
	int32 Count = 0;
	for (const ASoldier* Soldier : Squad)
	{
		if (Soldier && !Soldier->IsDead())
		{
			Sum += FVector2D(Soldier->GetActorLocation());
			++Count;
		}
	}
	FVector2D Where = Count > 0 ? Sum / Count : (GetPawn() ? FVector2D(GetPawn()->GetActorLocation()) : FVector2D::ZeroVector);
	Plan->AddRequest(GetSquadPlayerState(), Where);
}

void ASquadPlayerController::ServerAcknowledgeOrderRequest_Implementation(int32 RequestId)
{
	if (ATacticalPlan* Plan = GetTacticalPlan())
	{
		Plan->AcknowledgeRequest(GetSquadPlayerState(), RequestId);
	}
}

void ASquadPlayerController::ServerSetPlanningDelegated_Implementation(ASquadPlayerState* Target, bool bDelegated)
{
	const ASquadPlayerState* PS = GetSquadPlayerState();
	if (!PS || !Target || Target == PS || !PS->HasCommandRole() || Target->Team != PS->Team || Target->HasCommandRole())
	{
		return;
	}
	Target->bPlanningDelegated = bDelegated;
	Target->TacticalRole = bDelegated ? ETacticalRole::Planner : ETacticalRole::Soldier; // The game mode keeps it in step from here.
	Target->ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("%s %s %s planning rights %s %s."), TacticalPlan::RoleTag(PS->TacticalRole), *PS->GetPlayerName(),
		bDelegated ? TEXT("gave") : TEXT("took back"), bDelegated ? TEXT("to") : TEXT("from"), *Target->GetPlayerName());
}

void ASquadPlayerController::AddTacticalMarker(ETacticalMarkerType Type, FVector2D Start, FVector2D End, const TArray<FVector2D>& RoutePoints)
{
	FTacticalMarker Marker;
	Marker.Type = Type;
	Marker.Start = FVector2f(Start);
	Marker.End = FVector2f(End);
	for (const FVector2D& P : RoutePoints)
	{
		Marker.Points.Add(FVector2f(P));
	}
	CallServer([=, this]() { ServerAddTacticalMarker(Marker); });
}

int32 ASquadPlayerController::GetTacticalMarkerCount() const
{
	const ATacticalPlan* Plan = GetTacticalPlan();
	return Plan ? Plan->GetMarkers().Num() : -1;
}

int32 ASquadPlayerController::GetOpenOrderRequestCount() const
{
	const ATacticalPlan* Plan = GetTacticalPlan();
	if (!Plan)
	{
		return -1;
	}
	int32 Open = 0;
	for (const FTacticalOrderRequest& R : Plan->GetRequests())
	{
		Open += R.bAcknowledged ? 0 : 1;
	}
	return Open;
}
