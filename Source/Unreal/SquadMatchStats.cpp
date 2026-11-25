#include "SquadMatchStats.h"

#include "Soldier.h"
#include "VictoryPoint.h"
#include "WeaponData.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogSquadStats, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarMatchStats(
		TEXT("squad.MatchStats"), 0,
		TEXT("Server: record the match for balancing and write Saved/MatchStats/<time>_seed<seed>.json + _shots.csv at the end (default 0)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarMatchTimeLimit(
		TEXT("squad.MatchTimeLimit"), 0.f,
		TEXT("Server, with squad.MatchStats 1: end the record as a timeout after this many seconds of play (0: no limit)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarQuitAfterMatch(
		TEXT("squad.QuitAfterMatch"), 0,
		TEXT("Server: close the process once the match record is written (batch AI test matches)."),
		ECVF_Default);

	const TCHAR* TeamName(ESquadTeam Team)
	{
		return Team == ESquadTeam::Allies ? TEXT("Allies") : TEXT("Axis");
	}

	int32 TeamIdx(ESquadTeam Team)
	{
		return Team == ESquadTeam::Allies ? 0 : 1;
	}

	const TCHAR* StanceName(ESoldierStance Stance)
	{
		switch (Stance)
		{
		case ESoldierStance::Crouched: return TEXT("crouched");
		case ESoldierStance::Prone:    return TEXT("prone");
		default:                       return TEXT("standing");
		}
	}

	const TCHAR* MethodName(EKillMethod Method)
	{
		switch (Method)
		{
		case EKillMethod::Grenade: return TEXT("grenade");
		case EKillMethod::C4:      return TEXT("c4");
		default:                   return TEXT("rifle");
		}
	}

	FString WeaponName(const ASoldier& Soldier)
	{
		const UWeaponData* W = Soldier.GetWeaponData();
		return W ? W->DisplayName.ToString() : FString(TEXT("?"));
	}
}

USquadMatchStats* USquadMatchStats::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	USquadMatchStats* Stats = World->GetSubsystem<USquadMatchStats>();
	return Stats && Stats->bRecording ? Stats : nullptr;
}

bool USquadMatchStats::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

TStatId USquadMatchStats::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USquadMatchStats, STATGROUP_Tickables);
}

float USquadMatchStats::MatchTime() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() - StartTime : 0.f;
}

FString USquadMatchStats::SoldierId(const ASoldier& Soldier) const
{
	// e.g. "Allies-S2-3": squad 2 (1-based), soldier number 3.
	return FString::Printf(TEXT("%s-S%d-%d"), TeamName(Soldier.Team), Soldier.SquadId + 1, Soldier.GetSquadNumber());
}

USquadMatchStats::FSoldierRecord& USquadMatchStats::Find(const ASoldier& Soldier)
{
	if (FSoldierRecord* Existing = Soldiers.FindByPredicate([&Soldier](const FSoldierRecord& R) { return R.Soldier.Get() == &Soldier; }))
	{
		return *Existing;
	}
	FSoldierRecord& R = Soldiers.AddDefaulted_GetRef();
	R.Soldier = &Soldier;
	R.Id = SoldierId(Soldier);
	R.Team = Soldier.Team;
	R.SquadId = Soldier.SquadId;
	R.Number = Soldier.GetSquadNumber();
	return R;
}

void USquadMatchStats::OnMatchStarted(int32 Seed, int32 SquadsPerTeam)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || CVarMatchStats.GetValueOnGameThread() == 0)
	{
		return;
	}
	bRecording = true;
	bWritten = false;
	StartTime = World->GetTimeSeconds();
	MatchSeed = Seed;
	NumSquadsPerTeam = SquadsPerTeam;
	StartStamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	Soldiers.Reset();
	ShotRows.Reset();
	Events.Reset();
	ShotRows.Add(TEXT("time,shooter,shooter_team,shooter_weapon,shooter_stance,shooter_stress,target,target_stance,target_moving,distance_m,base_chance,stress_mult,cover,cover_reduction,cover_leaning,final_chance,roll,shooter_x,shooter_y,target_x,target_y,shooter_braced,target_injured,exposure_pct,hit_part,hit,outcome"));
	for (TActorIterator<ASoldier> It(World); It; ++It)
	{
		Find(**It);
	}
	UE_LOG(LogSquadStats, Log, TEXT("Recording match: seed %d, %d squad(s) per team, %d soldiers."), Seed, SquadsPerTeam, Soldiers.Num());
}

