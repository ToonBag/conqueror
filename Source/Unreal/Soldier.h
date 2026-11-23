#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SquadTypes.h"
#include "CoverData.h"
#include "LootTypes.h"
#include "KillFeedTypes.h"
#include "BuildableTypes.h"
#include "BallisticsTypes.h"
#include "Soldier.generated.h"

struct FBallisticRound;

class ABreachableWall;
class ASoldier;
class AGridManager;
class APlayerState;
class USoldierData;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class USoldierVisualData;
class UTextRenderComponent;
class UWeaponData;
struct FGrenadeThrow;
struct FStressTierEffects;
struct FWallPeek;

/** Who killed a soldier and how, passed to Die() for the kill feed. */
struct FKillCause
{
	/** Soldier that fired the round or threw the grenade (may be the victim itself, or a squad-mate). */
	const class ASoldier* Killer = nullptr;
	EKillMethod Method = EKillMethod::Rifle;
	/** Rifle that fired the killing round (rifle kills only). */
	const UWeaponData* Weapon = nullptr;
	/** Chance (percent) of the roll that killed: the shot's final hit chance, or the grenade kill chance. */
	float ChancePercent = 0.f;
};

/** How likely a shot from one soldier at another is to hit, and why (GDD §5.3, §9). */
struct FHitChanceInfo
{
	/** Weapon accuracy for the range band, after the shooter's stress penalty but before cover (percent). */
	float BaseChance = 0.f;
	/** Shooter's stress accuracy multiplier already applied to BaseChance (GDD §8.1). 1 = unstressed. */
	float StressAccuracyMultiplier = 1.f;
	/** After cover (percent). This is what the 0-100,000 roll is compared against. */
	float FinalChance = 0.f;
	ECoverType Cover = ECoverType::None;
	float CoverReduction = 0.f;
	/** False if the target has its back to the cover (reduced value, GDD §9.2). */
	bool bCoverLeaning = true;
	float DistanceMetres = 0.f;
	bool bInRange = false;
	bool bLineOfSight = true;

	// Projectile model (squad.Ballistics 1, GDD §16). FinalChance is then an estimate from sampled rounds.
	bool bBallistic = false;
	/** Share of the target's body (area-weighted) the shooter's muzzle has a clear line to, percent. */
	float ExposurePercent = 0.f;
	/** Shooter's one-sigma spread right now (mrad), recoil included. */
	float SpreadMrad = 0.f;
	/** Shooter rests its rifle on low cover. */
	bool bBraced = false;
	/** Where the shooter aims: the centre of the target's most visible body part. */
	FVector AimPoint = FVector::ZeroVector;
	EHitPart AimPart = EHitPart::Chest;

	bool CanFire() const { return bInRange && bLineOfSight; }

	/** e.g. "65% -50 LOW WALL = 15%", "85% EXPOSED", "NO LINE OF SIGHT". */
	FString Describe() const;
};

/**
 * One resolved rifle or machine-gun round, reported on the server for demos and tests (ASoldier::OnShotResolved).
 * Roll model: Outcome is "hit" or "miss". Projectile model (GDD §16): "injured" / "killed" / "none" when it hit the
 * soldier aimed at, else "miss_cover", "miss_ground", "miss_lost", "stray_enemy" or "friendly".
 */
struct FSoldierShotReport
{
	const ASoldier* Shooter = nullptr;
	const ASoldier* Target = nullptr;
	/** Soldier the round actually hit (null on a miss). */
	const ASoldier* Victim = nullptr;
	bool bHitTarget = false;
	FHitChanceInfo Chance;
	/** Body part hit (projectile model only). */
	EHitPart Part = EHitPart::Chest;
	FString Outcome;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSoldierShotResolved, const FSoldierShotReport&);

/**
 * Everything a client needs to show a soldier: the server copies its live fields in here every tick
 * (ASoldier::WriteNetState), and clients copy them back into the same fields (ReadNetState), so the HUD and
 * animation code read one set of fields everywhere. Server-only state (RNG, scan timers, AI choices) stays out.
 */
USTRUCT()
struct FSoldierNetState
{
	GENERATED_BODY()

	// Movement: clients glide the soldier to Location / Yaw (see ASoldier::TickClient).
	UPROPERTY() FVector_NetQuantize10 Location = FVector::ZeroVector;
	UPROPERTY() float Yaw = 0.f;
	UPROPERTY() FIntPoint CurrentCell = FIntPoint::ZeroValue;
	UPROPERTY() FIntPoint DestinationCell = FIntPoint::ZeroValue;
	UPROPERTY() FIntPoint StepCell = FIntPoint::ZeroValue;
	UPROPERTY() bool bStepping = false;
	UPROPERTY() TArray<FIntPoint> Path;

	// Condition.
	UPROPERTY() ESoldierStance Stance = ESoldierStance::Standing;
	UPROPERTY() bool bInjured = false;
	UPROPERTY() bool bDead = false;
	UPROPERTY() float StunRemaining = 0.f;
	UPROPERTY() float Stress = 0.f;
	UPROPERTY() bool bSpottedByEnemy = false;
	UPROPERTY() TObjectPtr<APlayerState> OwningPlayer;

	// Combat.
	UPROPERTY() TObjectPtr<ASoldier> AttackTarget;
	UPROPERTY() bool bAutoTarget = false;
	UPROPERTY() bool bHoldFire = false;
	UPROPERTY() float AimRemaining = 0.f;
	UPROPERTY() float LastHitChance = -1.f;
	UPROPERTY() FLootInventory Inventory;
	UPROPERTY() bool bWeaponSetUp = false;
	UPROPERTY() bool bSettingUpWeapon = false;
	UPROPERTY() float SetupRemaining = 0.f;
	UPROPERTY() float SetupDuration = 0.f;
	UPROPERTY() float SetupYaw = 0.f;
	UPROPERTY() bool bReloading = false;
	UPROPERTY() float ReloadRemaining = 0.f;
	UPROPERTY() float ReloadDuration = 0.f;
	UPROPERTY() float UnderFireRemaining = 0.f;
	UPROPERTY() float DamageFlashRemaining = 0.f;
	UPROPERTY() int32 ShotCount = 0;
	UPROPERTY() int32 HitsLanded = 0;
	UPROPERTY() int32 RoundsFiredStat = 0;
	UPROPERTY() float StressReceivedTotal = 0.f;

	// Grenade throw.
	UPROPERTY() float GrenadeCooldown = 0.f;
	UPROPERTY() float GrenadeWindupRemaining = 0.f;
	UPROPERTY() float GrenadeWindupDuration = 0.f;
	UPROPERTY() float GrenadeFollowThroughRemaining = 0.f;
	UPROPERTY() FIntPoint PendingGrenadeCell = FIntPoint::ZeroValue;
	UPROPERTY() bool bGrenadeStepBack = false;
	UPROPERTY() bool bGrenadeStepBackStarted = false;
	UPROPERTY() bool bThrowAfterStepBack = false;
	UPROPERTY() FIntPoint GrenadeStepBackFrom = FIntPoint::ZeroValue;
	UPROPERTY() int32 ThrowCount = 0;
	UPROPERTY() int32 GrenadesReleased = 0;

