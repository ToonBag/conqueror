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
 * threatened VP). Soldiers hold cells with cover facing the enemy, spaced apart, fall back to nearby cover
 * when pinned in the open with poor shots, throw grenades at noticed enemies when shots are poor or enemies
 * bunch up (never near a squad-mate), and run from grenades they notice landing near them.
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

	/** Fall back to cover when under fire, out of cover and the best shot is below this hit chance (percent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover")
	float SeekCoverHitChance = 20.f;

	/** How far (cells) a soldier looks for cover to fall back to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Cover", meta = (ClampMin = "1"))
	int32 CoverSearchRadius = 5;

	// ---------------------------------------------------------------- Medkits (GDD §5.2)

	/** Seconds after a squad-mate is injured before the bot sends a medic (a player noticing the red squad card). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Medkit", meta = (ClampMin = "0"))
	FVector2D MedicReactionTime = FVector2D(1.f, 2.5f);

	/** Only soldiers this many metres or closer to an injured squad-mate are sent to treat it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot|Medkit", meta = (ClampMin = "0"))
	float MaxMedicDistance = 12.f;

	/** Noticed enemies within this many metres of a VP make it "threatened" and draw spare soldiers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bot", meta = (ClampMin = "0"))
	float ThreatRadius = 15.f;

	/** The bot commanding Team in this world, if any. */
	static ASquadBot* FindForTeam(const UObject* WorldContextObject, ESquadTeam Team);

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
	void HoldObjective(FSoldierBrain& Brain);
	bool PickSlot(FSoldierBrain& Brain);

	/** Score (higher is better) of Cell as a place to hold: cover against the enemy, spacing from squad-mates. */
	float ScoreHoldCell(FIntPoint Cell, const ASoldier& Soldier, FIntPoint ThreatCell) const;

	/** Where the enemy is expected: the nearest noticed enemy's last known cell, else their deployment zone. */
	FIntPoint GetThreatCell(FIntPoint From) const;

	/** Noticed enemies that are alive, with the cell the bot believes each is on. */
	void GetKnownEnemies(TArray<TPair<ASoldier*, FIntPoint>>& Out) const;

	bool IsSlotTaken(FIntPoint Cell, const ASoldier* Except) const;
	int32 CountKnownEnemiesNear(FVector Location, float RadiusMetres) const;
	bool IsSquadMateNear(FVector Location, float RadiusMetres) const;

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
