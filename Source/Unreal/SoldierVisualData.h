#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SquadTypes.h"
#include "SoldierVisualData.generated.h"

class UAnimSequence;
class USkeletalMesh;
class UStaticMesh;

/**
 * Every animation clip a soldier plays (see SoldierAnimInstance). The clips are hand-keyed by
 * SourceArt/Characters/Tools/build_soldier_anims.py and imported to /Game/Characters/Soldier/Anims/Hand
 * as A_<Name> (e.g. A_Stand_Run).
 */
UENUM(BlueprintType)
enum class ESoldierClip : uint8
{
	StandIdle, StandAim, StandFire, StandRun, StandReload,
	CrouchIdle, CrouchAim, CrouchFire, CrouchRun, CrouchReload,
	ProneIdle, ProneFire, ProneCrawl, ProneReload,
	ThrowStand, ThrowCrouch, ThrowProne,
	StandToCrouch, CrouchToStand, CrouchToProne, ProneToCrouch, StandToProne, ProneToStand,
	CoverLowStand, CoverLowCrouch, CoverHighStand, CoverHighCrouch,
	PeekRightStand, PeekLeftStand, PeekRightCrouch, PeekLeftCrouch,
	Stunned, InjuredIdle, InjuredCrawl, DragIdle, DragMove, DeathStand, DeathProne,
	/** GDD §7: one step back while taking out the grenade and pulling the pin (time follows the step). */
	ThrowStepBackStand, ThrowStepBackCrouch, ThrowStepBackProne,
	/** The throw after a step back: starts with the grenade in hand, pin out; releases at ThrowReleaseTime. */
	ThrowArmedStand, ThrowArmedCrouch, ThrowArmedProne,
	/** Recoil while leaning out past a wall's edge; the Left clips are mirrored (rifle on the left shoulder). */
	PeekFireRightStand, PeekFireLeftStand, PeekFireRightCrouch, PeekFireLeftCrouch,
	Num UMETA(Hidden)
};

/**
 * Soldier models, held props and animation set (art only: no gameplay values live here).
 * The attach offsets come from the rig the clips were authored on; they only change if the rig or the
 * grip in build_soldier_anims.py changes (it writes soldier_anim_calibration.json to recompute them).
 */
UCLASS(BlueprintType)
class UNREAL_API USoldierVisualData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Uniformed soldier per team (GDD §4.2: Allied US infantry, Axis German infantry). Same skeleton. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TObjectPtr<USkeletalMesh> AlliedMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TObjectPtr<USkeletalMesh> AxisMesh;

	/** Grenade shown in the hand while throwing, and the one that flies (Mk 2 / Stielhandgranate 24). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TObjectPtr<UStaticMesh> AlliedGrenadeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TObjectPtr<UStaticMesh> AxisGrenadeMesh;

	/** The rifle rides on this non-deforming bone; every clip animates it (so the rifle can change hands). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attach")
	FName WeaponBone = TEXT("ik_hand_gun");

	/** Rifle mesh (origin at the grip, muzzle +X) relative to WeaponBone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attach")
	FTransform WeaponOffset = FTransform(FRotator(0.f, -107.526f, -90.f), FVector(-6.851f, -11.75f, -6.5f));

	/** Right-hand palm bone the grenade sits in while throwing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attach")
	FName GrenadeBone = TEXT("weapon_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attach")
	FTransform AlliedGrenadeOffset = FTransform(FRotator(0.f, -90.f, -90.f), FVector(0.f, -1.2f, -3.4f));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attach")
	FTransform AxisGrenadeOffset = FTransform(FRotator(0.f, -90.f, -90.f), FVector(-6.f, -1.2f, -3.f));

	/** Grenade and C4 detonation. Rifle shots and reloads are on UWeaponData. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<class USoundBase> ExplosionSound;

	/**
	 * Low-poly explosion pieces for AExplosionFX (SourceArt/Effects/build_explosion_fx.py), each 1 m across.
	 * Puff and Material are required; without them grenades and C4 fall back to a plain orange ball.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UStaticMesh> ExplosionPuffMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UStaticMesh> ExplosionSpikeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UStaticMesh> ExplosionClodMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UStaticMesh> ExplosionScorchMesh;

	/** M_FX_Master: colour, glow and dithered opacity come from custom primitive data (set per piece). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<class UMaterialInterface> ExplosionMaterial;

	/** Distance falloff for rifle, reload and explosion sounds (heard from the RTS camera). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<class USoundAttenuation> CombatSoundAttenuation;

	/** Plays a combat sound at a world location with CombatSoundAttenuation (no-op on a dedicated server). */
	void PlayCombatSound(const UObject* WorldContext, class USoundBase* Sound, const FVector& Location) const;

	/** The visual data every soldier starts with (ASoldier's default), for actors that have no soldier to ask. */
	static const USoldierVisualData* GetDefaultVisuals();

	/** Socket on the rifle meshes where tracers start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attach")
	FName MuzzleSocket = TEXT("Muzzle");

	/** Clips by role. Missing entries fall back to /Game/Characters/Soldier/Anims/Hand/A_<Name>. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TMap<ESoldierClip, TObjectPtr<UAnimSequence>> Clips;

	/** Ground speed (cm/s) each movement clip was authored at; play rate = actual speed / this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "1"))
	float StandRunSpeed = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "1"))
	float CrouchRunSpeed = 158.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "1"))
	float CrawlSpeed = 79.f;

	/** Time in the throw clips where the grenade leaves the hand (matches the GDD §7 wind-up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.05"))
	float ThrowReleaseTime = 0.5f;

	/** The grenade appears in the hand at this point of the throw clip (taken from the pouch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0"))
	float ThrowGrenadeShowTime = 0.2f;

	/** The grenade appears in the hand at this fraction (0-1) of the step-back clips. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0", ClampMax = "1"))
	float StepBackGrenadeShowFraction = 0.35f;

	/** Cross-fade times (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Blend", meta = (ClampMin = "0"))
	float DefaultBlend = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Blend", meta = (ClampMin = "0"))
	float MoveBlend = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Blend", meta = (ClampMin = "0"))
	float FireBlend = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Blend", meta = (ClampMin = "0"))
	float DeathBlend = 0.12f;

	/**
	 * Leaning out past a wall's edge, the model slides this far (cm) sideways towards the open side, so the
	 * rifle clears the wall's end and points down its outer face. Cosmetic only: sight and cover use the grid.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Cover", meta = (ClampMin = "0", ClampMax = "100"))
	float PeekSideShift = 15.f;

	/** How quickly the model slides into / out of that lean (interp speed, higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Cover", meta = (ClampMin = "0"))
	float PeekShiftSpeed = 6.f;

	/** Colour multiplier for a corpse's uniform (GDD §13: dead soldiers turn grey). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FLinearColor CorpseTint = FLinearColor(0.35f, 0.35f, 0.35f);

	USkeletalMesh* GetSoldierMesh(ESquadTeam Team) const { return Team == ESquadTeam::Allies ? AlliedMesh.Get() : AxisMesh.Get(); }
	UStaticMesh* GetGrenadeMesh(ESquadTeam Team) const { return Team == ESquadTeam::Allies ? AlliedGrenadeMesh.Get() : AxisGrenadeMesh.Get(); }
	const FTransform& GetGrenadeOffset(ESquadTeam Team) const { return Team == ESquadTeam::Allies ? AlliedGrenadeOffset : AxisGrenadeOffset; }

	/** The clip for a role (falls back to the A_<Name> naming convention). */
	UAnimSequence* GetClip(ESoldierClip Clip) const;
};
