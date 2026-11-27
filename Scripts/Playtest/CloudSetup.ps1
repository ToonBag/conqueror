<#
.SYNOPSIS
  One-time setup of the Google Cloud playtest server (see Docs/Playtest.md, "Cloud server"). Creates, in your
  Google Cloud project:
    - two Cloud Storage buckets: <project>-squad-server (private: the server's files and status) and
      <project>-squad-client (game zips and latest.json, readable by anyone with the link, not listable),
    - a static external IP address, so friends' Join.bat always finds the server,
    - a firewall rule letting UDP <port> reach the VM (from anywhere: friends' IPs don't need listing),
    - a Windows Server VM that, at every boot, runs Cloud/VmAgent.ps1 to keep the server on the newest build.
  It then writes Scripts/Playtest/cloud.json, which Release.ps1 and CloudServer.ps1 read.

  Running it costs money while the VM runs (see Docs/Playtest.md). Safe to run again: it skips what exists.

.EXAMPLE
  ./Scripts/Playtest/CloudSetup.ps1 -ProjectId squad-playtest-123456
#>
param(
    [Parameter(Mandatory = $true)][string]$ProjectId,
    [string]$Region = "europe-west2",          # London
    [string]$Zone = "europe-west2-b",
    [string]$MachineType = "e2-standard-2",    # 2 vCPU, 8 GB
    [int]$DiskGB = 100,
    [string]$Name = "squad-playtest",
    [int]$Port = 17777
)

. (Join-Path $PSScriptRoot "CloudCommon.ps1")
$ErrorActionPreference = "Stop"
$gcloud = Find-Gcloud

function Test-Exists {
    # True if the gcloud command (a describe) succeeds.
    $ErrorActionPreference = "Continue"
    & $gcloud @args --quiet *> $null
    $LASTEXITCODE -eq 0
}

$ServerBucket = "gs://$ProjectId-squad-server"
$ClientBucket = "gs://$ProjectId-squad-client"

"Project $ProjectId, region $Region, zone $Zone"
Invoke-Gcloud config set project $ProjectId --quiet | Out-Null
"Enabling the Compute Engine and Cloud Storage APIs (a minute the first time)..."
Invoke-Gcloud services enable compute.googleapis.com storage.googleapis.com --quiet | Out-Null

if (-not (Test-Exists storage buckets describe $ServerBucket)) {
    "Creating $ServerBucket (private)..."
    Invoke-Gcloud storage buckets create $ServerBucket --location=$Region --uniform-bucket-level-access --public-access-prevention --quiet | Out-Null
}
if (-not (Test-Exists storage buckets describe $ClientBucket)) {
    # Per-object access (no uniform access) so each uploaded zip can be made readable by link without
    # letting anyone list the bucket.
    "Creating $ClientBucket (files readable by link)..."
    Invoke-Gcloud storage buckets create $ClientBucket --location=$Region --no-uniform-bucket-level-access --quiet | Out-Null
}

$IpName = "$Name-ip"
if (-not (Test-Exists compute addresses describe $IpName --region=$Region)) {
    "Reserving a static IP address..."
    Invoke-Gcloud compute addresses create $IpName --region=$Region --quiet | Out-Null
}
$Ip = (Invoke-Gcloud compute addresses describe $IpName --region=$Region --format="value(address)").Trim()
"Server address: $Ip"

$RuleName = "$Name-udp-$Port"
if (-not (Test-Exists compute firewall-rules describe $RuleName)) {
    "Opening UDP $Port to the VM..."
    Invoke-Gcloud compute firewall-rules create $RuleName --direction=INGRESS --allow=udp:$Port `
        --target-tags=$Name --source-ranges=0.0.0.0/0 --quiet | Out-Null
}

# The VM's service account reads the server's files and writes its status.
$ServiceAccount = (Invoke-Gcloud compute project-info describe --format="value(defaultServiceAccount)").Trim()
Invoke-Gcloud storage buckets add-iam-policy-binding $ServerBucket --member="serviceAccount:$ServiceAccount" `
    --role=roles/storage.objectAdmin --quiet | Out-Null

if (-not (Test-Exists compute instances describe $Name --zone=$Zone)) {
    "Creating the VM $Name ($MachineType, Windows Server 2022, $DiskGB GB)..."
    Invoke-Gcloud compute instances create $Name --zone=$Zone --machine-type=$MachineType `
        --image-family=windows-2022 --image-project=windows-cloud `
        --boot-disk-size="${DiskGB}GB" --boot-disk-type=pd-balanced `
        --address=$Ip --tags=$Name --scopes=storage-rw `
        --metadata="squad-bucket=$ServerBucket" `
        "--metadata-from-file=windows-startup-script-ps1=$(Join-Path $PSScriptRoot 'Cloud\VmStartup.ps1')" --quiet | Out-Null
} else {
    "VM $Name exists; updating its startup script."
    Invoke-Gcloud compute instances add-metadata $Name --zone=$Zone --metadata="squad-bucket=$ServerBucket" `
        "--metadata-from-file=windows-startup-script-ps1=$(Join-Path $PSScriptRoot 'Cloud\VmStartup.ps1')" --quiet | Out-Null
}

$Config = [ordered]@{
    project = $ProjectId; region = $Region; zone = $Zone; vm = $Name; ip = $Ip; port = $Port
    serverBucket = $ServerBucket; clientBucket = $ClientBucket
    clientBaseUrl = "https://storage.googleapis.com/$ProjectId-squad-client"
}
$ConfigPath = Join-Path $PSScriptRoot "cloud.json"
Set-Content -Path $ConfigPath -Value ($Config | ConvertTo-Json) -Encoding UTF8
""
"Done. Wrote $ConfigPath."
"Next: ./Scripts/Playtest/Release.ps1 (the first one uploads the engine, ~15 GB, and takes a while)."
"The VM is running and costs money until you stop it: ./Scripts/Playtest/CloudServer.ps1 -Stop"
