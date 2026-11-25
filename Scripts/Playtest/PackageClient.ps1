<#
.SYNOPSIS
  Packages the Squad Command game client for friends to playtest with, and zips it with a Join.bat.

  The client is a cooked Win64 Development build of the Game target (the installed engine can build it).
  The server is not in the zip: you host it from this PC with Scripts/Playtest/HostServer.ps1.

.EXAMPLE
  ./Scripts/Playtest/PackageClient.ps1 -HostAddress 100.101.102.103   # bake your Tailscale / public address into Join.bat
  ./Scripts/Playtest/PackageClient.ps1                                  # Join.bat asks each friend for the address

  Output: Playtest/SquadCommand-Playtest-<date>-<commit>.zip (and the unzipped folder next to it).
  A first package cooks every shader and takes a while; later ones reuse the cook.
#>
param(
    [string]$HostAddress = "",
    [int]$Port = 17777,
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Project = Join-Path $Root "Unreal.uproject"
$RunUAT = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path $RunUAT)) { throw "RunUAT.bat not found at $RunUAT (pass -EngineRoot)." }

# Name the build after the commit so host and friends can check they run the same one.
Push-Location $Root
try {
    $Commit = (git rev-parse --short HEAD).Trim()
    if (git status --porcelain -- Source Content Config) { $Commit += "-dirty" }
} finally { Pop-Location }
$Stamp = Get-Date -Format "yyyyMMdd-HHmm"
$Name = "SquadCommand-Playtest-$Stamp-$Commit"
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

$JoinBat = @"
@echo off
rem Squad Command playtest: joins the host's server. Build $Name.
set HOST=$HostAddress
if "%HOST%"=="" set /p HOST=Host address (from the host, e.g. 100.101.102.103):
if "%HOST%"=="" exit /b 1
start "" "%~dp0Unreal.exe" %HOST%:$Port -windowed -ResX=1600 -ResY=900 -log
"@
Set-Content -Path (Join-Path $GameDir "Join.bat") -Value $JoinBat -Encoding ASCII

$Readme = @"
SQUAD COMMAND - PLAYTEST BUILD $Name

1. Unzip anywhere and run Join.bat. It connects to the host's server on port $Port.
   (No host address baked in? It asks for the one the host gave you.)
2. If you see a map but nobody else, the connection failed and you are in a single-player copy.
   Close it, check the address with the host, and try again.
3. Controls (you command one squad of 4):
   - 1-4 or left click a soldier: select.  Left click ground: move.
   - Shift + left click ground: face that way.  Shift + left click an enemy: attack it.
   - Alt + left click: throw a grenade.  Ctrl + left click a downed ally: drag / release.
   - Right click an injured ally: use a medkit.  C: stance.  H: hold fire.  Tab: loot.
   - Middle mouse: hold for the command wheel (build C4 / reinforce walls), tap to ping.  M: tactical map.
   - W/A/S/D or screen edge: pan.  Q/E: rotate.  Mouse wheel: zoom.
4. Logs for bug reports: Unreal\Saved\Logs\Unreal.log in this folder.

Everyone must run the same build as the host. If the host says there is a new build, download it again.
"@
Set-Content -Path (Join-Path $GameDir "README-PLAYTEST.txt") -Value $Readme -Encoding UTF8
Set-Content -Path (Join-Path $GameDir "BuildInfo.txt") -Value "$Name`r`ncommit $Commit`r`nport $Port`r`nhost $HostAddress" -Encoding ASCII

# Zip without debug symbols (.pdb, about a third of the size): friends don't need them, and the unzipped
# folder keeps them for reading crash call stacks from friends' logs.
$Zip = Join-Path $OutDir "$Name.zip"
if (Test-Path $Zip) { Remove-Item $Zip }
$ZipStage = Join-Path $OutDir "$Name-zipstage"
robocopy $GameDir $ZipStage /E /XF *.pdb /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying the build for zipping failed (robocopy exit $LASTEXITCODE)." }
Compress-Archive -Path (Join-Path $ZipStage "*") -DestinationPath $Zip
Remove-Item $ZipStage -Recurse -Force
$SizeMB = [math]::Round((Get-Item $Zip).Length / 1MB)
""
"Done: $Zip ($SizeMB MB)"
"Send that zip to your friends. Host with: ./Scripts/Playtest/HostServer.ps1"
exit 0   # robocopy's "files copied" (1) would otherwise be the script's exit code
