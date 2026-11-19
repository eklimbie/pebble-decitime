/*
 * Decimal Time — a French Revolutionary time watch face for Pebble.
 *
 *   1 day            = 10 decimal hours
 *   1 decimal hour   = 100 decimal minutes  (= 2 h 24 min)
 *   1 decimal minute = 100 decimal seconds  (= 86.4 s)
 *   1 decimal second = 0.864 s
 *
 * Midnight is 0:00:00, noon is 5:00:00, 18:00 is 7:50:00.
 * Decimal time is derived from local wall-clock time, so it follows your
 * time zone and jumps with DST exactly like the normal clock does.
 *
 * Styles:
 *   analog  - a ten-hour dial, 10 at the top. The hour hand turns once a day,
 *             the minute hand once per decimal hour, the second hand once
 *             per decimal minute.
 *   digital - date, H:MM(:SS), a 10-cell "day bar" (one cell per decimal
 *             hour; a ring on round watches) and the ordinary time.
 *
 * Everything is configurable from the phone (gear icon in the Pebble app):
 * style, seconds / normal time / date (never, on wrist flick, always),
 * accent colour, and whether the digital display shows hour 0 as 0 or 10.
 *
 * Platforms: aplite, basalt, chalk, diorite, emery (Time 2), flint (2 Duo),
 * gabbro (Round 2).
 *
 * ---------------------------------------------------------------------------
 * HOW TO READ THIS FILE (for first-time C programmers)
 * ---------------------------------------------------------------------------
 * How a Pebble app runs:
 *   main() calls init(), then app_event_loop(), then deinit().
 *   init() creates the window and tells the system which of our functions to
 *   call when something happens (a "callback"): a timer fires, the minute
 *   changes, the wrist is flicked, the phone sends new settings.
 *   app_event_loop() sleeps until one of those events arrives, calls our
 *   callback, and goes back to sleep. It only returns when the user leaves
 *   the watch face. Sleeping is what saves battery, so the less often we
 *   wake up and redraw, the longer the battery lasts.
 *
 *   We never draw "directly". We call layer_mark_dirty() to say "this layer
 *   is out of date"; the system then calls canvas_update_proc(), which
 *   redraws the whole face from scratch using the current time and settings.
 *
 * C features you will see:
 *   #include <pebble.h>   pulls in the declarations of the Pebble SDK.
 *   #define NAME value    a constant (or small macro); the compiler pastes
 *                         `value` wherever NAME appears.
 *   #if / #else / #endif  compile-time choices. Code for other watch models
 *                         is not even compiled into this model's app.
 *   static                on a function or global variable: private to this
 *                         file. Static globals live as long as the app runs
 *                         and start out as zero.
 *   uint8_t, int32_t      integers with an exact size (8-bit unsigned,
 *                         32-bit signed). Memory on a watch is small, so the
 *                         size is chosen on purpose.
 *   123u                  the `u` makes the number unsigned, so the maths
 *                         stays in unsigned 32-bit integers.
 *   typedef struct {...}  a named bundle of fields, like a record.
 *   T *p                  p is a pointer: the address of a T, not a copy.
 *                         p->field reads a field through the pointer, &x
 *                         takes the address of x, *p is the thing p points to.
 *   (Type) { .a = 1 }     builds a struct value with named fields on the spot.
 *   char buf[8]           a fixed-size text buffer. Text in C ends with a
 *                         hidden '\0' byte, so it holds 7 characters at most.
 *                         We always pass sizeof(buf) to snprintf/strftime so
 *                         they can never write past the end (a classic C
 *                         security bug, the "buffer overflow").
 *
 * No floating point: the watch CPU has no hardware for it, so everything is
 * whole-number (integer) maths. Integer division rounds down: 7 / 2 == 3.
 */

#include <pebble.h>

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
// The numbers below must match the values the settings page sends
// (src/pkjs/config.js).

#define STYLE_DIGITAL 0
#define STYLE_ANALOG  1

#define SHOW_NEVER    0
#define SHOW_ON_FLICK 1  // visible for 100 decimal seconds after a wrist flick
#define SHOW_ALWAYS   2

#define ACCENT_WHITE  0
#define ACCENT_GREY   1
#define ACCENT_RED    2
#define ACCENT_BLUE   3
#define ACCENT_CUSTOM 4

#define FLICK_DURATION_S 87  // ~100 decimal seconds (86.4 s)

