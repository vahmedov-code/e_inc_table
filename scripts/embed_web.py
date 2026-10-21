"""Embed the offline UI in firmware; no filesystem upload is needed for the UI."""
Import("env")
import gzip
from pathlib import Path
root = Path(env['PROJECT_DIR'])
data = gzip.compress((root / 'web/index.html').read_bytes(), mtime=0)
out = root / '.pio/generated'
out.mkdir(parents=True, exist_ok=True)
(out / 'web_ui.h').write_text('#pragma once\n#include <Arduino.h>\nconst uint8_t WEB_UI[] PROGMEM = {' + ','.join(map(str, data)) + '};\n', encoding='utf-8')
env.Append(CPPPATH=[str(out)])
