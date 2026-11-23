#include "TacticalPlan.h"

#include "GridManager.h"
#include "SquadPlayerController.h"
#include "SquadPlayerState.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogTacticalPlan, Log, All);

ATacticalPlan::ATacticalPlan()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	bReplicates = true;
	bAlwaysRelevant = false; // IsNetRelevantFor decides: own team only.
	SetReplicatingMovement(false);
	SetHidden(true);
	SetCanBeDamaged(false);
	SetNetUpdateFrequency(4.f);
}

void ATacticalPlan::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATacticalPlan, Team);
	DOREPLIFETIME(ATacticalPlan, Markers);
	DOREPLIFETIME(ATacticalPlan, Requests);
	DOREPLIFETIME(ATacticalPlan, Pings);
}

bool ATacticalPlan::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	// The plan is the team's secret: only its own players receive it.
	if (const ASquadPlayerController* PC = Cast<ASquadPlayerController>(RealViewer))
	{
		return PC->PlayerTeam == Team;
	}
	return false;
}

ATacticalPlan* ATacticalPlan::FindForTeam(const UObject* WorldContextObject, ESquadTeam InTeam)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATacticalPlan> It(World); It; ++It)
	{
		if (It->Team == InTeam)
		{
			return *It;
		}
	}
	return nullptr;
}

float ATacticalPlan::ServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}
	const AGameStateBase* GS = World->GetGameState();
	return GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : World->GetTimeSeconds();
}

bool ATacticalPlan::IsOnTeam(const ASquadPlayerState* Player) const
{
	return Player && Player->Team == Team;
}

bool ATacticalPlan::CanPlan(const ASquadPlayerState* Player) const
{
	return IsOnTeam(Player) && Player->CanPlan();
}

const FTacticalOrderRequest* ATacticalPlan::FindOpenRequestBy(const APlayerState* Player) const
{
	return Player ? Requests.FindByPredicate([Player](const FTacticalOrderRequest& R) { return !R.bAcknowledged && R.Requester == Player; }) : nullptr;
}

float ATacticalPlan::GetRequestCooldownLeft(const APlayerState* Player) const
{
	if (!Player)
	{
		return 0.f;
	}
	float Last = -FLT_MAX;
	if (HasAuthority())
	{
		if (const float* Found = LastRequestTime.Find(TWeakObjectPtr<APlayerState>(const_cast<APlayerState*>(Player))))
		{
			Last = *Found;
		}
	}
	else
	{
		for (const FTacticalOrderRequest& R : Requests)
		{
			if (R.Requester == Player)
			{
				Last = FMath::Max(Last, R.RequestTime);
			}
		}
	}
	return Last == -FLT_MAX ? 0.f : FMath::Max(0.f, RequestCooldown - (ServerNow() - Last));
}

void ATacticalPlan::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	const float Now = ServerNow();
	const int32 Removed = Requests.RemoveAll([this, Now](const FTacticalOrderRequest& R)
	{
		if (!R.Requester)
		{
			return true; // Left the match.
		}
		return R.bAcknowledged ? Now - R.AcknowledgeTime > AcknowledgedLinger : Now - R.RequestTime > RequestLifetime;
	});
	const int32 PingsRemoved = Pings.RemoveAll([Now](const FTacticalPing& P) { return Now - P.Time > P.Lifetime; });
	if (Removed > 0 || PingsRemoved > 0)
	{
		ForceNetUpdate();
	}
}

