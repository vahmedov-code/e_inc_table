"""One-time preparation of the built-in status templates from the owner's 800x480 designs
(assets/templates/originals/*.png): quantise to the panel's four colours, drop isolated
speckles, give «Закрыто» and «Открыто» the bottom row of «Скоро буду» (icons moved left to
make room for the QR code), clear the QR square. Writes assets/templates/{open,closed,soon}.png;
tools/make_templates.py turns those into include/templates.h."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
PAL = [(0, 0, 0), (255, 255, 255), (255, 213, 0), (196, 30, 35)]
SHARED_BOTTOM_FROM = 316          # rows from here on come from «Скоро буду»
QR_CLEAR = (681, 290, 780, 389)   # same square as QR_BOX in include/templates.h


def quantise(img):
    # Same rule as the panel: near-gray pixels go to black or white only.
    px = img.convert('RGB').load()
    out = [[1] * 800 for _ in range(480)]
    for y in range(480):
        for x in range(800):
            r, g, b = px[x, y]
            if max(r, g, b) - min(r, g, b) < 60:
                out[y][x] = 0 if 0.299 * r + 0.587 * g + 0.114 * b < 128 else 1
            else:
                out[y][x] = min(range(4), key=lambda c: (r - PAL[c][0]) ** 2 + (g - PAL[c][1]) ** 2 + (b - PAL[c][2]) ** 2)
    return out


def despeckle(m):
    # A pixel unlike all four neighbours, which agree with each other, takes their colour;
    # strokes two pixels wide or more are untouched.
    out = [row[:] for row in m]
    for y in range(1, 479):
        for x in range(1, 799):
            n = {m[y - 1][x], m[y + 1][x], m[y][x - 1], m[y][x + 1]}
            if len(n) == 1 and m[y][x] not in n:
                out[y][x] = n.pop()
    return out


def clear_under_banner(m):
    # The designs have a soft shadow under the red banner that quantises to a dashed line:
    # clear the strip between the banner's last solid row and the icon row.
    solid = [y for y in range(200, 330) if sum(m[y][x] == 3 for x in range(140, 650)) > 400]
    for y in range(max(solid) + 1, 320):
        for x in range(100, 680):
            m[y][x] = 1
    return max(solid)


src = {k: despeckle(quantise(Image.open(root / 'assets/templates/originals' / f'{k}.png'))) for k in ('soon', 'closed', 'open')}
for key, m in src.items():
    print(key, 'banner ends at row', clear_under_banner(m))
for key, m in src.items():
    if key != 'soon':
        m[SHARED_BOTTOM_FROM:] = [row[:] for row in src['soon'][SHARED_BOTTOM_FROM:]]
    x0, y0, x1, y1 = QR_CLEAR
    for y in range(y0, y1):
        for x in range(x0, x1):
            m[y][x] = 1
    img = Image.new('RGB', (800, 480))
    img.putdata([PAL[c] for row in m for c in row])
    img.save(root / 'assets/templates' / f'{key}.png')
    print('wrote', key)
