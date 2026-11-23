#include "SoldierAnimInstance.h"

#include "Soldier.h"
#include "AnimationRuntime.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequence.h"
#include "CoverData.h"

namespace
{
	bool IsLoopingClip(ESoldierClip Clip)
	{
		switch (Clip)
		{
		case ESoldierClip::StandFire: case ESoldierClip::CrouchFire: case ESoldierClip::ProneFire:
		case ESoldierClip::StandReload: case ESoldierClip::CrouchReload: case ESoldierClip::ProneReload:
		case ESoldierClip::ThrowStand: case ESoldierClip::ThrowCrouch: case ESoldierClip::ThrowProne:
		case ESoldierClip::ThrowStepBackStand: case ESoldierClip::ThrowStepBackCrouch: case ESoldierClip::ThrowStepBackProne:
		case ESoldierClip::ThrowArmedStand: case ESoldierClip::ThrowArmedCrouch: case ESoldierClip::ThrowArmedProne:
		case ESoldierClip::StandToCrouch: case ESoldierClip::CrouchToStand: case ESoldierClip::CrouchToProne:
		case ESoldierClip::ProneToCrouch: case ESoldierClip::StandToProne: case ESoldierClip::ProneToStand:
		case ESoldierClip::DeathStand: case ESoldierClip::DeathProne:
			return false;
		default:
			return true;
		}
	}

	ESoldierClip TransitionClip(ESoldierStance From, ESoldierStance To)
	{
		switch (From)
		{
		case ESoldierStance::Standing: return To == ESoldierStance::Crouched ? ESoldierClip::StandToCrouch : ESoldierClip::StandToProne;
		case ESoldierStance::Crouched: return To == ESoldierStance::Standing ? ESoldierClip::CrouchToStand : ESoldierClip::CrouchToProne;
		default:                       return To == ESoldierStance::Crouched ? ESoldierClip::ProneToCrouch : ESoldierClip::ProneToStand;
		}
	}

	constexpr int32 MaxLayers = 4;
	/** Below this ground speed (cm/s) a soldier between steps still counts as moving if it has a path. */
	constexpr float MovingSpeedThreshold = 15.f;
}

// ------------------------------------------------------------------------------------------------ Proxy

void FSoldierAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	// Game thread: copy the layer stack for evaluation (which may run on a worker thread).
	Layers.Reset();
	if (const USoldierAnimInstance* Instance = Cast<USoldierAnimInstance>(InAnimInstance))
	{
		for (const FSoldierAnimLayer& Layer : Instance->Layers)
		{
			if (Layer.Sequence)
			{
				Layers.Add({ Layer.Sequence.Get(), Layer.Time, Layer.bLoop, Layer.Weight });
			}
		}
	}
}

bool FSoldierAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (Layers.Num() == 0)
	{
		Output.ResetToRefPose();
		return true;
	}

	FAnimationPoseData OutData(Output);
	Layers[0].Sequence->GetAnimationPose(OutData, FAnimExtractContext(static_cast<double>(Layers[0].Time), false, FDeltaTimeRecord(), Layers[0].bLoop));
	for (int32 i = 1; i < Layers.Num(); ++i)
	{
		const FLayer& Layer = Layers[i];
		FPoseContext LayerPose(Output);
		FAnimationPoseData LayerData(LayerPose);
		Layer.Sequence->GetAnimationPose(LayerData, FAnimExtractContext(static_cast<double>(Layer.Time), false, FDeltaTimeRecord(), Layer.bLoop));
		// Weight of pose one = what stays of everything below this layer.
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutData, LayerData, 1.f - FMath::Clamp(Layer.Weight, 0.f, 1.f));
	}
	return true;
}

// ------------------------------------------------------------------------------------------------ Instance

void USoldierAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Layers.Reset();
	Action = EAction::None;
	LastShotCount = LastThrowCount = LastGrenadesReleased = -1;
	bWasDead = bWasDowned = bGrenadeInHand = bThrowReleased = false;
	bHasLastLocation = false;
	SmoothedSpeed = 0.f;
	Visuals = nullptr;
	ClipCache.Reset();
}