namespace
{
	/** Clamp a world XY point onto the grid; snapped to the centre of its cell if bSnap. */
	FVector2f ClampToGrid(const AGridManager* Grid, const FVector2f& P, bool bSnap)
	{
		if (!Grid || Grid->GridSizeX <= 0 || Grid->GridSizeY <= 0)
		{
			return P;
		}
		const FVector Half(Grid->CellSize * 0.5f, Grid->CellSize * 0.5f, 0.f);
		const FVector Lo = Grid->CellToWorld(FIntPoint(0, 0)) - Half;
		const FVector Hi = Grid->CellToWorld(FIntPoint(Grid->GridSizeX - 1, Grid->GridSizeY - 1)) + Half;
		FVector World(FMath::Clamp<double>(P.X, FMath::Min(Lo.X, Hi.X) + 1.0, FMath::Max(Lo.X, Hi.X) - 1.0),
			FMath::Clamp<double>(P.Y, FMath::Min(Lo.Y, Hi.Y) + 1.0, FMath::Max(Lo.Y, Hi.Y) - 1.0), Grid->GetActorLocation().Z);
		FIntPoint Cell;
		if (bSnap && Grid->WorldToCell(World, Cell))
		{
			World = Grid->CellToWorld(Cell);
		}
		return FVector2f(World.X, World.Y);
	}
}

bool ATacticalPlan::AddMarker(ASquadPlayerState* Author, const FTacticalMarker& In)
{
	if (!HasAuthority() || !CanPlan(Author))
	{
		return false;
	}
	const AGridManager* Grid = AGridManager::Find(this);
	const float CellSize = Grid ? Grid->CellSize : 100.f;

	FTacticalMarker M;
	M.Type = In.Type;
	M.Author = Author;
	M.AuthorName = Author->GetPlayerName();
	M.AuthorRole = Author->TacticalRole;
	if (M.IsPoint())
	{
		M.Start = ClampToGrid(Grid, In.Start, true);
	}
	else if (M.IsLine())
	{
		M.Start = ClampToGrid(Grid, In.Start, true);
		M.End = ClampToGrid(Grid, In.End, true);
		if (FVector2f::Distance(M.Start, M.End) < CellSize * 0.99f)
		{
			return false; // Too short to mean anything.
		}
	}
	else // Route
	{
		if (In.Points.Num() < 2)
		{
			return false;
		}
		// Thin evenly down to the limit, always keeping the last point.
		const int32 Num = In.Points.Num();
		const int32 Keep = FMath::Min(Num, MaxRoutePoints);
		M.Points.Reserve(Keep);
		for (int32 I = 0; I < Keep; ++I)
		{
			const int32 Src = Keep == 1 ? 0 : FMath::RoundToInt(static_cast<float>(I) * (Num - 1) / (Keep - 1));
			const FVector2f P = ClampToGrid(Grid, In.Points[Src], false);
			if (M.Points.Num() == 0 || FVector2f::Distance(M.Points.Last(), P) > 1.f)
			{
				M.Points.Add(P);
			}
		}
		float Length = 0.f;
		for (int32 I = 1; I < M.Points.Num(); ++I)
		{
			Length += FVector2f::Distance(M.Points[I - 1], M.Points[I]);
		}
		if (M.Points.Num() < 2 || Length < CellSize)
		{
			return false;
		}
		M.Start = M.Points[0];
		M.End = M.Points.Last();
	}
	M.Id = NextId++;
	Markers.Add(MoveTemp(M));
	while (Markers.Num() > MaxMarkers)
	{
		Markers.RemoveAt(0); // Oldest first.
	}
	ForceNetUpdate();
	UE_LOG(LogTacticalPlan, Log, TEXT("%s %s drew %s (%d marks)."), TacticalPlan::RoleTag(Author->TacticalRole),
		*Author->GetPlayerName(), TacticalPlan::MarkerName(In.Type), Markers.Num());
	return true;
}

