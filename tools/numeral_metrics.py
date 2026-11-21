#!/usr/bin/env python3
"""Measure the dial numerals and write their placement into decimal_time.c.

The analog dial places each numeral by its ink, not by its text box: centred
on the line from the dial centre through its hour mark, with the same space
between the numeral's outline and the mark all round. The SDK can't tell
where a glyph's ink is, so this script draws the numerals of every dial font
in the emulator (with a small throwaway app), measures them from screenshots
and rewrites the generated block in src/c/decimal_time.c.

Run from the project root, then `pebble build`:

    python3 tools/numeral_metrics.py

Only needed when a dial font, the hour marks or the spacing change. Needs
pebble-tool and Pillow; uses the Pebble Time 2 (emery) emulator.
"""

import json
import math
import shutil
import subprocess
import tempfile
import uuid
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
C_FILE = ROOT / 'src' / 'c' / 'decimal_time.c'
BEGIN = '// BEGIN numeral placement'
END = '// END numeral placement'

LABELS = ['10', '1', '2', '3', '4', '5', '6', '7', '8', '9']   # DIAL_LABELS order, 36 deg apart

# One table per dial font and dial size. The hour mark must match draw_analog():
# 4 px wide on large dials (Time 2, Round 2), 3 px on small ones; its length
# (r / 9) only matters at the margins, so a typical one is used.
#   name in C, font key, FontMetrics in C, mark width, mark length, space (px), large screens only
SETS = [
    ('LECO_20', 'LECO_20_BOLD_NUMBERS', 'F_LECO_20_BOLD', 3, 8, 3, False),
    ('LECO_28', 'LECO_28_LIGHT_NUMBERS', 'F_LECO_28_LIGHT', 4, 11, 4, False),
    ('GOTHIC_24', 'GOTHIC_24_BOLD', 'F_GOTHIC_24_BOLD', 3, 8, 3, True),
    ('GOTHIC_28', 'GOTHIC_28_BOLD', 'F_GOTHIC_28_BOLD', 4, 11, 4, True),
    ('ROBOTO_21_S', 'ROBOTO_CONDENSED_21', 'F_ROBOTO_21', 3, 8, 3, True),
    ('ROBOTO_21_L', 'ROBOTO_CONDENSED_21', 'F_ROBOTO_21', 4, 11, 4, True),
]

# The throwaway app: the ten labels in a 5 x 2 grid, each left-aligned with
# its text box at (col * 40 + 2, row * 60 + 10).
PROBE_C = r'''#include <pebble.h>
static const char *LABELS[10] = { "10", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
static Window *s_window;
static void update(Layer *layer, GContext *ctx) {
  graphics_context_set_text_color(ctx, GColorWhite);
  GFont f = fonts_get_system_font(FONT_KEY_%s);
  for (int i = 0; i < 10; i++) {
    graphics_draw_text(ctx, LABELS[i], f, GRect((i %% 5) * 40 + 2, (i / 5) * 60 + 10, 38, 50),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}
static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  Layer *l = layer_create(layer_get_bounds(root));
  layer_set_update_proc(l, update);
  layer_add_child(root, l);
}
int main(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) { .load = load });
  window_stack_push(s_window, true);
  app_event_loop();
}
'''


def pebble(args, cwd):
    subprocess.run(['pebble', *args], cwd=cwd, check=True, capture_output=True, text=True)