	// Drag, medkit, build.
	UPROPERTY() TObjectPtr<ASoldier> DraggedSoldier;
	UPROPERTY() TObjectPtr<ASoldier> DraggedBy;
	UPROPERTY() TObjectPtr<ASoldier> MedkitPatient;
	UPROPERTY() TObjectPtr<ASoldier> MedkitTreatedBy;
	UPROPERTY() bool bApplyingMedkit = false;
	UPROPERTY() float MedkitRemaining = 0.f;
	/** Buildables left, indexed by EBuildableKind. */
	UPROPERTY() TArray<int32> BuildableCounts;
	UPROPERTY() TObjectPtr<ABreachableWall> BuildWall;
	UPROPERTY() EBuildableKind BuildKind = EBuildableKind::C4;
	UPROPERTY() bool bBuildChanneling = false;
	UPROPERTY() float BuildRemaining = 0.f;
	UPROPERTY() float BuildDuration = 0.f;

	// Status line text set by an order the server refused or changed ("SQUAD LIMIT", "NO LINE OF SIGHT").
	UPROPERTY() FString Message;
	UPROPERTY() float MessageRemaining = 0.f;
};

/** An order a client sends for one of its soldiers (ASquadPlayerController::ServerSoldierOrder). */
UENUM()
enum class ESoldierOrderType : uint8
{
	Move,
	MoveInGroup,
	Stop,
	Face,
	Attack,
	Stance,
	HoldFire,
	Grenade,
	StartDrag,
	StopDrag,
	Medkit,
	CancelMedkit,
	CancelBuild
};

USTRUCT()
struct FSoldierOrder
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<ASoldier> Soldier;
	UPROPERTY() ESoldierOrderType Type = ESoldierOrderType::Move;
	UPROPERTY() FIntPoint Cell = FIntPoint::ZeroValue;
	/** Attack target, drag casualty or medkit patient. */
	UPROPERTY() TObjectPtr<ASoldier> Other;
	/** Stance (as uint8) or hold fire (0 / 1). */
	UPROPERTY() int32 Param = 0;
	/** MoveInGroup: the slot each squad-mate in the same order is heading to. */
	UPROPERTY() TArray<TObjectPtr<ASoldier>> GroupSoldiers;
	UPROPERTY() TArray<FIntPoint> GroupCells;
};

/**
 * A squad member. Placeholder "bean" body; moves one grid cell at a time.
 *
 * Networking: the server simulates every soldier (Tick, HasAuthority) and replicates FSoldierNetState. Clients
 * only animate (TickClient). An order called on a client is sent to the server (ForwardOrder) and runs there.
 * Stance and health are shown by the bean's posture plus a status line above its number,
 * since there are no proper models or animations yet.
 */