void USquadMatchStats::Tick(float DeltaTime)
{
	if (!bRecording || bWritten)
	{
		return;
	}
	// Stress tiers, sampled every half second (GDD §8.1).
	SampleAccumulator += DeltaTime;
	if (SampleAccumulator >= 0.5f)
	{
		for (FSoldierRecord& R : Soldiers)
		{
			const ASoldier* S = R.Soldier.Get();
			if (S && !S->IsDead())
			{
				R.TierSeconds[static_cast<int32>(S->GetStressTier())] += SampleAccumulator;
				R.PeakStress = FMath::Max(R.PeakStress, S->GetStress());
			}
		}
		SampleAccumulator = 0.f;
	}
	const float Limit = CVarMatchTimeLimit.GetValueOnGameThread();
	if (Limit > 0.f && MatchTime() >= Limit)
	{
		OnMatchEnded(ESquadTeam::Allies, TEXT("timeout"));
	}
}

void USquadMatchStats::RecordShot(const ASoldier& Shooter, const ASoldier& Target, const FHitChanceInfo& Chance, float Roll, bool bHit, const TCHAR* HitPart,
	const FShotTargetSnapshot* TargetBefore)
{
	FShotTargetSnapshot Now;
	Now.Stance = Target.GetEffectiveStance();
	Now.bMoving = Target.IsMoving();
	Now.bInjured = Target.IsInjured();
	const FShotTargetSnapshot& T = TargetBefore ? *TargetBefore : Now;
	if (bWritten)
	{
		return;
	}
	const UEnum* CoverEnum = StaticEnum<ECoverType>();
	ShotRows.Add(FString::Printf(TEXT("%.2f,%s,%s,%s,%s,%.1f,%s,%s,%d,%.2f,%.3f,%.3f,%s,%.1f,%d,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%.1f,%s,%d,"),
		MatchTime(), *SoldierId(Shooter), TeamName(Shooter.Team), *WeaponName(Shooter), StanceName(Shooter.GetEffectiveStance()),
		Shooter.GetStress(), *SoldierId(Target), StanceName(T.Stance), T.bMoving ? 1 : 0,
		Chance.DistanceMetres, Chance.BaseChance, Chance.StressAccuracyMultiplier,
		CoverEnum ? *CoverEnum->GetNameStringByValue(static_cast<int64>(Chance.Cover)) : TEXT("?"),
		Chance.CoverReduction, Chance.bCoverLeaning ? 1 : 0, Chance.FinalChance, Roll,
		Shooter.GetCurrentCell().X, Shooter.GetCurrentCell().Y, Target.GetCurrentCell().X, Target.GetCurrentCell().Y,
		Chance.bBraced ? 1 : 0, T.bInjured ? 1 : 0, Chance.bBallistic ? Chance.ExposurePercent : -1.f, HitPart, bHit ? 1 : 0));
	PendingOutcomeRow = bHit ? ShotRows.Num() - 1 : INDEX_NONE;
	if (bHit)
	{
		Find(Target).HitsTaken++;
	}
}

void USquadMatchStats::RecordBallisticShot(const ASoldier& Shooter, const ASoldier& Target, const FHitChanceInfo& Chance, bool bHitTarget,
	EHitPart Part, const FString& Outcome, const FShotTargetSnapshot& TargetBefore)
{
	if (bWritten)
	{
		return;
	}
	bBallisticsMatch = true;
	RecordShot(Shooter, Target, Chance, -1.f, bHitTarget, bHitTarget ? GetHitPartName(Part) : TEXT(""), &TargetBefore);
	ShotRows.Last() += Outcome;
	PendingOutcomeRow = INDEX_NONE;
	const int32 T = TeamIdx(Shooter.Team);
	if (bHitTarget)
	{
		HitParts[T][static_cast<int32>(Part)]++;
	}
	else if (Outcome == TEXT("miss_cover"))
	{
		RoundsStoppedByCover[T]++;
	}
}

