#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "SquadTypes.h"
#include "SquadBot.generated.h"

class AGrenade;
class AGridManager;
class ASoldier;
class AVictoryPoint;

/**
 * Server-only AI commander for one team's squad, so a single player can play test against a full opposing
 * squad (GDD §11.1). It plays like a person at the keyboard rather than an all-seeing script:
 *
 *  - It only gives orders a player could give (move, stop, face, grenade) through the ASoldier order API,
 *    so it obeys every game rule. Soldiers still fire on their own (GDD §5.3) exactly as the player's do.
 *  - It has a simulated camera: the same angle, field of view and zoom limits as the player's. It looks at
 *    one place at a time, pans at the player's pan speed, and moves its attention between its soldiers. A
 *    soldier coming under fire off screen draws its attention there after a moment, as a flashing squad card
 *    would.
 *  - It notices an enemy only when that enemy is spotted by its team (GDD §9.3), is on its screen, and isn't
 *    hidden from the camera behind a wall or tree, and only after a reaction delay that grows for prone
 *    targets, targets in cover and targets near the screen edge. It remembers enemies for a while after
 *    losing sight of them, at their last seen position.
 *  - It issues orders one at a time with a short gap between them, like clicks.
 *  - Soldiers on the move usually finish their move into position; only a good shot at a noticed enemy
 *    with a long way still to go may make it stop to fight.
 *
 * Plan: every able soldier is assigned a VP (unowned VPs first, one each; spares reinforce the most
 * threatened VP). Soldiers hold cells with cover facing the enemy and a view of it, spaced apart, throw
 * grenades at noticed enemies when shots are poor or enemies bunch up (never near a squad-mate), and run from
 * grenades they notice landing near them.
 *
 * Tactics (GDD §11.1): long moves go in bounds from cover to cover with pauses, half the squad covering the
 * other half when contact is expected; soldiers don't cross ground a free enemy can see (squad-mates pin it
 * first), break for cover when caught in the open under fire, keep their heads down behind low cover when
 * badly stressed, and flank an enemy whose cover they can't shoot through. Enemies the team spots off screen
 * are noticed on the minimap, more slowly.
 *
 * With several squads per team (squad.SquadsPerTeam) each squad has its own bot. Bots of one team see each
 * other's VP assignments and slots (as teammates would) to spread out and not grenade each other.
 *
 * The game mode spawns one for each squad no player commands (console: squad.Bot 0 / 1, squad.BotDebug 1).
 */
UCLASS(NotPlaceable)
class UNREAL_API ASquadBot : public AInfo
{
	GENERATED_BODY()

public:
	ASquadBot();

	virtual void Tick(float DeltaSeconds) override;