UCLASS()
class UNREAL_API ASoldier : public APawn
{
	GENERATED_BODY()

public:
	ASoldier();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: run an order a client sent for this soldier (the controller has checked it is that player's). */
	void ExecuteOrder(const FSoldierOrder& Order);

	// ---------------------------------------------------------------- Identity / tuning

	/** Order within the squad (0-3). The soldier is labelled SquadIndex+1 and number key SquadIndex+1 selects it. */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Soldier", meta = (ClampMin = "0", ClampMax = "3"))
	int32 SquadIndex = 0;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Soldier")
	ESquadTeam Team = ESquadTeam::Allies;

	/**
	 * Which of its team's squads this soldier belongs to (0 = the squad placed in the level). A team holds one
	 * squad per player (GDD §4.1); the game mode spawns squads beyond the first (squad.SquadsPerTeam). Team,
	 * squad, number, colour and data assets replicate once, so soldiers spawned during play are right on clients.
	 */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Soldier", meta = (ClampMin = "0"))
	int32 SquadId = 0;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Soldier")
	FLinearColor SoldierColor = FLinearColor(0.2f, 0.4f, 0.9f);

	/**
	 * Starting rifle. Falls back to UWeaponData class defaults (M1 Garand) if unset. During play the rifle
	 * actually held lives in the inventory and can change by looting (GDD §5.5); use GetWeaponData().
	 */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Soldier")
	TObjectPtr<UWeaponData> Weapon;

	/** Loadout, stance speeds, damage and grenade tuning. Falls back to class defaults if unset. */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Soldier")
	TObjectPtr<USoldierData> SoldierData;

	/** Uniformed models, held rifle / grenade attachment and animation clips. Without it the soldier is a bean. */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Soldier")
	TObjectPtr<USoldierVisualData> VisualData;

	/** Colour of the flashing square on the cell this soldier has been ordered to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soldier|Move Marker")
	FLinearColor MoveMarkerColor = FLinearColor(1.f, 0.05f, 0.05f);

	/** Move marker flashes per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soldier|Move Marker", meta = (ClampMin = "0.1"))
	float MoveMarkerFlashRate = 1.5f;

	/** Standing walk speed in cm/s. Crouched/prone scale this by SoldierData multipliers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soldier", meta = (ClampMin = "1"))
	float MoveSpeed = 240.f;

	/** Turning speed in degrees/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soldier", meta = (ClampMin = "1"))
	float TurnSpeed = 720.f;

	/** Number shown above the soldier and on its move marker (1-4). */
	UFUNCTION(BlueprintPure, Category = "Soldier")
	int32 GetSquadNumber() const { return SquadIndex + 1; }

	const UWeaponData* GetWeaponData() const;
	const USoldierData* GetSoldierData() const;
	const USoldierVisualData* GetVisualData() const { return VisualData.Get(); }

	/**
	 * Server: puts NewWeapon in the soldier's hands with that weapon's starting rounds (loaded, then spare),
	 * replacing the weapon held and all loose rounds. Used for the starting loadout (e.g. the test MG).
	 */
	void EquipStartingWeapon(UWeaponData* NewWeapon);

	// ---------------------------------------------------------------- Machine gun set-up (GDD §6.1)

	/** The held weapon must be set up before it fires (a machine gun). */
	bool HasSetupWeapon() const;

	/** Set up and able to fire across its arc. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Combat")
	bool IsWeaponSetUp() const { return bWeaponSetUp; }

	/** Setting up right now (standing still, facing the way it will fire). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Combat")
	bool IsSettingUpWeapon() const { return bSettingUpWeapon; }

	/** Seconds of set-up left (0 when set up or not setting up). */
	float GetSetupRemaining() const { return bSettingUpWeapon ? SetupRemaining : 0.f; }

	/** Centre of the arc a set-up weapon fires across (world yaw, degrees). */
	float GetSetupYaw() const { return SetupYaw; }

	/** True if Yaw lies within the set-up weapon's firing arc. */
	bool IsYawInSetupArc(float Yaw) const;

	/** 0 torn down, rising to 1 as the set-up completes; 1 while set up. */
	float GetSetupProgress() const;

	/** Cosmetic: the set-up gun's bipod (or barrel) is resting on the ground or on cover, not held in the hands. */
	bool IsSetupWeaponRested() const { return SetupVisualAlpha > 0.f && bSetupRestFound; }

	/** Cosmetic: what the set-up gun rests on, for the demo log ("ground", "cover", "none"). */
	const TCHAR* GetSetupRestName() const;

	// ---------------------------------------------------------------- Combat statistics (debug / demo)

	/** Rounds that hit, fired by this soldier since the last DebugResetCombatStats (hit rolls won; god mode still counts). */
	int32 GetHitsLanded() const { return HitsLanded; }

	/** Rounds this soldier has fired since the last DebugResetCombatStats. */
	int32 GetRoundsFiredStat() const { return RoundsFiredStat; }

	/** Stress gained from incoming fire and cross-fire since the last reset, before the 100 cap. */
	float GetStressReceivedTotal() const { return StressReceivedTotal; }

	/** Server: every resolved rifle / MG round of every soldier (debug, demos, tests; nothing in the game binds it). */
	static FOnSoldierShotResolved OnShotResolved;

	/** Debug: zeroes the counters above. */
	void DebugResetCombatStats() { HitsLanded = 0; RoundsFiredStat = 0; StressReceivedTotal = 0.f; }

	/** Debug / demo: rifle fire, grenades and C4 can't injure, kill or stun this soldier (any team; stress still applies). */
	void DebugSetInvulnerable(bool bInvulnerable) { bDebugForceInvulnerable = bInvulnerable; }

	/** Cosmetic: pitch of the rested set-up gun (degrees, muzzle up positive). */
	float GetSetupRestPitch() const { return SetupRestPitch; }

	// ---------------------------------------------------------------- Animation queries (cosmetic)

	/** Rounds fired so far; the animation plays a recoil each time it goes up. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Testing")
	int32 GetShotCount() const { return ShotCount; }

	/** Grenade throws started (orders accepted) and grenades that actually left the hand. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Testing")
	int32 GetThrowCount() const { return ThrowCount; }
	UFUNCTION(BlueprintPure, Category = "Soldier|Testing")
	int32 GetGrenadesReleased() const { return GrenadesReleased; }

	/** 0 when the wind-up starts, 1 when the grenade leaves the hand (GDD §7 wind-up). 0 when not winding up. */
	float GetGrenadeWindupAlpha() const;

	/** GDD §7: winding up the throw (after any step back), the grenade not yet released. */
	bool IsGrenadeWindingUp() const { return GrenadeWindupRemaining > 0.f; }

	/** GDD §7: stepping back onto its previous cell to throw, pulling the pin on the way. */
	bool IsGrenadeSteppingBack() const { return bGrenadeStepBack && bGrenadeStepBackStarted && bStepping; }

	/** Progress of that step back, 0 at its start to 1 on arrival. 0 when not stepping back. */
	float GetGrenadeStepBackAlpha() const;

	/** The wind-up in progress follows a step back, so the pin is already out (picks the throw animation). */
	bool IsThrowAfterStepBack() const { return bThrowAfterStepBack; }

	/** +1 / -1 if the soldier leans out past the right / left edge of the wall it faces (GDD §9.2), else 0. */
	int32 GetPeekSide() const;

	/** The wall edge the soldier leans out past right now (the side it is turned towards if both are open). */
	bool GetActivePeek(FWallPeek& OutPeek) const;

	/** The uniformed skeletal model (null-mesh if no VisualData). */
	USkeletalMeshComponent* GetModel() const { return Model; }

	// ---------------------------------------------------------------- Orders

	/** Path to Goal and start walking. Returns false if the cell can't be reached. */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Orders")
	bool MoveToCell(FIntPoint Goal);

	/**
	 * MoveToCell for one soldier of a group order. GroupPlan gives the cells the other soldiers in the same order
	 * are being sent to, so the squad cohesion limit judges them where they're going, not where they are.
	 */
	bool MoveToCellInGroup(FIntPoint Goal, const TMap<const ASoldier*, FIntPoint>& GroupPlan);

	// ---------------------------------------------------------------- Squad cohesion (GDD §4.3, server only)

	/** Same squad: same team and squad (GDD §4.3 holds each squad together, not the whole team). */
	bool IsSquadMate(const ASoldier* Other) const { return Other && Other != this && Other->Team == Team && Other->SquadId == SquadId; }

	/** Server: reseeds this soldier's hit-roll stream (reproducible test matches, squad.MatchSeed). */
	void SeedRandom(int32 Seed) { Rng.Initialize(Seed); }

	/** Where this soldier counts for the cohesion limit: the cell it is heading to (its dragger's, while dragged). */
	FIntPoint GetCohesionCell(const TMap<const ASoldier*, FIntPoint>* GroupPlan = nullptr) const;

	/**
	 * True if sending this soldier to Goal keeps it within SquadCohesionLimit of every living squad-mate, or at least
	 * no further from any mate it is already beyond the limit from. GroupPlan overrides mates' cells (see MoveToCellInGroup).
	 */
	bool IsCohesiveDestination(FIntPoint Goal, const TMap<const ASoldier*, FIntPoint>* GroupPlan = nullptr) const;

	/** Nearest reachable free cell to Goal that IsCohesiveDestination; OutPath is the path to it. */
	bool FindCohesiveDestination(FIntPoint Goal, const TMap<const ASoldier*, FIntPoint>* GroupPlan, FIntPoint& OutCell, TArray<FIntPoint>& OutPath) const;

	/** Stop moving once the step in progress is finished (a player clicking the soldier's own cell). */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Orders")
	bool OrderStop();

	/** Turn to face a cell (after finishing any move in progress). */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Orders")
	bool FaceCell(FIntPoint Cell);

	/** Explicit attack: stop, turn, aim and keep firing at Target until it dies or the order is replaced. */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Orders")
	bool OrderAttack(ASoldier* Target);

	/**
	 * GDD §7: throw a grenade to land on Cell. Ordered while moving, the soldier finishes the step it is on, steps
	 * back onto the cell it came from (pulling the pin), winds up for GrenadeThrowDelay and throws from there.
	 * Standing still, it winds up and throws where it stands.
	 */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Orders")
	bool ThrowGrenadeAt(FIntPoint Cell);

	/** Change stance. Fails while stunned, injured (crawling only) or dead. */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Orders")
	bool OrderStance(ESoldierStance NewStance);

	/** Standing -> Crouched -> Prone -> Standing. */
	UFUNCTION(BlueprintPure, Category = "Soldier")
	ESoldierStance GetNextStance() const;

	/**
	 * GDD §5.3 Hold fire: while on, the soldier never picks a target on its own (saves ammo against enemies in hard
	 * cover). Turning it on also stops any attack in progress. An explicit attack order still fires; once that
	 * target is gone the soldier goes back to holding. Grenades are unaffected. Any living soldier can be given it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Orders")
	bool OrderHoldFire(bool bHold);

	UFUNCTION(BlueprintPure, Category = "Soldier|Combat")
	bool IsHoldingFire() const { return bHoldFire && !bDead; }

	// ---------------------------------------------------------------- Damage (server only)

	/** A rifle round hit this soldier, fired from DistanceMetres away. Cause is reported if the hit kills. */
	void ReceiveHit(float DistanceMetres, const FKillCause& Cause);
	void ApplyStun(float Seconds);
	/** Kills the soldier and announces it in the kill feed (GDD §13). */
	void Die(const FKillCause& Cause);

	// ---------------------------------------------------------------- Owner

	/** Server: the player commanding this soldier (set when the player's controller gathers its squad). */
	void SetOwningPlayer(APlayerState* Player);
	APlayerState* GetOwningPlayer() const;

	/** True when this soldier is in the squad of this machine's local player. */
	bool IsOwnedByLocalPlayer() const;

	/** Rank, regiment and name of the owning player for the kill feed; a placeholder for soldiers no player owns. */
	FKillFeedPlayer GetKillFeedIdentity() const;

	// ---------------------------------------------------------------- Stress (GDD §8, server only)

	/** Add stress (clamped to 0..MaxStress) and hold off decay for StressDecayDelay. Reaching MaxStress shocks the soldier. */
	void AddStress(float Amount);

	/** Set stress directly (debug / tests). */
	void SetStress(float NewStress);

	UFUNCTION(BlueprintPure, Category = "Soldier|Stress")
	float GetStress() const { return Stress; }

	UFUNCTION(BlueprintPure, Category = "Soldier|Stress")
	EStressTier GetStressTier() const;

	/** Penalties for the current stress tier. */
	const FStressTierEffects& GetStressEffects() const;

	/** At full stress: cannot move (GDD §8.1). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Stress")
	bool IsShocked() const { return GetStressTier() == EStressTier::Shocked; }

	// ---------------------------------------------------------------- Queries

	UFUNCTION(BlueprintPure, Category = "Soldier")
	ESoldierStance GetStance() const { return Stance; }

	/** Posture actually in effect: Injured (crawling) and Stunned soldiers are always prone; draggers crouch. */
	UFUNCTION(BlueprintPure, Category = "Soldier")
	ESoldierStance GetEffectiveStance() const
	{
		if (bInjured || StunRemaining > 0.f)
		{
			return ESoldierStance::Prone; // Injured soldiers crawl; stunned soldiers are knocked flat.
		}
		return IsDragging() ? ESoldierStance::Crouched : Stance; // GDD §5.4: dragging moves "as if crouching".
	}

	UFUNCTION(BlueprintPure, Category = "Soldier")
	ESoldierHealth GetHealthState() const;

	UFUNCTION(BlueprintPure, Category = "Soldier")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category = "Soldier")
	bool IsInjured() const { return bInjured && !bDead; }

