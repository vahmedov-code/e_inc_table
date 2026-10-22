# Run locally by the owner: build and send the firmware to the sign over Wi-Fi, no USB.
# The sign must already run 0.3.0 or later (the first install of 0.3.0 goes over USB).
# Usage: powershell -ExecutionPolicy Bypass -File tools\ota.ps1 [-Address 192.168.0.33]
param(
  [string]$Address = '192.168.0.33',
  [ValidateSet('esp32-m075-v3','esp32-m075-recovery','esp32-m075-recovery-wifi')]
  [string]$Environment = 'esp32-m075-v3'
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path $python)) { throw "PlatformIO Python not found: $python" }

Write-Host "== Build $Environment"
& $python -m platformio run -e $Environment
if ($LASTEXITCODE) { throw 'Build failed. Nothing was sent.' }
$bin = ".pio\build\$Environment\firmware.bin"

# The key is read without echo and not stored; set AVIOR_TOKEN to skip the prompt.
$token = $env:AVIOR_TOKEN
if (-not $token) {
  $secure = Read-Host 'API token' -AsSecureString
  $token = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
}
$base = "http://$Address/api/v1"
function Get-SignState {
  try { curl.exe -s --max-time 5 -H "Authorization: Bearer $token" "$base/state" | ConvertFrom-Json } catch { $null }
}

$before = Get-SignState
if (-not $before.version) { throw "No answer from $Address, or the key is wrong. Is the sign refreshing (Wi-Fi off for ~25 s)?" }
Write-Host "== Sign runs $($before.version); sending $bin"
$answer = curl.exe -s --max-time 300 -H "Authorization: Bearer $token" -F "file=@$bin;type=application/octet-stream" "$base/firmware"
Write-Host $answer
if ($answer -notmatch '"accepted":true') { throw 'The sign refused the firmware; the previous version keeps running.' }

Write-Host '== Waiting for restart'
for ($i = 0; $i -lt 30; $i++) {
  Start-Sleep -Seconds 3
  $after = Get-SignState
  if ($after.version -and $after.uptimeSeconds -lt 180) {
    Write-Host "== Sign runs $($after.version). It keeps the new image after one minute of normal work."
    exit 0
  }
}
Write-Host 'No answer within 90 s. Check the panel in a minute; after a failed start the sign returns to the previous version.'
