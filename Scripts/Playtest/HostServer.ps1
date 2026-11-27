<#
.SYNOPSIS
  Hosts a Squad Command playtest server on this PC for friends running a packaged client
  (Scripts/Playtest/PackageClient.ps1), and reports players joining and leaving.

  The server is the editor binary in -server mode (the installed engine can't build a packaged server),
  so it runs today's code from Binaries/: build first if you changed C++ since packaging.

.EXAMPLE
  ./Scripts/Playtest/HostServer.ps1                    # 1v1: one squad per team, bots fill empty squads
  ./Scripts/Playtest/HostServer.ps1 -Players 3         # wait for 3 players before the match starts
  ./Scripts/Playtest/HostServer.ps1 -SquadsPerTeam 2   # 2v2
  ./Scripts/Playtest/HostServer.ps1 -NoBots            # empty squads stand still instead of a bot playing them
  ./Scripts/Playtest/HostServer.ps1 -Stop              # stop the server

  The server only lets in clients of the newest packaged build (Playtest/last-build.json, see
  PackageClient.ps1); -BuildId picks another, -NoBuildCheck lets any build in.

  Log: Saved/Logs/Playtest_Server.log. Ctrl+C stops watching; the server keeps running until -Stop.
#>
param(
    [int]$Players = 2,           # the match (and the bots) start once this many players have joined
    [int]$SquadsPerTeam = 1,
    [switch]$NoBots,
    [int]$Port = 17777,
    [string]$Map = "/Game/Maps/Farmstead",
    [switch]$Stop,
    [string]$BuildId = "",       # default: the newest packaged client's build
    [switch]$NoBuildCheck,
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Project = Join-Path $Root "Unreal.uproject"
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$PidFile = Join-Path $Root "Saved\Playtest.pid"
$Log = Join-Path $Root "Saved\Logs\Playtest_Server.log"

if ($Stop) {
    if (Test-Path $PidFile) {
        try { Stop-Process -Id ([int](Get-Content $PidFile)) -ErrorAction Stop; "Server stopped." } catch { "Server was not running." }
        Remove-Item $PidFile
    } else { "No playtest server to stop." }
    return
}
if ((Test-Path $PidFile) -and (Get-Process -Id ([int](Get-Content $PidFile)) -ErrorAction SilentlyContinue)) {
    throw "A playtest server is already running (pid $(Get-Content $PidFile)). Stop it with -Stop first."
}
if (-not (Test-Path $Editor)) { throw "UnrealEditor.exe not found at $Editor (pass -EngineRoot)." }

# The server runs the C++ in Binaries/. Warn if the source is newer: friends' clients and this server must match.
$Dll = Join-Path $Root "Binaries\Win64\UnrealEditor-Unreal.dll"
$Newer = Get-ChildItem (Join-Path $Root "Source") -Recurse -File | Where-Object { $_.LastWriteTime -gt (Get-Item $Dll).LastWriteTime } | Select-Object -First 1
if ($Newer) { Write-Warning "Source changed since the last build ($($Newer.Name)). Build the editor target (and repackage the client) so everyone runs the same code." }

Push-Location $Root
try { $Commit = (git rev-parse --short HEAD).Trim() } finally { Pop-Location }
$Latest = Get-ChildItem (Join-Path $Root "Playtest") -Filter "SquadCommand-Playtest-*.zip" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
if ($Latest -and $Latest.Name -notmatch [regex]::Escape($Commit)) {
    Write-Warning "Latest packaged client ($($Latest.Name)) was built from a different commit than this server ($Commit). Repackage if the code changed."
}

# Inbound UDP must reach the server. Report, don't change: firewall rules are yours to add.
$Rule = Get-NetFirewallApplicationFilter -Program $Editor -ErrorAction SilentlyContinue | Get-NetFirewallRule -ErrorAction SilentlyContinue |
    Where-Object { $_.Enabled -eq "True" -and $_.Direction -eq "Inbound" -and $_.Action -eq "Allow" }
if (-not $Rule) {
    Write-Warning "No firewall rule lets UnrealEditor.exe receive connections. Add one from an admin PowerShell (Tailscale addresses only, any network profile):"
    "  New-NetFirewallRule -DisplayName 'Squad Command playtest (Tailscale)' -Direction Inbound -Program '$Editor' -Protocol UDP -LocalPort $Port -RemoteAddress 100.64.0.0/10 -Action Allow"
    "  (Port forwarding instead? Use -RemoteAddress Any. If Windows pops up its own prompt, tick both Private and Public.)"
}

$LastBuild = Join-Path $Root "Playtest\last-build.json"
if (-not $BuildId -and -not $NoBuildCheck -and (Test-Path $LastBuild)) { $BuildId = (Get-Content $LastBuild -Raw | ConvertFrom-Json).build }
if ($NoBuildCheck) { $BuildId = "" }

$ServerArgs = @("`"$Project`"", $Map, "-server", "-port=$Port", "-log", "-unattended", "-nosplash", "-abslog=`"$Log`"",
    "-ini:Engine:[ConsoleVariables]:squad.SquadsPerTeam=$SquadsPerTeam", "-ini:Engine:[ConsoleVariables]:squad.MinPlayers=$Players")
if ($NoBots) { $ServerArgs += "-ini:Engine:[ConsoleVariables]:squad.Bot=0" }
if ($BuildId) { $ServerArgs += "-ini:Engine:[ConsoleVariables]:squad.BuildId=$BuildId" }
if (Test-Path $Log) { Remove-Item $Log }
$Server = Start-Process $Editor -PassThru -WindowStyle Minimized -ArgumentList $ServerArgs
$Server.Id | Set-Content $PidFile

""
"Squad Command playtest server: pid $($Server.Id), port UDP $Port, map $Map, $SquadsPerTeam squad(s) per team, starts when $Players player(s) have joined, commit $Commit"
if ($BuildId) { "Lets in clients of build $BuildId only (-NoBuildCheck: any build)." } else { "Lets in clients of any build." }
"Give friends ONE of these addresses (they run Join.bat and type it, or you bake it in with PackageClient.ps1 -HostAddress):"
$Tailscale = Get-Command tailscale -ErrorAction SilentlyContinue
if ($Tailscale) {
    $Ts = (& tailscale ip -4 2>$null | Select-Object -First 1)
    if ($Ts) { "  Tailscale:  $Ts   (friends on your tailnet)" }
}
Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" -and $_.PrefixOrigin -ne "WellKnown" } |
    ForEach-Object { "  $($_.InterfaceAlias): $($_.IPAddress)" }
"  Internet (port forwarding UDP $Port): your public IP, e.g. from your router's status page"
"  Yourself: run Join.bat with 127.0.0.1"
""
"Watching for players (Ctrl+C stops watching, not the server; stop it with -Stop)..."

# Wait for the log, then follow it.
$Deadline = (Get-Date).AddSeconds(60)
while (-not (Test-Path $Log)) {
    if ((Get-Date) -gt $Deadline -or $Server.HasExited) { throw "Server did not start; see $Log." }
    Start-Sleep -Milliseconds 500
}
try {
Get-Content $Log -Wait -ErrorAction Stop | ForEach-Object {
    switch -Regex ($_) {
        "Bringing World .* up for play|Match starts"     { "[server] $($_ -replace '^\[[^\]]*\]\[[^\]]*\]', '')" }
        "Join succeeded: (.*)"                            { "[join]   $($Matches[1])" }
        "LogSquadMatch: (.* joins the .*)"                { "[team]   $($Matches[1])" }
        "LogSquadMatch: (Restarting the map.*)"           { "[server] $($Matches[1])"; break }
        "LogSquadMatch: (.*hands its squad.*|.*has left.*|Every .* player has left.*)" { "[bots]   $($Matches[1])" }
        "LogSquadMatch: (Player left: .*)"                { "[left]   $($Matches[1])" }
        "LogSquadMatch: (Refused .*)"                     { "[refused] $($Matches[1])" }
        "MATCH OVER: (.*)"                                { "[result] $($Matches[1])" }
        "Critical error|Fatal error|Assertion failed"     { Write-Warning "SERVER ERROR: $_" }
    }
}
} catch {
    "Stopped watching: the server log is gone (server stopped?)."
}
