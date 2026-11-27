<#
.SYNOPSIS
  Runs on the cloud playtest VM (started at boot by VmStartup.ps1). Keeps the Squad Command dedicated server
  running the build that Release.ps1 last deployed.

  Every few seconds it reads server.json from the server bucket. When the engine or build in it changes, it
  stops the server, downloads the new files and starts it again; when the settings change it restarts; when
  the server process has exited (crash) it starts it again. It writes status/status.json and
  status/server.log back to the bucket for CloudServer.ps1 -Status.

  Testing on this PC: -Bucket <local folder> -Root <scratch folder> -SkipPrereqs (see Docs/Playtest.md).
#>
param(
    [string]$Bucket = "",                 # gs://name, or a local folder for tests. Default: the VM's 'squad-bucket' metadata.
    [string]$Root = "C:\SquadServer",
    [int]$PollSeconds = 15,
    [switch]$SkipPrereqs,
    [switch]$Once                         # one pass then exit (tests)
)

# On the VM CloudCommon.ps1 sits next to this script; in the repo it is one folder up.
$Common = Join-Path $PSScriptRoot "CloudCommon.ps1"
if (-not (Test-Path $Common)) { $Common = Join-Path $PSScriptRoot "..\CloudCommon.ps1" }
. $Common
$ErrorActionPreference = "Stop"