UAnimSequence* USoldierAnimInstance::Clip(ESoldierClip Which) const
{
	const int32 Index = static_cast<int32>(Which);
	return ClipCache.IsValidIndex(Index) ? ClipCache[Index].Get() : nullptr;
}

ESoldierClip USoldierAnimInstance::PerStance(ESoldierStance Stance, ESoldierClip Standing, ESoldierClip Crouched, ESoldierClip Prone)
{
	switch (Stance)
	{
	case ESoldierStance::Crouched: return Crouched;
	case ESoldierStance::Prone:    return Prone;
	default:                       return Standing;
	}
}

void USoldierAnimInstance::Play(ESoldierClip Which, float BlendTime, float Rate, bool bRestart)
{
	UAnimSequence* Sequence = Clip(Which);
	if (!Sequence)
	{
		return;
	}
	if (!bRestart && Layers.Num() > 0 && Layers.Last().Clip == Which)
	{
		Layers.Last().Rate = Rate;
		return;
	}
	FSoldierAnimLayer& Layer = Layers.AddDefaulted_GetRef();
	Layer.Sequence = Sequence;
	Layer.Clip = Which;
	Layer.Rate = Rate;
	Layer.bLoop = IsLoopingClip(Which);
	Layer.BlendTime = BlendTime;
	Layer.Weight = (BlendTime <= 0.f || Layers.Num() == 1) ? 1.f : 0.f;
	if (Layer.Weight >= 1.f)
	{
		Layers.RemoveAt(0, Layers.Num() - 1);
	}
	while (Layers.Num() > MaxLayers)
	{
		Layers.RemoveAt(0); // Oldest fades are dropped; the new bottom layer counts as fully weighted.
	}
}

void USoldierAnimInstance::AdvanceLayers(float DeltaSeconds)
{
	for (FSoldierAnimLayer& Layer : Layers)
	{
		const float Length = Layer.Sequence ? Layer.Sequence->GetPlayLength() : 0.f;
		Layer.Time += DeltaSeconds * Layer.Rate;
		if (Length <= 0.f)
		{
			Layer.Time = 0.f;
		}
		else if (Layer.bLoop)
		{
			Layer.Time = FMath::Fmod(Layer.Time, Length);
			if (Layer.Time < 0.f)
			{
				Layer.Time += Length;
			}
		}
		else
		{
			Layer.Time = FMath::Clamp(Layer.Time, 0.f, Length);
		}
	}
	if (Layers.Num() > 0)
	{
		FSoldierAnimLayer& Top = Layers.Last();
		Top.Weight = Top.BlendTime > 0.f ? FMath::Min(1.f, Top.Weight + DeltaSeconds / Top.BlendTime) : 1.f;
		if (Top.Weight >= 1.f && Layers.Num() > 1)
		{
			Layers.RemoveAt(0, Layers.Num() - 1);
		}
	}
	if (Layers.Num() > 0)
	{
		Layers[0].Weight = 1.f;
	}
}

void USoldierAnimInstance::StartAction(EAction NewAction, ESoldierClip Which, float BlendTime)
{
	Action = NewAction;
	ActionClip = Which;
	Play(Which, BlendTime, 1.f, true);
}

