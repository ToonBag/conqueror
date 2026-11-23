#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "SoldierVisualData.h"
#include "SquadTypes.h"
#include "SoldierAnimInstance.generated.h"

class ASoldier;
class UAnimSequence;

/** One clip being played, with its cross-fade weight. */
struct FSoldierAnimLayer
{
	TObjectPtr<UAnimSequence> Sequence;
	ESoldierClip Clip = ESoldierClip::StandIdle;
	float Time = 0.f;
	float Rate = 1.f;
	bool bLoop = true;
	/** Fades in from 0 to 1 over BlendTime; the bottom layer always counts as 1. */
	float Weight = 1.f;
	float BlendTime = 0.f;
};

/** Evaluates the layer stack natively (no AnimGraph): samples each clip and cross-fades them in order. */
struct FSoldierAnimInstanceProxy : public FAnimInstanceProxy
{
	FSoldierAnimInstanceProxy() = default;
	explicit FSoldierAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	struct FLayer
	{
		const UAnimSequence* Sequence = nullptr;
		float Time = 0.f;
		bool bLoop = true;
		float Weight = 1.f;
	};
	TArray<FLayer, TInlineAllocator<4>> Layers;
};

/**
 * Animation for ASoldier: picks a clip from the soldier's state every frame (stance, moving, aiming, cover,
 * reload, grenade throw, stunned / injured / dragging / dead) and cross-fades to it. Purely cosmetic: it reads
 * gameplay state and never changes it, so it can be skipped on a dedicated server.
 */
UCLASS(Transient, NotBlueprintable)
class UNREAL_API USoldierAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** True while the throw clip has the grenade in the right hand (taken out of the pouch, not yet released). */
	bool IsGrenadeInHand() const { return bGrenadeInHand; }

	/** The clip on top of the stack (debugging / tests). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Animation")
	ESoldierClip GetCurrentClip() const { return Layers.Num() > 0 ? Layers.Last().Clip : ESoldierClip::StandIdle; }

	/** Playback time of the clip on top (debugging / tests). */
	UFUNCTION(BlueprintPure, Category = "Soldier|Animation")
	float GetCurrentClipTime() const { return Layers.Num() > 0 ? Layers.Last().Time : 0.f; }

	const TArray<FSoldierAnimLayer>& GetLayers() const { return Layers; }

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FSoldierAnimInstanceProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete InProxy; }

private:
	friend struct FSoldierAnimInstanceProxy;

	enum class EAction : uint8 { None, StepBack, Throw, Reload, Fire, StanceChange };

	UAnimSequence* Clip(ESoldierClip Which) const;
	/** Cross-fade to a clip. Does nothing if it is already on top, unless bRestart. */
	void Play(ESoldierClip Which, float BlendTime, float Rate = 1.f, bool bRestart = false);
	void AdvanceLayers(float DeltaSeconds);
	ESoldierClip ChooseBaseClip(const ASoldier& Soldier, ESoldierStance Stance, float& OutRate, float& OutBlend) const;
	void UpdateAction(const ASoldier& Soldier, ESoldierStance Stance, float DeltaSeconds);
	void StartAction(EAction NewAction, ESoldierClip Which, float BlendTime);

	static ESoldierClip PerStance(ESoldierStance Stance, ESoldierClip Standing, ESoldierClip Crouched, ESoldierClip Prone);

	UPROPERTY(Transient)
	TObjectPtr<const USoldierVisualData> Visuals;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> ClipCache;

	TArray<FSoldierAnimLayer> Layers;

	// What the soldier was doing last frame, to spot shots, throws and stance changes.
	EAction Action = EAction::None;
	ESoldierClip ActionClip = ESoldierClip::StandIdle;
	int32 LastShotCount = -1;
	int32 LastThrowCount = -1;
	int32 LastGrenadesReleased = -1;
	ESoldierStance LastStance = ESoldierStance::Standing;
	bool bWasDead = false;
	bool bWasDowned = false;
	bool bGrenadeInHand = false;
	bool bThrowReleased = false;
	/** The throw clip playing is an armed one (grenade already in hand, pin out). */
	bool bArmedThrow = false;

	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
	float SmoothedSpeed = 0.f;
};
