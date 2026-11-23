#include "SquadPlayerState.h"

#include "Net/UnrealNetwork.h"

void ASquadPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASquadPlayerState, Elo);
	DOREPLIFETIME(ASquadPlayerState, RegimentTag);
	DOREPLIFETIME(ASquadPlayerState, Team);
	DOREPLIFETIME(ASquadPlayerState, TacticalRole);
	DOREPLIFETIME(ASquadPlayerState, bPlanningDelegated);
}

FString ASquadPlayerState::RankAbbreviationForElo(int32 InElo)
{
	// GDD §12.1 Player Rank Ladder (upper bound of each band, inclusive).
	struct FBand { int32 MaxElo; const TCHAR* Tag; };
	static const FBand Bands[] = {
		{ 100,  TEXT("PVT") }, // Private
		{ 200,  TEXT("PFC") }, // Private First Class
		{ 300,  TEXT("CPL") }, // Corporal
		{ 400,  TEXT("SGT") }, // Sergeant
		{ 500,  TEXT("SSG") }, // Staff Sergeant
		{ 600,  TEXT("PSG") }, // Platoon Sergeant
		{ 700,  TEXT("MSG") }, // Master Sergeant
		{ 800,  TEXT("1SG") }, // First Sergeant
		{ 1000, TEXT("SGM") }, // Sergeant Major
		{ 1500, TEXT("WO1") }, // Warrant Officer (W-1)
		{ 2000, TEXT("WO2") }, // Warrant Officer (W-2)
		{ 2500, TEXT("WO3") }, // Warrant Officer (W-3)
	};
	for (const FBand& Band : Bands)
	{
		if (InElo <= Band.MaxElo)
		{
			return Band.Tag;
		}
	}
	return TEXT("CWO"); // Chief Warrant Officer (2501+; the ladder tops out here).
}
