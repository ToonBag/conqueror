// In-game debug menu (F6, non-shipping builds). The controller builds the rows every frame while the menu is
// open; ASquadHUD draws them top left (SquadHUDDebug.cpp) and reports which button is under the cursor.
// Everything that changes gameplay goes through ServerDebugAction, so it stays server-authoritative.

#include "SquadPlayerController.h"

#include "GridManager.h"
#include "Soldier.h"
#include "SquadHUD.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogSquadDebug, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarAllowDebugMenu(
		TEXT("squad.AllowDebugMenu"), 0,
		TEXT("1: a dedicated server running outside the editor accepts debug-menu actions from clients (non-shipping builds only)."),
		ECVF_Default);

	/** Stun given by the debug menu: the GDD §7.1 grenade stun (2.5 m ring). */
	constexpr float DebugStunSeconds = 5.f;

	const TCHAR* CVarNameFor(ESquadDebugAction Action)
	{
		switch (Action)
		{
		case ESquadDebugAction::RevealAll: return TEXT("squad.RevealAll");
		case ESquadDebugAction::Bot:       return TEXT("squad.Bot");
		case ESquadDebugAction::BotDebug:  return TEXT("squad.BotDebug");
		case ESquadDebugAction::GodMode:   return TEXT("squad.GodMode");
		case ESquadDebugAction::VPWinTime: return TEXT("squad.VPWinTime");
		default:                           return nullptr;
		}
	}

	float GetCVarFloat(const TCHAR* Name)
	{
		const IConsoleVariable* Var = Name ? IConsoleManager::Get().FindConsoleVariable(Name) : nullptr;
		return Var ? Var->GetFloat() : 0.f;
	}

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = Name ? IConsoleManager::Get().FindConsoleVariable(Name) : nullptr)
		{
			Var->Set(*FString::Printf(TEXT("%g"), Value), ECVF_SetByConsole);
		}
	}

	const TCHAR* NetModeName(ENetMode Mode)
	{
		switch (Mode)
		{
		case NM_Standalone:      return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("Dedicated server");
		case NM_ListenServer:    return TEXT("Listen server");
		case NM_Client:          return TEXT("Client");
		default:                 return TEXT("?");
		}
	}

	const TCHAR* HealthName(const ASoldier& Soldier)
	{
		switch (Soldier.GetHealthState())
		{
		case ESoldierHealth::Active:  return TEXT("Active");
		case ESoldierHealth::Stunned: return TEXT("Stunned");
		case ESoldierHealth::Injured: return TEXT("Injured");
		default:                      return TEXT("KIA");
		}
	}
}

FString ASquadPlayerController::GetDebugClickToolName(EDebugClickTool Tool)
{
	switch (Tool)
	{
	case EDebugClickTool::Teleport: return TEXT("Teleport");
	case EDebugClickTool::Heal:     return TEXT("Heal");
	case EDebugClickTool::Injure:   return TEXT("Injure");
	case EDebugClickTool::Stun:     return TEXT("Stun");
	case EDebugClickTool::Kill:     return TEXT("Kill");
	default:                        return FString();
	}
}

void ASquadPlayerController::SetDebugMenuOpen(bool bOpen)
{
#if UE_BUILD_SHIPPING
	bOpen = false;
#endif
	bDebugMenuOpen = bOpen;
	RebuildDebugMenu();
}

