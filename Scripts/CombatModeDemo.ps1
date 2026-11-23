<#
.SYNOPSIS
  Runs the firing-range comparison of hit-chance rolls and projectile combat (squad.DemoCombat, GDD §16) headless:
  one dedicated server process per part (hit-rate trials, engagements), no clients, bots off, on a fixed 30 Hz
  step as fast as the CPU allows. Each process closes when its part is done.
  Results: Saved/Demo/Combat/DemoCombat_<part>_<time>_{trials,engagements,shots}.csv.
  Summarise (with AI match records) using Scripts/Tests/compare_combat_modes.py.

  Uses the DLLs on disk: build with UBT first.

.EXAMPLE
  ./Scripts/CombatModeDemo.ps1                           # 40 rounds per hit-rate trial, 12 engagements per set-up
  ./Scripts/CombatModeDemo.ps1 -Rounds 100 -Reps 30      # bigger samples
  ./Scripts/CombatModeDemo.ps1 -Parts hitrate            # one part only
  ./Scripts/CombatModeDemo.ps1 -Rounds 60 -Reps 40 -HitRateProcesses 4   # the 2026-11-23 review (240 rounds per cell)
#>
param(
    [int]$Rounds = 40,
    [int]$Reps = 12,
    [int]$HitRateProcesses = 1,  # hit-rate trials in this many processes at once (each runs every trial; results are pooled)
    [string[]]$Parts = @("hitrate", "engage"),
    [string]$Map = "/Game/Maps/Farmstead",
    [int]$BasePort = 17900,
    [int]$Seed = 4242,
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..\Unreal.uproject")).Path
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$LogDir = Join-Path $PSScriptRoot "..\Saved\Logs"
if (-not (Test-Path $Editor)) { throw "UnrealEditor.exe not found at $Editor (pass -EngineRoot)." }

$procs = @()
$i = 0
$runs = foreach ($part in $Parts) { if ($part -eq "hitrate") { 1..$HitRateProcesses | % { "hitrate" } } else { $part } }
foreach ($part in $runs) {
    $cv = "-ini:Engine:[ConsoleVariables]:"
    $argv = @("`"$Project`"", $Map, "-server", "-port=$($BasePort + $i)", "-nullrhi", "-nosound", "-unattended", "-nosplash",
        "-benchmark", "-fps=30", "-log", "-abslog=`"$(Join-Path $LogDir "DemoCombat_${part}_$i.log")`"",
        "${cv}squad.SquadsPerTeam=1", "${cv}squad.MinPlayers=0", "${cv}squad.Bot=0", "${cv}squad.MatchSeed=$($Seed + $i)",
        "-ExecCmds=`"squad.DemoCombat $part $Rounds $Reps quit`"")
    $p = Start-Process $Editor -PassThru -WindowStyle Hidden -ArgumentList $argv
    Write-Host "$part  pid $($p.Id)  log Saved/Logs/DemoCombat_${part}_$i.log"
    $procs += $p
    $i++
}
$sw = [Diagnostics.Stopwatch]::StartNew()
$procs | Wait-Process
"Done in $([int]$sw.Elapsed.TotalMinutes) min. Results: Saved/Demo/Combat/"