void USquadMatchStats::RecordStrayHit(const ASoldier& Shooter, const ASoldier& Victim, bool bFriendly, float DistanceMetres, const FString& Effect)
{
	if (bWritten)
	{
		return;
	}
	const int32 T = TeamIdx(Shooter.Team);
	FSoldierRecord& V = Find(Victim);
	V.HitsTaken++;
	if (Effect == TEXT("injured"))
	{
		V.TimesInjured++;
	}
	if (bFriendly)
	{
		FriendlyFireHits[T]++;
		FriendlyFireInjuries[T] += Effect == TEXT("injured") ? 1 : 0;
	}
	else
	{
		StrayEnemyHits[T]++;
	}
	Events.Add(FString::Printf(TEXT("%.1f %s %s hits %s at %.1fm: %s"), MatchTime(), bFriendly ? TEXT("friendly_fire") : TEXT("stray_round"),
		*SoldierId(Shooter), *SoldierId(Victim), DistanceMetres, *Effect));
}

void USquadMatchStats::RecordHitOutcome(const FString& Outcome)
{
	if (ShotRows.IsValidIndex(PendingOutcomeRow))
	{
		ShotRows[PendingOutcomeRow] += Outcome;
	}
	PendingOutcomeRow = INDEX_NONE;
}

void USquadMatchStats::RecordGrenadeThrown(const ASoldier& Thrower, FIntPoint From, FIntPoint Target)
{
	if (bWritten)
	{
		return;
	}
	Find(Thrower).GrenadesThrown++;
	GrenadesThrownTotal[TeamIdx(Thrower.Team)]++;
	Events.Add(FString::Printf(TEXT("%.1f grenade %s throws %.1fm"), MatchTime(), *SoldierId(Thrower),
		FVector2D::Distance(FVector2D(From), FVector2D(Target))));
}

void USquadMatchStats::RecordDeath(const ASoldier& Victim, const FKillCause& Cause)
{
	if (bWritten)
	{
		return;
	}
	FSoldierRecord& V = Find(Victim);
	V.DeathTime = MatchTime();
	V.DeathCause = Cause.bBledOut ? TEXT("bled_out") : MethodName(Cause.Method);
	FString KillerId = TEXT("none");
	if (const ASoldier* Killer = Cause.Killer)
	{
		KillerId = SoldierId(*Killer);
		if (Killer->Team != Victim.Team)
		{
			FSoldierRecord& K = Find(*Killer);
			K.Kills++;
			if (Cause.Method != EKillMethod::Rifle)
			{
				K.GrenadeKills++;
			}
		}
	}
	Events.Add(FString::Printf(TEXT("%.1f death %s by %s (%s, %.1f%%)"), MatchTime(), *V.Id, *KillerId,
		Cause.bBledOut ? TEXT("bled_out") : MethodName(Cause.Method), Cause.ChancePercent));
}

void USquadMatchStats::RecordBlastVictim(const ASoldier* Instigator, EKillMethod Method, const ASoldier& Victim, const TCHAR* Effect)
{
	if (bWritten)
	{
		return;
	}
	const bool bKill = FCString::Strcmp(Effect, TEXT("killed")) == 0;
	if (!bKill)
	{
		Find(Victim).TimesStunned++;
	}
	if (Instigator)
	{
		const int32 T = TeamIdx(Instigator->Team);
		if (Instigator->Team == Victim.Team)
		{
			FriendlyBlastCasualties[T]++;
		}
		else
		{
			(bKill ? BlastKills : BlastStuns)[T]++;
		}
	}
	Events.Add(FString::Printf(TEXT("%.1f blast %s %s %s"), MatchTime(), MethodName(Method), *SoldierId(Victim), Effect));
}