void ASquadPlayerController::RebuildDebugMenu()
{
	DebugRows.Reset();
#if !UE_BUILD_SHIPPING
	if (!bDebugMenuOpen)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const int32 NumSelected = Selected.Num();

	auto Header = [this](const FString& Label)
	{
		FDebugMenuRow& Row = DebugRows.AddDefaulted_GetRef();
		Row.Label = Label;
		Row.bHeader = true;
	};
	auto AddRow = [this](const FString& Label) -> FDebugMenuRow&
	{
		FDebugMenuRow& Row = DebugRows.AddDefaulted_GetRef();
		Row.Label = Label;
		return Row;
	};
	auto Button = [](FDebugMenuRow& Row, const FString& Label, ESquadDebugAction Action, float Param = 0.f, bool bActive = false, bool bEnabled = true)
	{
		FDebugMenuButton& B = Row.Buttons.AddDefaulted_GetRef();
		B.Label = Label;
		B.Action = Action;
		B.Param = Param;
		B.bActive = bActive;
		B.bEnabled = bEnabled;
	};
	auto ToolButton = [this](FDebugMenuRow& Row, EDebugClickTool Tool)
	{
		FDebugMenuButton& B = Row.Buttons.AddDefaulted_GetRef();
		B.Label = GetDebugClickToolName(Tool);
		B.Tool = Tool;
		B.bActive = DebugClickTool == Tool;
	};
	auto OnOff = [&](const FString& Label, ESquadDebugAction Action)
	{
		const bool bOn = GetCVarFloat(CVarNameFor(Action)) != 0.f;
		FDebugMenuRow& Row = AddRow(Label);
		Button(Row, TEXT("On"), Action, 1.f, bOn);
		Button(Row, TEXT("Off"), Action, 0.f, !bOn);
	};

	Header(TEXT("WORLD"));
	OnOff(TEXT("Reveal all"), ESquadDebugAction::RevealAll);
	OnOff(TEXT("Bot AI"), ESquadDebugAction::Bot);
	OnOff(TEXT("Bot debug view"), ESquadDebugAction::BotDebug);
	OnOff(TEXT("God mode (players)"), ESquadDebugAction::GodMode);
	{
		const float Dilation = World && World->GetWorldSettings() ? World->GetWorldSettings()->TimeDilation : 1.f;
		FDebugMenuRow& Row = AddRow(TEXT("Game speed"));
		for (const float Speed : { 0.25f, 0.5f, 1.f, 2.f, 4.f })
		{
			Button(Row, FString::Printf(TEXT("%gx"), Speed), ESquadDebugAction::GameSpeed, Speed, FMath::IsNearlyEqual(Dilation, Speed, 0.01f));
		}
	}
	{
		const bool bPaused = World && World->IsPaused();
		FDebugMenuRow& Row = AddRow(TEXT("Pause"));
		Button(Row, TEXT("On"), ESquadDebugAction::Pause, 1.f, bPaused);
		Button(Row, TEXT("Off"), ESquadDebugAction::Pause, 0.f, !bPaused);
	}
	{
		const float VPTime = GetCVarFloat(CVarNameFor(ESquadDebugAction::VPWinTime));
		FDebugMenuRow& Row = AddRow(TEXT("VP win time"));
		Button(Row, TEXT("Default"), ESquadDebugAction::VPWinTime, 0.f, VPTime <= 0.f);
		for (const float Seconds : { 10.f, 30.f })
		{
			Button(Row, FString::Printf(TEXT("%gs"), Seconds), ESquadDebugAction::VPWinTime, Seconds, FMath::IsNearlyEqual(VPTime, Seconds));
		}
	}
	{
		FDebugMenuRow& Row = AddRow(TEXT("Map"));
		Button(Row, TEXT("Breach all walls"), ESquadDebugAction::BreachAllWalls);
		FDebugMenuButton& Restart = Row.Buttons.AddDefaulted_GetRef();
		Restart.Label = TEXT("Restart");
		Restart.bRestart = true;
	}

	const bool bAny = NumSelected > 0;
	Header(bAny ? FString::Printf(TEXT("SELECTED (%d)"), NumSelected) : FString(TEXT("SELECTED (none)")));
	{
		FDebugMenuRow& Row = AddRow(TEXT("Stress"));
		for (const float Stress : { 0.f, 33.f, 66.f, 100.f })
		{
			Button(Row, FString::Printf(TEXT("%g"), Stress), ESquadDebugAction::SetStress, Stress, false, bAny);
		}
	}
	{
		FDebugMenuRow& Row = AddRow(TEXT("Health"));
		Button(Row, TEXT("Heal"), ESquadDebugAction::Heal, 0.f, false, bAny);
		Button(Row, TEXT("Injure"), ESquadDebugAction::Injure, 0.f, false, bAny);
		Button(Row, FString::Printf(TEXT("Stun %gs"), DebugStunSeconds), ESquadDebugAction::Stun, DebugStunSeconds, false, bAny);
		Button(Row, TEXT("Kill"), ESquadDebugAction::Kill, 0.f, false, bAny);
	}
	{
		FDebugMenuRow& Row = AddRow(TEXT("Kit"));
		Button(Row, TEXT("Restock"), ESquadDebugAction::Restock, 0.f, false, bAny);
	}

	Header(TEXT("ENEMIES"));
	{
		FDebugMenuRow& Row = AddRow(TEXT("All enemies"));
		Button(Row, TEXT("Injure"), ESquadDebugAction::InjureEnemies);
		Button(Row, TEXT("Kill"), ESquadDebugAction::KillEnemies);
	}
	{
		FDebugMenuRow& Row = AddRow(TEXT("Enemy stress"));
		Button(Row, TEXT("0"), ESquadDebugAction::StressEnemies, 0.f);
		Button(Row, TEXT("100"), ESquadDebugAction::StressEnemies, 100.f);
	}

	Header(TEXT("CLICK TOOL (left click map, right click / Esc stops)"));
	{
		FDebugMenuRow& Row = AddRow(TEXT("Selection"));
		ToolButton(Row, EDebugClickTool::Teleport);
	}
	{
		FDebugMenuRow& Row = AddRow(TEXT("Any soldier"));
		ToolButton(Row, EDebugClickTool::Heal);
		ToolButton(Row, EDebugClickTool::Injure);
		ToolButton(Row, EDebugClickTool::Stun);
		ToolButton(Row, EDebugClickTool::Kill);
	}
#endif
}

