# Startup script of the cloud playtest VM (metadata windows-startup-script-ps1, set by CloudSetup.ps1).
# Runs as SYSTEM at every boot: fetches the agent from the server bucket and runs it as a scheduled task,
# so it keeps running after this script ends. Release.ps1 uploads a new agent with every deploy.
$ErrorActionPreference = "Stop"
$Root = "C:\SquadServer"
$AgentDir = Join-Path $Root "Agent"
New-Item -ItemType Directory -Force $AgentDir | Out-Null

$Bucket = Invoke-RestMethod -Headers @{ "Metadata-Flavor" = "Google" } `
    -Uri "http://metadata.google.internal/computeMetadata/v1/instance/attributes/squad-bucket"
$gcloud = (Get-Command gcloud -ErrorAction SilentlyContinue).Source
if (-not $gcloud) { $gcloud = "C:\Program Files (x86)\Google\Cloud SDK\google-cloud-sdk\bin\gcloud.cmd" }

# Until the first Release.ps1 there is no agent in the bucket yet; retry for a while.
for ($i = 0; $i -lt 60; $i++) {
    & $gcloud storage cp "$Bucket/agent/*" $AgentDir --quiet
    if ($LASTEXITCODE -eq 0 -and (Test-Path (Join-Path $AgentDir "VmAgent.ps1"))) { break }
    Start-Sleep -Seconds 60
}

$TaskName = "SquadPlaytestAgent"
Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
$action = New-ScheduledTaskAction -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$AgentDir\VmAgent.ps1`" -Root `"$Root`""
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1)
Register-ScheduledTask -TaskName $TaskName -Action $action -Settings $settings -User "SYSTEM" -RunLevel Highest -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName
