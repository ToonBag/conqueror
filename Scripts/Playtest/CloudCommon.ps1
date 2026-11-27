<#
  Shared helpers for the cloud playtest scripts (dot-source this file).

  A "bucket" is either a Cloud Storage URL (gs://name) or, for testing on this PC, a local folder that stands in
  for one. Paths inside a bucket use forward slashes (server/project, client/latest.json).
#>

$ErrorActionPreference = "Stop"

function Get-PlaytestRoot { (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path }

function Read-CloudConfig([string]$Path) {
    if (-not $Path) { $Path = Join-Path $PSScriptRoot "cloud.json" }
    if (-not (Test-Path $Path)) {
        throw "No cloud config at $Path. Run Scripts/Playtest/CloudSetup.ps1 once first (see Docs/Playtest.md)."
    }
    Get-Content $Path -Raw | ConvertFrom-Json
}

function Test-IsGcs([string]$Bucket) { $Bucket -like "gs://*" }

function Join-BucketPath([string]$Bucket, [string]$Rel) {
    if (Test-IsGcs $Bucket) { return ($Bucket.TrimEnd("/") + "/" + $Rel.TrimStart("/")) }
    return (Join-Path $Bucket ($Rel -replace "/", "\"))
}

function Find-Gcloud {
    $cmd = Get-Command gcloud -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in @("$env:ProgramFiles\Google\Cloud SDK\google-cloud-sdk\bin\gcloud.cmd",
                     "${env:ProgramFiles(x86)}\Google\Cloud SDK\google-cloud-sdk\bin\gcloud.cmd",
                     "$env:LOCALAPPDATA\Google\Cloud SDK\google-cloud-sdk\bin\gcloud.cmd")) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    throw "gcloud (Google Cloud CLI) not found. Install it (see Docs/Playtest.md) and run 'gcloud auth login'."
}

function Invoke-Gcloud {
    # Runs gcloud with the given arguments; throws on failure. Returns its standard output.
    $gcloud = Find-Gcloud
    $ErrorActionPreference = "Continue" # gcloud reports progress on stderr; Windows PowerShell would treat it as an error.
    $out = & $gcloud @args
    if ($LASTEXITCODE -ne 0) { throw "gcloud $($args -join ' ') failed (exit $LASTEXITCODE)." }
    $out
}

function Sync-Folder([string]$Source, [string]$Dest) {
    # Makes Dest a copy of Source (deleting extra files). Either side may be a gs:// path; local-to-local uses robocopy.
    if ((Test-IsGcs $Source) -or (Test-IsGcs $Dest)) {
        if (-not (Test-IsGcs $Source)) { New-Item -ItemType Directory -Force $Source | Out-Null }
        if (-not (Test-IsGcs $Dest)) { New-Item -ItemType Directory -Force $Dest | Out-Null }
        Invoke-Gcloud storage rsync $Source $Dest --recursive --delete-unmatched-destination-objects --quiet | Out-Null
        return
    }
    New-Item -ItemType Directory -Force $Dest | Out-Null
    robocopy $Source $Dest /MIR /NFL /NDL /NJH /NJS /NP /R:2 /W:2 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copying $Source to $Dest failed (robocopy exit $LASTEXITCODE)." }
    $global:LASTEXITCODE = 0
}

function Write-BucketFile([string]$LocalFile, [string]$Dest, [switch]$Public, [switch]$NoCache) {
    # Public: readable by anyone with the link (not listable). NoCache: always re-read (latest.json).
    if (Test-IsGcs $Dest) {
        $extra = @()
        if ($Public) { $extra += "--predefined-acl=publicRead" }
        if ($NoCache) { $extra += "--cache-control=no-cache" }
        Invoke-Gcloud storage cp $LocalFile $Dest @extra --quiet | Out-Null
        return
    }
    New-Item -ItemType Directory -Force (Split-Path $Dest) | Out-Null
    Copy-Item $LocalFile $Dest -Force
}

function Write-BucketText([string]$Text, [string]$Dest, [switch]$Public, [switch]$NoCache) {
    $tmp = [System.IO.Path]::GetTempFileName()
    try {
        [System.IO.File]::WriteAllText($tmp, $Text, (New-Object System.Text.UTF8Encoding $false))
        Write-BucketFile $tmp $Dest -Public:$Public -NoCache:$NoCache
    } finally { Remove-Item $tmp -ErrorAction SilentlyContinue }
}

function Read-BucketText([string]$Src) {
    # Returns the file's text, or $null if it doesn't exist.
    if (Test-IsGcs $Src) {
        $gcloud = Find-Gcloud
        $ErrorActionPreference = "Continue"
        $out = & $gcloud storage cat $Src 2>$null
        if ($LASTEXITCODE -ne 0) { return $null }
        return ($out -join "`n")
    }
    if (-not (Test-Path $Src)) { return $null }
    Get-Content $Src -Raw
}

function Get-BuildId([string]$Root) {
    # <date>-<time>-<commit>[-dirty]: names the client zip and is what the server checks clients against.
    Push-Location $Root
    try {
        $commit = (git rev-parse --short HEAD).Trim()
        if (git status --porcelain -- Source Content Config) { $commit += "-dirty" }
    } finally { Pop-Location }
    "$(Get-Date -Format 'yyyyMMdd-HHmm')-$commit"
}

function Get-EngineMarker([string]$EngineRoot) {
    # Changes when the engine is updated (hotfix), so the server's copy is refreshed.
    $v = Get-Content (Join-Path $EngineRoot "Engine\Build\Build.version") -Raw | ConvertFrom-Json
    "$($v.MajorVersion).$($v.MinorVersion).$($v.PatchVersion)-CL$($v.Changelist)"
}

# Engine folders the editor binary needs to run a dedicated server (-server: no rendering, no editor UI).
# Symbols, source, and other platforms are left out (about 15 GB instead of 60+).
$script:EngineDirs = @("Engine\Binaries\Win64", "Engine\Binaries\ThirdParty", "Engine\Build", "Engine\Config",
    "Engine\Content", "Engine\Plugins", "Engine\Shaders", "Engine\Extras\Redist")
$script:EngineExcludeDirs = @("Source", "Intermediate", "Linux", "LinuxArm64", "Mac", "Android", "IOS", "TVOS",
    "VisionOS", "Saved", "DerivedDataCache")

function Copy-EngineForServer([string]$EngineRoot, [string]$Stage) {
    foreach ($d in $script:EngineDirs) {
        $src = Join-Path $EngineRoot $d
        if (-not (Test-Path $src)) { continue }
        if ($d -eq "Engine\Build") {
            New-Item -ItemType Directory -Force (Join-Path $Stage $d) | Out-Null
            Copy-Item (Join-Path $src "*") (Join-Path $Stage $d) -Include *.version, *.txt -Force
            continue
        }
        robocopy $src (Join-Path $Stage $d) /MIR /XF *.pdb /XD @script:EngineExcludeDirs /NFL /NDL /NJH /NJS /NP /R:2 /W:2 | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Staging $d failed (robocopy exit $LASTEXITCODE)." }
    }
    $global:LASTEXITCODE = 0
}

function Copy-ProjectForServer([string]$Root, [string]$Stage) {
    # What the server loads: the project file, config, content and the game module built for the editor binary.
    New-Item -ItemType Directory -Force $Stage | Out-Null
    Copy-Item (Join-Path $Root "Unreal.uproject") $Stage -Force
    foreach ($d in "Config", "Content") {
        robocopy (Join-Path $Root $d) (Join-Path $Stage $d) /MIR /NFL /NDL /NJH /NJS /NP /R:2 /W:2 | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Staging $d failed (robocopy exit $LASTEXITCODE)." }
    }
    $bin = Join-Path $Stage "Binaries\Win64"
    if (Test-Path $bin) { Remove-Item $bin -Recurse -Force }
    New-Item -ItemType Directory -Force $bin | Out-Null
    foreach ($f in "UnrealEditor-Unreal.dll", "UnrealEditor-Unreal.pdb", "UnrealEditor.modules", "UnrealEditor.target") {
        $src = Join-Path $Root "Binaries\Win64\$f"
        if (Test-Path $src) { Copy-Item $src $bin -Force }
    }
    if (-not (Test-Path (Join-Path $bin "UnrealEditor-Unreal.dll"))) { throw "Binaries\Win64\UnrealEditor-Unreal.dll is missing: build the editor target first." }
    $global:LASTEXITCODE = 0
}
