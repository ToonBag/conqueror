#include "SquadGameMode.h"

#include "DeploymentZone.h"
#include "GridManager.h"
#include "Soldier.h"
#include "SquadBot.h"
#include "SquadCameraPawn.h"
#include "SquadGameState.h"
#include "SquadHUD.h"
#include "SquadMatchStats.h"
#include "SquadPlayerController.h"
#include "SquadPlayerState.h"
#include "TacticalPlan.h"
#include "VictoryPoint.h"
#include "WeaponData.h"
#include "Ballistics.h"
#include "BallisticsData.h"
#include "SoldierData.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogSquadMatch, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarMinPlayers(
		TEXT("squad.MinPlayers"), 1,
		TEXT("Dedicated server: players who must have joined before the match starts and bots take empty teams (default 1)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarSquadsPerTeam(
		TEXT("squad.SquadsPerTeam"), 1,
		TEXT("Squads per team (one per player, GDD §4.1). Above 1 the game mode copies the level's squad into each deployment zone when the map loads; empty squads get bots. Set before the map loads."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarSwapSides(
		TEXT("squad.SwapSides"), 0,
		TEXT("Balancing test (server): each faction starts in the other's deployment zone, with its own rifles. Set before the map loads."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarMatchSeed(
		TEXT("squad.MatchSeed"), 0,
		TEXT("Seed for the bots' choices and every soldier's hit rolls, for reproducible test matches (0: a new random seed each match)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarVPWinTime(
		TEXT("squad.VPWinTime"), 0.f,
		TEXT("Overrides the Victory Point win timer in seconds for testing (0: use the game mode's VPWinTime, 5 minutes)."),
		ECVF_Cheat);

	TAutoConsoleVariable<FString> CVarTuningOverrides(
		TEXT("squad.TuningOverrides"), TEXT(""),
		TEXT("Balancing test (server): 'Prop=Value;Prop=Value' set on the soldier, weapon and ballistics data assets in memory when the match starts (e.g. StressPerIncomingShot=12;SettleTime=1). Changes the loaded assets for the rest of the process, so use it on throwaway servers (AIMatchBatch -Tuning), not in the editor."),
		ECVF_Default);

	/** Sets Name=Value on Object if it has that property. True if it did. */
	bool ApplyTuningOverride(UObject* Object, const FString& Name, const FString& Value)
	{
		FProperty* Prop = Object ? Object->GetClass()->FindPropertyByName(FName(*Name)) : nullptr;
		if (!Prop)
		{
			return false;
		}
		if (!Prop->ImportText_Direct(*Value, Prop->ContainerPtrToValuePtr<void>(Object), Object, PPF_None))
		{
			UE_LOG(LogSquadMatch, Warning, TEXT("Tuning override %s=%s: bad value for %s."), *Name, *Value, *Object->GetName());
			return false;
		}
		UE_LOG(LogSquadMatch, Log, TEXT("Tuning override: %s.%s = %s"), *Object->GetName(), *Name, *Value);
		return true;
	}

	const TCHAR* TeamName(ESquadTeam Team)
	{
		return Team == ESquadTeam::Allies ? TEXT("Allies") : TEXT("Axis");
	}
}

ASquadGameMode::ASquadGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	DefaultPawnClass = ASquadCameraPawn::StaticClass();
	PlayerControllerClass = ASquadPlayerController::StaticClass();
	HUDClass = ASquadHUD::StaticClass();
	GameStateClass = ASquadGameState::StaticClass();
	PlayerStateClass = ASquadPlayerState::StaticClass();
}

void ASquadGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	ASquadPlayerController* PC = Cast<ASquadPlayerController>(NewPlayer);
	if (!PC)
	{
		return;
	}
	// Fewest players first, Allies on a tie. (Matchmaking will set teams later; GDD §14.)
	const TArray<ESquadTeam> Teams = GetSquadTeams();
	ESquadTeam Team = ESquadTeam::Allies;
	int32 Fewest = MAX_int32;
	for (const ESquadTeam Candidate : Teams)
	{
		const int32 Count = CountPlayers(Candidate, PC);
		if (Count < Fewest)
		{
			Fewest = Count;
			Team = Candidate;
		}
	}
	// One squad per player (GDD §4.1): the team's first squad no other player commands.
	const TSet<int32> Commanded = GetCommandedSquads(PC);
	int32 SquadId = 0;
	bool bFreeSquad = false;
	for (const int32 Candidate : GetSquadIds(Team))
	{
		if (!Commanded.Contains(SquadKey(Team, Candidate)))
		{
			SquadId = Candidate;
			bFreeSquad = true;
			break;
		}
	}
	if (!bFreeSquad)
	{
		UE_LOG(LogSquadMatch, Warning, TEXT("Every %s squad already has a player; this player shares squad 1."), TeamName(Team));
	}
	PC->AssignTeam(Team, SquadId);
	UE_LOG(LogSquadMatch, Log, TEXT("%s joins the %s (squad %d)."), PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("Player"),
		TeamName(Team), SquadId + 1);

	// A player joining a bot-run squad takes it over.
	if (ASquadBot* Bot = ASquadBot::FindForSquad(this, Team, SquadId))
	{
		UE_LOG(LogSquadMatch, Log, TEXT("The %s squad %d AI bot hands its squad to the player."), TeamName(Team), SquadId + 1);
		Bot->Destroy();
	}
	if (bMatchStarted)
	{
		GiveTestMachineGuns();
	}
}

void ASquadGameMode::Logout(AController* Exiting)
{
	const ASquadPlayerController* PC = Cast<ASquadPlayerController>(Exiting);
	Super::Logout(Exiting);
	if (PC && bMatchStarted && !bMatchEnded && bSpawnBotsForUncommandedTeams)
	{
		UE_LOG(LogSquadMatch, Log, TEXT("The %s squad %d player has left."), TeamName(PC->PlayerTeam), PC->PlayerSquadId + 1);
		SpawnBots(PC); // Their squad goes back to a bot.
	}
}

void ASquadGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (CVarSwapSides.GetValueOnGameThread() != 0)
	{
		SwapSides();
	}
	// Before anyone logs in, so joining players can be given any of the squads.
	SpawnExtraSquads();
}

void ASquadGameMode::StartMatch()
{
	bMatchStarted = true;
	UE_LOG(LogSquadMatch, Log, TEXT("Match starts with %d player(s)."), GetNumPlayers());

	// Reproducible test matches: one seed drives the bots' choices (FMath) and every soldier's hit rolls.
	MatchSeed = CVarMatchSeed.GetValueOnGameThread();
	if (MatchSeed == 0)
	{
		MatchSeed = static_cast<int32>(FPlatformTime::Cycles() & 0x7fffffff) | 1;
	}
	FMath::RandInit(MatchSeed);
	FMath::SRandInit(MatchSeed);
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		It->SeedRandom(FMath::Rand());
	}
	UE_LOG(LogSquadMatch, Log, TEXT("Match seed %d (squad.MatchSeed)."), MatchSeed);

	ApplyTuningOverrides();
	SpawnTacticalPlans();
	GiveTestMachineGuns();
	if (bSpawnBotsForUncommandedTeams)
	{
		SpawnBots();
	}
	if (USquadMatchStats* Stats = GetWorld()->GetSubsystem<USquadMatchStats>())
	{
		Stats->OnMatchStarted(MatchSeed, NumSquadsPerTeam);
	}
}