if (-not $Bucket) {
    $Bucket = Invoke-RestMethod -Headers @{ "Metadata-Flavor" = "Google" } `
        -Uri "http://metadata.google.internal/computeMetadata/v1/instance/attributes/squad-bucket"
}
$UE = Join-Path $Root "UE"
$Proj = Join-Path $Root "Project"
$Logs = Join-Path $Root "Logs"
$StateFile = Join-Path $Root "state.json"
$AgentLog = Join-Path $Logs "agent.log"
$ServerLog = Join-Path $Logs "Server.log"
New-Item -ItemType Directory -Force $Root, $Logs | Out-Null

function Write-AgentLog([string]$Message) {
    $line = "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] $Message"
    Add-Content -Path $AgentLog -Value $line
    Write-Host $line
}

function Read-State {
    if (Test-Path $StateFile) { return (Get-Content $StateFile -Raw | ConvertFrom-Json) }
    [pscustomobject]@{ engine = ""; build = ""; config = ""; pid = 0 }
}
function Save-State($s) { $s | ConvertTo-Json | Set-Content $StateFile -Encoding UTF8 }

function Get-ServerProcess($s) {
    if (-not $s.pid) { return $null }
    $p = Get-Process -Id $s.pid -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq "UnrealEditor") { return $p }
    $null
}

function Stop-Server($s) {
    $p = Get-ServerProcess $s
    if ($p) {
        Write-AgentLog "Stopping the server (pid $($p.Id))."
        Stop-Process -Id $p.Id -Force
        $p.WaitForExit(30000) | Out-Null
    }
    $s.pid = 0
}

function Start-Server($s, $cfg) {
    $exe = Join-Path $UE "Engine\Binaries\Win64\UnrealEditor.exe"
    $uproject = Join-Path $Proj "Unreal.uproject"
    if (Test-Path $ServerLog) { Move-Item $ServerLog (Join-Path $Logs "Server-previous.log") -Force }
    $serverArgs = @("`"$uproject`"", $cfg.map, "-server", "-port=$($cfg.port)", "-log", "-unattended", "-nosplash",
        "-abslog=`"$ServerLog`"",
        "-ini:Engine:[ConsoleVariables]:squad.BuildId=$($cfg.build)",
        "-ini:Engine:[ConsoleVariables]:squad.MinPlayers=$($cfg.players)",
        "-ini:Engine:[ConsoleVariables]:squad.SquadsPerTeam=$($cfg.squadsPerTeam)",
        "-ini:Engine:[ConsoleVariables]:squad.RestartAfterMatch=$($cfg.restartAfterMatch)")
    if ($cfg.noBots) { $serverArgs += "-ini:Engine:[ConsoleVariables]:squad.Bot=0" }
    $p = Start-Process $exe -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $s.pid = $p.Id
    Write-AgentLog "Started the server: build $($cfg.build), $($cfg.players) player(s) to start, $($cfg.squadsPerTeam) squad(s) per team, pid $($p.Id)."
}

function Install-Prereqs {
    $marker = Join-Path $Root "prereqs.done"
    if ($SkipPrereqs -or (Test-Path $marker)) { return }
    $setup = Join-Path $UE "Engine\Extras\Redist\en-us\UEPrereqSetup_x64.exe"
    if (Test-Path $setup) {
        Write-AgentLog "Installing the Unreal prerequisites (Visual C++ runtime)."
        Start-Process $setup -ArgumentList "/quiet", "/norestart" -Wait
    }
    Set-Content $marker "done"
}

function Open-Firewall($port) {
    # Only on the VM (Windows firewall there; the cloud firewall rule is made by CloudSetup.ps1).
    if (-not (Test-IsGcs $Bucket)) { return }
    if (-not (Get-NetFirewallRule -DisplayName "Squad Command playtest" -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName "Squad Command playtest" -Direction Inbound -Protocol UDP -LocalPort $port -Action Allow | Out-Null
        Write-AgentLog "Opened UDP $port in the Windows firewall."
    }
}

function Publish-Status($s, $cfg, [string]$note) {
    $running = [bool](Get-ServerProcess $s)
    $players = 0; $events = @()
    if (Test-Path $ServerLog) {
        $lines = Get-Content $ServerLog -Tail 4000 -ErrorAction SilentlyContinue
        $events = @($lines | Select-String -Pattern "player\(s\) connected|Match starts|MATCH OVER|Restarting the map|Refused|Bringing World|Fatal error|Assertion failed" |
            Select-Object -Last 25 | ForEach-Object { $_.Line })
        $last = $lines | Select-String -Pattern "(\d+) player\(s\) connected|Restarting the map|Bringing World" | Select-Object -Last 1
        if ($last -and $last.Matches[0].Groups[1].Success) { $players = [int]$last.Matches[0].Groups[1].Value }
        Write-BucketFile $ServerLog (Join-BucketPath $Bucket "status/server.log")
    }
    $status = [ordered]@{
        time = (Get-Date).ToUniversalTime().ToString("o"); running = $running; build = $s.build; engine = $s.engine
        players = $players; note = $note; settings = $cfg; events = $events
    }
    Write-BucketText ($status | ConvertTo-Json -Depth 4) (Join-BucketPath $Bucket "status/status.json")
}

Write-AgentLog "Agent started (bucket $Bucket, root $Root)."
$state = Read-State
$lastStatus = [datetime]::MinValue
$failures = 0
while ($true) {
    $note = ""
    $cfg = $null
    try {
        $cfgText = Read-BucketText (Join-BucketPath $Bucket "server.json")
        if (-not $cfgText) {
            $note = "Waiting for the first deploy (Release.ps1)."
        } else {
            $cfg = $cfgText | ConvertFrom-Json
            Open-Firewall $cfg.port
            if ($cfg.engine -ne $state.engine) {
                Stop-Server $state
                Write-AgentLog "Downloading the engine ($($cfg.engine)); the first time takes a while."
                Sync-Folder (Join-BucketPath $Bucket "server/engine") $UE
                Install-Prereqs
                $state.engine = $cfg.engine; $state.config = ""; Save-State $state
            }
            if ($cfg.build -ne $state.build) {
                Stop-Server $state
                Write-AgentLog "Downloading build $($cfg.build)."
                Sync-Folder (Join-BucketPath $Bucket "server/project") $Proj
                $state.build = $cfg.build; $state.config = ""; Save-State $state
            }
            $proc = Get-ServerProcess $state
            if (-not $proc -or $state.config -ne $cfgText) {
                if ($proc) { Write-AgentLog "Settings changed: restarting the server." }
                elseif ($state.config) { Write-AgentLog "The server is not running (exited or crashed): starting it." }
                Stop-Server $state
                Start-Server $state $cfg
                $state.config = $cfgText; Save-State $state
            }
        }
        $failures = 0
    } catch {
        $failures++
        $note = "Error: $($_.Exception.Message)"
        Write-AgentLog $note
    }
    if (((Get-Date) - $lastStatus).TotalSeconds -ge 30 -or $note -or $Once) {
        try { Publish-Status $state $cfg $note } catch { Write-AgentLog "Could not publish the status: $($_.Exception.Message)" }
        $lastStatus = Get-Date
    }
    if ($Once) { break }
    Start-Sleep -Seconds ([math]::Min(120, $PollSeconds * [math]::Max(1, $failures)))
}
