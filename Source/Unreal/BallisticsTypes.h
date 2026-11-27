#pragma once

#include "CoreMinimal.h"

class ASoldier;

/** Body part a hit shape stands for (projectile model, GDD §16). */
enum class EHitPart : uint8
{
	Head,
	Chest,
	Belly,
	Hips,
	Thighs,
	Shins,
};

const TCHAR* GetHitPartName(EHitPart Part);

/** One sphere of a soldier's hit shapes, in world space. Analytic, so it is identical on server and clients. */
struct FBallisticShape
{
	FVector Centre = FVector::ZeroVector;
	float Radius = 10.f;
	EHitPart Part = EHitPart::Chest;
};

using FBallisticShapes = TArray<FBallisticShape, TInlineAllocator<6>>;

/** What a straight round segment ran into first. */
enum class EBallisticHit : uint8
{
	None,
	Ground,
	Cover,
	Soldier,
};

struct FBallisticTrace
{
	EBallisticHit Kind = EBallisticHit::None;
	/** Fraction along the traced segment (0..1). */
	float T = 1.f;
	FVector Point = FVector::ZeroVector;
	ASoldier* Soldier = nullptr;
	EHitPart Part = EHitPart::Chest;
	/** Cover cell hit (Kind == Cover). */
	FIntPoint CoverCell = FIntPoint::ZeroValue;
	/**
	 * Estimates only: product of the chances of passing every hay bale crossed before the hit (1 if none).
	 * A real round rolls each bale instead.
	 */
	float PassWeight = 1.f;
};