ESoldierClip USoldierAnimInstance::ChooseBaseClip(const ASoldier& Soldier, ESoldierStance Stance, float& OutRate, float& OutBlend) const
{
	OutRate = 1.f;
	OutBlend = Visuals->DefaultBlend;
	const bool bMoving = Soldier.IsMoving() || SmoothedSpeed > MovingSpeedThreshold;
	auto MoveRate = [this](float AuthoredSpeed)
	{
		return FMath::Clamp(SmoothedSpeed / FMath::Max(AuthoredSpeed, 1.f), 0.35f, 2.f);
	};

	if (Soldier.GetStunRemaining() > 0.f)
	{
		return ESoldierClip::Stunned;
	}
	if (Soldier.IsInjured())
	{
		if (bMoving && !Soldier.IsBeingDragged())
		{
			OutRate = MoveRate(Visuals->CrawlSpeed);
			OutBlend = Visuals->MoveBlend;
			return ESoldierClip::InjuredCrawl;
		}
		return ESoldierClip::InjuredIdle;
	}
	if (Soldier.IsBeingDragged())
	{
		return ESoldierClip::InjuredIdle;
	}
	if (Soldier.IsDragging())
	{
		OutBlend = Visuals->MoveBlend;
		if (bMoving)
		{
			OutRate = MoveRate(Visuals->CrouchRunSpeed);
			return ESoldierClip::DragMove;
		}
		return ESoldierClip::DragIdle;
	}
	if (Soldier.IsBuilding() && !bMoving)
	{
		// GDD §13.3: kneeling at the wall to place C4 / a reinforcement (placeholder: the crouch idle).
		return Stance == ESoldierStance::Prone ? ESoldierClip::ProneIdle : ESoldierClip::CrouchIdle;
	}
	if (bMoving)
	{
		OutBlend = Visuals->MoveBlend;
		switch (Stance)
		{
		case ESoldierStance::Crouched: OutRate = MoveRate(Visuals->CrouchRunSpeed); return ESoldierClip::CrouchRun;
		case ESoldierStance::Prone:    OutRate = MoveRate(Visuals->CrawlSpeed);     return ESoldierClip::ProneCrawl;
		default:                       OutRate = MoveRate(Visuals->StandRunSpeed);  return ESoldierClip::StandRun;
		}
	}

	const bool bAttacking = Soldier.GetAttackTarget() != nullptr;
	if (Stance != ESoldierStance::Prone)
	{
		// GDD §9.2: leaning on the cover it faces. Low cover: rifle rested on top (also how it fires over it).
		// Full walls and trees: shoulder to the cover, or leaning out past a wall's edge.
		const ECoverType Cover = Soldier.GetLeaningCoverType();
		if (Cover != ECoverType::None)
		{
			const bool bCrouched = Stance == ESoldierStance::Crouched;
			const bool bHigh = Cover == ECoverType::WallCorner || Cover == ECoverType::Tree;
			if (!bHigh)
			{
				return bCrouched ? ESoldierClip::CoverLowCrouch : ESoldierClip::CoverLowStand;
			}
			const int32 PeekSide = Soldier.GetPeekSide();
			if (PeekSide != 0)
			{
				return PeekSide > 0 ? (bCrouched ? ESoldierClip::PeekRightCrouch : ESoldierClip::PeekRightStand)
				                    : (bCrouched ? ESoldierClip::PeekLeftCrouch : ESoldierClip::PeekLeftStand);
			}
			return bCrouched ? ESoldierClip::CoverHighCrouch : ESoldierClip::CoverHighStand;
		}
	}
	if (bAttacking)
	{
		OutBlend = Visuals->MoveBlend;
		return PerStance(Stance, ESoldierClip::StandAim, ESoldierClip::CrouchAim, ESoldierClip::ProneIdle);
	}
	return PerStance(Stance, ESoldierClip::StandIdle, ESoldierClip::CrouchIdle, ESoldierClip::ProneIdle);
}

