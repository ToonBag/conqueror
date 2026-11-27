<#
.SYNOPSIS
  Packages the Squad Command game client for friends to playtest with, and zips it with Join.bat.

  The client is a cooked Win64 Development build of the Game target (the installed engine can build it).
  The server is not in the zip: Release.ps1 deploys it to the cloud VM, or you host it from this PC with
  HostServer.ps1. Release.ps1 calls this script; run it yourself only for home hosting.

.EXAMPLE
  ./Scripts/Playtest/PackageClient.ps1 -HostAddress 100.101.102.103   # home hosting: bake your address into the zip
  ./Scripts/Playtest/PackageClient.ps1                                  # Join.bat asks each friend for the address

  Output: Playtest/SquadCommand-Playtest-<build>.zip (and the unzipped folder next to it), where <build> is
  <date>-<time>-<commit>. Join.bat sends the build to the server, which refuses other builds once it is told
  its own (squad.BuildId; the cloud server always is). Playtest/last-build.json describes the newest package.
  A first package cooks every shader and takes a while; later ones reuse the cook.
#>
param(
    [string]$HostAddress = "",
    [int]$Port = 17777,
    [string]$BuildId = "",     # default: <date>-<time>-<commit>[-dirty]
    [string]$UpdateUrl = "",   # cloud: URL of latest.json; Join.bat then updates the game itself before joining
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

. (Join-Path $PSScriptRoot "CloudCommon.ps1")
$ErrorActionPreference = "Stop"
$Root = Get-PlaytestRoot
$Project = Join-Path $Root "Unreal.uproject"
$RunUAT = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path $RunUAT)) { throw "RunUAT.bat not found at $RunUAT (pass -EngineRoot)." }

# Name the build after the commit so host, server and friends can check they run the same one.
if (-not $BuildId) { $BuildId = Get-BuildId $Root }
$Name = "SquadCommand-Playtest-$BuildId"
$OutDir = Join-Path $Root "Playtest"
$ArchiveDir = Join-Path $OutDir $Name
New-Item -ItemType Directory -Force $OutDir | Out-Null

"Packaging $Name (this cooks content; the first run takes a while)..."
& $RunUAT BuildCookRun "-project=$Project" -noP4 -platform=Win64 -clientconfig=Development `
    -build -cook -stage -pak -archive "-archivedirectory=$ArchiveDir" `
    -map=/Game/Maps/Farmstead -nocompileeditor -unattended -utf8output
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed (exit $LASTEXITCODE). See the log above." }

# BuildCookRun archives into <dir>\Windows.
$GameDir = Join-Path $ArchiveDir "Windows"
if (-not (Test-Path (Join-Path $GameDir "Unreal.exe"))) { throw "Packaged Unreal.exe not found under $GameDir." }

# Join.bat only starts Play.ps1 (one line, so replacing the folder's files while it runs is safe). Play.ps1 reads
# Playtest.json, updates the game if the host has published a newer build, then joins with ?Build=<id>.
Set-Content -Path (Join-Path $GameDir "Join.bat") -Value "@powershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0Play.ps1`"" -Encoding ASCII
Copy-Item (Join-Path $PSScriptRoot "Client\Play.ps1") $GameDir -Force
$PlaytestCfg = [ordered]@{ build = $BuildId; host = $HostAddress; port = $Port; updateUrl = $UpdateUrl }
Set-Content -Path (Join-Path $GameDir "Playtest.json") -Value ($PlaytestCfg | ConvertTo-Json) -Encoding ASCII

$Readme = @"
SQUAD COMMAND - PLAYTEST BUILD $BuildId

1. Unzip anywhere and run Join.bat. It connects to the host's server on port $Port.
   (No host address baked in? It asks for the one the host gave you.)
   When the host has published a newer build, Join.bat downloads it first and then starts the game.
   If Windows says it protected your PC, click "More info" and then "Run anyway".
2. If you see a map but nobody else, the connection failed and you are in an offline copy. A red box
   says why if the server refused you. Close it, check with the host, and try again.
3. Controls (you command one squad of 4):
   - 1-4 or left click a soldier: select.  Left click ground: move.
   - Shift + left click ground: face that way.  Shift + left click an enemy: attack it.
   - Alt + left click: throw a grenade.  Ctrl + left click a downed ally: drag / release.
   - Right click an injured ally: use a medkit.  C: stance.  H: hold fire.  Tab: loot.
   - Middle mouse: hold for the command wheel (build C4 / reinforce walls), tap to ping.  M: tactical map.
   - W/A/S/D or screen edge: pan.  Q/E: rotate.  Mouse wheel: zoom.
4. Logs for bug reports: Unreal\Saved\Logs\Unreal.log in this folder.

Everyone must run the same build as the server. Join.bat keeps you up to date when the host publishes
builds online; otherwise download the new zip when the host says there is one.
"@
Set-Content -Path (Join-Path $GameDir "README-PLAYTEST.txt") -Value $Readme -Encoding UTF8

# Zip without debug symbols (.pdb, about a third of the size): friends don't need them, and the unzipped
# folder keeps them for reading crash call stacks from friends' logs.
$Zip = Join-Path $OutDir "$Name.zip"
if (Test-Path $Zip) { Remove-Item $Zip }
$ZipStage = Join-Path $OutDir "$Name-zipstage"
robocopy $GameDir $ZipStage /E /XF *.pdb /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying the build for zipping failed (robocopy exit $LASTEXITCODE)." }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($ZipStage, $Zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
Remove-Item $ZipStage -Recurse -Force
$SizeMB = [math]::Round((Get-Item $Zip).Length / 1MB)
$Last = [ordered]@{ name = $Name; build = $BuildId; zip = $Zip; sizeMB = $SizeMB; gameDir = $GameDir }
Set-Content -Path (Join-Path $OutDir "last-build.json") -Value ($Last | ConvertTo-Json) -Encoding UTF8
""
"Done: $Zip ($SizeMB MB), build $BuildId"
if (-not $UpdateUrl) { "Send that zip to your friends. Host with: ./Scripts/Playtest/HostServer.ps1 -BuildId $BuildId" }
exit 0   # robocopy's "files copied" (1) would otherwise be the script's exit code
