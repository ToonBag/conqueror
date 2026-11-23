#pragma once

#include "CoreMinimal.h"
#include "TacticalPlanTypes.generated.h"

class APlayerState;

/**
 * A player's command role in the current match (GDD §13.1). Decided by the server from the team's ELOs:
 * the highest is the Commanding Officer, the second the Executive Officer. Anyone else can be given
 * planning rights by the CO or XO (Planner). Not the same thing as the regiment roles of §12.3.
 */
UENUM(BlueprintType)
enum class ETacticalRole : uint8
{
	Soldier,
	/** Given planning rights by the CO or XO: may draw on the tactical map. */
	Planner,
	/** Executive Officer: second-highest ELO on the team. */
	XO,
	/** Commanding Officer: highest ELO on the team. */
	CO
};

/** What a mark on the tactical map means (GDD §13.1). */
UENUM(BlueprintType)
enum class ETacticalMarkerType : uint8
{
	/** Arrow from Start to End: advance / push this way. */
	Push,
	/** Line from Start to End with ticks on the enemy's side: hold this line. */
	HoldLine,
	/** Freehand path through Points: route to take. */
	Route,
	/** Shield at Start: defend this position. */
	Defend,
	/** Target at Start: attack / assault this position. */
	Attack,
	/** Flag at Start: rally / regroup point. */
	Rally,
	/** Hostile diamond at Start: enemy reported here. */
	Enemy
};

/** What a left click on the tactical map does (client-side tool choice, GDD §13.1). */
UENUM(BlueprintType)
enum class ETacticalTool : uint8
{
	/** Click / drag moves the camera there. The only tool for players without planning rights. */
	View,
	Push,
	HoldLine,
	Route,
	Defend,
	Attack,
	Rally,
	Enemy,
	/** Click a mark to remove it. */
	Erase
};

/** A short-lived ping any player can drop (GDD §13.2). */
UENUM(BlueprintType)
enum class ETacticalPingType : uint8
{
	/** Enemy spotted here (snaps onto a visible enemy near the point). */
	Spot,
	/** Look here / go here. */
	Ping
};

/** One slice of the middle-mouse command wheel (GDD §13.2). */
UENUM(BlueprintType)
enum class ERadialAction : uint8
{
	Spot,
	Ping,
	RequestOrders,
	Push,
	HoldLine,
	Route,
	Defend,
	Attack,
	Rally,
	Erase,
	/** Opens the Build sub-wheel (GDD §13.3): C4, wall reinforcement. */
	Build
};

/** A ping on a team's plan. Replicated with the plan, so only the team sees it. */
USTRUCT()
struct FTacticalPing
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	ETacticalPingType Type = ETacticalPingType::Ping;

	/** World XY, cm. */
	UPROPERTY()
	FVector2f Location = FVector2f::ZeroVector;

	UPROPERTY()
	TObjectPtr<APlayerState> Author;

	/** Rank tag and name, e.g. "PFC Smith". */
	UPROPERTY()
	FString AuthorLabel;

	/** Server world time it was made. */
	UPROPERTY()
	float Time = 0.f;

	/** Seconds it lasts. */
	UPROPERTY()
	float Lifetime = 6.f;
};

/** One mark on a team's tactical plan. Positions are world XY in cm; point marks sit on cell centres. */
USTRUCT()
struct FTacticalMarker
{
	GENERATED_BODY()

	/** Unique within the team's plan, assigned by the server. */
	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	ETacticalMarkerType Type = ETacticalMarkerType::Defend;

	UPROPERTY()
	FVector2f Start = FVector2f::ZeroVector;

	/** Push / HoldLine only. */
	UPROPERTY()
	FVector2f End = FVector2f::ZeroVector;

	/** Route only: the path, Start first. */
	UPROPERTY()
	TArray<FVector2f> Points;

	/** Filled in by the server. */
	UPROPERTY()
	TObjectPtr<APlayerState> Author;

	UPROPERTY()
	FString AuthorName;

	UPROPERTY()
	ETacticalRole AuthorRole = ETacticalRole::Soldier;

	bool IsLine() const { return Type == ETacticalMarkerType::Push || Type == ETacticalMarkerType::HoldLine; }
	bool IsPoint() const { return !IsLine() && Type != ETacticalMarkerType::Route; }
};

