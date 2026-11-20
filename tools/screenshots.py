#!/usr/bin/env python3
"""Screenshots of every watch face, on every watch, in every font.

For each platform in package.json this boots the emulator, installs the app,
and for each face and font sends the settings (pebble send-app-message), sets
the clock (pebble emu-set-time) and saves a screenshot as

    screenshots/<platform>/<face>-<font>.png

Then it writes src/pkjs/previews.js: the example pictures on the settings
page (the faces in the default font, and a crop of the date and time for each
font), so run `pebble build` again afterwards to include them.

Run from the project root, after `pebble build`:

    python3 tools/screenshots.py                  # every platform
    python3 tools/screenshots.py emery gabbro     # just these
    python3 tools/screenshots.py --previews-only  # only rewrite previews.js

Needs pebble-tool 5.2 or later (emu-set-time, send-app-message) and Pillow.
"""

import base64
import datetime
import io
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SHOTS = ROOT / 'screenshots'
PREVIEWS_JS = ROOT / 'src' / 'pkjs' / 'previews.js'

ROUND = {'chalk', 'gabbro'}       # no square dial
LARGE = {'emery', 'gabbro'}       # the only watches with a font choice

# name -> settings that select it. Values match src/pkjs/config.js.
FACES = {
    'digital':       {'Style': 0, 'Dial': 0},
    'analog-round':  {'Style': 1, 'Dial': 0},
    'analog-square': {'Style': 1, 'Dial': 1},
}
FONTS = {'default': 0, 'gothic': 1, 'roboto': 2}

# The same for every screenshot: everything shown, red accent, hour 0 as "0".
COMMON = {'Seconds': 2, 'NormalTime': 2, 'Date': 2, 'Accent': 2, 'HourZero': 0}

# Wednesday 31 May 2023, 17:31 on the host's clock: decimal 7:29:86.
SHOT_TIME = int(datetime.datetime(2023, 5, 31, 17, 31, 0).timestamp())

# Text colours on colour watches: COLOR_MAIN (time) and COLOR_SUB (date).
TEXT_COLOURS = {(255, 255, 255), (170, 170, 170)}


def faces_for(platform):
    return [f for f in FACES if not (f == 'analog-square' and platform in ROUND)]


def fonts_for(platform):
    return list(FONTS) if platform in LARGE else ['default']


def pebble(*args, retries=2):
    """Run a pebble-tool command; retry a couple of times, as emulators can be slow to answer."""
    for attempt in range(retries + 1):
        result = subprocess.run(['pebble', *args], cwd=ROOT, capture_output=True, text=True)
        if result.returncode == 0:
            return
        if attempt == retries:
            raise RuntimeError('pebble %s failed:\n%s%s' % (' '.join(args), result.stdout, result.stderr))
        time.sleep(3)


def capture(platform, pbw, keys):
    print('==', platform, flush=True)
    emu = ['--emulator', platform]
    pebble('install', *emu, str(pbw))
    pebble('emu-time-format', *emu, '--format', '24h')
    out = SHOTS / platform
    out.mkdir(parents=True, exist_ok=True)
    for face in faces_for(platform):
        for font in fonts_for(platform):
            settings = dict(COMMON, **FACES[face], Font=FONTS[font])
            pebble('send-app-message', *emu, '--int', *('%d=%d' % (keys[k], v) for k, v in settings.items()))
            pebble('emu-set-time', *emu, str(SHOT_TIME))
            time.sleep(0.5)  # one redraw (the face redraws every decimal second)
            path = out / ('%s-%s.png' % (face, font))
            # --no-correction: the palette colours the app draws, not sunlight-shaded ones.
            pebble('screenshot', *emu, '--no-open', '--no-correction', str(path))
            print('  ', path.relative_to(ROOT), flush=True)
    pebble('kill')


