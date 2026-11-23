#include "SoldierVisualData.h"

#include "Soldier.h"
#include "Animation/AnimSequence.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	// Asset names of the hand-keyed clips, in ESoldierClip order.
	const TCHAR* const ClipAssetNames[] = {
		TEXT("A_Stand_Idle"), TEXT("A_Stand_Aim"), TEXT("A_Stand_Fire"), TEXT("A_Stand_Run"), TEXT("A_Stand_Reload"),
		TEXT("A_Crouch_Idle"), TEXT("A_Crouch_Aim"), TEXT("A_Crouch_Fire"), TEXT("A_Crouch_Run"), TEXT("A_Crouch_Reload"),
		TEXT("A_Prone_Idle"), TEXT("A_Prone_Fire"), TEXT("A_Prone_Crawl"), TEXT("A_Prone_Reload"),
		TEXT("A_Throw_Stand"), TEXT("A_Throw_Crouch"), TEXT("A_Throw_Prone"),
		TEXT("A_Stand_To_Crouch"), TEXT("A_Crouch_To_Stand"), TEXT("A_Crouch_To_Prone"), TEXT("A_Prone_To_Crouch"),
		TEXT("A_Stand_To_Prone"), TEXT("A_Prone_To_Stand"),
		TEXT("A_Cover_Low_Stand"), TEXT("A_Cover_Low_Crouch"), TEXT("A_Cover_High_Stand"), TEXT("A_Cover_High_Crouch"),
		TEXT("A_Cover_Peek_R_Stand"), TEXT("A_Cover_Peek_L_Stand"), TEXT("A_Cover_Peek_R_Crouch"), TEXT("A_Cover_Peek_L_Crouch"),
		TEXT("A_Stunned"), TEXT("A_Injured_Idle"), TEXT("A_Injured_Crawl"), TEXT("A_Drag_Idle"), TEXT("A_Drag_Move"),
		TEXT("A_Death_Stand"), TEXT("A_Death_Prone"),
		TEXT("A_Throw_StepBack_Stand"), TEXT("A_Throw_StepBack_Crouch"), TEXT("A_Throw_StepBack_Prone"),
		TEXT("A_Throw_Armed_Stand"), TEXT("A_Throw_Armed_Crouch"), TEXT("A_Throw_Armed_Prone"),
	};
	static_assert(UE_ARRAY_COUNT(ClipAssetNames) == static_cast<int32>(ESoldierClip::Num), "One asset name per ESoldierClip");
}

void USoldierVisualData::PlayCombatSound(const UObject* WorldContext, USoundBase* Sound, const FVector& Location) const
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!Sound || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator, 1.f, 1.f, 0.f,
		CombatSoundAttenuation);
}

const USoldierVisualData* USoldierVisualData::GetDefaultVisuals()
{
	return GetDefault<ASoldier>()->GetVisualData();
}

UAnimSequence* USoldierVisualData::GetClip(ESoldierClip Clip) const
{
	if (const TObjectPtr<UAnimSequence>* Found = Clips.Find(Clip); Found && *Found)
	{
		return Found->Get();
	}
	const int32 Index = static_cast<int32>(Clip);
	if (Index < 0 || Index >= static_cast<int32>(ESoldierClip::Num))
	{
		return nullptr;
	}
	const FString Path = FString::Printf(TEXT("/Game/Characters/Soldier/Anims/Hand/%s.%s"), ClipAssetNames[Index], ClipAssetNames[Index]);
	return LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}
