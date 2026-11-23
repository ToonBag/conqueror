#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SquadTypes.h"
#include "TacticalPlanTypes.h"
#include "TacticalPlan.generated.h"

class ASquadPlayerState;

/**
 * One team's tactical plan (GDD §13.1): the marks its CO, XO and planners have drawn on the tactical map, and
 * the open requests for orders from the rest of the team. The game mode spawns one per team.
 *
 * Server-owned: every change goes through the Server* RPCs on ASquadPlayerController, which call the functions
 * below; they check the caller's team and role. The actor only replicates to players on its own team, so the
 * enemy never receives the plan.
 */
UCLASS(NotPlaceable)
class UNREAL_API ATacticalPlan : public AActor
{
	GENERATED_BODY()

public:
	ATacticalPlan();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** This team's plan, or nullptr (also nullptr on clients of the other team: it never reaches them). */
	static ATacticalPlan* FindForTeam(const UObject* WorldContextObject, ESquadTeam Team);

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Tactical Plan")
	ESquadTeam Team = ESquadTeam::Allies;

	// ---- Tuning (provisional, GDD §13.1)

	/** Marks a plan can hold; drawing one more removes the oldest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan", meta = (ClampMin = "1"))
	int32 MaxMarkers = 40;

	/** Points kept on a freehand route (the server thins longer ones). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan", meta = (ClampMin = "2"))
	int32 MaxRoutePoints = 64;

	/** Seconds before a player may ask for orders again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan", meta = (ClampMin = "0", Units = "s"))
	float RequestCooldown = 15.f;

	/** Seconds an unanswered request stays open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan", meta = (ClampMin = "1", Units = "s"))
	float RequestLifetime = 60.f;

	/** Seconds an acknowledged request stays listed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan", meta = (ClampMin = "0", Units = "s"))
	float AcknowledgedLinger = 8.f;

	/** Seconds a Ping lasts (GDD §13.2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan|Pings", meta = (ClampMin = "0.5", Units = "s"))
	float PingLifetime = 6.f;

	/** Seconds a Spot (enemy spotted) lasts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan|Pings", meta = (ClampMin = "0.5", Units = "s"))
	float SpotLifetime = 10.f;

	/** Minimum seconds between two pings from one player (anti-spam). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan|Pings", meta = (ClampMin = "0", Units = "s"))
	float MinPingInterval = 0.5f;

	/** Pings one player can have up at once; another replaces their oldest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical Plan|Pings", meta = (ClampMin = "1"))
	int32 MaxPingsPerPlayer = 3;

	const TArray<FTacticalPing>& GetPings() const { return Pings; }

	/** Server: any player on the team. False if too soon after their last ping. */
	bool AddPing(ASquadPlayerState* By, ETacticalPingType Type, const FVector2D& Location);

	const TArray<FTacticalMarker>& GetMarkers() const { return Markers; }
	const TArray<FTacticalOrderRequest>& GetRequests() const { return Requests; }

	/** The open (unacknowledged) request this player made, if any. */
	const FTacticalOrderRequest* FindOpenRequestBy(const APlayerState* Player) const;

	/** Seconds until this player may request orders again (0 = now). Uses the replicated request times. */
	float GetRequestCooldownLeft(const APlayerState* Player) const;

	// ---- Server only. Each returns false (and changes nothing) if the caller may not do it.

	/** Who drew it and which role they had are filled in here; positions are clamped to the map. */
	bool AddMarker(ASquadPlayerState* Author, const FTacticalMarker& Marker);

	/** CO / XO may remove any mark; planners only their own. */
	bool RemoveMarker(ASquadPlayerState* By, int32 MarkerId);

	/** CO / XO only. */
	bool ClearMarkers(ASquadPlayerState* By);

	/** Anyone on the team who isn't CO / XO. Renews their open request rather than adding a second one. */
	bool AddRequest(ASquadPlayerState* By, const FVector2D& Location);

	/** CO / XO only. */
	bool AcknowledgeRequest(ASquadPlayerState* By, int32 RequestId);

	/** Server: the player's team and role allow drawing on this plan. */
	bool CanPlan(const ASquadPlayerState* Player) const;

private:
	bool IsOnTeam(const ASquadPlayerState* Player) const;
	float ServerNow() const;

	UPROPERTY(Replicated)
	TArray<FTacticalMarker> Markers;

	UPROPERTY(Replicated)
	TArray<FTacticalOrderRequest> Requests;

	UPROPERTY(Replicated)
	TArray<FTacticalPing> Pings;

	/** Server: last time each player pinged. */
	TMap<TWeakObjectPtr<APlayerState>, float> LastPingTime;

	int32 NextId = 1;

	/** Server: last time each player asked for orders. */
	TMap<TWeakObjectPtr<APlayerState>, float> LastRequestTime;
};