void USquadMatchStats::RecordMedkit(const ASoldier& Medic, const ASoldier& Patient)
{
	if (bWritten)
	{
		return;
	}
	Find(Medic).MedkitsGiven++;
	Events.Add(FString::Printf(TEXT("%.1f medkit %s treats %s"), MatchTime(), *SoldierId(Medic), *SoldierId(Patient)));
}

void USquadMatchStats::RecordVPCapture(const AVictoryPoint& VP, ESquadTeam Team)
{
	if (bWritten)
	{
		return;
	}
	VPCaptures[TeamIdx(Team)]++;
	Events.Add(FString::Printf(TEXT("%.1f capture %s by %s"), MatchTime(), *VP.PointName.ToString(), TeamName(Team)));
}

void USquadMatchStats::OnMatchEnded(ESquadTeam Winner, const FString& Reason)
{
	if (!bRecording || bWritten)
	{
		return;
	}
	bWritten = true;
	WriteFiles(Winner, Reason);
	if (CVarQuitAfterMatch.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogSquadStats, Log, TEXT("squad.QuitAfterMatch: closing."));
		FPlatformMisc::RequestExit(false, TEXT("squad.QuitAfterMatch"));
	}
}

void USquadMatchStats::WriteFiles(ESquadTeam Winner, const FString& Reason)
{
	UWorld* World = GetWorld();
	// Injured flags only go on rows for soldiers recorded as injured; count injuries from the shot outcomes.
	for (const FString& Row : ShotRows)
	{
		if (Row.EndsWith(TEXT(",injured")))
		{
			// Target id is the 7th column.
			TArray<FString> Cols;
			Row.ParseIntoArray(Cols, TEXT(","), false);
			if (Cols.IsValidIndex(6))
			{
				if (FSoldierRecord* R = Soldiers.FindByPredicate([&Cols](const FSoldierRecord& S) { return S.Id == Cols[6]; }))
				{
					R->TimesInjured++;
				}
			}
		}
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("map"), World ? World->GetMapName() : FString());
	Root->SetNumberField(TEXT("seed"), MatchSeed);
	Root->SetBoolField(TEXT("ballistics"), bBallisticsMatch);
	Root->SetNumberField(TEXT("squads_per_team"), NumSquadsPerTeam);
	Root->SetNumberField(TEXT("duration_s"), MatchTime());
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetStringField(TEXT("winner"), Reason == TEXT("draw") || Reason == TEXT("timeout") ? TEXT("none") : TeamName(Winner));

	// VP owners at the end.
	TSharedRef<FJsonObject> VPs = MakeShared<FJsonObject>();
	if (World)
	{
		for (TActorIterator<AVictoryPoint> It(World); It; ++It)
		{
			VPs->SetStringField(It->PointName.ToString(), It->IsOwned() ? TeamName(It->GetOwningTeam()) : TEXT("neutral"));
		}
	}
	Root->SetObjectField(TEXT("vp_owners"), VPs);

	TArray<TSharedPtr<FJsonValue>> TeamArray;
	for (const ESquadTeam Team : { ESquadTeam::Allies, ESquadTeam::Axis })
	{
		const int32 T = TeamIdx(Team);
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("team"), TeamName(Team));
		J->SetNumberField(TEXT("grenades_thrown"), GrenadesThrownTotal[T]);
		J->SetNumberField(TEXT("blast_kills"), BlastKills[T]);
		J->SetNumberField(TEXT("blast_stuns"), BlastStuns[T]);
		J->SetNumberField(TEXT("friendly_blast_casualties"), FriendlyBlastCasualties[T]);
		J->SetNumberField(TEXT("vp_captures"), VPCaptures[T]);
		if (bBallisticsMatch)
		{
			J->SetNumberField(TEXT("friendly_fire_hits"), FriendlyFireHits[T]);
			J->SetNumberField(TEXT("friendly_fire_injuries"), FriendlyFireInjuries[T]);
			J->SetNumberField(TEXT("stray_enemy_hits"), StrayEnemyHits[T]);
			J->SetNumberField(TEXT("rounds_stopped_by_cover"), RoundsStoppedByCover[T]);
			TSharedRef<FJsonObject> Parts = MakeShared<FJsonObject>();
			for (int32 P = 0; P < 6; ++P)
			{
				Parts->SetNumberField(GetHitPartName(static_cast<EHitPart>(P)), HitParts[T][P]);
			}
			J->SetObjectField(TEXT("target_hit_parts"), Parts);
		}
		TeamArray.Add(MakeShared<FJsonValueObject>(J));
	}
	Root->SetArrayField(TEXT("teams"), TeamArray);

	TArray<TSharedPtr<FJsonValue>> SoldierArray;
	for (const FSoldierRecord& R : Soldiers)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("id"), R.Id);
		J->SetStringField(TEXT("team"), TeamName(R.Team));
		J->SetNumberField(TEXT("squad"), R.SquadId + 1);
		J->SetNumberField(TEXT("number"), R.Number);
		const ASoldier* S = R.Soldier.Get();
		J->SetStringField(TEXT("weapon"), S ? WeaponName(*S) : FString());
		J->SetNumberField(TEXT("rounds_fired"), S ? S->GetRoundsFiredStat() : 0);
		J->SetNumberField(TEXT("hits"), S ? S->GetHitsLanded() : 0);
		J->SetNumberField(TEXT("kills"), R.Kills);
		J->SetNumberField(TEXT("grenade_kills"), R.GrenadeKills);
		J->SetNumberField(TEXT("hits_taken"), R.HitsTaken);
		J->SetNumberField(TEXT("times_injured"), R.TimesInjured);
		J->SetNumberField(TEXT("times_stunned"), R.TimesStunned);
		J->SetNumberField(TEXT("grenades_thrown"), R.GrenadesThrown);
		J->SetNumberField(TEXT("medkits_given"), R.MedkitsGiven);
		J->SetNumberField(TEXT("death_time"), R.DeathTime);
		J->SetStringField(TEXT("death_cause"), R.DeathCause);
		J->SetStringField(TEXT("end_state"), !S ? TEXT("gone") : S->IsDead() ? TEXT("dead") : S->IsInjured() ? TEXT("injured") : TEXT("active"));
		if (S)
		{
			const FLootInventory& Inv = S->GetInventory();
			int32 Rounds = Inv.Weapons.Num() > 0 ? Inv.Weapons[0].LoadedRounds + Inv.GetAmmo(Inv.Weapons[0].Weapon) : 0;
			J->SetNumberField(TEXT("rounds_left"), Rounds);
			J->SetNumberField(TEXT("grenades_left"), Inv.Grenades);
			J->SetNumberField(TEXT("medkits_left"), Inv.Medkits);
		}
		J->SetNumberField(TEXT("peak_stress"), R.PeakStress);
		TArray<TSharedPtr<FJsonValue>> Tiers;
		for (float Seconds : R.TierSeconds)
		{
			Tiers.Add(MakeShared<FJsonValueNumber>(Seconds));
		}
		J->SetArrayField(TEXT("stress_tier_seconds"), Tiers);
		SoldierArray.Add(MakeShared<FJsonValueObject>(J));
	}
	Root->SetArrayField(TEXT("soldiers"), SoldierArray);

	TArray<TSharedPtr<FJsonValue>> EventArray;
	for (const FString& E : Events)
	{
		EventArray.Add(MakeShared<FJsonValueString>(E));
	}
	Root->SetArrayField(TEXT("events"), EventArray);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);

	const FString Dir = FPaths::ProjectSavedDir() / TEXT("MatchStats");
	const FString Base = Dir / FString::Printf(TEXT("%s_seed%d"), *StartStamp, MatchSeed);
	const bool bJson = FFileHelper::SaveStringToFile(Json, *(Base + TEXT(".json")));
	const bool bCsv = FFileHelper::SaveStringArrayToFile(ShotRows, *(Base + TEXT("_shots.csv")));
	UE_LOG(LogSquadStats, Log, TEXT("MATCH STATS: %s, %.0f s, %d shots -> %s.json%s"), *Reason, MatchTime(), ShotRows.Num() - 1, *Base,
		bJson && bCsv ? TEXT("") : TEXT(" (WRITE FAILED)"));
}