bool ATacticalPlan::RemoveMarker(ASquadPlayerState* By, int32 MarkerId)
{
	if (!HasAuthority() || !CanPlan(By))
	{
		return false;
	}
	const int32 Index = Markers.IndexOfByPredicate([MarkerId](const FTacticalMarker& M) { return M.Id == MarkerId; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (!TacticalPlan::IsCommandRole(By->TacticalRole) && Markers[Index].Author != By)
	{
		return false; // Planners may only erase their own marks.
	}
	Markers.RemoveAt(Index);
	ForceNetUpdate();
	return true;
}

bool ATacticalPlan::ClearMarkers(ASquadPlayerState* By)
{
	if (!HasAuthority() || !IsOnTeam(By) || !TacticalPlan::IsCommandRole(By->TacticalRole))
	{
		return false;
	}
	Markers.Reset();
	ForceNetUpdate();
	UE_LOG(LogTacticalPlan, Log, TEXT("%s %s cleared the plan."), TacticalPlan::RoleTag(By->TacticalRole), *By->GetPlayerName());
	return true;
}

bool ATacticalPlan::AddRequest(ASquadPlayerState* By, const FVector2D& Location)
{
	if (!HasAuthority() || !IsOnTeam(By) || TacticalPlan::IsCommandRole(By->TacticalRole) || GetRequestCooldownLeft(By) > 0.f)
	{
		return false;
	}
	const float Now = ServerNow();
	LastRequestTime.Add(By, Now);
	const FVector2f Where = ClampToGrid(AGridManager::Find(this), FVector2f(Location), false);
	FTacticalOrderRequest* Request = Requests.FindByPredicate([By](const FTacticalOrderRequest& R) { return !R.bAcknowledged && R.Requester == By; });
	if (!Request)
	{
		Request = &Requests.AddDefaulted_GetRef();
		Request->Id = NextId++;
		Request->Requester = By;
	}
	Request->RequesterLabel = FString::Printf(TEXT("%s %s"), *By->GetRankAbbreviation(), *By->GetPlayerName());
	Request->Location = Where;
	Request->RequestTime = Now;
	ForceNetUpdate();
	UE_LOG(LogTacticalPlan, Log, TEXT("%s requests orders."), *Request->RequesterLabel);
	return true;
}

bool ATacticalPlan::AddPing(ASquadPlayerState* By, ETacticalPingType Type, const FVector2D& Location)
{
	if (!HasAuthority() || !IsOnTeam(By))
	{
		return false;
	}
	const float Now = ServerNow();
	const TWeakObjectPtr<APlayerState> Key(By);
	if (const float* Last = LastPingTime.Find(Key); Last && Now - *Last < MinPingInterval)
	{
		return false;
	}
	LastPingTime.Add(Key, Now);

	// Keep at most MaxPingsPerPlayer from this player: the oldest goes.
	int32 Count = 0;
	for (int32 I = Pings.Num() - 1; I >= 0; --I)
	{
		if (Pings[I].Author == By && ++Count >= MaxPingsPerPlayer)
		{
			Pings.RemoveAt(I);
		}
	}

	FTacticalPing& Ping = Pings.AddDefaulted_GetRef();
	Ping.Id = NextId++;
	Ping.Type = Type;
	Ping.Location = ClampToGrid(AGridManager::Find(this), FVector2f(Location), false);
	Ping.Author = By;
	Ping.AuthorLabel = FString::Printf(TEXT("%s %s"), *By->GetRankAbbreviation(), *By->GetPlayerName());
	Ping.Time = Now;
	Ping.Lifetime = Type == ETacticalPingType::Spot ? SpotLifetime : PingLifetime;
	ForceNetUpdate();
	return true;
}

bool ATacticalPlan::AcknowledgeRequest(ASquadPlayerState* By, int32 RequestId)
{
	if (!HasAuthority() || !IsOnTeam(By) || !TacticalPlan::IsCommandRole(By->TacticalRole))
	{
		return false;
	}
	FTacticalOrderRequest* Request = Requests.FindByPredicate([RequestId](const FTacticalOrderRequest& R) { return R.Id == RequestId; });
	if (!Request || Request->bAcknowledged)
	{
		return false;
	}
	Request->bAcknowledged = true;
	Request->AcknowledgedBy = FString::Printf(TEXT("%s %s"), TacticalPlan::RoleTag(By->TacticalRole), *By->GetPlayerName());
	Request->AcknowledgeTime = ServerNow();
	ForceNetUpdate();
	return true;
}
