"""Check repository payload and JavaScript syntax; never opens a serial device."""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[1]
html = (root/'web/index.html').read_text(encoding='utf-8')
scripts = re.findall(r'<script>(.*?)</script>',html,re.S)
assert len(scripts)==1
assert 'http://' not in html.split('<script>')[1] and 'https://' not in html.split('<script>')[1], 'UI must work offline'
node=shutil.which('node')
if not node:
    raise SystemExit('Node.js is required for JavaScript syntax verification.')
scratch=root/'.pio/checks'
scratch.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as tmp:
    path=Path(tmp)/'ui.js';path.write_text(scripts[0],encoding='utf-8')
    subprocess.run([node,'--check',str(path)],check=True)
print('Offline UI and JavaScript syntax: OK')