TArray<FString> ASquadPlayerController::GetDebugInfoLines() const
{
	TArray<FString> Lines;
#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Lines;
	}
	const float Dilation = World->GetWorldSettings() ? World->GetWorldSettings()->TimeDilation : 1.f;
	Lines.Add(FString::Printf(TEXT("%s   %.0f fps   speed %gx%s"), NetModeName(World->GetNetMode()),
		FApp::GetDeltaTime() > 0.0 ? 1.0 / FApp::GetDeltaTime() : 0.0, Dilation, World->IsPaused() ? TEXT("   PAUSED") : TEXT("")));

	FVector2D Cursor;
	FIntPoint Cell;
	const AGridManager* GridManager = Grid ? Grid.Get() : AGridManager::Find(World);
	if (GridManager && GetCursorWorld2D(Cursor) && GridManager->WorldToCell(FVector(Cursor, 0.f), Cell))
	{
		Lines.Add(FString::Printf(TEXT("Cursor cell (%d, %d)"), Cell.X, Cell.Y));
	}
	else
	{
		Lines.Add(TEXT("Cursor cell -"));
	}
	if (const ASoldier* Soldier = FindSoldierUnderCursor(true))
	{
		Lines.Add(FString::Printf(TEXT("%s #%d  %s  stress %.0f  ammo %d+%d  gren %d  med %d"),
			Soldier->Team == ESquadTeam::Allies ? TEXT("Allies") : TEXT("Axis"), Soldier->GetSquadNumber(), HealthName(*Soldier),
			Soldier->GetStress(), Soldier->GetMagazineAmmo(), Soldier->GetReserveAmmo(), Soldier->GetGrenades(), Soldier->GetMedkits()));
	}
#endif
	return Lines;
}

bool ASquadPlayerController::PressDebugMenuButton(int32 RowIndex, int32 ButtonIndex)
{
	if (!DebugRows.IsValidIndex(RowIndex) || !DebugRows[RowIndex].Buttons.IsValidIndex(ButtonIndex))
	{
		return false;
	}
	const FDebugMenuButton Button = DebugRows[RowIndex].Buttons[ButtonIndex];
	if (!Button.bEnabled)
	{
		return false;
	}
	ExecuteDebugButton(Button);
	RebuildDebugMenu(); // Show the new state at once.
	return true;
}

void ASquadPlayerController::ExecuteDebugButton(const FDebugMenuButton& Button)
{
#if !UE_BUILD_SHIPPING
	if (Button.Tool != EDebugClickTool::None)
	{
		DebugClickTool = DebugClickTool == Button.Tool ? EDebugClickTool::None : Button.Tool;
		return;
	}
	if (Button.bRestart)
	{
		// Only the server can restart a networked match; a local game restarts the same way.
		CallServer([this]() { ServerDebugAction(ESquadDebugAction::Restart, 0.f, FIntPoint::ZeroValue, {}); });
		return;
	}
	if (const TCHAR* Name = CVarNameFor(Button.Action))
	{
		SetCVar(Name, Button.Param); // Also here: some are read on the client (squad.RevealAll draws enemies).
	}
	CallServer([this, Action = Button.Action, Param = Button.Param, Soldiers = GetSelectedSoldiers()]() { ServerDebugAction(Action, Param, FIntPoint::ZeroValue, Soldiers); });
#endif
}

