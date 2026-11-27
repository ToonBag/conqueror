#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SquadTypes.h"
#include "SquadGameMode.generated.h"

class ASquadBot;
enum class EVictoryReason : uint8;

/**
 * Match rules (server only, GDD §2.1, §11):
 *  - Annihilation: a team wins at once when every soldier of the other team is dead.
 *  - Victory Points: a team wins when it has owned every VP at the same time for VPWinTime seconds
 *    without a break. A contested VP keeps its owner (GDD §10.3), so it still counts as held.
 * When the match ends the result goes to the game state (shown by the HUD) and the game pauses.
 *
 * Test opponents: a squad that no player commands is given to an AI bot (ASquadBot), so one player can play
 * test against a full enemy squad. Console: squad.Bot 0 / 1, squad.VPWinTime <seconds>.
 */
UCLASS()
class UNREAL_API ASquadGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASquadGameMode();

	virtual void Tick(float DeltaSeconds) override;

	/** GDD §3.5 VP Win Timer: seconds a team must hold every VP to win (default 5 minutes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match", meta = (ClampMin = "1"))
	float VPWinTime = 300.f;

	/** GDD §3.5 Match Time Limit: seconds of play before the match is decided on points (a map's AGridManager can set its own). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match", meta = (ClampMin = "1"))
	float MatchTimeLimit = 1200.f;

	/** GDD §11: points the opposing team earns each time a soldier dies (bleed-outs and team kills included). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match", meta = (ClampMin = "0"))
	float PointsPerKill = 4.f;

	/** Server: a soldier died; its enemies score PointsPerKill. Called by ASoldier::Die. */
	void OnSoldierDied(const class ASoldier& Victim);

	/** Give squads no player commands to an AI bot (play testing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match")
	bool bSpawnBotsForUncommandedTeams = true;

	/** Pause the game when the match ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match")
	bool bPauseOnMatchEnd = true;

	/**
	 * Play testing (GDD §6.1): the soldier with this number (1-4) in every player-commanded squad starts with its
	 * faction's machine gun instead of a rifle. 0 turns it off. Bot squads keep their rifles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match|Testing", meta = (ClampMin = "0", ClampMax = "4"))
	int32 TestMachineGunSoldier = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match|Testing")
	TSoftObjectPtr<class UWeaponData> AlliedTestMachineGun = TSoftObjectPtr<UWeaponData>(FSoftObjectPath(TEXT("/Game/Data/DA_M1919.DA_M1919")));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match|Testing")
	TSoftObjectPtr<class UWeaponData> AxisTestMachineGun = TSoftObjectPtr<UWeaponData>(FSoftObjectPath(TEXT("/Game/Data/DA_MG42.DA_MG42")));

	/**
	 * Play testing (GDD §6.2): the soldier with this number (1-4) in every player-commanded squad starts with its
	 * faction's submachine gun instead of a rifle. 0 turns it off. Bot squads keep their rifles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match|Testing", meta = (ClampMin = "0", ClampMax = "4"))
	int32 TestSubmachineGunSoldier = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match|Testing")
	TSoftObjectPtr<class UWeaponData> AlliedTestSubmachineGun = TSoftObjectPtr<UWeaponData>(FSoftObjectPath(TEXT("/Game/Data/DA_Thompson.DA_Thompson")));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match|Testing")
	TSoftObjectPtr<class UWeaponData> AxisTestSubmachineGun = TSoftObjectPtr<UWeaponData>(FSoftObjectPath(TEXT("/Game/Data/DA_MP40.DA_MP40")));

	/** Each joining player goes to the team with fewer players (Allies on a tie) and takes over its bot, if any. */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** A team left with no players goes back to a bot (play testing). */
	virtual void Logout(AController* Exiting) override;

private:
	/** Starts the match: plans, test MGs and bots. Waits for the first player on a dedicated server (squad.MinPlayers). */
	void StartMatch();

	/** Balancing tests: applies squad.TuningOverrides to the loaded data assets. */
	void ApplyTuningOverrides();

	/** Bots for squads no player commands (Except: a player logging out). */
	void SpawnBots(const AController* Except = nullptr);

	/**
	 * Gives TestMachineGunSoldier and TestSubmachineGunSoldier in each player-commanded squad their faction's
	 * machine gun and submachine gun (once per squad).
	 */
	void GiveTestWeapons();

	/** Teams that have soldiers on this map. */
	TArray<ESquadTeam> GetSquadTeams() const;

	/** Players on Team, leaving out Except. */
	int32 CountPlayers(ESquadTeam Team, const AController* Except = nullptr) const;

	/** Squads (SquadKey) that have had their test weapons. */
	TSet<int32> SquadsGivenTestWeapons;

	static int32 SquadKey(ESquadTeam Team, int32 SquadId) { return static_cast<int32>(Team) * 1000 + SquadId; }

	/** squad.SquadsPerTeam: copies the level's squad of each team into its deployment zone until each team has that many. */
	void SpawnExtraSquads();

	/** squad.SwapSides: each faction starts in the other's deployment zone (balancing test). */
	void SwapSides();

	/** Squads of Team on this map (SquadIds, ascending). */
	TArray<int32> GetSquadIds(ESquadTeam Team) const;

	/** Squads (SquadKey) a player commands, leaving out Except. */
	TSet<int32> GetCommandedSquads(const AController* Except = nullptr) const;

	virtual void BeginPlay() override;

	/** Squads per team and seed this match runs with (for the stats record). */
	int32 NumSquadsPerTeam = 1;
	int32 MatchSeed = 0;

	/** One tactical plan per team (GDD §13.1). */
	void SpawnTacticalPlans();

	/**
	 * GDD §13.1: copies each player's team onto their player state and gives each team's highest-ELO player the
	 * CO role and the second-highest the XO role (ties: whoever joined first). Others with delegated planning
	 * rights are Planners. Runs every tick so roles follow players joining, leaving or changing ELO.
	 */
	void UpdateTacticalRoles();
	void CheckVictory(float DeltaSeconds);
	void EndMatch(ESquadTeam Winner, EVictoryReason Reason);
	float GetVPWinTime() const;

	/** squad.MatchTime, else the map's limit, else MatchTimeLimit. 0: no limit. */
	float GetMatchTimeLimit() const;

	/** Seconds of play since the match started. */
	float MatchElapsed = 0.f;

	bool bMatchStarted = false;
	bool bMatchEnded = false;

	/** Team holding every VP right now, and for how long without a break. */
	bool bVPHeld = false;
	ESquadTeam VPHoldingTeam = ESquadTeam::Allies;
	float VPHoldElapsed = 0.f;
};