// Settings are saved on the watch's flash under this key. If the layout of
// the Settings struct ever changes, bump SETTINGS_VERSION: old saved data is
// then ignored instead of being misread.
#define SETTINGS_KEY     1
#define SETTINGS_VERSION 3

// Every field is one byte (uint8_t), so the struct is exactly 8 bytes with no
// hidden padding, and can be saved and loaded as a single block of bytes.
typedef struct {
  uint8_t version;
  uint8_t style;          // STYLE_*
  uint8_t seconds;        // SHOW_*
  uint8_t normal_time;    // SHOW_*
  uint8_t date;           // SHOW_*
  uint8_t accent;         // ACCENT_*
  uint8_t accent_custom;  // GColor8 .argb (one byte per colour)
  uint8_t hour_zero_ten;  // digital only: show hour 0 as "10"
} Settings;

static Settings s_settings;

static void settings_defaults(Settings *s) {
  *s = (Settings) {
    .version = SETTINGS_VERSION,
    .style = STYLE_ANALOG,
    .seconds = SHOW_ON_FLICK,
    .normal_time = SHOW_ON_FLICK,
    .date = SHOW_ALWAYS,
    .accent = ACCENT_GREY,
    .accent_custom = GColorVividCeruleanARGB8,
    .hour_zero_ten = 0,
  };
}

// Start from the defaults and only use what is stored if it is complete and
// written by this version. persist_read_data() returns the number of bytes it
// read, or a negative error code if nothing is stored, so one size check
// covers "missing", "too short" and "wrong size".
static void settings_load(void) {
  settings_defaults(&s_settings);
  Settings stored;
  if (persist_read_data(SETTINGS_KEY, &stored, sizeof(stored)) == (int)sizeof(stored) &&
      stored.version == SETTINGS_VERSION) {
    s_settings = stored;
  }
}

