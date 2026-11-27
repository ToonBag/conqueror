#include "SquadJoinError.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void USquadJoinError::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &USquadJoinError::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &USquadJoinError::OnTravelFailure);
	}
}

void USquadJoinError::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	Super::Deinitialize();
}

const USquadJoinError* USquadJoinError::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<USquadJoinError>() : nullptr;
}

void USquadJoinError::OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// The engine broadcasts for every world in the process (PIE runs several); keep only this instance's.
	if (World && World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	Message = ErrorString.IsEmpty() ? FString(ENetworkFailure::ToString(FailureType)) : ErrorString;
	UE_LOG(LogNet, Warning, TEXT("Connection failed: %s"), *Message);
}

void USquadJoinError::OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (World && World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	Message = ErrorString.IsEmpty() ? FString(ETravelFailure::ToString(FailureType)) : ErrorString;
	UE_LOG(LogNet, Warning, TEXT("Travel failed: %s"), *Message);
}