TArray<int32> ASquadGameMode::GetSquadIds(ESquadTeam Team) const
{
	TArray<int32> Ids;
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		if (It->Team == Team)
		{
			Ids.AddUnique(It->SquadId);
		}
	}
	Ids.Sort();
	return Ids;
}

TSet<int32> ASquadGameMode::GetCommandedSquads(const AController* Except) const
{
	TSet<int32> Commanded;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ASquadPlayerController* PC = Cast<ASquadPlayerController>(It->Get()); PC && PC != Except)
		{
			Commanded.Add(SquadKey(PC->PlayerTeam, PC->PlayerSquadId));
		}
	}
	return Commanded;
}

void ASquadGameMode::SwapSides()
{
	// Balancing test: each faction starts in the other's deployment zone, keeping its own rifle, colour and
	// uniform, so a side bias can be told apart from a faction bias. Server-only: clients still label the zones
	// the old way.
	TMap<ESquadTeam, TArray<ASoldier*>> ByTeam;
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		ByTeam.FindOrAdd(It->Team).Add(*It);
	}
	TArray<ASoldier*>& Allies = ByTeam.FindOrAdd(ESquadTeam::Allies);
	TArray<ASoldier*>& Axis = ByTeam.FindOrAdd(ESquadTeam::Axis);
	auto BySquadIndex = [](const ASoldier& A, const ASoldier& B) { return A.SquadIndex < B.SquadIndex; };
	Allies.Sort(BySquadIndex);
	Axis.Sort(BySquadIndex);
	if (Allies.Num() != Axis.Num())
	{
		UE_LOG(LogSquadMatch, Warning, TEXT("squad.SwapSides: squads differ in size; sides not swapped."));
		return;
	}
	for (int32 I = 0; I < Allies.Num(); ++I)
	{
		ASoldier* A = Allies[I];
		ASoldier* X = Axis[I];
		Swap(A->Weapon, X->Weapon);
		Swap(A->VisualData, X->VisualData);
		Swap(A->SoldierColor, X->SoldierColor);
		A->Team = ESquadTeam::Axis;
		X->Team = ESquadTeam::Allies;
		for (ASoldier* S : { A, X })
		{
			if (S->HasActorBegunPlay() && S->Weapon)
			{
				S->EquipStartingWeapon(S->Weapon); // Its loadout was handed out with the old rifle.
			}
			S->OnConstruction(S->GetActorTransform()); // Colour, labels and uniform for the new team.
		}
	}
	for (TActorIterator<ADeploymentZone> It(GetWorld()); It; ++It)
	{
		It->Team = It->Team == ESquadTeam::Allies ? ESquadTeam::Axis : ESquadTeam::Allies;
	}
	UE_LOG(LogSquadMatch, Log, TEXT("squad.SwapSides: Allies start in the north, Axis in the south."));
}