void USoldierAnimInstance::UpdateAction(const ASoldier& Soldier, ESoldierStance Stance, float DeltaSeconds)
{
	const int32 Shots = Soldier.GetShotCount();
	const int32 Throws = Soldier.GetThrowCount();
	const int32 Released = Soldier.GetGrenadesReleased();
	const bool bNewShot = LastShotCount >= 0 && Shots != LastShotCount;
	const bool bNewRelease = LastGrenadesReleased >= 0 && Released != LastGrenadesReleased;
	LastShotCount = Shots;
	LastThrowCount = Throws;
	LastGrenadesReleased = Released;

	const bool bDowned = Soldier.IsDowned() || Soldier.IsBeingDragged();
	const bool bMoving = Soldier.IsMoving() || SmoothedSpeed > MovingSpeedThreshold;
	if (bDowned || Soldier.IsDragging())
	{
		Action = EAction::None;
	}

	// Grenade (GDD §7): the step-back clip follows the step back (grenade out, pin pulled), then the throw clip
	// follows the wind-up exactly and plays its follow-through. After a step back the armed throw clip is used
	// (pin already out); a soldier that couldn't step back plays the full throw. A cancelled throw (move order,
	// stun, death) keeps the grenade, so the clip just blends away.
	if (Soldier.IsGrenadeSteppingBack())
	{
		if (Action != EAction::StepBack)
		{
			StartAction(EAction::StepBack, PerStance(Stance, ESoldierClip::ThrowStepBackStand, ESoldierClip::ThrowStepBackCrouch, ESoldierClip::ThrowStepBackProne), 0.1f);
		}
	}
	else if (Soldier.IsGrenadeWindingUp())
	{
		if (Action != EAction::Throw)
		{
			bArmedThrow = Soldier.IsThrowAfterStepBack();
			const ESoldierClip ThrowClip = bArmedThrow
				? PerStance(Stance, ESoldierClip::ThrowArmedStand, ESoldierClip::ThrowArmedCrouch, ESoldierClip::ThrowArmedProne)
				: PerStance(Stance, ESoldierClip::ThrowStand, ESoldierClip::ThrowCrouch, ESoldierClip::ThrowProne);
			StartAction(EAction::Throw, ThrowClip, bArmedThrow ? 0.08f : 0.12f);
			bThrowReleased = false;
		}
	}
	else if (Soldier.IsThrowingGrenade())
	{
		// Finishing the step it was on before stepping back, or the frame between the step back and the
		// wind-up: whatever is playing carries on.
	}
	else if (Action == EAction::StepBack)
	{
		Action = EAction::None;
	}
	else if (Action == EAction::Throw && !bThrowReleased)
	{
		if (bNewRelease)
		{
			bThrowReleased = true;
		}
		else
		{
			Action = EAction::None;
		}
	}

	if (Action != EAction::Throw && Action != EAction::StepBack)
	{
		if (Soldier.IsReloading() && !bMoving && !bDowned)
		{
			if (Action != EAction::Reload)
			{
				StartAction(EAction::Reload, PerStance(Stance, ESoldierClip::StandReload, ESoldierClip::CrouchReload, ESoldierClip::ProneReload), 0.2f);
			}
		}
		else if (Action == EAction::Reload)
		{
			Action = EAction::None;
		}
	}

	// A set-up machine gun (GDD §6.1) fires off its bipod: the gun itself kicks back (ASoldier::UpdateSetupWeaponVisual)
	// instead of the rifle's fire clip, which would restart up to 20 times a second.
	if (bNewShot && !bDowned && !Soldier.HasSetupWeapon() && Action != EAction::Throw && Action != EAction::StepBack && Action != EAction::Reload)
	{
		StartAction(EAction::Fire, PerStance(Stance, ESoldierClip::StandFire, ESoldierClip::CrouchFire, ESoldierClip::ProneFire), Visuals->FireBlend);
	}

	// Stance change ordered while standing still (not one forced by a stun or injury).
	if (Stance != LastStance && !bDowned && !bWasDowned && !bMoving && (Action == EAction::None || Action == EAction::Fire || Action == EAction::StanceChange))
	{
		StartAction(EAction::StanceChange, TransitionClip(LastStance, Stance), 0.1f);
	}

	// Moving off ends one-shots (a throw only after the grenade has gone).
	if (bMoving && (Action == EAction::Fire || Action == EAction::StanceChange || (Action == EAction::Throw && bThrowReleased)))
	{
		Action = EAction::None;
	}
}

void USoldierAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const ASoldier* Soldier = Cast<ASoldier>(GetOwningActor());
	if (!Soldier)
	{
		return;
	}
	if (!Visuals)
	{
		Visuals = Soldier->GetVisualData();
		if (!Visuals)
		{
			return;
		}
		ClipCache.SetNum(static_cast<int32>(ESoldierClip::Num));
		for (int32 i = 0; i < ClipCache.Num(); ++i)
		{
			ClipCache[i] = Visuals->GetClip(static_cast<ESoldierClip>(i));
		}
		LastStance = Soldier->GetEffectiveStance();
	}

	// Ground speed drives the movement clips' play rate so feet keep pace with the grid movement.
	const FVector Location = Soldier->GetActorLocation();
	if (bHasLastLocation && DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		const float Speed = FVector::Dist2D(Location, LastLocation) / DeltaSeconds;
		SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Speed, DeltaSeconds, 10.f);
	}
	LastLocation = Location;
	bHasLastLocation = true;

	const ESoldierStance Stance = Soldier->GetEffectiveStance();

	if (Soldier->IsDead())
	{
		if (!bWasDead)
		{
			const bool bWasProne = LastStance == ESoldierStance::Prone || bWasDowned;
			Play(bWasProne ? ESoldierClip::DeathProne : ESoldierClip::DeathStand, Visuals->DeathBlend, 1.f, true);
			bWasDead = true;
			Action = EAction::None;
		}
		bGrenadeInHand = false;
		AdvanceLayers(DeltaSeconds);
		return;
	}
	bWasDead = false;

	UpdateAction(*Soldier, Stance, DeltaSeconds);

	if (Action == EAction::None)
	{
		float Rate = 1.f;
		float Blend = Visuals->DefaultBlend;
		const ESoldierClip Base = ChooseBaseClip(*Soldier, Stance, Rate, Blend);
		Play(Base, Blend, Rate);
	}
	AdvanceLayers(DeltaSeconds);

	// Clips whose time follows gameplay timers, and one-shots that have finished.
	bGrenadeInHand = false;
	if (Action != EAction::None && Layers.Num() > 0 && Layers.Last().Clip == ActionClip)
	{
		FSoldierAnimLayer& Top = Layers.Last();
		const float Length = Top.Sequence->GetPlayLength();
		switch (Action)
		{
		case EAction::StepBack:
			// In time with the step; held on its last pose for the frame before the wind-up starts.
			Top.Rate = 0.f;
			Top.Time = Soldier->IsGrenadeSteppingBack() ? Soldier->GetGrenadeStepBackAlpha() * Length : Length;
			bGrenadeInHand = Top.Time >= Visuals->StepBackGrenadeShowFraction * Length;
			break;
		case EAction::Throw:
			if (!bThrowReleased)
			{
				Top.Rate = 0.f;
				Top.Time = FMath::Min(Soldier->GetGrenadeWindupAlpha() * Visuals->ThrowReleaseTime, Length);
				bGrenadeInHand = bArmedThrow || Top.Time >= Visuals->ThrowGrenadeShowTime;
			}
			else
			{
				Top.Rate = 1.f;
				Top.Time = FMath::Max(Top.Time, Visuals->ThrowReleaseTime);
			}
			break;
		case EAction::Reload:
			Top.Rate = 0.f;
			Top.Time = Soldier->GetReloadProgress() * Length;
			break;
		default:
			break;
		}
		const bool bFinished = (Action == EAction::Fire || Action == EAction::StanceChange || (Action == EAction::Throw && bThrowReleased))
			&& Top.Time >= Length - KINDA_SMALL_NUMBER;
		if (bFinished)
		{
			Action = EAction::None;
		}
	}
	else if (Action != EAction::None && (Layers.Num() == 0 || Layers.Last().Clip != ActionClip))
	{
		Action = EAction::None; // Clip missing.
	}

	LastStance = Stance;
	bWasDowned = Soldier->IsDowned() || Soldier->IsBeingDragged();
}
