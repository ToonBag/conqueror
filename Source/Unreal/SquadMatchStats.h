#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KillFeedTypes.h"
#include "SquadTypes.h"
#include "BallisticsTypes.h"
#include "SquadMatchStats.generated.h"

class ASoldier;
class AVictoryPoint;
struct FHitChanceInfo;
struct FKillCause;

/** The target as it was when a round reached it, before the hit changed it (projectile rounds are recorded afterwards). */
struct FShotTargetSnapshot
{
	ESoldierStance Stance = ESoldierStance::Standing;
	bool bMoving = false;
	bool bInjured = false;
};

/**
 * Server-only match recorder for balancing (AI test matches, squad.MatchStats 1). It records every rifle shot
 * (with its hit chance and roll), what each hit did, every grenade and C4 blast, medkit treatments, VP captures
 * and how long soldiers spend in each stress tier, then writes one JSON summary and one CSV of shots per match
 * to Saved/MatchStats/. Nothing here changes the game; clients never record.
 *
 * Console:
 *   squad.MatchStats 1          record and write files at match end (default 0)
 *   squad.MatchTimeLimit <s>    end the record as a "timeout" after this many seconds of play (0: none)
 *   squad.QuitAfterMatch 1      close the process once the record is written (batch runs)
 */
UCLASS()
class UNREAL_API USquadMatchStats : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The recorder of this world if it records (server with squad.MatchStats 1), else null. */
	static USquadMatchStats* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bRecording; }

	void OnMatchStarted(int32 Seed, int32 SquadsPerTeam);
	/** Writes the record. Reason: "annihilation", "victory_points", "draw" or "timeout". */
	void OnMatchEnded(ESquadTeam Winner, const FString& Reason);

	/** Call before the hit is applied, or pass TargetBefore. HitPart: projectile model only. */
	void RecordShot(const ASoldier& Shooter, const ASoldier& Target, const FHitChanceInfo& Chance, float Roll, bool bHit, const TCHAR* HitPart = TEXT(""),
		const FShotTargetSnapshot* TargetBefore = nullptr);
	/** After a rifle hit: Outcome "injured", "killed" or "none" (invulnerable / already dead). */
	void RecordHitOutcome(const FString& Outcome);
	/** The shot row RecordHitOutcome would fill (the last rolled shot); INDEX_NONE if none. */
	int32 TakePendingOutcomeRow() { const int32 Row = PendingOutcomeRow; PendingOutcomeRow = INDEX_NONE; return Row; }
	/** Fills a row taken earlier with TakePendingOutcomeRow (hits resolved at the end of the server tick). */
	void RecordHitOutcomeAt(int32 Row, const FString& Outcome);
	/**
	 * Projectile model (GDD §16): one row per round when it stops. bHitTarget: it hit the soldier aimed at, and
	 * Outcome is "injured" / "killed" / "none"; otherwise Outcome says where it went ("miss_cover",
	 * "miss_ground", "miss_lost", "stray_enemy", "friendly").
	 */
	void RecordBallisticShot(const ASoldier& Shooter, const ASoldier& Target, const FHitChanceInfo& Chance, bool bHitTarget,
		EHitPart Part, const FString& Outcome, const FShotTargetSnapshot& TargetBefore);
	/** Projectile model: a round hit a soldier it was not aimed at. Effect: "injured", "killed" or "none". */
	void RecordStrayHit(const ASoldier& Shooter, const ASoldier& Victim, bool bFriendly, float DistanceMetres, const FString& Effect);
	void RecordGrenadeThrown(const ASoldier& Thrower, FIntPoint From, FIntPoint Target);
	void RecordDeath(const ASoldier& Victim, const FKillCause& Cause);
	void RecordBlastVictim(const ASoldier* Instigator, EKillMethod Method, const ASoldier& Victim, const TCHAR* Effect);
	void RecordMedkit(const ASoldier& Medic, const ASoldier& Patient);
	void RecordVPCapture(const AVictoryPoint& VP, ESquadTeam Team);

private:
	struct FSoldierRecord
	{
		TWeakObjectPtr<const ASoldier> Soldier;
		FString Id;
		ESquadTeam Team = ESquadTeam::Allies;
		int32 SquadId = 0;
		int32 Number = 0;
		int32 Kills = 0;
		int32 GrenadeKills = 0;
		int32 HitsTaken = 0;
		int32 TimesInjured = 0;
		int32 TimesStunned = 0;
		int32 GrenadesThrown = 0;
		int32 MedkitsGiven = 0;
		float DeathTime = -1.f;
		FString DeathCause;
		float TierSeconds[4] = { 0.f, 0.f, 0.f, 0.f };
		float PeakStress = 0.f;
	};

	FSoldierRecord& Find(const ASoldier& Soldier);
	FString SoldierId(const ASoldier& Soldier) const;
	float MatchTime() const;
	void WriteFiles(ESquadTeam Winner, const FString& Reason);

	bool bRecording = false;
	bool bWritten = false;
	float StartTime = 0.f;
	float SampleAccumulator = 0.f;
	int32 MatchSeed = 0;
	int32 NumSquadsPerTeam = 1;
	FString StartStamp;

	TArray<FSoldierRecord> Soldiers;
	/** One CSV row per shot; the last row's outcome is filled in by RecordHitOutcome. */
	TArray<FString> ShotRows;
	int32 PendingOutcomeRow = INDEX_NONE;
	TArray<FString> Events;
	/** Per blast-causing thrower: blasts that hit enemies / friends. */
	int32 GrenadesThrownTotal[2] = { 0, 0 };
	int32 BlastKills[2] = { 0, 0 };
	int32 BlastStuns[2] = { 0, 0 };
	int32 FriendlyBlastCasualties[2] = { 0, 0 };
	int32 VPCaptures[2] = { 0, 0 };
	/** Projectile model, per shooter's team. */
	bool bBallisticsMatch = false;
	int32 FriendlyFireHits[2] = { 0, 0 };
	int32 FriendlyFireInjuries[2] = { 0, 0 };
	int32 StrayEnemyHits[2] = { 0, 0 };
	int32 HitParts[2][6] = { { 0 } };
	int32 RoundsStoppedByCover[2] = { 0, 0 };
};
