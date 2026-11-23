<#
.SYNOPSIS
  Runs a local Squad Command match the way it runs live: one dedicated server process and N client
  processes joining it over the network (127.0.0.1). Uses the editor binary in -server / -game mode,
  so it works with the installed engine (no packaged Server target needed).

.EXAMPLE
  ./Scripts/LocalMultiplayer.ps1                       # server + 2 clients on Farmstead
  ./Scripts/LocalMultiplayer.ps1 -Clients 1 -Lag       # one client, with emulated lag / loss
  ./Scripts/LocalMultiplayer.ps1 -ServerOnly           # just the server (join with -game 127.0.0.1:17777)
  ./Scripts/LocalMultiplayer.ps1 -ServerArgs "-ini:Engine:[ConsoleVariables]:squad.SquadsPerTeam=4"   # 4v4
  ./Scripts/LocalMultiplayer.ps1 -Stop                 # close every process this script started

  Logs: Saved/Logs/LocalMP_Server.log, LocalMP_Client1.log, ...
#>
param(
    [string]$Map = "/Game/Maps/Farmstead",
    [int]$Clients = 2,
    [int]$Port = 17777,
    [switch]$Lag,          # ~45 ms each way, 1% loss on the clients (PktLag / PktLoss)
    [switch]$ServerOnly,
    [switch]$Stop,
    [string[]]$ServerArgs = @(),   # extra server arguments, e.g. "-ini:Engine:[ConsoleVariables]:squad.SquadsPerTeam=4"
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..\Unreal.uproject")).Path
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$PidFile = Join-Path $PSScriptRoot "..\Saved\LocalMP.pids"

if ($Stop) {
    if (Test-Path $PidFile) {
        foreach ($id in Get-Content $PidFile) {
            try { Stop-Process -Id ([int]$id) -ErrorAction Stop; "Stopped $id" } catch { }
        }
        Remove-Item $PidFile
    } else { "Nothing to stop." }
    return
}

if (-not (Test-Path $Editor)) { throw "UnrealEditor.exe not found at $Editor (pass -EngineRoot)." }
New-Item -ItemType Directory -Force (Split-Path $PidFile) | Out-Null

$started = @()

$server = Start-Process $Editor -PassThru -ArgumentList (@("`"$Project`"", "$Map", "-server", "-port=$Port", "-log", "-unattended", "-nosplash",
    "-abslog=`"$(Join-Path $PSScriptRoot '..\Saved\Logs\LocalMP_Server.log')`"") + $ServerArgs)
$started += $server.Id
"Server  pid $($server.Id)  port $Port  map $Map"

if (-not $ServerOnly) {
    Start-Sleep -Seconds 8   # let the server load the map before clients knock
    for ($i = 1; $i -le $Clients; $i++) {
        $x = 40 + ($i - 1) * 980
        $clientArgs = @("`"$Project`"", "127.0.0.1:$Port", "-game", "-windowed", "-ResX=960", "-ResY=540", "-WinX=$x", "-WinY=60",
            "-log", "-nosplash", "-abslog=`"$(Join-Path $PSScriptRoot "..\Saved\Logs\LocalMP_Client$i.log")`"")
        if ($Lag) { $clientArgs += @("-ExecCmds=`"Net PktLag=45, Net PktLagVariance=15, Net PktLoss=1`"") }
        $client = Start-Process $Editor -PassThru -ArgumentList $clientArgs
        $started += $client.Id
        "Client$i pid $($client.Id)"
    }
}

$started | Set-Content $PidFile
"Stop everything with: ./Scripts/LocalMultiplayer.ps1 -Stop"