	/** Can take orders: not stunned and not dead. */
	UFUNCTION(BlueprintPure, Category = "Soldier")
	bool CanAct() const { return !bDead && StunRemaining <= 0.f && !IsBeingDragged(); }

	/** True from the throw order until the grenade leaves the hand: finishing a step, stepping back and winding up (GDD §7). */
	UFUNCTION(BlueprintPure, Category = "Soldier")
	bool IsThrowingGrenade() const { return bGrenadeStepBack || GrenadeWindupRemaining > 0.f; }

	/** The status-line message from the last refused or changed order ("BLOCKED", "SQUAD LIMIT"), empty once it times out. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Testing")
	FString GetStatusMessage() const { return MessageRemaining > 0.f ? Message : FString(); }

	/** Drops a throw that hasn't released yet; the grenade is kept. A step back already under way still finishes. */
	void CancelGrenadeThrow();

	/** Wind-up or follow-through of a throw: hands busy with the grenade, firing paused, facing the throw. */
	bool IsBusyThrowing() const { return IsThrowingGrenade() || GrenadeFollowThroughRemaining > 0.f; }

	/** Stunned, Injured or Dead: can be dragged by an ally (GDD §5.4). */
	UFUNCTION(BlueprintPure, Category = "Soldier")
	bool IsDowned() const { return bDead || bInjured || StunRemaining > 0.f; }

	// ---------------------------------------------------------------- Dragging (GDD §5.4)

	/** True if this soldier could start dragging Target right now (Active, Target a downed ally within range). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Drag")
	bool CanStartDragging(const ASoldier* Target) const;

	/** Start dragging a downed ally. Shows the reason on the status line if it can't. */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Drag")
	bool StartDragging(ASoldier* Target);

	UFUNCTION(BlueprintCallable, Category = "Soldier|Drag")
	void StopDragging();

	UFUNCTION(BlueprintPure, Category = "Soldier|Drag")
	ASoldier* GetDraggedSoldier() const { return DraggedSoldier.Get(); }

	UFUNCTION(BlueprintPure, Category = "Soldier|Drag")
	ASoldier* GetDraggedBy() const { return DraggedBy.Get(); }

