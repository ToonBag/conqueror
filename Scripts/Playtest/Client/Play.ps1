<#
  Squad Command playtest launcher (run by Join.bat). Ships inside the playtest zip.

  1. If the host publishes builds online (Playtest.json has an updateUrl), checks for a newer build and, if
     there is one, downloads it and replaces this folder's files (your settings and logs in Unreal\Saved stay).
  2. Starts the game connected to the host's server, sending this build's ID so the server can check it.
#>
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$GameDir = $PSScriptRoot
$Cfg = Get-Content (Join-Path $GameDir "Playtest.json") -Raw | ConvertFrom-Json

function Save-Download([string]$Url, [string]$File) {
    # Plain HTTP download with a percentage, using only what ships with Windows.
    $req = [System.Net.HttpWebRequest]::Create($Url)
    $resp = $req.GetResponse()
    $total = $resp.ContentLength
    $in = $resp.GetResponseStream()
    $out = [System.IO.File]::Create($File)
    try {
        $buf = New-Object byte[] 1048576
        $done = 0L; $lastPct = -1
        while (($n = $in.Read($buf, 0, $buf.Length)) -gt 0) {
            $out.Write($buf, 0, $n); $done += $n
            if ($total -gt 0) {
                $pct = [int](100 * $done / $total)
                if ($pct -ne $lastPct -and $pct % 5 -eq 0) { Write-Host ("  {0,3}%  ({1:N0} of {2:N0} MB)" -f $pct, ($done / 1MB), ($total / 1MB)); $lastPct = $pct }
            }
        }
    } finally { $out.Close(); $in.Close(); $resp.Close() }
}

if ($Cfg.updateUrl) {
    $latest = $null
    try {
        $latest = Invoke-RestMethod -Uri "$($Cfg.updateUrl)?t=$([DateTime]::UtcNow.Ticks)" -TimeoutSec 15
    } catch {
        Write-Host "Could not check for a new build ($($_.Exception.Message)). Starting this one." -ForegroundColor Yellow
    }
    if ($latest -and $latest.build -and $latest.build -ne $Cfg.build) {
        Write-Host ""
        Write-Host "New build available: $($latest.build) (you have $($Cfg.build)). Downloading $($latest.sizeMB) MB..." -ForegroundColor Cyan
        $tmp = Join-Path ([System.IO.Path]::GetTempPath()) "SquadCommandUpdate"
        if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
        New-Item -ItemType Directory -Force $tmp | Out-Null
        $zip = Join-Path $tmp "build.zip"
        Save-Download $latest.url $zip
        Write-Host "Unpacking..."
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $unpacked = Join-Path $tmp "files"
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $unpacked)
        if (-not (Test-Path (Join-Path $unpacked "Unreal.exe"))) { throw "The downloaded build looks broken (no Unreal.exe). Ask the host." }
        # Replace this folder's files with the new build's, keeping Unreal\Saved (settings, logs).
        robocopy $unpacked $GameDir /MIR /XD Saved /NFL /NDL /NJH /NJS /NP /R:3 /W:2 | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Updating the game files failed (robocopy exit $LASTEXITCODE). Is the game still running? Close it and run Join.bat again." }
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
        $Cfg = Get-Content (Join-Path $GameDir "Playtest.json") -Raw | ConvertFrom-Json
        Write-Host "Updated to $($Cfg.build)." -ForegroundColor Green
    }
    if ($latest -and $latest.host) { $Cfg.host = $latest.host }
}

$HostAddress = $Cfg.host
if (-not $HostAddress) { $HostAddress = Read-Host "Host address (from the host, e.g. 34.89.10.20)" }
if (-not $HostAddress) { exit 1 }
Write-Host "Starting build $($Cfg.build), joining ${HostAddress}:$($Cfg.port)..."
Start-Process (Join-Path $GameDir "Unreal.exe") -ArgumentList "${HostAddress}:$($Cfg.port)?Build=$($Cfg.build)", "-windowed", "-ResX=1600", "-ResY=900", "-log"