def capture_glyphs(font, work):
    """Draw the labels in `font` on the emulator; return each label's ink pixels
    relative to its text box origin."""
    (work / 'src' / 'c' / 'main.c').write_text(PROBE_C % font)
    pebble(['build'], work)
    pebble(['install', '--emulator', 'emery'], work)
    shot = work / ('%s.png' % font)
    pebble(['screenshot', '--no-open', '--no-correction', '--emulator', 'emery', str(shot)], work)
    px = Image.open(shot).convert('RGB').load()
    glyphs = []
    for i in range(10):
        bx, by = (i % 5) * 40 + 2, (i // 5) * 60 + 10
        glyphs.append([(x - bx, y - by) for y in range(by - 10, by + 50) for x in range(bx - 2, bx + 38)
                       if px[x, y] == (255, 255, 255)])
    return glyphs


def hull(points):
    """Convex hull (monotone chain)."""
    pts = sorted(set(points))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def seg_seg_dist(a, b, c, d):
    """Distance between segments ab and cd (which never cross here)."""
    def point_seg(p, q, r):
        length = (r[0] - q[0]) ** 2 + (r[1] - q[1]) ** 2
        t = 0 if length == 0 else max(0, min(1, ((p[0] - q[0]) * (r[0] - q[0]) + (p[1] - q[1]) * (r[1] - q[1])) / length))
        return math.hypot(p[0] - q[0] - t * (r[0] - q[0]), p[1] - q[1] - t * (r[1] - q[1]))
    return min(point_seg(a, c, d), point_seg(b, c, d), point_seg(c, a, b), point_seg(d, a, b))


def placement(ink, angle, width, length, space):
    """Ink centre (in half pixels, relative to the text box) and reach: how far
    inside the mark's inner end the ink centre goes so that `space` pixels
    separate the numeral's outline (the convex hull of its pixels, so that
    e.g. the open corner of a 7 counts as space) from the mark."""
    a = math.radians(angle)
    ux, uy = math.sin(a), -math.cos(a)
    xs = [x for x, _ in ink]
    ys = [y for _, y in ink]
    cx, cy = (min(xs) + max(xs) + 1) / 2, (min(ys) + max(ys) + 1) / 2
    outline = hull([(x + dx - cx, y + dy - cy) for x, y in ink for dx in (0, 1) for dy in (0, 1)])
    s = 0.0
    while True:
        mark = ((s * ux, s * uy), ((s + length) * ux, (s + length) * uy))   # the mark's centre line
        gap = min(seg_seg_dist(outline[k], outline[(k + 1) % len(outline)], *mark)
                  for k in range(len(outline))) - width / 2
        if gap >= space:
            return round(2 * cx), round(2 * cy), round(s)
        s += 0.25


def c_table(name, metrics, rows):
    col = lambda k: ', '.join('%2d' % r[k] for r in rows)
    return ('static const DialNumerals N_%s = {\n  &%s,\n  { %s },  // ink_x\n'
            '  { %s },  // ink_y\n  { %s },  // reach\n};\n' % (name, metrics, col(0), col(1), col(2)))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / 'src' / 'c').mkdir(parents=True)
        shutil.copy(ROOT / 'wscript', work)
        (work / 'package.json').write_text(json.dumps({
            'name': 'numeral-probe', 'author': 'tools', 'version': '1.0.0', 'private': True,
            'keywords': ['pebble-app'], 'dependencies': {},
            'pebble': {'displayName': 'Numeral probe', 'uuid': str(uuid.uuid4()), 'sdkVersion': '3',
                       'enableMultiJS': False, 'targetPlatforms': ['emery'],
                       'watchapp': {'watchface': False}, 'resources': {'media': []}}}))
        glyphs = {}
        for _, font, *_ in SETS:
            if font not in glyphs:
                print('measuring', font, flush=True)
                glyphs[font] = capture_glyphs(font, work)
        pebble(['kill'], work)

    small, large = [], []
    for name, font, metrics, width, length, space, large_only in SETS:
        rows = [placement(glyphs[font][i], i * 36, width, length, space) for i in range(10)]
        (large if large_only else small).append(c_table(name, metrics, rows))
    block = ''.join(small) + '#if LARGE_SCREEN\n' + ''.join(large) + '#endif\n'

    text = C_FILE.read_text()
    start = text.index(BEGIN)
    start = text.index('\n', start) + 1
    end = text.index(END)
    C_FILE.write_text(text[:start] + block + text[end:])
    print('updated', C_FILE.relative_to(ROOT))


if __name__ == '__main__':
    main()
