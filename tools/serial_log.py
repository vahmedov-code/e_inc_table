"""Run locally by the owner. Read-only serial log that survives USB re-enumeration.

Opens the port with DTR/RTS released, so opening the log does not reset the ESP32
through the auto-reset circuit. Never writes to the device. Secrets printed at boot
(AP password, API token, Wi-Fi password) are redacted before saving.
"""
import argparse
import re
import sys
import time
from datetime import datetime
from pathlib import Path

import serial

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True, help='Explicit serial port, e.g. COM20')
parser.add_argument('--baud', type=int, default=115200)
args = parser.parse_args()

SECRET = re.compile(r'(AP password|API token|password|token)(\s*[:=]\s*)\S+', re.I)
MARK = re.compile(r'rst:|Brownout|Guru Meditation|Backtrace|abort\(\)|\[boot\]|\[display\]|interrupted_render|wdt', re.I)
logs = root / 'logs'
logs.mkdir(exist_ok=True)
path = logs / datetime.now().strftime('serial-%Y%m%d-%H%M%S.log')
print('Saving to', path, '- Ctrl+C to stop')


def note(out, text):
    line = datetime.now().strftime('%H:%M:%S.%f')[:-3] + ' ' + text
    out.write(line + '\n')
    out.flush()
    print(line)


with path.open('w', encoding='utf-8') as out:
    port_state = None
    while True:
        try:
            link = serial.Serial()
            link.port, link.baudrate, link.timeout = args.port, args.baud, 0.5
            link.dtr = False
            link.rts = False
            link.open()
            note(out, '[log] port open')
            port_state = 'open'
            buffer = b''
            while True:
                buffer += link.read(512)
                *lines, buffer = buffer.split(b'\n')
                for raw in lines:
                    text = SECRET.sub(r'\1\2<redacted>', raw.decode('utf-8', 'replace').rstrip('\r'))
                    note(out, ('!! ' if MARK.search(text) else '') + text)
        except KeyboardInterrupt:
            sys.exit(0)
        except (serial.SerialException, OSError) as error:
            # PermissionError 13 on Windows: another program owns the port, or USB is re-enumerating.
            state = type(error).__name__ + ': ' + str(error)
            if state != port_state:
                note(out, '[log] port unavailable: ' + state)
                port_state = state
            time.sleep(1)
