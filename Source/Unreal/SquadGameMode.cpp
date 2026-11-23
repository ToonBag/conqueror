#include "SquadGameMode.h"

#include "Soldier.h"
#include "SquadBot.h"
#include "SquadCameraPawn.h"
#include "SquadGameState.h"
#include "SquadHUD.h"
#include "SquadPlayerController.h"
#include "SquadPlayerState.h"
#include "TacticalPlan.h"
#include "VictoryPoint.h"
#include "WeaponData.h"
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

	TAutoConsoleVariable<float> CVarVPWinTime(
		TEXT("squad.VPWinTime"), 0.f,
		TEXT("Overrides the Victory Point win timer in seconds for testing (0: use the game mode's VPWinTime, 5 minutes)."),
		ECVF_Cheat);

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
	PC->AssignTeam(Team);
	UE_LOG(LogSquadMatch, Log, TEXT("%s joins the %s."), PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("Player"), TeamName(Team));

	// A player joining a bot-run team takes the squad over.
	if (ASquadBot* Bot = ASquadBot::FindForTeam(this, Team))
	{
		UE_LOG(LogSquadMatch, Log, TEXT("The %s AI bot hands its squad to the player."), TeamName(Team));
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
	if (PC && bMatchStarted && !bMatchEnded && bSpawnBotsForUncommandedTeams && CountPlayers(PC->PlayerTeam, PC) == 0)
	{
		UE_LOG(LogSquadMatch, Log, TEXT("Every %s player has left."), TeamName(PC->PlayerTeam));
		SpawnBots(PC);
	}
}

void ASquadGameMode::StartMatch()
{
	bMatchStarted = true;
	UE_LOG(LogSquadMatch, Log, TEXT("Match starts with %d player(s)."), GetNumPlayers());
	SpawnTacticalPlans();
	GiveTestMachineGuns();
	if (bSpawnBotsForUncommandedTeams)
	{
		SpawnBots();
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
	TSet<ESquadTeam> CommandedTeams;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ASquadPlayerController* PC = Cast<ASquadPlayerController>(It->Get()))
		{
			if (!TeamsGivenTestMG.Contains(PC->PlayerTeam))
			{
				CommandedTeams.Add(PC->PlayerTeam);
			}
		}
	}
	TeamsGivenTestMG.Append(CommandedTeams);
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		ASoldier* Soldier = *It;
		if (Soldier->GetSquadNumber() != TestMachineGunSoldier || !CommandedTeams.Contains(Soldier->Team) || Soldier->IsDead())
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
	if (!bMatchStarted && (GetNetMode() != NM_DedicatedServer || GetNumPlayers() >= FMath::Max(1, CVarMinPlayers.GetValueOnGameThread())))
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
	TSet<ESquadTeam> SquadTeams;
	for (TActorIterator<ASoldier> It(GetWorld()); It; ++It)
	{
		SquadTeams.Add(It->Team);
	}
	TSet<ESquadTeam> CommandedTeams;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ASquadPlayerController* PC = Cast<ASquadPlayerController>(It->Get()); PC && PC != Except)
		{
			CommandedTeams.Add(PC->PlayerTeam);
		}
	}
	for (ESquadTeam Team : SquadTeams)
	{
		if (CommandedTeams.Contains(Team) || ASquadBot::FindForTeam(this, Team))
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.Name = MakeUniqueObjectName(GetWorld()->GetCurrentLevel(), ASquadBot::StaticClass(), *FString::Printf(TEXT("SquadBot_%s"), TeamName(Team)));
		Params.bDeferConstruction = true;
		if (ASquadBot* Bot = GetWorld()->SpawnActor<ASquadBot>(ASquadBot::StaticClass(), FTransform::Identity, Params))
		{
			Bot->Team = Team;
			Bot->FinishSpawning(FTransform::Identity);
			UE_LOG(LogSquadMatch, Log, TEXT("No player commands the %s squad: an AI bot takes over."), TeamName(Team));

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
	// TODO (GDD §14.4): report the result to the post-match ELO endpoint once the backend exists.
	if (bPauseOnMatchEnd)
	{
		UGameplayStatics::SetGamePaused(this, true);
	}
}