void ASquadGameMode::SpawnExtraSquads()
{
	const int32 Wanted = FMath::Max(1, CVarSquadsPerTeam.GetValueOnGameThread());
	NumSquadsPerTeam = Wanted;
	if (Wanted <= 1)
	{
		return;
	}
	const AGridManager* Grid = AGridManager::Find(this);
	if (!Grid)
	{
		UE_LOG(LogSquadMatch, Warning, TEXT("squad.SquadsPerTeam %d: no GridManager, no extra squads."), Wanted);
		return;
	}
	for (const ESquadTeam Team : GetSquadTeams())
	{
		// The level's squad is the template: same soldiers, data assets and look.
		TArray<ASoldier*> Template;
		for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
		{
			if (It->Team == Team && It->SquadId == 0)
			{
				Template.Add(*It);
			}
		}
		Template.Sort([](const ASoldier& A, const ASoldier& B) { return A.SquadIndex < B.SquadIndex; });
		const ADeploymentZone* DZ = ADeploymentZone::FindForTeam(this, Team);
		if (Template.Num() == 0 || !DZ)
		{
			UE_LOG(LogSquadMatch, Warning, TEXT("squad.SquadsPerTeam: the %s have no squad or deployment zone to copy."), TeamName(Team));
			continue;
		}

		// Free cells of the deployment zone, rows nearest the team's own map edge first (GDD §10.4).
		TArray<FIntPoint> Free;
		const FIntPoint Min = DZ->GetMinCell();
		for (int32 Y = 0; Y < DZ->SizeY; ++Y)
		{
			for (int32 X = 0; X < DZ->SizeX; ++X)
			{
				const FIntPoint Cell = Min + FIntPoint(X, Y);
				if (Grid->IsInBounds(Cell) && Grid->IsCellFreeAsDestination(Cell))
				{
					Free.Add(Cell);
				}
			}
		}
		const float MapMidY = Grid->GridSizeY * 0.5f;
		Free.Sort([MapMidY](const FIntPoint& A, const FIntPoint& B)
		{
			const float DA = FMath::Abs(A.Y - MapMidY), DB = FMath::Abs(B.Y - MapMidY);
			return DA != DB ? DA > DB : A.X < B.X;
		});

		int32 NextFree = 0;
		for (int32 SquadId = 1; SquadId < Wanted; ++SquadId)
		{
			for (const ASoldier* Source : Template)
			{
				if (!Free.IsValidIndex(NextFree))
				{
					UE_LOG(LogSquadMatch, Warning, TEXT("squad.SquadsPerTeam: the %s deployment zone is full; squad %d is short."), TeamName(Team), SquadId + 1);
					break;
				}
				const FIntPoint Cell = Free[NextFree++];
				FVector Location = Grid->CellToWorld(Cell);
				Location.Z = Source->GetActorLocation().Z;
				const FTransform Transform(Source->GetActorRotation(), Location);
				ASoldier* Soldier = GetWorld()->SpawnActorDeferred<ASoldier>(Source->GetClass(), Transform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Soldier)
				{
					continue;
				}
				Soldier->Team = Team;
				Soldier->SquadId = SquadId;
				Soldier->SquadIndex = Source->SquadIndex;
				Soldier->SoldierColor = Source->SoldierColor;
				Soldier->Weapon = Source->Weapon;
				Soldier->SoldierData = Source->SoldierData;
				Soldier->VisualData = Source->VisualData;
				Soldier->MoveSpeed = Source->MoveSpeed;
				Soldier->TurnSpeed = Source->TurnSpeed;
				Soldier->MoveMarkerColor = Source->MoveMarkerColor;
				Soldier->FinishSpawning(Transform);
			}
		}
		UE_LOG(LogSquadMatch, Log, TEXT("squad.SquadsPerTeam: the %s now have %d squad(s)."), TeamName(Team), GetSquadIds(Team).Num());
	}
}

