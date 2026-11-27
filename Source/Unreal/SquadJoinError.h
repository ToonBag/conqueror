#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "SquadJoinError.generated.h"

class UNetDriver;

/**
 * Client: remembers why joining or staying on a server failed (for example the server refused an out-of-date
 * build, see ASquadGameMode::PreLogin), so the HUD can say so on the local map the engine falls back to.
 */
UCLASS()
class UNREAL_API USquadJoinError : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The last connection error, empty if none. */
	const FString& GetMessage() const { return Message; }

	static const USquadJoinError* Get(const UObject* WorldContextObject);

private:
	void OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	FString Message;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
