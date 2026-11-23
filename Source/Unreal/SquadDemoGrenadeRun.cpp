// Demo (non-shipping): a soldier is ordered to throw a grenade while running, finishes its step, steps back onto the
// cell it came from (pulling the pin) and throws from there (GDD §7 "Throwing"). Server / standalone only.
//
//   squad.DemoGrenadeRun [squad number = 1] [run cells = 6]
//
// Picks a straight, free lane next to that soldier of the local player's squad, sends it running down the lane,
// and orders the throw part-way along, towards a cell further down the lane. The camera follows the soldier and each
// phase is logged (LogSquadDemo) so the cells can be checked.

#include "GridManager.h"
#include "Soldier.h"
#include "SquadPlayerController.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSquadDemo, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
	/** Steps the soldier completes before the throw is ordered; the order lands during the next step. */
	constexpr int32 StepsBeforeThrow = 2;
	/** Longest throw tried, in lane cells beyond the step-back cell (GDD §7: 10 m max). */
	constexpr int32 MaxThrowCells = 8;
	constexpr int32 MinThrowCells = 3;
	/** The order is given this far (cm) into the step after StepsBeforeThrow, so the soldier has a step to finish first. */
	constexpr float MidStepDistance = 40.f;
	constexpr float DemoTimeout = 15.f;
	/** Seconds the camera keeps following after the grenade leaves the hand (to see it land and go off). */
	constexpr float FollowAfterRelease = 2.5f;

	FTSTicker::FDelegateHandle ActiveDemo;

	enum class EPhase : uint8 { Running, Ordered, SteppingBack, WindUp, Released };

	struct FDemoState
	{
		TWeakObjectPtr<ASoldier> Soldier;
		TWeakObjectPtr<ASquadPlayerController> Controller;
		TWeakObjectPtr<const AGridManager> Grid;
		FIntPoint Start = FIntPoint::ZeroValue;
		FIntPoint Dir = FIntPoint::ZeroValue;
		FIntPoint Target = FIntPoint::ZeroValue;
		EPhase Phase = EPhase::Running;
		int32 GrenadesAtOrder = 0;
		float Elapsed = 0.f;
		float SinceRelease = 0.f;
	};

	FString CellStr(FIntPoint C) { return FString::Printf(TEXT("(%d,%d)"), C.X, C.Y); }

	/** Lane cells 1..Length from Start along Dir are all in bounds and free to walk onto. */
	bool IsLaneFree(const AGridManager& Grid, const ASoldier* Soldier, FIntPoint Start, FIntPoint Dir, int32 Length)
	{
		for (int32 K = 1; K <= Length; ++K)
		{
			const FIntPoint C = Start + Dir * K;
			if (!Grid.IsInBounds(C) || !Grid.IsCellFreeAsDestination(C, Soldier))
			{
				return false;
			}
			if (Dir.X != 0 && Dir.Y != 0) // No cutting a blocked corner on a diagonal.
			{
				const FIntPoint Prev = C - Dir;
				if (!Grid.IsCellFreeAsDestination(FIntPoint(C.X, Prev.Y), Soldier) || !Grid.IsCellFreeAsDestination(FIntPoint(Prev.X, C.Y), Soldier))
				{
					return false;
				}
			}
		}
		return true;
	}

	void FollowWithCamera(const FDemoState& S)
	{
		const ASoldier* Soldier = S.Soldier.Get();
		ASquadPlayerController* PC = S.Controller.Get();
		APawn* Cam = PC ? PC->GetPawn() : nullptr;
		if (Soldier && Cam)
		{
			const FVector L = Soldier->GetActorLocation();
			Cam->SetActorLocation(FVector(L.X, L.Y, Cam->GetActorLocation().Z));
		}
	}

	bool TickDemo(float DeltaTime, TSharedRef<FDemoState> S)
	{
		ASoldier* Soldier = S->Soldier.Get();
		const float GameDelta = Soldier && Soldier->GetWorld() ? Soldier->GetWorld()->GetDeltaSeconds() : DeltaTime; // Game time (respects slomo).
		S->Elapsed += GameDelta;
		if (!Soldier || Soldier->IsDead() || S->Elapsed > DemoTimeout)
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: ended early (soldier gone, dead or timed out)."));
			ActiveDemo.Reset();
			return false;
		}
		FollowWithCamera(*S);

		const FIntPoint Cell = Soldier->GetCurrentCell();
		switch (S->Phase)
		{
		case EPhase::Running:
			if (Cell == S->Start + S->Dir * StepsBeforeThrow && Soldier->IsMoving() && S->Grid.IsValid()
				&& FVector::Dist2D(Soldier->GetActorLocation(), S->Grid->CellToWorld(Cell)) >= MidStepDistance)
			{
				// Mid-run, part-way into its next step. Throw down the lane, as far as range and cover allow.
				const FIntPoint StepBackCell = Cell; // The cell it is stepping off now = where it will step back to.
				S->GrenadesAtOrder = Soldier->GetGrenades();
				for (int32 D = MaxThrowCells; D >= MinThrowCells; --D)
				{
					const FIntPoint Target = StepBackCell + S->Dir * D;
					if (Soldier->ThrowGrenadeAt(Target))
					{
						S->Target = Target;
						S->Phase = EPhase::Ordered;
						Soldier->ShowMessage(TEXT("GRENADE!"));
						UE_LOG(LogSquadDemo, Log, TEXT("DemoGrenadeRun: throw ordered while running off %s, target %s (%d cells on)."),
							*CellStr(StepBackCell), *CellStr(Target), D);
						break;
					}
				}
				if (S->Phase != EPhase::Ordered)
				{
					UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: the throw was refused (no grenades, cooldown, out of range or blocked)."));
					ActiveDemo.Reset();
					return false;
				}
			}
			else if (!Soldier->IsMoving())
			{
				UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: the soldier stopped at %s before the throw point."), *CellStr(Cell));
				ActiveDemo.Reset();
				return false;
			}
			break;

		case EPhase::Ordered:
			if (Soldier->IsGrenadeSteppingBack())
			{
				S->Phase = EPhase::SteppingBack;
				UE_LOG(LogSquadDemo, Log, TEXT("DemoGrenadeRun: finished its step onto %s, stepping back (pin out)."), *CellStr(Cell));
			}
			else if (Soldier->GetGrenades() < S->GrenadesAtOrder)
			{
				// (Between the end of its step and the start of the step back there is a frame with neither, so
				// "no step back" is only known for sure once the grenade has gone.)
				S->Phase = EPhase::Released;
				UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: no step back, grenade thrown from where it stood, %s."), *CellStr(Cell));
			}
			else if (!Soldier->IsThrowingGrenade())
			{
				UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: the throw was cancelled."));
				ActiveDemo.Reset();
				return false;
			}
			break;

		case EPhase::SteppingBack:
			if (!Soldier->IsGrenadeSteppingBack())
			{
				S->Phase = EPhase::WindUp;
				UE_LOG(LogSquadDemo, Log, TEXT("DemoGrenadeRun: stepped back onto %s (%s the cell it ran from), winding up."),
					*CellStr(Cell), Cell == S->Start + S->Dir * StepsBeforeThrow ? TEXT("=") : TEXT("NOT"));
			}
			break;

		case EPhase::WindUp:
			if (Soldier->GetGrenades() < S->GrenadesAtOrder)
			{
				S->Phase = EPhase::Released;
				UE_LOG(LogSquadDemo, Log, TEXT("DemoGrenadeRun: grenade thrown from %s at %s after %.2f s."), *CellStr(Cell), *CellStr(S->Target), S->Elapsed);
			}
			else if (!Soldier->IsThrowingGrenade())
			{
				UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: the throw was cancelled on release (re-check failed)."));
				ActiveDemo.Reset();
				return false;
			}
			break;

		case EPhase::Released:
			S->SinceRelease += GameDelta;
			if (S->SinceRelease >= FollowAfterRelease)
			{
				UE_LOG(LogSquadDemo, Log, TEXT("DemoGrenadeRun: done."));
				ActiveDemo.Reset();
				return false;
			}
			break;
		}
		return true;
	}

	void StartDemo(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: run it on the server (standalone or listen server PIE)."));
			return;
		}
		const AGridManager* Grid = AGridManager::Find(World);
		ASquadPlayerController* PC = Cast<ASquadPlayerController>(World->GetFirstPlayerController());
		if (!Grid || !PC)
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: no grid or squad player controller in this world."));
			return;
		}
		const int32 Number = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 4) : 1;
		const int32 RunCells = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), StepsBeforeThrow + 1, 20) : 6;

		ASoldier* Soldier = nullptr;
		for (ASoldier* S : PC->GetSquad())
		{
			if (S && S->GetSquadNumber() == Number)
			{
				Soldier = S;
			}
		}
		if (!Soldier || !Soldier->CanAct() || Soldier->IsMoving() || Soldier->IsThrowingGrenade())
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: soldier %d can't start (missing, downed, moving or already throwing)."), Number);
			return;
		}
		if (Soldier->GetGrenades() <= 0)
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: soldier %d has no grenades left."), Number);
			return;
		}

		// A straight lane long enough to run down and still have room for the grenade to land ahead.
		static const FIntPoint Dirs[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
		const FIntPoint Start = Soldier->GetCurrentCell();
		FIntPoint Dir = FIntPoint::ZeroValue;
		for (const FIntPoint& D : Dirs)
		{
			if (IsLaneFree(*Grid, Soldier, Start, D, FMath::Max(RunCells, StepsBeforeThrow + MinThrowCells + 1)))
			{
				Dir = D;
				break;
			}
		}
		if (Dir == FIntPoint::ZeroValue)
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: no free straight lane of %d cells next to soldier %d at %s; move it into the open."),
				RunCells, Number, *CellStr(Start));
			return;
		}
		if (!Soldier->MoveToCell(Start + Dir * RunCells))
		{
			UE_LOG(LogSquadDemo, Warning, TEXT("DemoGrenadeRun: the move order was refused."));
			return;
		}

		if (ActiveDemo.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(ActiveDemo);
		}
		TSharedRef<FDemoState> State = MakeShared<FDemoState>();
		State->Soldier = Soldier;
		State->Controller = PC;
		State->Grid = Grid;
		State->Start = Start;
		State->Dir = Dir;
		ActiveDemo = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickDemo, State));
		UE_LOG(LogSquadDemo, Log, TEXT("DemoGrenadeRun: soldier %d running from %s towards %s; throw ordered after %d steps."),
			Number, *CellStr(Start), *CellStr(Start + Dir * RunCells), StepsBeforeThrow);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdDemoGrenadeRun(
		TEXT("squad.DemoGrenadeRun"),
		TEXT("squad.DemoGrenadeRun [squad number=1] [run cells=6]: demo of a grenade thrown while running. The soldier runs down a free lane, "
			 "is ordered to throw part-way, finishes its step, steps back onto the cell it came from and throws from there (GDD §7)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartDemo),
		ECVF_Cheat);
}
#endif