TArray<ESquadTeam> ASquadGameMode::GetSquadTeams() const
{
	TArray<ESquadTeam> Teams;
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		Teams.AddUnique(It->Team);
	}
	Teams.Sort([](ESquadTeam A, ESquadTeam B) { return static_cast<uint8>(A) < static_cast<uint8>(B); });
	return Teams;
}

int32 ASquadGameMode::CountPlayers(ESquadTeam Team, const AController* Except) const
{
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const ASquadPlayerController* PC = Cast<ASquadPlayerController>(It->Get());
		if (PC && PC != Except && PC->PlayerTeam == Team)
		{
			++Count;
		}
	}
	return Count;
}

void ASquadGameMode::GiveTestMachineGuns()
{
	if (TestMachineGunSoldier <= 0)
	{
		return;
	}
	TSet<int32> NewSquads = GetCommandedSquads().Difference(SquadsGivenTestMG);
	SquadsGivenTestMG.Append(NewSquads);
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		ASoldier* Soldier = *It;
		if (Soldier->GetSquadNumber() != TestMachineGunSoldier || !NewSquads.Contains(SquadKey(Soldier->Team, Soldier->SquadId)) || Soldier->IsDead())
		{
			continue;
		}
		UWeaponData* MG = (Soldier->Team == ESquadTeam::Allies ? AlliedTestMachineGun : AxisTestMachineGun).LoadSynchronous();
		if (!MG)
		{
			UE_LOG(LogSquadMatch, Warning, TEXT("Testing: no %s machine gun data asset; soldier %d keeps its rifle."),
				TeamName(Soldier->Team), TestMachineGunSoldier);
		}
		else
		{
			Soldier->EquipStartingWeapon(MG);
			UE_LOG(LogSquadMatch, Log, TEXT("Testing: %s soldier %d starts with the %s."), TeamName(Soldier->Team),
				TestMachineGunSoldier, *MG->DisplayName.ToString());
		}
	}
}

float ASquadGameMode::GetVPWinTime() const
{
	const float Override = CVarVPWinTime.GetValueOnGameThread();
	return Override > 0.f ? Override : VPWinTime;
}

void ASquadGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// A dedicated server waits for players before the bots start fighting; a local game starts at once (its
	// players have logged in before the first tick).
	if (!bMatchStarted && (GetNetMode() != NM_DedicatedServer || GetNumPlayers() >= FMath::Max(0, CVarMinPlayers.GetValueOnGameThread())))
	{
		StartMatch();
	}
	if (!bMatchStarted)
	{
		return;
	}
	UpdateTacticalRoles();
	if (!bMatchEnded)
	{
		CheckVictory(DeltaSeconds);
	}
}

void ASquadGameMode::SpawnBots(const AController* Except)
{
	const TSet<int32> Commanded = GetCommandedSquads(Except);
	for (const ESquadTeam Team : GetSquadTeams())
	for (const int32 SquadId : GetSquadIds(Team))
	{
		if (Commanded.Contains(SquadKey(Team, SquadId)) || ASquadBot::FindForSquad(this, Team, SquadId))
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.Name = MakeUniqueObjectName(GetWorld()->GetCurrentLevel(), ASquadBot::StaticClass(),
			*FString::Printf(TEXT("SquadBot_%s_%d"), TeamName(Team), SquadId + 1));
		Params.bDeferConstruction = true;
		if (ASquadBot* Bot = GetWorld()->SpawnActor<ASquadBot>(ASquadBot::StaticClass(), FTransform::Identity, Params))
		{
			Bot->Team = Team;
			Bot->SquadId = SquadId;
			Bot->FinishSpawning(FTransform::Identity);
			UE_LOG(LogSquadMatch, Log, TEXT("No player commands the %s squad %d: an AI bot takes over."), TeamName(Team), SquadId + 1);

			// squad.Bot lasts for the whole editor session, so a test script that turned it off would
			// otherwise leave every later play test with a squad that silently never moves.
			const IConsoleVariable* BotVar = IConsoleManager::Get().FindConsoleVariable(TEXT("squad.Bot"));
			if (BotVar && BotVar->GetInt() == 0)
			{
				const FString Message = FString::Printf(TEXT("The %s AI bot is OFF (squad.Bot 0). Type squad.Bot 1 in the console to turn it on."), TeamName(Team));
				UE_LOG(LogSquadMatch, Warning, TEXT("%s"), *Message);
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Orange, Message);
				}
			}
		}
	}
}

void ASquadGameMode::SpawnTacticalPlans()
{
	for (ESquadTeam Team : { ESquadTeam::Allies, ESquadTeam::Axis })
	{
		if (ATacticalPlan::FindForTeam(this, Team))
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.Name = MakeUniqueObjectName(GetWorld()->GetCurrentLevel(), ATacticalPlan::StaticClass(), *FString::Printf(TEXT("TacticalPlan_%s"), TeamName(Team)));
		Params.bDeferConstruction = true;
		if (ATacticalPlan* Plan = GetWorld()->SpawnActor<ATacticalPlan>(ATacticalPlan::StaticClass(), FTransform::Identity, Params))
		{
			Plan->Team = Team;
			Plan->FinishSpawning(FTransform::Identity);
		}
	}
}

void ASquadGameMode::UpdateTacticalRoles()
{
	TMap<ESquadTeam, TArray<ASquadPlayerState*>> ByTeam;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const ASquadPlayerController* PC = Cast<ASquadPlayerController>(It->Get());
		ASquadPlayerState* PS = PC ? PC->GetPlayerState<ASquadPlayerState>() : nullptr;
		if (PS)
		{
			PS->Team = PC->PlayerTeam;
			ByTeam.FindOrAdd(PC->PlayerTeam).Add(PS);
		}
	}
	for (TPair<ESquadTeam, TArray<ASquadPlayerState*>>& Pair : ByTeam)
	{
		TArray<ASquadPlayerState*>& Players = Pair.Value;
		Players.Sort([](const ASquadPlayerState& A, const ASquadPlayerState& B)
		{
			return A.Elo != B.Elo ? A.Elo > B.Elo : A.GetPlayerId() < B.GetPlayerId();
		});
		for (int32 I = 0; I < Players.Num(); ++I)
		{
			ASquadPlayerState* PS = Players[I];
			const ETacticalRole NewRole = I == 0 ? ETacticalRole::CO
				: I == 1 ? ETacticalRole::XO
				: PS->bPlanningDelegated ? ETacticalRole::Planner : ETacticalRole::Soldier;
			if (PS->TacticalRole != NewRole)
			{
				UE_LOG(LogSquadMatch, Log, TEXT("%s %s is now %s."), TeamName(Pair.Key), *PS->GetPlayerName(), TacticalPlan::RoleName(NewRole));
				PS->TacticalRole = NewRole;
				PS->ForceNetUpdate();
			}
		}
	}
}