def png_data_uri(im):
    """Smallest PNG we can easily make: a palette image (the watches have at most 64 colours)."""
    im = im.convert('RGB')
    colours = len(im.getcolors(1 << 16) or [])
    if 0 < colours <= 256:
        im = im.convert('P', palette=Image.Palette.ADAPTIVE, colors=colours)
    buf = io.BytesIO()
    im.save(buf, 'PNG', optimize=True)
    return 'data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode('ascii')


def text_box(path, round_screen):
    """The box around a digital screenshot's first two lines of text: the date and the decimal time."""
    im = Image.open(path).convert('RGB')
    w, h = im.size
    px = im.load()
    cx, cy, inner = w / 2, h / 2, w / 2 - 20  # on round watches, stay inside the day ring

    def is_text(x, y):
        if round_screen and (x - cx) ** 2 + (y - cy) ** 2 > inner ** 2:
            return False
        return px[x, y] in TEXT_COLOURS

    rows = [y for y in range(h) if any(is_text(x, y) for x in range(w))]
    # Split the rows into bands of text separated by empty rows.
    bands = []
    for y in rows:
        if bands and y - bands[-1][1] <= 2:
            bands[-1][1] = y
        else:
            bands.append([y, y])
    top, bottom = bands[0][0], bands[1][1]
    cols = [x for x in range(w) if any(is_text(x, y) for y in range(top, bottom + 1))]
    pad = 6
    return (max(0, cols[0] - pad), max(0, top - pad), min(w, cols[-1] + pad + 1), min(h, bottom + pad + 1))


def write_previews(platforms):
    previews = {}
    for platform in platforms:
        d = SHOTS / platform
        if not d.is_dir():
            continue
        p = {}
        for face in faces_for(platform):
            shot = d / ('%s-default.png' % face)
            if shot.exists():
                p[face] = png_data_uri(Image.open(shot))
        if platform in LARGE:
            shots = {v: d / ('digital-%s.png' % f) for f, v in FONTS.items() if (d / ('digital-%s.png' % f)).exists()}
            if shots:
                # One tile size that fits every font's text, so the choices are
                # the same size on the page and show how big each font really
                # is. Each tile holds only its own date and time, in place, on
                # black (a smaller font's tile would otherwise catch the edge
                # of the day bar or the normal time).
                boxes = {v: text_box(shot, platform in ROUND) for v, shot in shots.items()}
                box = (min(b[0] for b in boxes.values()), min(b[1] for b in boxes.values()),
                       max(b[2] for b in boxes.values()), max(b[3] for b in boxes.values()))
                for value, shot in shots.items():
                    own = boxes[value]
                    tile = Image.new('RGB', (box[2] - box[0], box[3] - box[1]), (0, 0, 0))
                    tile.paste(Image.open(shot).convert('RGB').crop(own), (own[0] - box[0], own[1] - box[1]))
                    p['font-%d' % value] = png_data_uri(tile)
        previews[platform] = p
    body = json.dumps(previews, indent=1, sort_keys=True)
    PREVIEWS_JS.write_text(
        '// Generated by tools/screenshots.py from screenshots/. Do not edit by hand.\n'
        '// Example pictures for the settings page, per watch platform: each face in\n'
        '// the default font, and "font-N" (date and time) for each font.\n'
        'module.exports = ' + body + ';\n')
    print('wrote %s (%d KB)' % (PREVIEWS_JS.relative_to(ROOT), PREVIEWS_JS.stat().st_size // 1024))


def main():
    package = json.loads((ROOT / 'package.json').read_text())
    all_platforms = package['pebble']['targetPlatforms']
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    platforms = args or all_platforms

    if '--previews-only' not in sys.argv:
        appinfo = json.loads((ROOT / 'build' / 'appinfo.json').read_text())
        keys = appinfo['appKeys']
        # Install a copy, so a `pebble build` while this runs can't swap the app mid-way.
        with tempfile.TemporaryDirectory() as tmp:
            pbw = Path(tmp) / 'app.pbw'
            shutil.copy(next((ROOT / 'build').glob('*.pbw')), pbw)
            for platform in platforms:
                capture(platform, pbw, keys)

    write_previews(all_platforms)


if __name__ == '__main__':
    main()
