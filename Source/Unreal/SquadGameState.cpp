#include "SquadGameState.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

void ASquadGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASquadGameState, bVPHeld);
	DOREPLIFETIME(ASquadGameState, VPHoldingTeam);
	DOREPLIFETIME(ASquadGameState, VPHoldElapsed);
	DOREPLIFETIME(ASquadGameState, VPHoldRequired);
	DOREPLIFETIME(ASquadGameState, WinningTeam);
	DOREPLIFETIME(ASquadGameState, VictoryReason);
	DOREPLIFETIME(ASquadGameState, bBallistics);
}

void ASquadGameState::AddKill(const FKillFeedEntry& Entry)
{
	if (HasAuthority())
	{
		MulticastKill(Entry);
	}
}

void ASquadGameState::SetVPHold(bool bHolding, ESquadTeam HoldingTeam, float Elapsed, float Required)
{
	if (HasAuthority())
	{
		bVPHeld = bHolding;
		VPHoldingTeam = HoldingTeam;
		VPHoldElapsed = bHolding ? Elapsed : 0.f;
		VPHoldRequired = Required;
	}
}

void ASquadGameState::SetMatchResult(ESquadTeam Winner, EVictoryReason Reason)
{
	if (HasAuthority() && !IsMatchOver())
	{
		WinningTeam = Winner;
		VictoryReason = Reason;
		ForceNetUpdate();
	}
}

void ASquadGameState::MulticastKill_Implementation(const FKillFeedEntry& Entry)
{
	FKillFeedItem& Item = KillFeed.AddDefaulted_GetRef();
	Item.Entry = Entry;
	Item.ReceivedTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	if (KillFeed.Num() > MaxStoredKills)
	{
		KillFeed.RemoveAt(0, KillFeed.Num() - MaxStoredKills);
	}

	const FString Who = [](const FKillFeedPlayer& P)
	{
		return FString::Printf(TEXT("%s%s %s"), *P.Rank, P.Regiment.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *P.Regiment), *P.Name);
	}(Entry.Killer);
	UE_LOG(LogTemp, Log, TEXT("KILL: %s -> %s %s (%s%s, %.1f%%)"), *Who, *Entry.Victim.Rank, *Entry.Victim.Name,
		Entry.Method == EKillMethod::Grenade ? TEXT("grenade") : Entry.Method == EKillMethod::C4 ? TEXT("C4") : *Entry.WeaponName,
		Entry.bBledOut ? TEXT(", bled out") : TEXT(""), Entry.ChancePercent);
}