ASoldier* ASquadPlayerController::FindSoldierUnderCursor(bool bAllowDead) const
{
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		ASoldier* Soldier = Cast<ASoldier>(Hit.GetActor());
		if (Soldier && (bAllowDead || !Soldier->IsDead()))
		{
			return Soldier;
		}
	}
	// Prone and injured soldiers are small targets: the cell under the cursor counts too.
	const AGridManager* GridManager = Grid ? Grid.Get() : AGridManager::Find(GetWorld());
	FVector2D Cursor;
	FIntPoint Cell;
	if (!GridManager || !GetCursorWorld2D(Cursor) || !GridManager->WorldToCell(FVector(Cursor, 0.f), Cell))
	{
		return nullptr;
	}
	ASoldier* Found = nullptr;
	for (const TWeakObjectPtr<ASoldier>& Weak : GridManager->GetSoldiers())
	{
		ASoldier* Soldier = Weak.Get();
		if (Soldier && Soldier->GetCurrentCell() == Cell && (bAllowDead || !Soldier->IsDead()))
		{
			if (!Found || (Found->IsDead() && !Soldier->IsDead()))
			{
				Found = Soldier; // A living soldier over a corpse on the same cell.
			}
		}
	}
	return Found;
}

bool ASquadPlayerController::HandleDebugMouse()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const ASquadHUD* SquadHUD = Cast<ASquadHUD>(GetHUD());
	float MouseX = 0.f, MouseY = 0.f;
	if (!SquadHUD || !GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	const FVector2D Mouse(MouseX, MouseY);
	const bool bLeft = WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bRight = WasInputKeyJustPressed(EKeys::RightMouseButton);

	if (bDebugMenuOpen && SquadHUD->IsOverDebugMenu(Mouse))
	{
		FIntPoint Index;
		if (bLeft && SquadHUD->GetDebugMenuButtonAt(Mouse, Index))
		{
			PressDebugMenuButton(Index.X, Index.Y);
		}
		return true; // Nothing under the menu gets the click.
	}
	if (DebugClickTool == EDebugClickTool::None || CommandMode != ECommandMode::None || SquadHUD->IsOverSquadBar(Mouse))
	{
		// The squad bar still selects soldiers while a tool is picked (pick who to teleport).
		return false;
	}
	if (bRight)
	{
		DebugClickTool = EDebugClickTool::None;
		return true;
	}
	if (!bLeft)
	{
		return false; // Hover as usual (cell highlight, hit chances).
	}

	if (DebugClickTool == EDebugClickTool::Teleport)
	{
		const AGridManager* GridManager = Grid ? Grid.Get() : AGridManager::Find(GetWorld());
		FVector2D Cursor;
		FIntPoint Cell;
		if (GridManager && Selected.Num() > 0 && GetCursorWorld2D(Cursor) && GridManager->WorldToCell(FVector(Cursor, 0.f), Cell))
		{
			CallServer([this, Cell, Soldiers = GetSelectedSoldiers()]() { ServerDebugAction(ESquadDebugAction::Teleport, 0.f, Cell, Soldiers); });
		}
		return true;
	}

	ASoldier* Target = FindSoldierUnderCursor(false);
	if (!Target)
	{
		return true; // Missed: the click still belongs to the tool.
	}
	ESquadDebugAction Action = ESquadDebugAction::None;
	float Param = 0.f;
	switch (DebugClickTool)
	{
	case EDebugClickTool::Heal:   Action = ESquadDebugAction::Heal; break;
	case EDebugClickTool::Injure: Action = ESquadDebugAction::Injure; break;
	case EDebugClickTool::Stun:   Action = ESquadDebugAction::Stun; Param = DebugStunSeconds; break;
	case EDebugClickTool::Kill:   Action = ESquadDebugAction::Kill; break;
	default: break;
	}
	CallServer([this, Action, Param, Cell = Target->GetCurrentCell(), Targets = TArray<ASoldier*>{ Target }]() { ServerDebugAction(Action, Param, Cell, Targets); });
	return true;
#endif
}

void ASquadPlayerController::ServerDebugAction_Implementation(ESquadDebugAction Action, float Param, FIntPoint Cell, const TArray<ASoldier*>& Targets)
{
#if !UE_BUILD_SHIPPING
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (World->GetNetMode() == NM_DedicatedServer && !GIsEditor && CVarAllowDebugMenu.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogSquadDebug, Warning, TEXT("%s: debug action refused (dedicated server; set squad.AllowDebugMenu 1 to allow)."), *GetName());
		return;
	}
	UE_LOG(LogSquadDebug, Log, TEXT("%s: debug %s param=%g cell=(%d,%d) targets=%d"), *GetName(),
		*UEnum::GetValueAsString(Action), Param, Cell.X, Cell.Y, Targets.Num());

	TArray<ASoldier*> Living;
	for (ASoldier* Soldier : Targets)
	{
		if (IsValid(Soldier) && !Soldier->IsDead())
		{
			Living.Add(Soldier);
		}
	}
	auto ForEachEnemy = [&](TFunctionRef<void(ASoldier&)> Fn)
	{
		if (const AGridManager* GridManager = AGridManager::Find(World))
		{
			for (const TWeakObjectPtr<ASoldier>& Weak : GridManager->GetSoldiers())
			{
				ASoldier* Soldier = Weak.Get();
				if (Soldier && Soldier->Team != PlayerTeam && !Soldier->IsDead())
				{
					Fn(*Soldier);
				}
			}
		}
	};

	switch (Action)
	{
	case ESquadDebugAction::RevealAll:
	case ESquadDebugAction::Bot:
	case ESquadDebugAction::BotDebug:
	case ESquadDebugAction::GodMode:
	case ESquadDebugAction::VPWinTime:
		SetCVar(CVarNameFor(Action), Param);
		break;
	case ESquadDebugAction::GameSpeed:
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			Settings->SetTimeDilation(FMath::Clamp(Param, 0.05f, 10.f));
		}
		break;
	case ESquadDebugAction::Pause:
		SetPause(Param > 0.5f);
		break;
	case ESquadDebugAction::Restart:
		if (World->GetNetMode() == NM_Standalone)
		{
			ConsoleCommand(TEXT("RestartLevel"));
		}
		else
		{
			World->ServerTravel(TEXT("?Restart"), false); // Every connected client travels with the server.
		}
		break;
	case ESquadDebugAction::BreachAllWalls:
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("squad.BreachAllWalls"));
		}
		break;
	case ESquadDebugAction::SetStress:
		for (ASoldier* Soldier : Living) { Soldier->SetStress(Param); }
		break;
	case ESquadDebugAction::Heal:
		for (ASoldier* Soldier : Living) { Soldier->DebugHeal(); }
		break;
	case ESquadDebugAction::Injure:
		for (ASoldier* Soldier : Living)
		{
			if (!Soldier->IsInjured()) // Injuring an injured soldier would kill it: that is what Kill is for.
			{
				Soldier->DebugInjure();
			}
		}
		break;
	case ESquadDebugAction::Stun:
		for (ASoldier* Soldier : Living) { Soldier->DebugStun(Param); }
		break;
	case ESquadDebugAction::Kill:
		for (ASoldier* Soldier : Living) { Soldier->DebugKill(); }
		break;
	case ESquadDebugAction::Restock:
		for (ASoldier* Soldier : Living) { Soldier->DebugRestock(); }
		break;
	case ESquadDebugAction::Teleport:
		if (Living.Num() > 0)
		{
			// The first soldier goes to Cell; the rest keep their offsets from it, or the nearest free cell.
			const AGridManager* GridManager = AGridManager::Find(World);
			const FIntPoint LeadCell = Living[0]->GetCurrentCell();
			TArray<TPair<ASoldier*, FIntPoint>> Moves;
			for (ASoldier* Soldier : Living)
			{
				Moves.Emplace(Soldier, Cell + (Soldier->GetCurrentCell() - LeadCell));
			}
			for (const TPair<ASoldier*, FIntPoint>& Move : Moves)
			{
				FIntPoint Target = Move.Value;
				if (!Move.Key->DebugTeleportTo(Target) && GridManager && GridManager->FindNearestFreeCell(Target, Move.Key, Target))
				{
					Move.Key->DebugTeleportTo(Target);
				}
			}
		}
		break;
	case ESquadDebugAction::InjureEnemies:
		ForEachEnemy([](ASoldier& Soldier) { if (!Soldier.IsInjured()) { Soldier.DebugInjure(); } });
		break;
	case ESquadDebugAction::KillEnemies:
		ForEachEnemy([](ASoldier& Soldier) { Soldier.DebugKill(); });
		break;
	case ESquadDebugAction::StressEnemies:
		ForEachEnemy([Param](ASoldier& Soldier) { Soldier.SetStress(Param); });
		break;
	default:
		break;
	}
#endif
}