/** A player asking the CO / XO for orders (GDD §13.1). */
USTRUCT()
struct FTacticalOrderRequest
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	TObjectPtr<APlayerState> Requester;

	/** Rank tag and name, e.g. "PFC Smith". */
	UPROPERTY()
	FString RequesterLabel;

	/** Where the requester's squad is (centre of its living soldiers), world XY cm. */
	UPROPERTY()
	FVector2f Location = FVector2f::ZeroVector;

	/** Server world time the request was made (or last renewed). */
	UPROPERTY()
	float RequestTime = 0.f;

	UPROPERTY()
	bool bAcknowledged = false;

	/** Role tag and name of whoever acknowledged it. */
	UPROPERTY()
	FString AcknowledgedBy;

	UPROPERTY()
	float AcknowledgeTime = 0.f;
};

namespace TacticalPlan
{
	inline const TCHAR* RoleTag(ETacticalRole Role)
	{
		switch (Role)
		{
		case ETacticalRole::CO: return TEXT("CO");
		case ETacticalRole::XO: return TEXT("XO");
		case ETacticalRole::Planner: return TEXT("PLN");
		default: return TEXT("");
		}
	}

	inline const TCHAR* RoleName(ETacticalRole Role)
	{
		switch (Role)
		{
		case ETacticalRole::CO: return TEXT("Commanding Officer");
		case ETacticalRole::XO: return TEXT("Executive Officer");
		case ETacticalRole::Planner: return TEXT("Planner");
		default: return TEXT("Soldier");
		}
	}

	inline const TCHAR* MarkerName(ETacticalMarkerType Type)
	{
		switch (Type)
		{
		case ETacticalMarkerType::Push: return TEXT("Push");
		case ETacticalMarkerType::HoldLine: return TEXT("Hold line");
		case ETacticalMarkerType::Route: return TEXT("Route");
		case ETacticalMarkerType::Defend: return TEXT("Defend");
		case ETacticalMarkerType::Attack: return TEXT("Attack");
		case ETacticalMarkerType::Rally: return TEXT("Rally point");
		default: return TEXT("Enemy");
		}
	}

	inline bool IsCommandRole(ETacticalRole Role) { return Role == ETacticalRole::CO || Role == ETacticalRole::XO; }

	inline const TCHAR* RadialName(ERadialAction Action)
	{
		switch (Action)
		{
		case ERadialAction::Spot: return TEXT("Spot enemy");
		case ERadialAction::Ping: return TEXT("Ping");
		case ERadialAction::RequestOrders: return TEXT("Request orders");
		case ERadialAction::Push: return TEXT("Push");
		case ERadialAction::HoldLine: return TEXT("Hold line");
		case ERadialAction::Route: return TEXT("Route");
		case ERadialAction::Defend: return TEXT("Defend");
		case ERadialAction::Attack: return TEXT("Attack");
		case ERadialAction::Rally: return TEXT("Rally");
		case ERadialAction::Build: return TEXT("Build");
		default: return TEXT("Erase");
		}
	}

	/** The plan mark a wheel slice makes. False for pings, requests and Erase. */
	inline bool RadialMarker(ERadialAction Action, ETacticalMarkerType& OutType)
	{
		switch (Action)
		{
		case ERadialAction::Push: OutType = ETacticalMarkerType::Push; return true;
		case ERadialAction::HoldLine: OutType = ETacticalMarkerType::HoldLine; return true;
		case ERadialAction::Route: OutType = ETacticalMarkerType::Route; return true;
		case ERadialAction::Defend: OutType = ETacticalMarkerType::Defend; return true;
		case ERadialAction::Attack: OutType = ETacticalMarkerType::Attack; return true;
		case ERadialAction::Rally: OutType = ETacticalMarkerType::Rally; return true;
		default: return false;
		}
	}

	/** The mark a drawing tool makes. False for View / Erase. */
	inline bool ToolMarker(ETacticalTool Tool, ETacticalMarkerType& OutType)
	{
		switch (Tool)
		{
		case ETacticalTool::Push: OutType = ETacticalMarkerType::Push; return true;
		case ETacticalTool::HoldLine: OutType = ETacticalMarkerType::HoldLine; return true;
		case ETacticalTool::Route: OutType = ETacticalMarkerType::Route; return true;
		case ETacticalTool::Defend: OutType = ETacticalMarkerType::Defend; return true;
		case ETacticalTool::Attack: OutType = ETacticalMarkerType::Attack; return true;
		case ETacticalTool::Rally: OutType = ETacticalMarkerType::Rally; return true;
		case ETacticalTool::Enemy: OutType = ETacticalMarkerType::Enemy; return true;
		default: return false;
		}
	}
}
