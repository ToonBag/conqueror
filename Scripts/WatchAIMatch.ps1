<#
.SYNOPSIS
  Watch a bot-only match (default 4v4) through a game client, the way it really runs: a local dedicated server
  where bots command every squad (squad.BotsKeepSquads), plus one client that joins as a viewer and sees every
  soldier (squad.RevealAll). GDD §11.2.

  It plays the code on disk, so it builds the editor target first (UBT does nothing when the build is current).
  The build is skipped, with a warning, while the Unreal Editor is open: UBT can't build under Live Coding, and
  the match then plays the last build. Any match this script (or LocalMultiplayer.ps1) started before is closed
  first, since its processes hold the DLLs.

.EXAMPLE
  ./Scripts/WatchAIMatch.ps1                         # 4v4 on Farmstead
  ./Scripts/WatchAIMatch.ps1 -SquadsPerTeam 2        # 2v2
  ./Scripts/WatchAIMatch.ps1 -Rolls                  # the older hit-chance rolls (projectile combat is the default, GDD §16)
  ./Scripts/WatchAIMatch.ps1 -Seed 1234 -NoReveal    # fixed seed; see only what the Allies see
  ./Scripts/WatchAIMatch.ps1 -Stop                   # close the match

  The match is recorded to Saved/MatchStats/ (squad.MatchStats). Logs: Saved/Logs/LocalMP_Server.log, LocalMP_Client1.log.
#>
param(
    [int]$SquadsPerTeam = 4,
    [string]$Map = "/Game/Maps/Farmstead",
    [int]$Seed = 0,              # squad.MatchSeed (0: random)
    [switch]$Rolls,              # squad.Ballistics 0: the older hit-chance rolls instead of projectile combat (GDD §5.3, §16)
    [switch]$NoReveal,           # fog of war as the Allies see it, instead of every soldier
    [switch]$NoBuild,
    [switch]$Lag,                # emulated lag / loss on the client
    [switch]$Stop,
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Project = (Join-Path $Root "Unreal.uproject")
$LocalMP = Join-Path $PSScriptRoot "LocalMultiplayer.ps1"
$PidFile = Join-Path $Root "Saved\LocalMP.pids"

# Close a match started earlier (it also holds the DLLs the build would replace).
if (Test-Path $PidFile) { & $LocalMP -Stop | Out-Null; Start-Sleep -Seconds 1 }
if ($Stop) { "Match closed."; return }

if (-not $NoBuild) {
    $editorOpen = Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'" |
        Where-Object { $_.CommandLine -notmatch ' -(game|server)\b' }
    if ($editorOpen) {
        Write-Warning "The Unreal Editor is open, so the build is skipped and the match plays the last UBT build. Close the editor for an up-to-date build."
    } else {
        "Building the editor target (UBT; quick when nothing changed)..."
        $build = & (Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat") UnrealEditor Win64 Development "-Project=$Project" -WaitMutex 2>&1
        if ($LASTEXITCODE -ne 0) {
            $build | Select-String -Pattern "error" | Select-Object -First 15 | ForEach-Object { $_.Line }
            throw "Build failed (exit $LASTEXITCODE); not starting the match."
        }
        "Build up to date."
    }
}

$cv = "-ini:Engine:[ConsoleVariables]:"
$serverArgs = @("${cv}squad.SquadsPerTeam=$SquadsPerTeam", "${cv}squad.BotsKeepSquads=1", "${cv}squad.MinPlayers=1",
    "${cv}squad.MatchStats=1", "${cv}squad.MatchSeed=$Seed", "${cv}squad.Ballistics=$([int](-not $Rolls))")
$clientArgs = @("${cv}squad.RevealAll=$([int](-not $NoReveal))")
$mpArgs = @{ Clients = 1; Map = $Map; ServerArgs = $serverArgs; ExtraClientArgs = $clientArgs; EngineRoot = $EngineRoot }
if ($Lag) { $mpArgs.Lag = $true }
& $LocalMP @mpArgs

# Wait for the join (a client that fails to connect falls back to a single-player map: check the server log).
$serverLog = Join-Path $Root "Saved\Logs\LocalMP_Server.log"
$joined = $false
for ($i = 0; $i -lt 90 -and -not $joined; $i++) {
    Start-Sleep -Seconds 1
    $joined = (Test-Path $serverLog) -and (Select-String -Path $serverLog -Pattern "Join succeeded" -Quiet)
}
if (-not $joined) { Write-Warning "The client hasn't joined after 90 s; check $serverLog."; return }

# Maximise the client window.
Add-Type -Namespace WatchAIMatch -Name Win -MemberDefinition @"
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
"@
$client = Get-Process -Id ([int](Get-Content $PidFile)[-1]) -ErrorAction SilentlyContinue
if ($client -and $client.MainWindowHandle -ne 0) {
    [WatchAIMatch.Win]::ShowWindow($client.MainWindowHandle, 3) | Out-Null   # SW_MAXIMIZE
    [WatchAIMatch.Win]::SetForegroundWindow($client.MainWindowHandle) | Out-Null
}
"Watching: $SquadsPerTeam v $SquadsPerTeam bots on $Map$(if ($Rolls) { ' (hit-chance rolls)' }). Close with: ./Scripts/WatchAIMatch.ps1 -Stop"
