#pragma once

#include "CoreMinimal.h"
#include "SquadTypes.h"
#include "KillFeedTypes.generated.h"

/** What killed a soldier, for the kill feed (GDD §13). */
UENUM(BlueprintType)
enum class EKillMethod : uint8
{
	Rifle,
	Grenade,
	/** GDD §7.2: a C4 charge's blast, credited to the soldier who placed it. Shown with the grenade icon. */
	C4
};

/** One side of a kill-feed line: the player who owns the soldier, as everyone sees them. */
USTRUCT(BlueprintType)
struct FKillFeedPlayer
{
	GENERATED_BODY()

	/** Rank abbreviation from the player's ELO (GDD §12.1), e.g. "PVT", "1SG". */
	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	FString Rank;

	/** Regiment tag without brackets (GDD §12.3), empty if the player isn't in a regiment. */
	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	FString Regiment;

	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	ESquadTeam Team = ESquadTeam::Allies;
};

/** A kill, built by the server and multicast to every client by ASquadGameState. */
USTRUCT(BlueprintType)
struct FKillFeedEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	FKillFeedPlayer Killer;

	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	FKillFeedPlayer Victim;

	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	EKillMethod Method = EKillMethod::Rifle;

	/** Rifle that fired the killing round (empty for grenades). */
	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	FString WeaponName;

	/**
	 * Chance (percent) of the roll that killed: the killing shot's final hit chance for a rifle (GDD §5.3, after
	 * cover), the grenade kill chance for a grenade (GDD §7.1).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Kill Feed")
	float ChancePercent = 0.f;
};