static void settings_save(void) {
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
// Presets picked from the 64-colour Pebble palette to sit close to the
// Pebble Time 2 strap colours (white, grey, red, blue).
// PBL_IF_COLOR_ELSE(a, b) is `a` on colour watches and `b` on black & white.

#define COLOR_BG    GColorBlack
#define COLOR_MAIN  GColorWhite
#define COLOR_SUB   PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COLOR_TRACK PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define COLOR_TICK  PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)

static GColor accent_color(void) {
#if defined(PBL_COLOR)
  switch (s_settings.accent) {
    case ACCENT_WHITE: return GColorWhite;
    case ACCENT_RED:   return GColorRed;
    case ACCENT_BLUE:  return GColorBlueMoon;
    case ACCENT_CUSTOM: {
      GColor c = (GColor) { .argb = s_settings.accent_custom };
      // Black would vanish on the black background.
      return gcolor_equal(c, GColorBlack) ? GColorWhite : c;
    }
    case ACCENT_GREY:
    default:           return GColorLightGray;
  }
#else
  return GColorWhite;
#endif
}

// ---------------------------------------------------------------------------
// Time maths
// ---------------------------------------------------------------------------
// A day has 86 400 000 ms and 100 000 decimal seconds, so one decimal second
// is exactly 864 ms. Working in whole milliseconds keeps every step exact.

#define MS_PER_DAY        86400000u
#define MS_PER_DEC_SECOND 864u     // 86 400 000 ms / 100 000
#define MS_PER_DEC_MINUTE 86400u   // 100 decimal seconds

typedef struct {
  uint32_t ds;  // decimal seconds since local midnight, 0 .. 99 999
  int h;        // 0 .. 9
  int m;        // 0 .. 99
  int s;        // 0 .. 99
} DecimalTime;

// Milliseconds since local midnight. If out_tm is not NULL, the broken-down
// local time (hour, minute, weekday, ...) is copied into *out_tm as well.
static uint32_t local_ms_of_day(struct tm *out_tm) {
  time_t t;
  uint16_t ms;
  time_ms(&t, &ms);  // SDK 3+: t is UTC seconds, ms is the 0..999 remainder
  // localtime() applies the watch's time zone. It returns a pointer to one
  // shared struct that the next call overwrites, so we copy it out. On Pebble
  // it never returns NULL.
  struct tm *lt = localtime(&t);
  if (out_tm) {
    *out_tm = *lt;
  }
  uint32_t secs = (uint32_t)lt->tm_hour * 3600u + (uint32_t)lt->tm_min * 60u + (uint32_t)lt->tm_sec;
  uint32_t total = secs * 1000u + ms;
  return total < MS_PER_DAY ? total : MS_PER_DAY - 1;  // guard against tm_sec == 60 (leap second)
}

// Split "decimal seconds since midnight" into H:MM:SS with / (divide) and
// % (remainder): 54 321 -> h 5, m 43, s 21.
static DecimalTime to_decimal(uint32_t ms_of_day) {
  DecimalTime d;
  d.ds = ms_of_day / MS_PER_DEC_SECOND;
  d.h = (int)(d.ds / 10000u);
  d.m = (int)((d.ds / 100u) % 100u);
  d.s = (int)(d.ds % 100u);
  return d;
}

// ---------------------------------------------------------------------------
// Visibility (never / on wrist flick / always)
// ---------------------------------------------------------------------------

static time_t s_flick_until;  // wall-clock second when the flick window ends

static bool flick_active(void) {
  return time(NULL) < s_flick_until;
}

static bool is_visible(uint8_t mode) {
  return mode == SHOW_ALWAYS || (mode == SHOW_ON_FLICK && flick_active());
}

static bool uses_flick(void) {
  return s_settings.seconds == SHOW_ON_FLICK || s_settings.normal_time == SHOW_ON_FLICK ||
         s_settings.date == SHOW_ON_FLICK;
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

// Metrics measured from the system .pbf files: where the digits' ink starts
// below the top of the text box, line height, widest digit, colon advance.
typedef struct {
  const char *key;
  int8_t ink_top;
  uint8_t line_h;
  uint8_t digit_w;
  uint8_t colon_w;
} BigFont;

// Largest first: pick_big_font() takes the first one that fits.
static const BigFont BIG_FONTS[] = {
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  { FONT_KEY_LECO_60_NUMBERS_AM_PM, 18, 60, 37, 16 },
#endif
  { FONT_KEY_LECO_42_NUMBERS,       13, 42, 26, 12 },
  { FONT_KEY_LECO_38_BOLD_NUMBERS,  11, 38, 24, 11 },
  { FONT_KEY_LECO_36_BOLD_NUMBERS,  11, 36, 23, 10 },
  { FONT_KEY_LECO_32_BOLD_NUMBERS,  10, 32, 20,  9 },
  { FONT_KEY_LECO_28_LIGHT_NUMBERS,  8, 28, 18,  8 },
  { FONT_KEY_LECO_20_BOLD_NUMBERS,   6, 20, 13,  6 },
};
// Number of entries: total size of the array / size of one entry.
#define NUM_BIG_FONTS ((int)(sizeof(BIG_FONTS) / sizeof(BIG_FONTS[0])))

typedef struct {
  const char *key;
  int8_t ink_top;
  uint8_t line_h;
} SmallFont;

static const SmallFont SMALL_FONT    = { FONT_KEY_GOTHIC_18,      7, 18 };
static const SmallFont SMALL_FONT_XL = { FONT_KEY_GOTHIC_24_BOLD, 10, 24 };

// Dial numerals
static const BigFont NUMERAL_FONT    = { FONT_KEY_LECO_20_BOLD_NUMBERS,  6, 20, 13, 6 };
static const BigFont NUMERAL_FONT_XL = { FONT_KEY_LECO_28_LIGHT_NUMBERS, 8, 28, 18, 8 };

// Width of "H:MM" / "H:MM:SS" with 1 or 2 hour digits.
static int big_text_width(const BigFont *f, int hour_digits, bool with_seconds) {
  int digits = hour_digits + 2 + (with_seconds ? 2 : 0);
  int colons = with_seconds ? 2 : 1;
  return digits * f->digit_w + colons * f->colon_w;
}

static const BigFont *pick_big_font(int max_width, int hour_digits, bool with_seconds) {
  for (int i = 0; i < NUM_BIG_FONTS; i++) {
    if (big_text_width(&BIG_FONTS[i], hour_digits, with_seconds) <= max_width) {
      return &BIG_FONTS[i];
    }
  }
  return &BIG_FONTS[NUM_BIG_FONTS - 1];  // nothing fits: use the smallest
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
// These are created in init()/window_load() and destroyed in
// window_unload()/deinit(). The callbacks below only run in between, so the
// pointers are always valid when they are used.

static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_timer;     // NULL when no timer is pending
static bool s_tap_subscribed;

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
// GRect(x, y, w, h) is a rectangle; GPoint(x, y) a point. y grows downwards.
// GContext *ctx is the drawing context the system hands to the update proc:
// we set colours on it, then draw shapes and text with it.

// Draw text so that its ink (not its padded text box) starts at ink_y.
static void draw_small(GContext *ctx, const char *text, const SmallFont *f, int x, int w, int ink_y) {
  graphics_draw_text(ctx, text, fonts_get_system_font(f->key),
                     GRect(x, ink_y - f->ink_top, w, f->line_h + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Fixed-width cells per character so digits don't jiggle as they change.
static void draw_big(GContext *ctx, const char *text, const BigFont *f, int center_x, int ink_y) {
  // A C string is a run of chars ending in '\0'; `*c` is false at the end.
  int total = 0;
  for (const char *c = text; *c; c++) {
    total += (*c == ':') ? f->colon_w : f->digit_w;
  }
  GFont font = fonts_get_system_font(f->key);
  int x = center_x - total / 2;
  char glyph[2] = { 0, 0 };  // one character + the terminating '\0'
  for (const char *c = text; *c; c++) {
    int cell = (*c == ':') ? f->colon_w : f->digit_w;
    glyph[0] = *c;
    // Box a little wider than the cell so the glyph never wraps; centred on the cell.
    graphics_draw_text(ctx, glyph, font, GRect(x - 4, ink_y - f->ink_top, cell + 8, f->line_h + 4),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    x += cell;
  }
}

#if !defined(PBL_ROUND)
// Rectangular screens: ten cells, one per decimal hour; the current one fills up.
static void draw_day_bar(GContext *ctx, GRect bar, uint32_t ds, GColor accent) {
  const int gap = 2;
  int cell_w = (bar.size.w - 9 * gap) / 10;
  int total_w = cell_w * 10 + gap * 9;
  int x0 = bar.origin.x + (bar.size.w - total_w) / 2;
  int full = (int)(ds / 10000u);                                    // finished hours
  int partial = (int)((ds % 10000u) * (uint32_t)cell_w / 10000u);   // pixels of the current one
  // Black & white has no grey track, and an accent as dark as the track
  // would disappear: draw cell outlines instead.
  const bool outline = PBL_IF_BW_ELSE(true, gcolor_equal(accent, COLOR_TRACK));
  if (outline) {
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, COLOR_MAIN));
    graphics_context_set_stroke_width(ctx, 1);
  }

  for (int i = 0; i < 10; i++) {
    GRect cell = GRect(x0 + i * (cell_w + gap), bar.origin.y, cell_w, bar.size.h);
    if (outline) {
      graphics_draw_rect(ctx, cell);
    } else {
      graphics_context_set_fill_color(ctx, COLOR_TRACK);
      graphics_fill_rect(ctx, cell, 0, GCornerNone);
    }
    int fill = (i < full) ? cell_w : (i == full ? partial : 0);
    if (fill > 0) {
      graphics_context_set_fill_color(ctx, accent);
      graphics_fill_rect(ctx, GRect(cell.origin.x, cell.origin.y, fill, cell.size.h), 0, GCornerNone);
    }
  }
}

#else
// Round screens: the same idea as a ring around the edge, midnight at the top.
// Angles are fixed-point: TRIG_MAX_ANGLE (65 536) is one full turn.
static void draw_day_ring(GContext *ctx, GRect bounds, uint32_t ds, uint16_t thickness, GColor accent) {
  GRect r = grect_inset(bounds, GEdgeInsets(2));
  // ds / 100 000 of a turn. 65 536 / 100 000 reduces to 4096 / 6250, which
  // keeps ds * 4096 (at most ~410 million) inside 32 bits.
  int32_t end = (int32_t)(ds * 4096u / 6250u);
  GColor track = gcolor_equal(accent, COLOR_TRACK) ? GColorLightGray : COLOR_TRACK;

  graphics_context_set_fill_color(ctx, track);
  graphics_fill_radial(ctx, r, GOvalScaleModeFitCircle, thickness, 0, TRIG_MAX_ANGLE);
  if (end > 0) {
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_radial(ctx, r, GOvalScaleModeFitCircle, thickness, 0, end);
  }

  // Cut small gaps at each decimal hour by painting background over the ring.
  const int32_t half_gap = DEG_TO_TRIGANGLE(1);
  graphics_context_set_fill_color(ctx, COLOR_BG);
  // The midnight gap straddles angle 0, so it is drawn as two slivers.
  graphics_fill_radial(ctx, r, GOvalScaleModeFitCircle, thickness, 0, half_gap);
  graphics_fill_radial(ctx, r, GOvalScaleModeFitCircle, thickness, TRIG_MAX_ANGLE - half_gap, TRIG_MAX_ANGLE);
  for (int i = 1; i < 10; i++) {
    int32_t a = (int32_t)i * TRIG_MAX_ANGLE / 10;
    graphics_fill_radial(ctx, r, GOvalScaleModeFitCircle, thickness, a - half_gap, a + half_gap);
  }
}
#endif

// Ordinary time as "22:39" or "10:39"; returns a pointer into buf.
static const char *format_normal_time(char *buf, size_t size, const struct tm *lt) {
  const bool h24 = clock_is_24h_style();
  strftime(buf, size, h24 ? "%H:%M" : "%I:%M", lt);
  // 12-hour "09:41" -> "9:41": skip the first character by returning buf + 1.
  return (!h24 && buf[0] == '0') ? buf + 1 : buf;
}

// What to draw this frame.
typedef struct {
  bool seconds;
  bool normal_time;
  bool date;
  GColor accent;
} Frame;

// ---------------------------------------------------------------------------
// Analog dial
// ---------------------------------------------------------------------------
//
// Like the clocks of 1793: ten hours around the dial with 10 at the top.
//   hour hand    one turn per day          (100 000 decimal seconds)
//   minute hand  one turn per decimal hour (10 000 decimal seconds, 2 h 24 min)
//   second hand  one turn per decimal minute (100 decimal seconds, 86.4 s)
// The 100 ticks are decimal minutes; every tenth is an hour.

// The point `radius` pixels from c in direction `angle` (0 = straight up,
// clockwise). sin_lookup()/cos_lookup() return -TRIG_MAX_RATIO..TRIG_MAX_RATIO
// for -1..1, so multiply first and divide by TRIG_MAX_RATIO last.
static GPoint polar(GPoint c, int32_t angle, int radius) {
  return GPoint(c.x + (int16_t)(sin_lookup(angle) * radius / TRIG_MAX_RATIO),
                c.y - (int16_t)(cos_lookup(angle) * radius / TRIG_MAX_RATIO));
}

static void draw_hand(GContext *ctx, GPoint c, int32_t angle, int length, int tail, uint8_t width, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  graphics_draw_line(ctx, polar(c, angle + TRIG_MAX_ANGLE / 2, tail), polar(c, angle, length));
}

// Hour labels clockwise from the top: ten texts of up to 2 characters + '\0'.
// Constant text, so nothing needs formatting each frame.
static const char DIAL_LABELS[10][3] = { "10", "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static void draw_analog(GContext *ctx, GRect b, DecimalTime d, const struct tm *lt, Frame f) {
  // Fit the dial into the visible area (it shrinks for a Timeline Quick View).
  const int size = b.size.w < b.size.h ? b.size.w : b.size.h;
  const int r = size / 2 - 2;
  const GPoint c = GPoint(b.origin.x + b.size.w / 2, b.origin.y + b.size.h / 2);
  const bool xl = r >= 95;  // Time 2 and Round 2 sized dials

  // Ticks: 100 decimal minutes, longer every 5, an hour mark (accent) every 10.
  const int hour_len = r / 9;
  for (int i = 0; i < 100; i++) {
    int32_t a = (int32_t)i * TRIG_MAX_ANGLE / 100;
    int len;
    uint8_t width;
    GColor color;
    if (i % 10 == 0) {
      len = hour_len; width = xl ? 4 : 3; color = f.accent;
    } else if (i % 5 == 0) {
      len = r / 16; width = xl ? 2 : 1; color = COLOR_SUB;
    } else {
      len = xl ? 4 : 2; width = 1; color = COLOR_TICK;
    }
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, width);
    graphics_draw_line(ctx, polar(c, a, r - len), polar(c, a, r));
  }

  // Numerals 10, 1 .. 9, centred on a circle just inside the hour marks.
  const BigFont *nf = xl ? &NUMERAL_FONT_XL : &NUMERAL_FONT;
  const int nf_ink = nf->line_h - nf->ink_top;
  const int nr = r - hour_len - 5 - nf_ink / 2;
  GFont numeral_font = fonts_get_system_font(nf->key);
  graphics_context_set_text_color(ctx, COLOR_MAIN);
  for (int h = 0; h < 10; h++) {
    GPoint p = polar(c, (int32_t)h * TRIG_MAX_ANGLE / 10, nr);
    graphics_draw_text(ctx, DIAL_LABELS[h], numeral_font,
                       GRect(p.x - 30, p.y - nf_ink / 2 - nf->ink_top, 60, nf->line_h + 4),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }

  // Date above the centre, normal time below, if there is room
  // (not on the smallest dials, e.g. 144 px wide during a Quick View).
  if (r >= 62) {
    const SmallFont *small = xl ? &SMALL_FONT_XL : &SMALL_FONT;
    const int small_ink = small->line_h - small->ink_top;
    graphics_context_set_text_color(ctx, COLOR_SUB);
    if (f.date) {
      char date[24];
      strftime(date, sizeof(date), "%a %d", lt);
      draw_small(ctx, date, small, c.x - r, 2 * r, c.y - r * 2 / 5 - small_ink / 2);
    }
    if (f.normal_time) {
      char normal[8];
      draw_small(ctx, format_normal_time(normal, sizeof(normal), lt), small,
                 c.x - r, 2 * r, c.y + r * 2 / 5 - small_ink / 2);
    }
  }

  // Hands. Same fixed-point trick as the day ring: reduce the fraction so the
  // multiplication cannot overflow 32 bits.
  const int32_t hour_angle = (int32_t)(d.ds * 4096u / 6250u);              // ds / 100 000 of a turn
  const int32_t minute_angle = (int32_t)((d.ds % 10000u) * 4096u / 625u);  // (ds % 10 000) / 10 000
  draw_hand(ctx, c, hour_angle, r * 50 / 100, 0, xl ? 8 : 6, COLOR_MAIN);
  draw_hand(ctx, c, minute_angle, r - hour_len - 2, 0, xl ? 5 : 4, COLOR_MAIN);
  if (f.seconds) {
    const int32_t second_angle = (int32_t)(d.s * TRIG_MAX_ANGLE / 100);
    draw_hand(ctx, c, second_angle, r - 3, r / 5, xl ? 2 : 1, f.accent);
  }

  // Centre cap; its dot takes the accent colour while the second hand shows.
  graphics_context_set_fill_color(ctx, COLOR_MAIN);
  graphics_fill_circle(ctx, c, xl ? 7 : 5);
  graphics_context_set_fill_color(ctx, f.seconds ? f.accent : COLOR_BG);
  graphics_fill_circle(ctx, c, xl ? 3 : 2);
}

// ---------------------------------------------------------------------------
// Digital layout
// ---------------------------------------------------------------------------

// full: the whole screen. b: the part not covered by a Timeline Quick View.
static void draw_digital(GContext *ctx, GRect full, GRect b, DecimalTime d, const struct tm *lt, Frame f) {
  const bool xl = full.size.w >= 200;
  const SmallFont *small = xl ? &SMALL_FONT_XL : &SMALL_FONT;
  const int small_ink = small->line_h - small->ink_top;
  const bool ten = s_settings.hour_zero_ten;
  const int hour = (ten && d.h == 0) ? 10 : d.h;

#if defined(PBL_ROUND)
  const int ring = xl ? 10 : 7;
  draw_day_ring(ctx, full, d.ds, ring, f.accent);
  const int avail_w = full.size.w - 2 * (ring + 8) - 12;
  const int bar_h = 0;  // the ring replaces the bar
#else
  const int margin = 4;
  const int avail_w = b.size.w - 2 * margin;
  const int bar_h = xl ? 12 : 8;
#endif

  // Size for two hour digits all day when "10" is on, so the font stays put.
  const BigFont *big = pick_big_font(avail_w, ten ? 2 : 1, f.seconds);
  const int big_ink = big->line_h - big->ink_top;
  const int gap = xl ? 12 : 8;

  // Stack: date / decimal time / day bar / normal time, centred vertically.
  // First add up the height of what will be shown, then draw top to bottom.
  bool show_date = f.date, show_normal = f.normal_time;
  int core = big_ink + (bar_h ? gap + bar_h : 0);
  int block = core + (show_date ? small_ink + gap : 0) + (show_normal ? gap + small_ink : 0);
  if (block > b.size.h - 8) {  // not enough room (Quick View): decimal time only
    show_date = show_normal = false;
    block = core;
  }
  int y = b.origin.y + (b.size.h - block) / 2;
  const int cx = b.origin.x + b.size.w / 2;

  if (show_date) {
    char date[24];
    strftime(date, sizeof(date), "%a %d %b", lt);
    graphics_context_set_text_color(ctx, COLOR_SUB);
    draw_small(ctx, date, small, b.origin.x, b.size.w, y);
    y += small_ink + gap;
  }

  char main_text[12];  // longest is "10:99:99" = 8 characters + '\0'
  if (f.seconds) {
    snprintf(main_text, sizeof(main_text), "%d:%02d:%02d", hour, d.m, d.s);
  } else {
    snprintf(main_text, sizeof(main_text), "%d:%02d", hour, d.m);
  }
  graphics_context_set_text_color(ctx, COLOR_MAIN);
  draw_big(ctx, main_text, big, cx, y);
  y += big_ink;

#if !defined(PBL_ROUND)
  y += gap;
  draw_day_bar(ctx, GRect(b.origin.x + margin, y, avail_w, bar_h), d.ds, f.accent);
  y += bar_h;
#endif

  if (show_normal) {
    y += gap;
    char normal[8];
    graphics_context_set_text_color(ctx, COLOR_SUB);
    draw_small(ctx, format_normal_time(normal, sizeof(normal), lt), small, b.origin.x, b.size.w, y);
  }
}

// Called by the system whenever s_canvas has been marked dirty. The window
// has already painted its black background (window_set_background_color in
// init), so we only draw the face on top of it.
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect full = layer_get_bounds(layer);
  GRect b = layer_get_unobstructed_bounds(layer);  // shrinks when a Timeline Quick View is shown

  struct tm lt;
  DecimalTime d = to_decimal(local_ms_of_day(&lt));
  Frame f = {
    .seconds = is_visible(s_settings.seconds),
    .normal_time = is_visible(s_settings.normal_time),
    .date = is_visible(s_settings.date),
    .accent = accent_color(),
  };

  if (s_settings.style == STYLE_DIGITAL) {
    draw_digital(ctx, full, b, d, &lt, f);
  } else {
    draw_analog(ctx, b, d, &lt, f);
  }
}

// ---------------------------------------------------------------------------
// Timing: wake exactly on decimal-second (or decimal-minute) boundaries
// ---------------------------------------------------------------------------
// The system's tick service only knows normal seconds and minutes, so a
// one-shot AppTimer is set to fire right after the next decimal boundary.
// Each time it fires it redraws and sets the next one. Redrawing once per
// decimal minute (instead of every decimal second) is what makes "seconds:
// on flick" cheap on the battery.

static void schedule_next(void);  // declared here, defined below: the two call each other

static void timer_callback(void *data) {
  s_timer = NULL;  // a fired timer is gone; don't reschedule it
  layer_mark_dirty(s_canvas);
  schedule_next();
}

static void schedule_next(void) {
  uint32_t ms = local_ms_of_day(NULL);
  uint32_t period = is_visible(s_settings.seconds) ? MS_PER_DEC_SECOND : MS_PER_DEC_MINUTE;
  uint32_t delay = period - (ms % period) + 5;  // +5 ms so we land just past the boundary

  // Also wake when a wrist-flick window closes, so those items hide on time.
  if (flick_active()) {
    time_t now;
    uint16_t now_ms;
    time_ms(&now, &now_ms);
    int32_t left = (int32_t)(s_flick_until - now) * 1000 - now_ms;
    if (left >= 0 && (uint32_t)left + 5 < delay) {
      delay = (uint32_t)left + 5;
    }
  }

  // Move the pending timer if there is one, otherwise create a new one.
  if (s_timer && app_timer_reschedule(s_timer, delay)) {
    return;
  }
  s_timer = app_timer_register(delay, timer_callback, NULL);
}

// Normal minute tick. The decimal timer above already redraws everything
// decimal, so the face only needs redrawing here when the ordinary time is
// on screen. The tick also re-syncs the decimal timer, which is how the face
// catches up after a time-zone change or the clock being set.
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (is_visible(s_settings.normal_time)) {
    layer_mark_dirty(s_canvas);
  }
  schedule_next();
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  s_flick_until = time(NULL) + FLICK_DURATION_S;
  layer_mark_dirty(s_canvas);
  schedule_next();
}

// Only listen for wrist flicks when a setting uses them, so the app is not
// woken up for flicks it would ignore.
static void update_tap_subscription(void) {
  bool want = uses_flick();
  if (want && !s_tap_subscribed) {
    accel_tap_service_subscribe(tap_handler);
  } else if (!want && s_tap_subscribed) {
    accel_tap_service_unsubscribe();
    s_flick_until = 0;
  }
  s_tap_subscribed = want;
}

// Timeline Quick View (a notification peeking up from the bottom) does not
// exist on aplite: its SDK is frozen at an older version and only has
// do-nothing stand-ins for this service, so the code is left out there.
// (A plain platform check: the SDK's PBL_API_EXISTS() macro triggers a
// "use of defined may not be portable" compiler warning.)
#if !defined(PBL_PLATFORM_APLITE)
// Called repeatedly while a Quick View slides in or out, so the layout
// follows the animation.
static void unobstructed_change(AnimationProgress progress, void *context) {
  layer_mark_dirty(s_canvas);
}
#endif

// ---------------------------------------------------------------------------
// Settings from the phone (Clay config page)
// ---------------------------------------------------------------------------
// When you press Save on the settings page, the phone sends one message with
// every setting. Each value arrives as a Tuple: a key, a type tag, a length
// in bytes and the value itself.
//
// Treat this as untrusted input: any phone-side code that addresses this
// app's UUID can send messages, not just our own settings page. So every
// value is type-checked and range-checked before it is used, and anything
// unexpected is ignored (the old setting stays).

// Read an integer tuple. config.js sends every setting as an integer
// (serializeValueAs: 'integer'; colours are numbers already), so strings and
// byte arrays are rejected, and only the integer sizes the format defines
// (1, 2 or 4 bytes) are read.
static bool tuple_int(const Tuple *t, int32_t *out) {
  if (t->type == TUPLE_INT) {
    switch (t->length) {
      case 1: *out = t->value->int8;  return true;
      case 2: *out = t->value->int16; return true;
      case 4: *out = t->value->int32; return true;
    }
  } else if (t->type == TUPLE_UINT) {
    switch (t->length) {
      case 1: *out = t->value->uint8;  return true;
      case 2: *out = t->value->uint16; return true;
      case 4: *out = (int32_t)t->value->uint32; return true;
    }
  }
  return false;
}

// Copy a setting into *dst only if it is present, an integer, and 0..max.
static void read_choice(DictionaryIterator *iter, uint32_t key, uint8_t *dst, int32_t max) {
  Tuple *t = dict_find(iter, key);  // NULL if the message has no such key
  int32_t v;
  if (t && tuple_int(t, &v) && v >= 0 && v <= max) {
    *dst = (uint8_t)v;
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  // MESSAGE_KEY_* constants are generated from "messageKeys" in package.json.
  read_choice(iter, MESSAGE_KEY_Style, &s_settings.style, STYLE_ANALOG);
  read_choice(iter, MESSAGE_KEY_HourZero, &s_settings.hour_zero_ten, 1);
  read_choice(iter, MESSAGE_KEY_Seconds, &s_settings.seconds, SHOW_ALWAYS);
  read_choice(iter, MESSAGE_KEY_NormalTime, &s_settings.normal_time, SHOW_ALWAYS);
  read_choice(iter, MESSAGE_KEY_Date, &s_settings.date, SHOW_ALWAYS);
  read_choice(iter, MESSAGE_KEY_Accent, &s_settings.accent, ACCENT_CUSTOM);

  // The colour picker sends 0xRRGGBB. GColorFromHEX keeps only the bits the
  // 64-colour palette can show, so any number gives a valid, opaque colour.
  Tuple *t = dict_find(iter, MESSAGE_KEY_AccentCustom);
  int32_t hex;
  if (t && tuple_int(t, &hex)) {
    s_settings.accent_custom = GColorFromHEX(hex).argb;
  }

  settings_save();
  update_tap_subscription();
  layer_mark_dirty(s_canvas);
  schedule_next();  // the redraw rate depends on the seconds setting
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

// The window calls this when it is pushed onto the screen: create its layers.
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
}

// ...and this when it is destroyed: free what window_load created.
static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  settings_load();

  s_window = window_create();
  // The window paints this colour behind its layers on every redraw.
  window_set_background_color(s_window, COLOR_BG);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  // An app's first window is never animated in, so window_load() runs during
  // this call and s_canvas exists from here on.
  window_stack_push(s_window, true);

  // Register the handler before opening, so no message can arrive unhandled.
  // Buffer sizes in bytes: a full settings message is under 100 bytes, and
  // the watch never sends anything, so the outbox can stay tiny.
  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  update_tap_subscription();
#if !defined(PBL_PLATFORM_APLITE)
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers) {
    .change = unobstructed_change,
  }, NULL);
#endif
  schedule_next();
}

// Undo init() in reverse: stop every source of callbacks first, then destroy
// the window they would draw into. (The system would clean up after us on
// exit anyway, but tidy shutdown makes leaks easy to spot.)
static void deinit(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  tick_timer_service_unsubscribe();
  if (s_tap_subscribed) {
    accel_tap_service_unsubscribe();
  }
#if !defined(PBL_PLATFORM_APLITE)
  unobstructed_area_service_unsubscribe();
#endif
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();  // returns when the user leaves the watch face
  deinit();
  return 0;
}