<#
.SYNOPSIS
  Controls the cloud playtest server (see Docs/Playtest.md, "Cloud server").

.EXAMPLE
  ./Scripts/Playtest/CloudServer.ps1                  # status: VM, build, players, recent server events
  ./Scripts/Playtest/CloudServer.ps1 -Start           # start the VM before a session (the server is up ~2-3 min later)
  ./Scripts/Playtest/CloudServer.ps1 -Stop            # stop the VM after a session (stops the running cost)
  ./Scripts/Playtest/CloudServer.ps1 -Log             # the server log's last lines
  ./Scripts/Playtest/CloudServer.ps1 -Players 4 -SquadsPerTeam 2   # change settings (restarts the server, ending a match)
  ./Scripts/Playtest/CloudServer.ps1 -NoBots / -Bots
#>
param(
    [switch]$Start,
    [switch]$Stop,
    [switch]$Log,
    [int]$Players = 0,
    [int]$SquadsPerTeam = 0,
    [switch]$NoBots,
    [switch]$Bots,
    [string]$Config = ""
)

. (Join-Path $PSScriptRoot "CloudCommon.ps1")
$ErrorActionPreference = "Stop"
$Cfg = Read-CloudConfig $Config
$Cloud = Test-IsGcs $Cfg.serverBucket

if ($Start -or $Stop) {
    if (-not $Cloud) { "Test config: no VM to start or stop."; return }
    if ($Start) {
        "Starting $($Cfg.vm)..."
        Invoke-Gcloud compute instances start $Cfg.vm --zone=$Cfg.zone --project=$Cfg.project --quiet | Out-Null
        "Started. Windows boots and the server loads in about 2-3 minutes; check with ./Scripts/Playtest/CloudServer.ps1"
    } else {
        "Stopping $($Cfg.vm)..."
        Invoke-Gcloud compute instances stop $Cfg.vm --zone=$Cfg.zone --project=$Cfg.project --quiet | Out-Null
        "Stopped. You now pay only for the disk and the reserved IP."
    }
    return
}

if ($Players -gt 0 -or $SquadsPerTeam -gt 0 -or $NoBots -or $Bots) {
    $Text = Read-BucketText (Join-BucketPath $Cfg.serverBucket "server.json")
    if (-not $Text) { throw "Nothing deployed yet: run ./Scripts/Playtest/Release.ps1 first." }
    $S = $Text | ConvertFrom-Json
    if ($Players -gt 0) { $S.players = $Players }
    if ($SquadsPerTeam -gt 0) { $S.squadsPerTeam = $SquadsPerTeam }
    if ($NoBots) { $S.noBots = $true }
    if ($Bots) { $S.noBots = $false }
    Write-BucketText ($S | ConvertTo-Json) (Join-BucketPath $Cfg.serverBucket "server.json")
    "Settings saved: starts at $($S.players) player(s), $($S.squadsPerTeam) squad(s) per team, bots $(if ($S.noBots) {'off'} else {'on'})."
    "The server restarts with them within ~15 s (a match in progress ends)."
    return
}

if ($Log) {
    $Text = Read-BucketText (Join-BucketPath $Cfg.serverBucket "status/server.log")
    if (-not $Text) { "No server log yet."; return }
    ($Text -split "`r?`n") | Select-Object -Last 60
    return
}

# Status.
"Server address: $($Cfg.ip):$($Cfg.port)"
if ($Cloud) {
    $VmStatus = (Invoke-Gcloud compute instances describe $Cfg.vm --zone=$Cfg.zone --project=$Cfg.project --format="value(status)").Trim()
    "VM: $VmStatus$(if ($VmStatus -ne 'RUNNING') { '  (start it with -Start)' })"
}
$StatusText = Read-BucketText (Join-BucketPath $Cfg.serverBucket "status/status.json")
$LatestText = Read-BucketText (Join-BucketPath $Cfg.clientBucket "latest.json")
if (-not $StatusText) { "No status from the VM yet (it reports every 30 s once its agent runs)."; return }
$St = $StatusText | ConvertFrom-Json
$Age = [math]::Round(((Get-Date).ToUniversalTime() - [datetime]::Parse($St.time).ToUniversalTime()).TotalSeconds)
"Agent report: $Age s ago$(if ($Age -gt 120) { '  (stale: the VM may be stopped or still booting)' })"
"Server: $(if ($St.running) { 'running' } else { 'NOT running' }), build $($St.build), $($St.players) player(s) connected"
if ($St.settings) { "Settings: starts at $($St.settings.players) player(s), $($St.settings.squadsPerTeam) squad(s) per team, bots $(if ($St.settings.noBots) {'off'} else {'on'})" }
if ($LatestText) {
    $Latest = $LatestText | ConvertFrom-Json
    if ($Latest.build -ne $St.build) { Write-Warning "Friends' latest build is $($Latest.build) but the server runs $($St.build): it is probably still switching over." }
}
if ($St.note) { "Note: $($St.note)" }
if ($St.events) { ""; "Recent server events:"; $St.events | ForEach-Object { "  " + ($_ -replace '^\[[^\]]*\]\[[^\]]*\]', '') } }
