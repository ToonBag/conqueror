<#
.SYNOPSIS
  Runs AI-only test matches for balancing: each match is one dedicated server process with no clients, where
  bots command every squad. The server simulates on a fixed 30 Hz timestep as fast as the CPU allows
  (-benchmark -fps=30), records the match (squad.MatchStats) and quits when it ends (squad.QuitAfterMatch).
  Records go to Saved/MatchStats/<time>_seed<seed>.json + _shots.csv; summarise them with
  Scripts/Tests/analyze_match_stats.py.

  Seeds are fixed per match number (SeedBase + i), so a batch can be re-run exactly after a code change.

.EXAMPLE
  ./Scripts/AIMatchBatch.ps1                          # 10 matches, 4 squads per team, Farmstead
  ./Scripts/AIMatchBatch.ps1 -Matches 20 -Parallel 4  # more, four at a time
  ./Scripts/AIMatchBatch.ps1 -SquadsPerTeam 1         # 1v1 squads
  ./Scripts/AIMatchBatch.ps1 -SwapSides               # Axis start south, Allies north (map-bias test)
  ./Scripts/AIMatchBatch.ps1 -Ballistics              # experimental projectile combat (GDD §16)
#>
param(
    [int]$Matches = 10,
    [int]$SquadsPerTeam = 4,
    [int]$Parallel = 3,
    [int]$SeedBase = 1000,
    [int]$TimeLimit = 2400,      # game seconds before a match is recorded as a timeout
    [switch]$SwapSides,          # each faction starts in the other's deployment zone (map-bias test)
    [switch]$Ballistics,         # squad.Ballistics 1: projectile combat instead of hit-chance rolls (GDD §16)
    [string]$Map = "/Game/Maps/Farmstead",
    [int]$BasePort = 17800,
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..\Unreal.uproject")).Path
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$LogDir = Join-Path $PSScriptRoot "..\Saved\Logs"
if (-not (Test-Path $Editor)) { throw "UnrealEditor.exe not found at $Editor (pass -EngineRoot)." }

function Start-Match([int]$i) {
    $seed = $SeedBase + $i
    $cv = "-ini:Engine:[ConsoleVariables]:"
    $argv = @("`"$Project`"", $Map, "-server", "-port=$($BasePort + $i)", "-nullrhi", "-nosound", "-unattended", "-nosplash",
        "-benchmark", "-fps=30", "-log", "-abslog=`"$(Join-Path $LogDir "AIMatch_$seed.log")`"",
        "${cv}squad.SquadsPerTeam=$SquadsPerTeam", "${cv}squad.MinPlayers=0", "${cv}squad.MatchStats=1",
        "${cv}squad.QuitAfterMatch=1", "${cv}squad.MatchSeed=$seed", "${cv}squad.MatchTimeLimit=$TimeLimit",
        "${cv}squad.SwapSides=$([int][bool]$SwapSides)", "${cv}squad.Ballistics=$([int][bool]$Ballistics)")
    $p = Start-Process $Editor -PassThru -WindowStyle Hidden -ArgumentList $argv
    Write-Host "match $i  seed $seed  pid $($p.Id)"
    return $p
}

$running = @()
$next = 1
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($next -le $Matches -or $running.Count -gt 0) {
    while ($next -le $Matches -and $running.Count -lt $Parallel) {
        $running += Start-Match $next
        $next++
    }
    Start-Sleep -Seconds 2
    $running = @($running | Where-Object { -not $_.HasExited })
}
"All $Matches matches finished in $([int]$sw.Elapsed.TotalMinutes) min. Records: Saved/MatchStats/"