	/** Team this bot commands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot")
	ESquadTeam Team = ESquadTeam::Axis;

	/** Squad of Team this bot commands (ASoldier::SquadId). A team with several squads has one bot per squad. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot", meta = (ClampMin = "0"))
	int32 SquadId = 0;

	/**
	 * With several squads on a team: each soldier of another friendly squad already heading for a VP makes that
	 * VP look this many metres further away, so squads spread over the VPs instead of all taking the nearest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot", meta = (ClampMin = "0"))
	float FriendlySquadSpreadPenalty = 6.f;

	// ---------------------------------------------------------------- Simulated player

	/** Camera arm length the bot plays at (cm), clamped to the player camera's zoom limits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Camera", meta = (ClampMin = "100"))
	float CameraZoom = 3000.f;

	/** Seconds the camera stays on one area before moving on to check another soldier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Camera", meta = (ClampMin = "0.1"))
	FVector2D CameraDwellTime = FVector2D(2.5f, 4.5f);

	/** Seconds before the camera reacts to a soldier coming under fire off screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Camera", meta = (ClampMin = "0"))
	FVector2D AlertReactionTime = FVector2D(0.6f, 1.2f);

	/** Base seconds to notice an enemy in plain view at the centre of the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Perception", meta = (ClampMin = "0"))
	FVector2D NoticeTime = FVector2D(0.5f, 1.1f);

	/** Extra seconds to notice a prone enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Perception", meta = (ClampMin = "0"))
	float NoticeProneDelay = 0.6f;

	/** Extra seconds to notice an enemy in cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Perception", meta = (ClampMin = "0"))
	float NoticeCoverDelay = 0.4f;

	/** Extra seconds to notice an enemy at the very edge of the screen (scales from 0 at the centre). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Perception", meta = (ClampMin = "0"))
	float NoticeEdgeDelay = 0.6f;

	/**
	 * Seconds to notice an enemy the team spots off screen: the minimap shows it as a red dot (GDD §13). Once noticed it
	 * is followed on the minimap while the team keeps it in sight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Perception", meta = (ClampMin = "0"))
	FVector2D MinimapNoticeTime = FVector2D(1.5f, 3.f);

	/** Seconds a noticed enemy is remembered (at its last seen cell) after it was last seen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Perception", meta = (ClampMin = "0"))
	float MemoryTime = 10.f;

	/** Seconds between two orders (a player clicks one order at a time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Orders", meta = (ClampMin = "0"))
	FVector2D OrderInterval = FVector2D(0.3f, 0.8f);

	/** Seconds between each soldier's decisions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Orders", meta = (ClampMin = "0.05"))
	float DecisionInterval = 0.4f;

	/** Seconds between squad-level re-plans (which soldier goes to which VP). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Orders", meta = (ClampMin = "0.1"))
	float AssignInterval = 2.f;

	// ---------------------------------------------------------------- Moving under fire

	/** A moving soldier may stop to fight only for a shot at least this good (percent)... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Contact")
	float HaltForShotChance = 45.f;

	/** ...with at least this many steps of its move still to go (shorter moves are always finished)... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Contact", meta = (ClampMin = "0"))
	int32 HaltMinRemainingSteps = 5;

	/** ...and then only with this probability, decided once per contact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Contact", meta = (ClampMin = "0", ClampMax = "1"))
	float HaltProbability = 0.4f;

	// ---------------------------------------------------------------- Grenades & cover

	/** Throw a grenade when every shot at the target is below this hit chance (percent)... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Grenades")
	float GrenadeHitChanceThreshold = 25.f;

	/** ...or when at least this many noticed enemies are within the stun radius of the target cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Grenades", meta = (ClampMin = "1"))
	int32 GrenadeClusterCount = 2;

	/** Seconds a soldier waits after throwing before it considers another grenade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Grenades", meta = (ClampMin = "0"))
	float GrenadeCooldown = 8.f;

	/** Never throw if a squad-mate is within the stun radius plus this many metres of the landing cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Grenades", meta = (ClampMin = "0"))
	float FriendlySafetyMargin = 1.f;

	/** Seconds to notice a grenade on screen before reacting to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Grenades", meta = (ClampMin = "0"))
	float GrenadeNoticeTime = 0.4f;

	/** Under fire and out of cover: move to cover unless its own shot is at least this good (percent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover")
	float SeekCoverHitChance = 35.f;

	/** How far (cells) a soldier looks for cover to fall back to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover", meta = (ClampMin = "1"))
	int32 CoverSearchRadius = 5;

	/** Score added to a VP cell that sees the expected enemy position within rifle range (cover is worth up to 60). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover", meta = (ClampMin = "0"))
	float FieldOfFireBonus = 20.f;

	/** A soldier caught in the open on a move with at least this many steps left breaks for the nearest cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover", meta = (ClampMin = "1"))
	int32 BreakForCoverMinSteps = 4;

	/** Behind hay or a low wall, at this stress or more and with poor shots, the soldier goes prone (hidden) to recover... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover", meta = (ClampMin = "0", ClampMax = "100"))
	float HeadDownStress = 66.f;

	/** ...and only while its best shot is below this hit chance (percent)... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover")
	float HeadDownMaxShot = 30.f;

	/** ...and comes back up (standing) once stress has fallen to this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover", meta = (ClampMin = "0", ClampMax = "100"))
	float HeadUpStress = 30.f;

	// ---------------------------------------------------------------- Advancing (bounds and overwatch)

	/**
	 * Longest bound (cells) between cover positions on the way to a slot, with no enemy expected (X) and in contact
	 * (Y: an enemy is known, the soldier is under fire, or it is within CautionRadius of a VP the team doesn't hold). Longer moves are broken into bounds that end in cover.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Advance")
	FVector2D BoundLength = FVector2D(12.f, 7.f);

	/**
	 * Within this many metres of a VP the team doesn't hold (or that is contested) the squad advances as if in contact:
	 * short bounds, longer pauses, half the squad covering the other half. The enemy is heading for the same VPs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Advance", meta = (ClampMin = "0"))
	float CautionRadius = 25.f;

	/** Seconds a soldier waits in cover at the end of a bound, with no enemy expected (X) and in contact (Y). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Advance")
	FVector2D BoundPauseQuiet = FVector2D(0.3f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Advance")
	FVector2D BoundPauseContact = FVector2D(1.5f, 3.f);

	/** In contact (see BoundLength), at most this fraction of the squad's able soldiers move at once; the rest cover them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Advance", meta = (ClampMin = "0.1", ClampMax = "1"))
	float MaxMovingFraction = 0.5f;

	/** A soldier fighting from a bound position keeps advancing (when it may move) while its shot is below this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Advance")
	float AdvanceUnderFireShot = 30.f;

	// ---------------------------------------------------------------- Flanking

	/** A soldier whose best shot is below this (an enemy in cover) may move round to a cell that cover doesn't protect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Flank")
	float FlankShotChance = 25.f;

	/** How far (cells) a soldier looks for a flanking position. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Flank", meta = (ClampMin = "1"))
	int32 FlankSearchRadius = 8;

	/** Seconds the soldier holds a flanking position before going back to its VP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Flank", meta = (ClampMin = "0"))
	float FlankHoldTime = 20.f;

	/** Seconds before the same soldier tries another flank. One soldier of a squad flanks at a time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Flank", meta = (ClampMin = "0"))
	float FlankCooldown = 25.f;

	// ---------------------------------------------------------------- Medkits (GDD §5.2)

	/** Seconds after a squad-mate is injured before the bot sends a medic (a player noticing the red squad card). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Medkit", meta = (ClampMin = "0"))
	FVector2D MedicReactionTime = FVector2D(1.f, 2.5f);

	/** Only soldiers this many metres or closer to an injured squad-mate are sent to treat it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Medkit", meta = (ClampMin = "0"))
	float MaxMedicDistance = 12.f;

	/**
	 * GDD §5.1: once an injured squad-mate has this many seconds or fewer left before it bleeds out, the bot
	 * sends medics from up to UrgentMedicDistance, if they can still get there and finish in time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Medkit", meta = (ClampMin = "0", Units = "s"))
	float UrgentBleedOutTime = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Medkit", meta = (ClampMin = "0"))
	float UrgentMedicDistance = 30.f;

	/** Noticed enemies within this many metres of a VP make it "threatened" and draw spare soldiers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot", meta = (ClampMin = "0"))
	float ThreatRadius = 15.f;

	/** The bot commanding that squad of Team in this world, if any. */
	static ASquadBot* FindForSquad(const UObject* WorldContextObject, ESquadTeam Team, int32 SquadId);

protected:
	virtual void BeginPlay() override;

private:
	struct FSoldierBrain
	{
		TWeakObjectPtr<ASoldier> Soldier;
		TWeakObjectPtr<AVictoryPoint> AssignedVP;
		/** Cell inside the assigned VP the soldier is heading for / holding. */
		FIntPoint Slot = FIntPoint(INT32_MIN, INT32_MIN);
		bool bHasSlot = false;
		bool bFacedAfterArriving = false;
		/** Already decided whether to stop for the current contact while moving. */
		bool bContactDecided = false;
		float NextDecision = 0.f;
		float NextGrenade = 0.f;
		/** Don't re-decide to reposition before this time (stops dithering between cover cells). */
		float NextReposition = 0.f;
		/** Don't retry walking to the slot before this time (the squad cohesion limit held the soldier short of it). */
		float NextHoldRetry = 0.f;
		/** Squad-card alert state last frame: under fire / damage flash. */
		bool bWasAlerting = false;
		/** While injured: when the bot will have noticed and may send a medic (-1 while not injured). */
		float InjuryNoticedAt = -1.f;
		/** On a bound towards the slot: the cover cell it is heading for. */
		bool bBounding = false;
		FIntPoint BoundCell = FIntPoint(INT32_MIN, INT32_MIN);
		/** Don't start the next bound before this time (pausing in cover). */
		float NextBound = 0.f;
		/** Went prone behind low cover to recover from stress (GDD §8, §9.3). */
		bool bHeadDown = false;
		/** Holding a flanking position (Slot, outside the VP) until this time. */
		float FlankUntil = 0.f;
		float NextFlank = 0.f;
	};

