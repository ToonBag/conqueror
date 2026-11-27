<#
.SYNOPSIS
  Publishes a playtest build to the cloud server in one go (see Docs/Playtest.md, "Cloud server"):
    1. builds the editor target (the server runs it) and packages the client, both stamped with one build ID,
    2. uploads the server's files (only what changed; the engine only the first time or after an engine update)
       and tells the VM to switch to the new build (it restarts its server within ~15 s, ending any match),
    3. uploads the client zip and latest.json, so friends' Join.bat downloads the new build before joining.

  Needs Scripts/Playtest/cloud.json from CloudSetup.ps1, and the editor closed (Live Coding blocks the build).

.EXAMPLE
  ./Scripts/Playtest/Release.ps1
  ./Scripts/Playtest/Release.ps1 -Players 4 -SquadsPerTeam 2     # also change the server settings (kept for later releases)
#>
param(
    [int]$Players = 0,            # 0: keep the server's setting (2 the first time)
    [int]$SquadsPerTeam = 0,      # 0: keep (1 the first time)
    [switch]$SkipBuild,           # the editor target is already built
    [switch]$ForceEngine,         # upload the engine again even if the server has this version
    [string]$Config = "",         # another cloud.json (tests)
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

. (Join-Path $PSScriptRoot "CloudCommon.ps1")
$ErrorActionPreference = "Stop"
$Root = Get-PlaytestRoot
$Cfg = Read-CloudConfig $Config
$Started = Get-Date

$BuildId = Get-BuildId $Root
if ($BuildId -like "*-dirty") { Write-Warning "Source, Content or Config have uncommitted changes; the build ID ends in -dirty. Commit first if you want builds you can trace back." }
"Releasing build $BuildId to $($Cfg.ip):$($Cfg.port)"

# 1. Server code, then the client.
if (-not $SkipBuild) {
    "Building the editor target (the server)..."
    & (Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat") UnrealEditor Win64 Development "-Project=$(Join-Path $Root 'Unreal.uproject')" -WaitMutex
    if ($LASTEXITCODE -ne 0) { throw "The editor build failed (exit $LASTEXITCODE). Close the editor (Live Coding blocks the build) and try again." }
}
$UpdateUrl = "$($Cfg.clientBaseUrl)/latest.json"
& (Join-Path $PSScriptRoot "PackageClient.ps1") -HostAddress $Cfg.ip -Port $Cfg.port -BuildId $BuildId -UpdateUrl $UpdateUrl -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) { throw "Packaging the client failed." }
$Last = Get-Content (Join-Path $Root "Playtest\last-build.json") -Raw | ConvertFrom-Json
if ($Last.build -ne $BuildId) { throw "Packaging did not produce build $BuildId." }

# 2. Server files.
$Stage = Join-Path $Root "Saved\CloudStage"
$EngineMarker = Get-EngineMarker $EngineRoot
$RemoteMarker = Read-BucketText (Join-BucketPath $Cfg.serverBucket "server/engine.marker")
if ($ForceEngine -or ($RemoteMarker -as [string]).Trim() -ne $EngineMarker) {
    "Uploading the engine $EngineMarker for the server (~15 GB; only needed once per engine version)..."
    Copy-EngineForServer $EngineRoot (Join-Path $Stage "engine")
    Sync-Folder (Join-Path $Stage "engine") (Join-BucketPath $Cfg.serverBucket "server/engine")
    Write-BucketText $EngineMarker (Join-BucketPath $Cfg.serverBucket "server/engine.marker")
}
"Uploading the server's project files..."
Copy-ProjectForServer $Root (Join-Path $Stage "project")
Sync-Folder (Join-Path $Stage "project") (Join-BucketPath $Cfg.serverBucket "server/project")
foreach ($f in (Join-Path $PSScriptRoot "Cloud\VmAgent.ps1"), (Join-Path $PSScriptRoot "CloudCommon.ps1")) {
    Write-BucketFile $f (Join-BucketPath $Cfg.serverBucket "agent/$(Split-Path $f -Leaf)")
}

# 3. Client zip.
"Uploading the client ($($Last.sizeMB) MB)..."
$ZipName = Split-Path $Last.zip -Leaf
Write-BucketFile $Last.zip (Join-BucketPath $Cfg.clientBucket "builds/$ZipName") -Public

# Switch the server and the clients over together: server.json (the VM picks it up), then latest.json.
$Old = Read-BucketText (Join-BucketPath $Cfg.serverBucket "server.json")
$Settings = if ($Old) { $Old | ConvertFrom-Json } else { [pscustomobject]@{ players = 2; squadsPerTeam = 1; noBots = $false; restartAfterMatch = 30 } }
if ($Players -gt 0) { $Settings.players = $Players }
if ($SquadsPerTeam -gt 0) { $Settings.squadsPerTeam = $SquadsPerTeam }
$ServerCfg = [ordered]@{
    build = $BuildId; engine = $EngineMarker; map = "/Game/Maps/Farmstead"; port = $Cfg.port
    players = $Settings.players; squadsPerTeam = $Settings.squadsPerTeam; noBots = [bool]$Settings.noBots
    restartAfterMatch = $Settings.restartAfterMatch
}
Write-BucketText ($ServerCfg | ConvertTo-Json) (Join-BucketPath $Cfg.serverBucket "server.json")
$Latest = [ordered]@{ build = $BuildId; url = "$($Cfg.clientBaseUrl)/builds/$ZipName"; sizeMB = $Last.sizeMB; host = $Cfg.ip; port = $Cfg.port }
Write-BucketText ($Latest | ConvertTo-Json) (Join-BucketPath $Cfg.clientBucket "latest.json") -Public -NoCache

$Minutes = [math]::Round(((Get-Date) - $Started).TotalMinutes, 1)
""
"Released $BuildId in $Minutes min."
"The server switches to it within a minute (after an engine upload, the VM first downloads ~15 GB: allow 10-20 min)."
"Friends: run Join.bat; it updates itself. New friends: send them $($Last.zip) or $($Latest.url)"
if (Test-IsGcs $Cfg.serverBucket) {
    $VmStatus = (Invoke-Gcloud compute instances describe $Cfg.vm --zone=$Cfg.zone --format="value(status)" --project=$Cfg.project).Trim()
    if ($VmStatus -ne "RUNNING") { Write-Warning "The VM is $VmStatus. Start it with ./Scripts/Playtest/CloudServer.ps1 -Start" }
}
"Check it with ./Scripts/Playtest/CloudServer.ps1"