	UFUNCTION(BlueprintPure, Category = "Soldier|Drag")
	bool IsDragging() const { return DraggedSoldier.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Soldier|Drag")
	bool IsBeingDragged() const { return DraggedBy.IsValid(); }

	// ---------------------------------------------------------------- Medkit (GDD §5.2, server only)

	/**
	 * Why this soldier can't treat Patient with a medkit right now, distance aside ("NO MEDKIT", "SHOCKED",
	 * "NOT INJURED", ...). Empty if it can.
	 */
	FString GetMedkitBlockReason(const ASoldier* Patient) const;

	/** Patient is within MedkitReachCells of this soldier's cell (own cell and the 8 around it) and neither is mid-step. */
	bool IsInMedkitReach(const ASoldier* Patient) const;

	/**
	 * Use a medkit on an injured ally (GDD §5.2). Walks next to it first if it is out of reach, then treats it for
	 * MedkitChannelTime, unable to shoot. Any new order, being stunned / injured / killed / Shocked, or the patient
	 * dying, being dragged or crawling out of reach stops it; the medkit is only used up when it finishes.
	 * Shows the reason on the status line if it can't.
	 */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Medkit")
	bool OrderUseMedkit(ASoldier* Patient);

	/** Stop treating (or heading to treat) the patient. The medkit is kept. */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Medkit")
	void CancelMedkit();

	/** The ally this soldier is treating or walking over to treat. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Medkit")
	ASoldier* GetMedkitPatient() const { return MedkitPatient.Get(); }

	/** The soldier treating (or heading to treat) this one. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Medkit")
	ASoldier* GetTreatedBy() const { return MedkitTreatedBy.Get(); }

	/** True while the 7-second treatment itself is under way (not while walking over). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Medkit")
	bool IsApplyingMedkit() const { return bApplyingMedkit && MedkitPatient.IsValid(); }

	/** Seconds left on the treatment in progress (0 when not treating). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Medkit")
	float GetMedkitRemaining() const { return IsApplyingMedkit() ? MedkitRemaining : 0.f; }

	// ---------------------------------------------------------------- Buildables (GDD §13.3, server only)

	/** How many of this buildable the soldier still carries. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Build")
	int32 GetBuildableCount(EBuildableKind Kind) const;

	/**
	 * Why this soldier can't place Kind on Wall right now, distance aside ("NO C4", "STUNNED", "BREACHED",
	 * "REINFORCED", ...). Empty if it can. Wall may be null (reported as "NO WALL").
	 */
	FString GetBuildBlockReason(EBuildableKind Kind, const ABreachableWall* Wall) const;

	/**
	 * The open cell to place something on Wall from (orthogonally adjacent to WallCell across its face, free, inside the
	 * squad cohesion limit and reachable) that is quickest to walk to, i.e. on the side the soldier approaches from.
	 * Falls back to the nearest face cell of any of the wall's cells. OutWallCell is the wall cell faced from there.
	 */
	bool FindBuildStandCell(const ABreachableWall* Wall, FIntPoint WallCell, FIntPoint& OutStandCell, FIntPoint& OutWallCell) const;

	/**
	 * GDD §13.3: walk to the wall's face (FindBuildStandCell), face it and channel for the buildable's PlaceTime, then
	 * place it (C4 sticks to the face and arms; a reinforcement fits the wall). Any new order, being stunned, injured,
	 * killed or dragged stops it, and the item is only used up when the channel finishes. Shows the reason if it can't.
	 */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Build")
	bool OrderBuild(EBuildableKind Kind, ABreachableWall* Wall, FIntPoint WallCell);

	/** Stop placing (or heading to place) a buildable. The item is kept. */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Build")
	void CancelBuild();

	/** Walking to a wall to build on it, or channelling there. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Build")
	bool HasBuildOrder() const { return BuildWall.IsValid(); }

	/** The placement channel itself is under way (kneeling at the wall). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Build")
	bool IsBuilding() const { return bBuildChanneling && BuildWall.IsValid(); }

	EBuildableKind GetBuildKind() const { return BuildKind; }
	ABreachableWall* GetBuildWall() const { return BuildWall.Get(); }

	/** 0..1 through the placement channel (0 when not channelling). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Build")
	float GetBuildProgress() const { return IsBuilding() && BuildDuration > 0.f ? FMath::Clamp(1.f - BuildRemaining / BuildDuration, 0.f, 1.f) : 0.f; }

	float GetBuildRemaining() const { return IsBuilding() ? BuildRemaining : 0.f; }

	/** Rounds loaded in the rifle the soldier holds. */
	UFUNCTION(BlueprintPure, Category = "Soldier")
	int32 GetMagazineAmmo() const { return Inventory.Weapons.Num() > 0 ? Inventory.Weapons[0].LoadedRounds : 0; }

	/** Loose rounds that fit the rifle the soldier holds (rounds for other rifles don't count). */
	UFUNCTION(BlueprintPure, Category = "Soldier")
	int32 GetReserveAmmo() const { return Inventory.GetAmmo(GetWeaponData()); }

	UFUNCTION(BlueprintPure, Category = "Soldier")
	int32 GetGrenades() const { return Inventory.Grenades; }

	UFUNCTION(BlueprintPure, Category = "Soldier")
	int32 GetMedkits() const { return Inventory.Medkits; }

	// ---------------------------------------------------------------- Inventory (GDD §4.2, §5.5)

	/** Everything the soldier carries. Weapons[0] is the rifle it holds. A dead soldier's body keeps it for looting. */
	const FLootInventory& GetInventory() const { return Inventory; }

	/** Server: mutable inventory for the loot system. Call NotifyWeaponChanged() after changing Weapons[0]. */
	FLootInventory& GetLootInventory() { return Inventory; }

	/** Server: the held rifle was swapped. Cancels any reload or attack in progress so the new rifle is aimed afresh. */
	void NotifyWeaponChanged();

	/** Show a short message on this soldier's status line ("NO AMMO", "+10 RDS", ...). */
	void ShowMessage(const FString& Message, float Seconds = 1.5f);

	// ---------------------------------------------------------------- Debug menu (server only, SoldierDebug.cpp)

	/** Debug: clears stun and injury and sets stress to 0. Does nothing to the dead. */
	void DebugHeal();

	/** Debug: back to the starting loadout (GDD §4.2): full magazine, starting rounds, grenades, medkits and buildables. */
	void DebugRestock();

	/** Debug: puts the soldier on Cell at once, dropping every order. False if the cell can't take it. */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Debug")
	bool DebugTeleportTo(FIntPoint Cell);

	/** Debug: injure (a second call kills), stun or kill, even under squad.GodMode. */
	UFUNCTION(BlueprintCallable, Category = "Soldier|Debug")
	void DebugInjure();
	UFUNCTION(BlueprintCallable, Category = "Soldier|Debug")
	void DebugStun(float Seconds);
	UFUNCTION(BlueprintCallable, Category = "Soldier|Debug")
	void DebugKill();

	/** squad.GodMode 1: soldiers commanded by a player ignore rifle hits, grenade / C4 blasts and stuns. */
	bool IsDebugInvulnerable() const;

	UFUNCTION(BlueprintPure, Category = "Soldier")
	ASoldier* GetAttackTarget() const { return AttackTarget.Get(); }

	/** True if the current target was picked automatically (GDD §5.3) rather than by an explicit attack order. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Combat")
	bool IsAutoTargeting() const { return bAutoTarget && AttackTarget.IsValid(); }

	UFUNCTION(BlueprintCallable, Category = "Soldier")
	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintPure, Category = "Soldier")
	bool IsSelected() const { return bSelected; }

	UFUNCTION(BlueprintPure, Category = "Soldier")
	FIntPoint GetCurrentCell() const { return CurrentCell; }

	UFUNCTION(BlueprintPure, Category = "Soldier")
	FIntPoint GetDestinationCell() const { return DestinationCell; }

	UFUNCTION(BlueprintPure, Category = "Soldier")
	bool IsMoving() const { return bStepping || Path.Num() > 0; }

	/** Steps left on the current path after the one in progress. */
	int32 GetRemainingSteps() const { return Path.Num(); }

	/**
	 * GDD §13 move path line: ground points (world, at the grid's height) still to walk - where the soldier is now,
	 * the cell of the step in progress, then every cell left on its path to the destination. Empty when standing still.
	 */
	void GetRemainingRoute(TArray<FVector>& OutPoints) const;

	/** Dead soldiers don't block cells. */
	bool BlocksCells() const { return !bDead; }

	/** The cell the soldier stands on, or the one it is currently stepping into. */
	bool OccupiesCell(FIntPoint Cell) const { return BlocksCells() && (CurrentCell == Cell || (bStepping && StepCell == Cell)); }

	/** Where shots at this soldier are aimed (chest height for the current posture). */
	FVector GetAimPoint() const;

	// ---------------------------------------------------------------- Vision (GDD §9.3)

	/** Cell used for sight lines: the one the soldier's body is in now (switches halfway through a step). */
	FIntPoint GetSightCell() const;

	/** Height (cm) this soldier sees from and is seen at, from its posture. Corpses lie flat. */
	float GetSightHeight() const;

	/** True if this soldier has line of sight to Target, accounting for both postures and cover heights. */
	bool CanSee(const ASoldier* Target) const;

	/**
	 * Cells this soldier's sight lines to/from a soldier on OtherCell can start at: its own sight cell, plus the
	 * cell beside a wall's end when it leans out past that edge and OtherCell is in the area it looks over.
	 */
	void GetSightOrigins(FIntPoint OtherCell, TArray<FIntPoint, TInlineAllocator<3>>& OutCells) const;

	/** True while standing still beside cover and facing it (GDD §9.2). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Cover")
	bool IsLeaningOnCover() const;

	// ---------------------------------------------------------------- Squad bar (HUD) queries

	UFUNCTION(BlueprintPure, Category = "Soldier|HUD")
	float GetStunRemaining() const { return StunRemaining; }

	UFUNCTION(BlueprintPure, Category = "Soldier|HUD")
	bool IsReloading() const { return bReloading; }

	/** 0..1 through the reload in progress (0 when not reloading). */
	UFUNCTION(BlueprintPure, Category = "Soldier|HUD")
	float GetReloadProgress() const
	{
		return bReloading && ReloadDuration > 0.f ? FMath::Clamp(1.f - ReloadRemaining / ReloadDuration, 0.f, 1.f) : 0.f;
	}

	/** True for a couple of seconds after a round is fired at this soldier or a grenade stuns it. */
	UFUNCTION(BlueprintPure, Category = "Soldier|HUD")
	bool IsUnderFire() const { return UnderFireRemaining > 0.f; }

	/** 1 the moment the soldier is hit or stunned, fading to 0. Drives the squad-bar damage flash. */
	UFUNCTION(BlueprintPure, Category = "Soldier|HUD")
	float GetDamageFlash() const { return DamageFlashRemaining / DamageFlashTime; }

	/**
	 * Where shots and the aim line start. Placeholder: chest height just in front of the bean. Once soldiers carry
	 * rifle meshes this becomes the muzzle socket of the held weapon.
	 */
	UFUNCTION(BlueprintPure, Category = "Soldier|Combat")
	FVector GetMuzzleLocation() const;

	/** Attacking, but still turning to face the target or settling the aim (AimTime) before the next shot. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Combat")
	bool IsAiming() const;

	/** Hit chance of the attack in progress (percent), or -1 if not attacking. */
	float GetCurrentHitChance() const { return AttackTarget.IsValid() ? LastHitChance : -1.f; }

	/** Type of the cover the soldier leans on right now (None if not leaning on any, GDD §9.2). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Cover")
	ECoverType GetLeaningCoverType() const;

	/** Server: set by the grid manager each frame. True while any living enemy can see this soldier. */
	void SetSpottedByEnemy(bool bSpotted) { bSpottedByEnemy = bSpotted; }

	UFUNCTION(BlueprintPure, Category = "Soldier|Vision")
	bool IsSpottedByEnemy() const { return bSpottedByEnemy; }

	/** A team always sees its own soldiers; enemies only while one of its soldiers has line of sight. */
	UFUNCTION(BlueprintPure, Category = "Soldier|Vision")
	bool IsVisibleToTeam(ESquadTeam Viewer) const { return Viewer == Team || bSpottedByEnemy; }

	/** Range, line of sight and cover for a shot at Target from where this soldier stands now. */
	FHitChanceInfo GetHitChanceAgainst(const ASoldier* Target) const;

	/** Show a line of text on this soldier's status label for the next fraction of a second (hover readouts). */
	void ShowHitPreview(const FString& Text);

	// ---------------------------------------------------------------- Projectile model (GDD §16, SoldierBallistics.cpp)

	/**
	 * Resting the rifle on low cover (hay, low wall, window sill): projectile model only, standing still, facing
	 * the cover, not prone. OutCoverTop: height of the cover's top above the ground; OutTowardCover: unit vector.
	 */
	bool IsBraced(float* OutCoverTop = nullptr, FVector* OutTowardCover = nullptr) const;

	/** Posture to animate: crouched while braced, else GetEffectiveStance. */
	ESoldierStance GetPostureStance() const;

	/** World-space hit spheres for the current posture (same on server and clients). Empty when dead. */
	void GetHitShapes(FBallisticShapes& OutShapes) const;

	/** Where this soldier's rounds start when firing towards Toward (analytic, so it works on a dedicated server). */
	FVector GetBallisticMuzzle(const FVector& Toward) const;

	/** One-sigma angular error of the next round (mrad): weapon x posture x stress x injury + recoil. */
	float GetBallisticSpreadMrad() const;

	/** Server: a round fired by this soldier stopped (or ran out of range). Applies hits, stats and the tracer. */
	void OnBallisticRoundResolved(const FBallisticRound& Round, const FBallisticTrace& Hit);

	/** Server: an enemy round passed close by (GDD §16 stress). */
	void OnBallisticNearMiss(const ASoldier* Shooter);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Parent of the bean parts; posed to show stance and death. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<USceneComponent> BodyRoot;

	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UStaticMeshComponent> BeanBody;

	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UStaticMeshComponent> BeanTop;

	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UStaticMeshComponent> BeanBottom;

	/** Small dark bar showing which way the bean faces. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UStaticMeshComponent> Visor;

	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UStaticMeshComponent> SelectionRing;

	/** Squad number (1-4) floating above the soldier's head. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UTextRenderComponent> NumberLabel;

	/** Status line above the number: stance, stunned, injured, reloading, ammo. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UTextRenderComponent> StatusLabel;

	/** "HOLD" tag (and a red squad number) between the number and the status line, shown to the soldier's own team only (GDD §5.3). */
	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UTextRenderComponent> HoldFireLabel;

	/** Flashing red square on the destination cell (world-space, shown while moving). */
	UPROPERTY(VisibleAnywhere, Category = "Soldier|Move Marker")
	TObjectPtr<UStaticMeshComponent> MoveMarker;

	/** Squad number drawn on the destination cell. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier|Move Marker")
	TObjectPtr<UTextRenderComponent> MoveMarkerLabel;

	/** Brief streak drawn for each shot. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UStaticMeshComponent> Tracer;

	/** Uniformed, animated soldier (VisualData). The bean parts stay as invisible click targets. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier|Model")
	TObjectPtr<USkeletalMeshComponent> Model;

	/** The rifle in hand (the held UWeaponData's mesh), on the rig's weapon bone. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier|Model")
	TObjectPtr<UStaticMeshComponent> WeaponModel;

	/** The grenade in the right hand, shown during the throw wind-up. */
	UPROPERTY(VisibleAnywhere, Category = "Soldier|Model")
	TObjectPtr<UStaticMeshComponent> GrenadeModel;

private:
	// ---------------------------------------------------------------- Networking
	UPROPERTY(ReplicatedUsing = OnRep_NetState)
	FSoldierNetState NetState;

	UFUNCTION()
	void OnRep_NetState();

	/** Server: copy the live fields into NetState (end of every Tick). */
	void WriteNetState();

	/** Client: copy NetState into the live fields and refresh visuals that depend on what changed. */
	void ReadNetState();

	/** Client tick: glide to the server's position, count down timers between updates, cosmetics. */
	void TickClient(float DeltaSeconds);

	/** Cosmetics shared by server and client ticks (labels, move marker, rifle and grenade models). */
	void TickCosmetics(float DeltaSeconds);

	/** Corpse look: grey body, labels and markers off (server on death, clients when bDead arrives). */
	void ApplyDeathVisuals();

	/** Client: send an order for this soldier to the server through the local player controller. */
	bool ForwardOrder(ESoldierOrderType Type, FIntPoint Cell = FIntPoint::ZeroValue, ASoldier* Other = nullptr, int32 Param = 0);

	/** A round fired: tracer and gunshot everywhere (cosmetic; the hit was rolled on the server). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(FVector_NetQuantize From, FVector_NetQuantize To, bool bHit);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastReloadSound();

	/** A server update has arrived: TickClient glides towards NetState.Location. */
	bool bHasNetLocation = false;

	/** World time of the last NetState update. Timers only count down for a moment past it (a paused one stays put). */
	float LastNetStateTime = 0.f;

	/** Model, uniform and held props from VisualData and Team; falls back to the bean without them. */
	void UpdateModel();
	/** Put the held rifle's mesh in the soldier's hands. */
	void RefreshWeaponModel();
	/** Grey out (or restore) the uniform (GDD §13: dead soldiers turn grey). */
	void ApplyModelTint(bool bCorpse);
	bool HasModel() const;
	void UpdateMovement(float DeltaSeconds);
	void UpdateFacing(float DeltaSeconds);
	void UpdateAttack(float DeltaSeconds);
	/** GDD §5.3: with no explicit target, a standing-still soldier engages the visible enemy with the best hit chance. */
	void UpdateAutoTarget(float DeltaSeconds);
	void UpdateStatusLabel();
	void UpdateHoldFireLabel();
	bool TryBeginNextStep();
	void StopMoving();
	void StopAfterCurrentStep();

	/** Range, ammo, dragging and cover checks for a throw at Cell from FromCell. Shows the reason on failure. */
	bool PlanGrenadeThrow(FIntPoint FromCell, FIntPoint Cell, FGrenadeThrow& OutThrow);

	/**
	 * GDD §7: the cell a soldier on From steps back onto to throw at Target: the cell it came from (Previous) if it
	 * can step there, else the free cell directly away from Target (then the two beside that). False if none is free:
	 * it throws where it stands.
	 */
	bool FindGrenadeStepBackCell(FIntPoint From, bool bHasPrevious, FIntPoint Previous, FIntPoint Target, FIntPoint& OutCell) const;

	/** Steps back, counts down the throw wind-up and releases the grenade when it ends. */
	void UpdateGrenadeThrow(float DeltaSeconds);
	void ClearAttack();
	void StartReload();
	void FinishReload();
	void FireAt(ASoldier* Target, const FHitChanceInfo& Chance);
	void ShowTracer(const FVector& From, const FVector& To, bool bHit);
	void ApplyPostureVisuals();
	void SetBodyColor(const FLinearColor& Color);
	void ShowMoveMarker(FIntPoint Cell);
	void UpdateMoveMarker(float DeltaSeconds);
	void FaceCamera(UTextRenderComponent* Label) const;
	/** False on a dedicated server, which doesn't load the (cosmetic) text labels: text has no font there. */
	bool HasLabels() const { return NumberLabel && StatusLabel && HoldFireLabel && MoveMarkerLabel; }
	float GetCurrentMoveSpeed() const;
	void FollowDragger(FIntPoint Cell);
	void UpdateDrag();

	/** Medic: walks the approach, starts the treatment on arrival, checks interruptions and counts it down. */
	void UpdateMedkit(float DeltaSeconds);
	/** Medic: heads for the free cell within reach of Patient nearest to it (or stops, if already within reach). */
	bool ApproachPatient(ASoldier* Patient);
	void LinkMedkitPatient(ASoldier* Patient);
	void BeginMedkitTreatment(ASoldier* Patient);
	/** Patient: treatment finished, back to Active (GDD §5.2). Stays prone; the player picks the stance. */
	void RecoverFromInjury();
	/** The cell the soldier is on, or stepping into. */
	FIntPoint GetStandCell() const { return bStepping ? StepCell : CurrentCell; }
	bool IsFacingYaw(float Yaw, float ToleranceDegrees = 10.f) const;
	void UpdateLocalVisibility();
	void UpdateStress(float DeltaSeconds);
	void OnShocked();
	/** Cross-fire stress on squad-mates between this soldier and Target (GDD §8.2). */
	void ApplyCrossfireStress(const ASoldier* Target);

	/** 0..MaxStress (GDD §8). Server-owned. */
	float Stress = 0.f;
	/** Seconds left before stress starts to decay again. */
	float StressDecayHold = 0.f;

	/** Server-owned: an enemy soldier currently has line of sight to this one. */
	bool bSpottedByEnemy = false;

	UPROPERTY(Transient)
	TObjectPtr<AGridManager> Grid;

	// Movement
	FIntPoint CurrentCell = FIntPoint::ZeroValue;
	FIntPoint DestinationCell = FIntPoint::ZeroValue;
	FIntPoint StepCell = FIntPoint::ZeroValue;
	bool bStepping = false;
	/** The cell the soldier stood on before CurrentCell (GDD §7: where it steps back to throw). */
	FIntPoint PreviousCell = FIntPoint::ZeroValue;
	bool bHasPreviousCell = false;
	TArray<FIntPoint> Path;
	float MoveMarkerTime = 0.f;

	/** How long the next step has been blocked with no way round. */
	float BlockedWaitTime = 0.f;
	static constexpr float MaxBlockedWait = 2.f;

	// Facing
	bool bHasFacingOrder = false;

	/** Set while a debug-menu injure / stun / kill runs, so it gets through squad.GodMode. */
	bool bDebugIgnoreGodMode = false;
	float DesiredYaw = 0.f;

	// Stance / health
	ESoldierStance Stance = ESoldierStance::Standing;
	bool bInjured = false;
	bool bDead = false;

	TWeakObjectPtr<APlayerState> OwningPlayer;
	float StunRemaining = 0.f;
	bool bSelected = false;

	// Combat
	TWeakObjectPtr<ASoldier> AttackTarget;
	/** AttackTarget was chosen automatically (GDD §5.3); an explicit attack order clears this. */
	bool bAutoTarget = false;
	/** GDD §5.3: no automatic target acquisition while set. */
	bool bHoldFire = false;
	/** Height of the number label for the current posture (the status line sits above it). */
	float LabelBaseHeight = 0.f;
	/** Status line height above the number while the hold-fire tag sits between them. */
	static constexpr float HoldFireStatusOffset = 66.f;
	float AutoTargetScanRemaining = 0.f;
	static constexpr float AutoTargetScanInterval = 0.25f;
	/** An automatic target is only swapped for one at least this many percentage points easier to hit. */
	static constexpr float AutoTargetSwitchMargin = 5.f;
	/** Rifle (with its loaded rounds), loose ammo, grenades and medkits. Filled from the loadout in BeginPlay. */
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Soldier|Loot")
	FLootInventory Inventory;
	float AimRemaining = 0.f;
	float ShotCooldown = 0.f;
	/** Rounds fired so far in the burst in progress (GDD §6.1); 0 between bursts. */
	int32 BurstRoundsFired = 0;
	/** GDD §6.1 machine gun set-up: set up (can fire across its arc), or setting up with SetupRemaining to go. */
	bool bWeaponSetUp = false;
	bool bSettingUpWeapon = false;
	float SetupRemaining = 0.f;
	float SetupYaw = 0.f;
	ESoldierStance SetupStance = ESoldierStance::Standing;
	/** Server: sets up, keeps up or tears down a weapon that needs it, after facing has been updated. */
	void UpdateWeaponSetup(float DeltaSeconds);
	void TearDownWeapon();
	/** The way the soldier means to face this tick: grenade, attack target, facing order, else as it stands. */
	float GetIntendedYaw() const;
	/** Set-up length when the set-up in progress started (for GetSetupProgress). */
	float SetupDuration = 0.f;
	/** Cosmetic set-up pose: 0 held in the hands, 1 rested on its bipod; follows GetSetupProgress. */
	float SetupVisualAlpha = 0.f;
	bool bSetupRestFound = false;
	bool bSetupRestOnCover = false;
	float SetupRestPitch = 0.f;
	float SetupRecoilOffset = 0.f;
	int32 SetupVisualShotCount = 0;
	/** Cosmetic: poses the gun for the set-up (bipod on the ground or on cover) and its recoil. Called after the anim pose. */
	void UpdateSetupWeaponVisual(float DeltaSeconds);
	/** Pitch at which the gun, pivoting on its grip at GripWorld facing Yaw, first touches something below it. */
	bool FindSetupRestPitch(const FVector& GripWorld, float Yaw, float& OutPitch, bool& bOutOnCover) const;
	bool bDebugForceInvulnerable = false;
	// Combat statistics (debug / demo).
	int32 HitsLanded = 0;
	int32 RoundsFiredStat = 0;
	float StressReceivedTotal = 0.f;
	bool bReloading = false;
	float ReloadRemaining = 0.f;
	/** Length of the reload in progress, for the squad-bar progress bar. */
	float ReloadDuration = 0.f;
	/** Squad-bar indicators: seconds left showing "under fire" and the damage flash. */
	float UnderFireRemaining = 0.f;
	float DamageFlashRemaining = 0.f;
	static constexpr float UnderFireTime = 2.f;
	static constexpr float DamageFlashTime = 0.6f;
	float GrenadeCooldown = 0.f;
	/** Seconds left before a throw in progress releases the grenade; 0 when not throwing. */
	float GrenadeWindupRemaining = 0.f;
	FIntPoint PendingGrenadeCell = FIntPoint::ZeroValue;
	/** Seconds left of the throwing motion after the release (SoldierData GrenadeFollowThroughTime). */
	float GrenadeFollowThroughRemaining = 0.f;
	/** Wind-up length of the throw in progress (for GetGrenadeWindupAlpha). */
	float GrenadeWindupDuration = 0.f;
	/** GDD §7: a throw ordered and not yet winding up: finishing the current step, then stepping back (if ordered while moving). */
	bool bGrenadeStepBack = false;
	/** The step back has been chosen (and begun, if there was a cell to step to). */
	bool bGrenadeStepBackStarted = false;
	/** Where the step back started (for GetGrenadeStepBackAlpha). */
	FIntPoint GrenadeStepBackFrom = FIntPoint::ZeroValue;
	/** The wind-up in progress came after a step back. */
	bool bThrowAfterStepBack = false;
	int32 ShotCount = 0;
	int32 ThrowCount = 0;
	int32 GrenadesReleased = 0;
	bool bModelTintedAsCorpse = false;
	float TracerRemaining = 0.f;
	FRandomStream Rng;

	// Projectile model (SoldierBallistics.cpp).
	/** Server: recoil building up from rounds fired (mrad), recovering over time. */
	float RecoilMrad = 0.f;
	void FillBallisticChance(const ASoldier& Target, FHitChanceInfo& Info) const;
	void FireBallisticRound(ASoldier* Target, const FHitChanceInfo& Chance);

	// Dragging: the soldier this one is pulling, and the one pulling this one.
	TWeakObjectPtr<ASoldier> DraggedSoldier;
	TWeakObjectPtr<ASoldier> DraggedBy;

	// Medkit: the ally this soldier is treating (or walking over to treat), and the soldier treating this one.
	TWeakObjectPtr<ASoldier> MedkitPatient;
	TWeakObjectPtr<ASoldier> MedkitTreatedBy;
	/** False while walking over to the patient, true once the treatment itself has started. */
	bool bApplyingMedkit = false;
	float MedkitRemaining = 0.f;
	/** Times the medic has had to set off again because the patient crawled away before it arrived. */
	int32 MedkitApproachTries = 0;
	static constexpr int32 MaxMedkitApproachTries = 3;

	// Buildables (GDD §13.3, SoldierBuild.cpp): what the soldier carries, and the placement in progress.
	TMap<EBuildableKind, int32> BuildableCounts;
	TWeakObjectPtr<ABreachableWall> BuildWall;
	EBuildableKind BuildKind = EBuildableKind::C4;
	FIntPoint BuildWallCell = FIntPoint::ZeroValue;
	FIntPoint BuildStandCell = FIntPoint::ZeroValue;
	/** False while walking to the wall, true once kneeling and channelling. */
	bool bBuildChanneling = false;
	float BuildRemaining = 0.f;
	float BuildDuration = 0.f;
	int32 BuildApproachTries = 0;
	static constexpr int32 MaxBuildApproachTries = 3;
	/** Fill BuildableCounts from the loadout (BeginPlay). */
	void InitBuildables();
	/** Walks the approach, starts the channel on arrival, checks interruptions and counts it down. */
	void UpdateBuild(float DeltaSeconds);
	/** Walk to (or stay on) the stand cell and link the build order. False if it can't get there. */
	bool ApproachBuild(EBuildableKind Kind, ABreachableWall* Wall, FIntPoint WallCell);
	void FinishBuild();

	// Transient message on the status line ("NO AMMO", "OUT OF RANGE", ...)
	FString Message;
	float MessageRemaining = 0.f;

	// Hover readout (hit chances against this soldier), refreshed every frame while the cursor is on it.
	FString HitPreview;
	float HitPreviewRemaining = 0.f;

	/** Hit chance of the attack in progress, for the status line. */
	float LastHitChance = -1.f;
};