	/**
	 * GDD §5.2: send this soldier to treat the nearest injured squad-mate the bot has noticed, if it carries a
	 * medkit, isn't under fire, is within MaxMedicDistance and is the closest able soldier to that squad-mate.
	 */
	bool TryTreatSquadMate(FSoldierBrain& Brain);

	/** What the bot knows about one enemy soldier. */
	struct FEnemyMemory
	{
		TWeakObjectPtr<ASoldier> Soldier;
		/** Seconds it has been continuously on screen and visible, working towards being noticed. */
		float NoticeProgress = 0.f;
		/** Delay needed to notice it this time (rolled when it comes into view). */
		float NoticeRequired = -1.f;
		/** The same for spotting it as a red dot on the minimap while it is off screen (slower than on screen). */
		float MapProgress = 0.f;
		float MapRequired = -1.f;
		bool bKnown = false;
		FIntPoint LastKnownCell = FIntPoint::ZeroValue;
		float LastSeenTime = -1000.f;
	};

	/** The bot's screen: the ground area its simulated camera shows. */
	struct FView
	{
		FVector CameraLocation = FVector::ZeroVector;
		FVector2D Corners[4];
		FVector2D Centre = FVector2D::ZeroVector;
		float Radius = 1.f;

		bool Contains(const FVector2D& Point) const;
		/** 0 at the centre of the screen, 1 at its edge. */
		float EdgeFactor(const FVector2D& Point) const;
	};

