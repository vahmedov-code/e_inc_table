# Run locally by the owner: build, verify the factory backup, flash with typed
# confirmation (tools/safe_flash.py), then start the read-only serial log.
# Usage: powershell -ExecutionPolicy Bypass -File tools\flash_and_log.ps1 [-Port COM20]
param(
  [string]$Port,
  [string]$Backup = 'C:\AviorSign\gooddisplay_factory_backup.bin',
  # Previously recorded backup SHA256; if omitted it is computed, as in docs/RECOVERY.md.
  [string]$Sha256,
  [ValidateSet('esp32-m075-v3','esp32-m075-v3-big','esp32-m075-recovery','esp32-m075-recovery-wifi')]
  [string]$Environment = 'esp32-m075-v3'
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path $python)) { throw "PlatformIO Python not found: $python" }

Write-Host "== Build $Environment"
& $python -m platformio run -e $Environment
if ($LASTEXITCODE) { throw 'Build failed. Nothing was written.' }

if (-not $Port) {
  # Present CH340 (1A86:7523) only; the owner still confirms the choice.
  $found = @(Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.PNPDeviceID -match 'VID_1A86&PID_7523' -and $_.ConfigManagerErrorCode -eq 0 } |
    ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } })
  if ($found.Count -ne 1) { throw "CH340 ports found: '$($found -join ', ')'. Run again with -Port COMxx." }
  if ((Read-Host "Board on $($found[0])? Press Enter to confirm, any text to cancel") -ne '') { throw 'Cancelled. Nothing was written.' }
  $Port = $found[0]
}

Write-Host "== Close every Serial Monitor using $Port before continuing."
if (-not $Sha256) { $Sha256 = (Get-FileHash $Backup -Algorithm SHA256).Hash }
& $python tools/safe_flash.py --environment $Environment --backup $Backup --sha256 $Sha256 --port $Port --write
if ($LASTEXITCODE) { throw 'Flash failed or cancelled. Read the message above; no automatic retry.' }

Write-Host "== Read-only log on $Port. Leave it 3-5 minutes, then Ctrl+C. File: logs\serial-*.log"
& $python tools/serial_log.py --port $Port