void ASquadGameMode::CheckVictory(float DeltaSeconds)
{
	ASquadGameState* GS = GetGameState<ASquadGameState>();
	if (!GS)
	{
		return;
	}

	// ---- Annihilation (GDD §11): every soldier of a team is dead.
	TMap<ESquadTeam, int32> Total, Alive;
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		Total.FindOrAdd(It->Team)++;
		if (!It->IsDead())
		{
			Alive.FindOrAdd(It->Team)++;
		}
	}
	const bool bAlliesWiped = Total.FindRef(ESquadTeam::Allies) > 0 && Alive.FindRef(ESquadTeam::Allies) == 0;
	const bool bAxisWiped = Total.FindRef(ESquadTeam::Axis) > 0 && Alive.FindRef(ESquadTeam::Axis) == 0;
	if (bAlliesWiped && bAxisWiped)
	{
		EndMatch(ESquadTeam::Allies, EVictoryReason::Draw);
		return;
	}
	if (bAlliesWiped || bAxisWiped)
	{
		EndMatch(bAlliesWiped ? ESquadTeam::Axis : ESquadTeam::Allies, EVictoryReason::Annihilation);
		return;
	}

	// ---- Victory Points (GDD §11): one team owns every VP at once, continuously, for the VP win time.
	int32 NumVPs = 0;
	bool bAllSameOwner = true;
	ESquadTeam VPOwner = ESquadTeam::Allies;
	for (TActorIterator<AVictoryPoint> It(GetWorld()); It; ++It)
	{
		if (!It->IsOwned() || (NumVPs > 0 && It->GetOwningTeam() != VPOwner))
		{
			bAllSameOwner = false;
		}
		VPOwner = It->GetOwningTeam();
		++NumVPs;
	}
	const bool bHeldNow = NumVPs > 0 && bAllSameOwner;
	if (!bHeldNow || !bVPHeld || VPOwner != VPHoldingTeam)
	{
		VPHoldElapsed = 0.f; // Any break restarts the timer.
	}
	bVPHeld = bHeldNow;
	VPHoldingTeam = VPOwner;
	if (bVPHeld)
	{
		VPHoldElapsed += DeltaSeconds;
	}
	const float Required = GetVPWinTime();
	GS->SetVPHold(bVPHeld, VPHoldingTeam, VPHoldElapsed, Required);
	if (bVPHeld && VPHoldElapsed >= Required)
	{
		EndMatch(VPHoldingTeam, EVictoryReason::VictoryPoints);
	}
}

void ASquadGameMode::EndMatch(ESquadTeam Winner, EVictoryReason Reason)
{
	bMatchEnded = true;
	if (ASquadGameState* GS = GetGameState<ASquadGameState>())
	{
		GS->SetMatchResult(Winner, Reason);
	}
	if (Reason == EVictoryReason::Draw)
	{
		UE_LOG(LogSquadMatch, Log, TEXT("MATCH OVER: draw (both squads wiped out)."));
	}
	else
	{
		UE_LOG(LogSquadMatch, Log, TEXT("MATCH OVER: %s win by %s."), TeamName(Winner),
			Reason == EVictoryReason::Annihilation ? TEXT("annihilation") : TEXT("holding every Victory Point"));
	}
	if (USquadMatchStats* Stats = USquadMatchStats::Get(this))
	{
		Stats->OnMatchEnded(Winner, Reason == EVictoryReason::Draw ? TEXT("draw")
			: Reason == EVictoryReason::Annihilation ? TEXT("annihilation") : TEXT("victory_points"));
	}
	// TODO (GDD §14.4): report the result to the post-match ELO endpoint once the backend exists.
	if (bPauseOnMatchEnd)
	{
		UGameplayStatics::SetGamePaused(this, true);
	}
}

void ASquadGameMode::ApplyTuningOverrides()
{
	const FString Spec = CVarTuningOverrides.GetValueOnGameThread();
	if (Spec.IsEmpty())
	{
		return;
	}
	// Every data asset the soldiers use (soldier tuning, each rifle / MG) plus the projectile tuning.
	TSet<UObject*> Targets;
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		Targets.Add(const_cast<USoldierData*>(It->GetSoldierData()));
		for (const FLootWeapon& W : It->GetInventory().Weapons)
		{
			Targets.Add(W.Weapon.Get());
		}
	}
	Targets.Add(const_cast<UBallisticsData*>(UBallisticsSubsystem::GetData()));
	Targets.Remove(nullptr);
	TArray<FString> Pairs;
	Spec.ParseIntoArray(Pairs, TEXT(";"));
	for (const FString& Pair : Pairs)
	{
		FString Name, Value;
		if (!Pair.Split(TEXT("="), &Name, &Value))
		{
			continue;
		}
		Name.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();
		bool bAny = false;
		for (UObject* Target : Targets)
		{
			bAny |= ApplyTuningOverride(Target, Name, Value);
		}
		if (!bAny)
		{
			UE_LOG(LogSquadMatch, Warning, TEXT("Tuning override %s: no data asset has that property."), *Name);
		}
	}
}
