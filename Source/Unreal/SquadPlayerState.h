#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SquadTypes.h"
#include "TacticalPlanTypes.h"
#include "SquadPlayerState.generated.h"

/**
 * Per-player data everyone can see (GDD §2.1): ELO (and the rank it gives) and regiment tag. The player name is
 * APlayerState::GetPlayerName() (the Steam persona once Online Subsystem Steam is in).
 * Until the backend exists, Elo and RegimentTag are set by hand (defaults, or the editor / console).
 */
UCLASS()
class UNREAL_API ASquadPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Individual ELO (GDD §12.1). New players start at 100. */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Squad|Player")
	int32 Elo = 100;

	/** Regiment tag shown in brackets in the kill feed (GDD §12.3). Empty if not in a regiment. */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Squad|Player")
	FString RegimentTag;

	/** Team this player commands a squad on. Server-set from the player controller. */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Squad|Player")
	ESquadTeam Team = ESquadTeam::Allies;

	/** Command role in this match (GDD §13.1): CO / XO by ELO, Planner if given planning rights. Server-set. */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Squad|Player")
	ETacticalRole TacticalRole = ETacticalRole::Soldier;

	/** The CO or XO gave this player planning rights (GDD §13.1). Server-set. */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Squad|Player")
	bool bPlanningDelegated = false;

	/** May draw on the team's tactical map: CO, XO or a delegated planner. */
	UFUNCTION(BlueprintPure, Category = "Squad|Player")
	bool CanPlan() const { return TacticalRole != ETacticalRole::Soldier; }

	/** CO or XO: may clear the plan, delegate planning and acknowledge order requests. */
	UFUNCTION(BlueprintPure, Category = "Squad|Player")
	bool HasCommandRole() const { return TacticalPlan::IsCommandRole(TacticalRole); }

	UFUNCTION(BlueprintPure, Category = "Squad|Player")
	FString GetRankAbbreviation() const { return RankAbbreviationForElo(Elo); }

	/** Player rank ladder (GDD §12.1) as a short tag: PVT, PFC, CPL, SGT, SSG, PSG, MSG, 1SG, SGM, WO1, WO2, WO3, CWO. */
	static FString RankAbbreviationForElo(int32 Elo);
};