	void GatherSquad();
	void AssignVictoryPoints();

	// Simulated player
	void UpdateCamera(float DeltaSeconds);
	void ComputeView();
	void UpdatePerception(float DeltaSeconds);
	bool IsOnScreen(const FVector& WorldLocation) const;
	bool IsHiddenFromCamera(const ASoldier& Enemy) const;
	bool CanIssueOrder() const;
	void OnOrderIssued();

	void Think(FSoldierBrain& Brain);
	bool TryDodgeGrenade(ASoldier& Soldier);
	bool TryThrowGrenade(FSoldierBrain& Brain);
	bool TrySeekCover(FSoldierBrain& Brain, FIntPoint ThreatCell);
	/** Move towards the slot (in bounds from cover to cover, covered by squad-mates while enemies are known). */
	void HoldObjective(FSoldierBrain& Brain);
	bool PickSlot(FSoldierBrain& Brain);

	/** Next cover cell on the way to Goal, at most a bound away; false if the soldier should just go straight. */
	bool PickBoundCell(const FSoldierBrain& Brain, FIntPoint Goal, FIntPoint& OutCell) const;

	/**
	 * True if a recently seen enemy that is free to shoot (not under fire itself, not downed) would see the soldier
	 * on the way to Goal or at Goal without cover: a player doesn't walk a soldier across an enemy's sights.
	 */
	bool IsRouteWatched(const ASoldier& Soldier, FIntPoint Goal) const;

	/** Contact or expected contact: an enemy is known, the soldier is under fire, or it is near a VP the team doesn't hold. */
	bool IsCautious(const ASoldier& Soldier) const;

	/** While cautious, true if enough squad-mates are already moving that this soldier should cover them. */
	bool ShouldHoldForOverwatch(const ASoldier& Soldier) const;

	/** Prone and able to stand: order it up before a move (prone soldiers crawl). True if an order was issued. */
	bool StandUpToMove(ASoldier& Soldier);

	/** Go prone behind low cover when stressed and outgunned; come back up once recovered. True if an order was issued. */
	bool UpdateHeadDown(FSoldierBrain& Brain, float BestChance);

	/** Move round an enemy that sits in cover to a cell its cover doesn't protect against. */
	bool TryFlank(FSoldierBrain& Brain, float BestChance);

	/** Score (higher is better) of Cell as a place to hold: cover against the enemy, spacing from squad-mates. */
	float ScoreHoldCell(FIntPoint Cell, const ASoldier& Soldier, FIntPoint ThreatCell) const;

	/** Where the enemy is expected: the nearest noticed enemy's last known cell, else their deployment zone. */
	FIntPoint GetThreatCell(FIntPoint From) const;

	/** Noticed enemies that are alive, with the cell the bot believes each is on. */
	void GetKnownEnemies(TArray<TPair<ASoldier*, FIntPoint>>& Out) const;

	bool IsSlotTaken(FIntPoint Cell, const ASoldier* Except) const;
	int32 CountKnownEnemiesNear(FVector Location, float RadiusMetres) const;
	/** Any living friendly soldier (any squad of the team) at, or heading for, a spot within RadiusMetres. */
	bool IsFriendlyNear(FVector Location, float RadiusMetres) const;

	/** Soldiers of the team's other bot squads assigned to VP. */
	int32 CountOtherSquadsAssigned(const AVictoryPoint* VP) const;

	float Now() const;

	UPROPERTY(Transient)
	TObjectPtr<AGridManager> Grid;

	TArray<FSoldierBrain> Brains;
	TArray<FEnemyMemory> Memory;
	TArray<TWeakObjectPtr<AVictoryPoint>> VictoryPoints;
	FIntPoint EnemyHomeCell = FIntPoint::ZeroValue;
	float NextAssign = 0.f;

	// Camera
	FVector2D CameraFocus = FVector2D::ZeroVector;
	FVector2D CameraGoal = FVector2D::ZeroVector;
	float CameraYaw = 0.f;
	float CameraDwellUntil = 0.f;
	int32 NextSoldierToCheck = 0;
	FView View;
	/** Pending "look at that soldier" after an off-screen alert, and when the camera reacts. */
	TWeakObjectPtr<ASoldier> PendingAlert;
	float PendingAlertTime = 0.f;

	// Orders
	float NextOrderTime = 0.f;

	/** When each grenade was first seen on screen. */
	TMap<TWeakObjectPtr<AGrenade>, float> SeenGrenades;
};
