#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "KillFeedTypes.h"
#include "SquadTypes.h"
#include "SquadGameState.generated.h"

/** How a match ended (GDD §11). */
UENUM(BlueprintType)
enum class EVictoryReason : uint8
{
	None,
	/** Every soldier on the losing team is dead. */
	Annihilation,
	/** The winner held every Victory Point for the full VP win timer. */
	VictoryPoints,
	/** Both teams were wiped out at the same moment (e.g. one grenade). */
	Draw,
	/** The match time limit ran out and the winner had more points. */
	Points,
	/** The match time limit ran out with the points level. */
	TimeDraw
};

/**
 * Replicated match state (GDD §2.1): the kill feed, the Victory Point hold timer and the match result.
 * The server reports each kill with AddKill(); it is multicast to every client, which keeps the recent ones
 * (with the local time they arrived) for the HUD. The game mode decides the result; this only carries it.
 */
UCLASS()
class UNREAL_API ASquadGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	struct FKillFeedItem
	{
		FKillFeedEntry Entry;
		/** World real time (seconds) when this machine received the kill. */
		float ReceivedTime = 0.f;
	};

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server only: announce a kill to every player. */
	void AddKill(const FKillFeedEntry& Entry);

	/** Recent kills on this machine, oldest first. */
	const TArray<FKillFeedItem>& GetKillFeed() const { return KillFeed; }

	// ---------------------------------------------------------------- Victory (GDD §11)

	/** Server only: one team holds every VP (bHolding) and has done so for Elapsed of Required seconds. */
	void SetVPHold(bool bHolding, ESquadTeam HoldingTeam, float Elapsed, float Required);

	/** Server only: end the match. Winner is ignored for a draw. */
	void SetMatchResult(ESquadTeam Winner, EVictoryReason Reason);

	UFUNCTION(BlueprintPure, Category = "Match")
	bool IsMatchOver() const { return VictoryReason != EVictoryReason::None; }

	UFUNCTION(BlueprintPure, Category = "Match")
	ESquadTeam GetWinningTeam() const { return WinningTeam; }

	UFUNCTION(BlueprintPure, Category = "Match")
	EVictoryReason GetVictoryReason() const { return VictoryReason; }

	/** Draw by simultaneous wipe-out or level points at the time limit. */
	UFUNCTION(BlueprintPure, Category = "Match")
	bool IsDraw() const { return VictoryReason == EVictoryReason::Draw || VictoryReason == EVictoryReason::TimeDraw; }

	// ---------------------------------------------------------------- Points (GDD §11)

	/** Server only: points for enemy soldiers that died. */
	void AddKillPoints(ESquadTeam Team, float Points);

	/** Server only: points for owning Victory Points over time. */
	void AddVPPoints(ESquadTeam Team, float Points);

	/** Server only: match time played and the limit (0: no limit). */
	void SetMatchClock(float Elapsed, float Limit);

	UFUNCTION(BlueprintPure, Category = "Match")
	float GetKillPoints(ESquadTeam Team) const { return KillPoints[TeamIndex(Team)]; }

	UFUNCTION(BlueprintPure, Category = "Match")
	float GetVPPoints(ESquadTeam Team) const { return VPPoints[TeamIndex(Team)]; }

	/** Whole points: kill points plus VP points, rounded down. */
	UFUNCTION(BlueprintPure, Category = "Match")
	int32 GetScore(ESquadTeam Team) const { return FMath::FloorToInt(KillPoints[TeamIndex(Team)] + VPPoints[TeamIndex(Team)]); }

	UFUNCTION(BlueprintPure, Category = "Match")
	bool HasMatchTimeLimit() const { return MatchTimeLimit > 0.f; }

	/** Seconds left on the match clock (0 with no limit). */
	UFUNCTION(BlueprintPure, Category = "Match")
	float GetMatchTimeRemaining() const { return HasMatchTimeLimit() ? FMath::Max(0.f, MatchTimeLimit - MatchElapsed) : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Match")
	bool IsVPHeld() const { return bVPHeld; }

	UFUNCTION(BlueprintPure, Category = "Match")
	ESquadTeam GetVPHoldingTeam() const { return VPHoldingTeam; }

	/** Seconds left before the holding team wins on VPs (approximate on clients). */
	UFUNCTION(BlueprintPure, Category = "Match")
	float GetVPHoldRemaining() const { return FMath::Max(0.f, VPHoldRequired - VPHoldElapsed); }

	/** The server runs the experimental projectile model (squad.Ballistics 1, GDD §16). Set by UBallisticsSubsystem. */
	UPROPERTY(Replicated)
	bool bBallistics = false;

private:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastKill(const FKillFeedEntry& Entry);

	TArray<FKillFeedItem> KillFeed;

	/** Older kills are dropped; the HUD shows fewer than this. */
	static constexpr int32 MaxStoredKills = 16;

	UPROPERTY(Replicated)
	bool bVPHeld = false;

	UPROPERTY(Replicated)
	ESquadTeam VPHoldingTeam = ESquadTeam::Allies;

	UPROPERTY(Replicated)
	float VPHoldElapsed = 0.f;

	UPROPERTY(Replicated)
	float VPHoldRequired = 300.f;

	UPROPERTY(Replicated)
	ESquadTeam WinningTeam = ESquadTeam::Allies;

	UPROPERTY(Replicated)
	EVictoryReason VictoryReason = EVictoryReason::None;

	static int32 TeamIndex(ESquadTeam Team) { return Team == ESquadTeam::Axis ? 1 : 0; }

	/** Per team, indexed by TeamIndex (Allies, Axis). */
	UPROPERTY(Replicated)
	float KillPoints[2]; // UObjects start zeroed.

	UPROPERTY(Replicated)
	float VPPoints[2];

	UPROPERTY(Replicated)
	float MatchElapsed = 0.f;

	UPROPERTY(Replicated)
	float MatchTimeLimit = 0.f;
};
