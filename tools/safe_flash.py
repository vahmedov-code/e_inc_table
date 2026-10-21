"""Run locally by the owner only. Default is validation, never a flash operation."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backup', required=True, type=Path)
parser.add_argument('--sha256', required=True, help='Previously recorded SHA256 of the factory backup')
parser.add_argument('--port', help='Explicit serial port, e.g. COM5')
parser.add_argument('--environment', choices=['esp32-m075-v3','esp32-m075-recovery','esp32-m075-recovery-wifi'], default='esp32-m075-v3')
parser.add_argument('--write', action='store_true', help='Actually replace bootloader, partition table, OTA selection and app0')
args = parser.parse_args()
backup = args.backup.resolve(strict=True)
if backup.is_relative_to(root):
    parser.error('Keep the factory backup OUTSIDE this repository.')
if backup.stat().st_size != 4 * 1024 * 1024:
    parser.error('Factory backup must be exactly 4194304 bytes.')
digest = hashlib.sha256(backup.read_bytes()).hexdigest()
if digest.lower() != args.sha256.lower():
    parser.error('Backup checksum mismatch. Stop and check the original backup.')
build = root / '.pio/build' / args.environment
for name in ('bootloader.bin','partitions.bin','firmware.bin'):
    if not (build/name).is_file():
        parser.error('Build first: python -m platformio run -e ' + args.environment)
if (build/'firmware.bin').stat().st_size > 0x140000:
    parser.error('Firmware exceeds app partition.')
print('Backup verified. Build exists. This does not validate wiring, panel identity or secure boot.')
if not args.write:
    print('Validation only: no device opened, no flash written.')
    sys.exit(0)
if not args.port:
    parser.error('--write requires --port; automatic port selection is forbidden.')
print('LOCAL FLASH: verify ESP32-D0WD, 4MB, no flash encryption/secure boot, stable USB power.')
print('This replaces firmware and OTA selection. Existing NVS and filesystem are preserved.')
confirmation = {'esp32-m075-v3':'FLASH AVIOR F53','esp32-m075-recovery':'FLASH AVIOR RECOVERY','esp32-m075-recovery-wifi':'FLASH AVIOR RECOVERY WIFI'}[args.environment]
print('Selected environment:', args.environment)
if input('Type ' + confirmation + ' to continue: ') != confirmation:
    sys.exit('Cancelled without writing.')
# PlatformIO uses sector writes at 0x1000, 0x8000, 0xe000 and 0x10000.
# No erase_flash, uploadfs, factory dump, remote update, or format command.
try:
    subprocess.run([sys.executable,'-m','platformio','run','-e',args.environment,
                    '--target','upload','--upload-port',args.port],cwd=root,check=True)
except subprocess.CalledProcessError as exc:
    sys.exit(f'Upload failed (exit {exc.returncode}). Read the esptool error above. No automatic retry.')
