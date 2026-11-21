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
 *   analog  - a ten-hour dial, 10 at the top, round or (on rectangular
 *             watches) square. The hour hand turns once a day, the minute
 *             hand once per decimal hour, the second hand once per decimal
 *             minute.
 *   digital - date, H:MM(:SS) and the ordinary time, with the day's progress
 *             around the edge of the screen in ten sections, one per decimal
 *             hour: a frame on rectangular watches, a ring on round ones.
 *
 * Everything is configurable from the phone (gear icon in the Pebble app):
 * style, seconds / normal time / date (never, on wrist flick, always),
 * accent colour, whether the digital display shows hour 0 as 0 or 10, the
 * dial shape on rectangular watches, and on Pebble Time 2 and Round 2 the
 * font family for all text.
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
#define SHOW_ON_FLICK 1  // visible for 50 decimal seconds after a wrist flick
#define SHOW_ALWAYS   2

#define ACCENT_WHITE  0
#define ACCENT_GREY   1
#define ACCENT_RED    2
#define ACCENT_BLUE   3
#define ACCENT_CUSTOM 4

#define FLICK_DURATION_S 43  // ~50 decimal seconds (43.2 s)

#define FONT_DEFAULT 0  // LECO time and numerals, Gothic date and normal time
#define FONT_GOTHIC  1
#define FONT_ROBOTO  2

#define DIAL_ROUND  0
#define DIAL_SQUARE 1  // rectangular watches only

// Pebble Time 2 and Round 2 have the screen space for a choice of fonts (and
// the LECO 60 font). Other watches keep the default fonts.
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
#define LARGE_SCREEN 1
#define FONT_COUNT   3
#else
#define LARGE_SCREEN 0
#define FONT_COUNT   1
#endif

// Settings are saved on the watch's flash under this key. If the layout of
// the Settings struct ever changes, bump SETTINGS_VERSION: old saved data is
// then ignored instead of being misread. New fields go at the end, so data
// saved by an older version is a prefix of this struct (see settings_load).
#define SETTINGS_KEY     1
#define SETTINGS_VERSION 4
#define SETTINGS_V3_SIZE 8   // version 3 had the first eight fields below

// Every field is one byte (uint8_t), so the struct is exactly 10 bytes with
// no hidden padding, and can be saved and loaded as a single block of bytes.
typedef struct {
  uint8_t version;
  uint8_t style;          // STYLE_*
  uint8_t seconds;        // SHOW_*
  uint8_t normal_time;    // SHOW_*
  uint8_t date;           // SHOW_*
  uint8_t accent;         // ACCENT_*
  uint8_t accent_custom;  // GColor8 .argb (one byte per colour)
  uint8_t hour_zero_ten;  // digital only: show hour 0 as "10"
  // Added in version 4:
  uint8_t font;           // FONT_*
  uint8_t dial;           // DIAL_*
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
    .font = FONT_DEFAULT,
    .dial = DIAL_ROUND,
  };
}

// Start from the defaults and only use what is stored if it is complete and
// written by a version we know. persist_read_data() returns the number of
// bytes it read, or a negative error code if nothing is stored, so one size
// check covers "missing", "too short" and "wrong size".
static void settings_load(void) {
  settings_defaults(&s_settings);
  Settings stored;
  int n = persist_read_data(SETTINGS_KEY, &stored, sizeof(stored));
  if (n == (int)sizeof(stored) && stored.version == SETTINGS_VERSION) {
    s_settings = stored;
  } else if (n == SETTINGS_V3_SIZE && stored.version == 3) {
    // Saved before the font and dial settings existed: keep the eight old ones.
    memcpy(&s_settings, &stored, SETTINGS_V3_SIZE);
    s_settings.version = SETTINGS_VERSION;
  }
  if (s_settings.font >= FONT_COUNT) {
    s_settings.font = FONT_DEFAULT;  // would index past FAMILIES
  }
  if (s_settings.dial > DIAL_SQUARE) {
    s_settings.dial = DIAL_ROUND;
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
// Only the system fonts built into the watch are used, so the app ships no
// font files. A font family sets all text: the decimal time, the date, the
// normal time and the dial numerals. The default keeps the original look:
// LECO for the time and numerals, and Gothic for the date and normal time,
// because LECO has digits only.

// The LECO 60 font exists in Pebble Time 2 and Round 2 firmware only. A font
// key is just the resource name as text, so an older SDK header that lacks it
// can be filled in.
#if LARGE_SCREEN && !defined(FONT_KEY_LECO_60_NUMBERS_AM_PM)
#define FONT_KEY_LECO_60_NUMBERS_AM_PM "RESOURCE_ID_LECO_60_NUMBERS_AM_PM"
#endif

// Metrics measured from the system .pbf files (PebbleOS resources):
//   ink_top  where the digits' ink starts below the top of the text box
//   line_h   font height, which is also the height of the text box
//   digit_w  advance of the widest digit
//   colon_w  advance of ':'
//   day_w    widest "%a %d" in English ("Wed 31"); 0 = no letters
typedef struct {
  const char *key;
  int8_t ink_top;
  uint8_t line_h;
  uint8_t digit_w;
  uint8_t colon_w;
  uint8_t day_w;
} FontMetrics;

static const FontMetrics F_GOTHIC_18      = { FONT_KEY_GOTHIC_18,              7, 18,  7,  3,  42 };
static const FontMetrics F_GOTHIC_24_BOLD = { FONT_KEY_GOTHIC_24_BOLD,        10, 24, 10,  4,  54 };
static const FontMetrics F_LECO_20_BOLD   = { FONT_KEY_LECO_20_BOLD_NUMBERS,   6, 20, 13,  6,   0 };
static const FontMetrics F_LECO_28_LIGHT  = { FONT_KEY_LECO_28_LIGHT_NUMBERS,  8, 28, 18,  8,   0 };
static const FontMetrics F_LECO_32_BOLD   = { FONT_KEY_LECO_32_BOLD_NUMBERS,  10, 32, 20,  9,   0 };
static const FontMetrics F_LECO_36_BOLD   = { FONT_KEY_LECO_36_BOLD_NUMBERS,  11, 36, 23, 10,   0 };
static const FontMetrics F_LECO_38_BOLD   = { FONT_KEY_LECO_38_BOLD_NUMBERS,  11, 38, 24, 11,   0 };
static const FontMetrics F_LECO_42        = { FONT_KEY_LECO_42_NUMBERS,       13, 42, 26, 12,   0 };
#if LARGE_SCREEN
static const FontMetrics F_LECO_60        = { FONT_KEY_LECO_60_NUMBERS_AM_PM, 18, 60, 37, 16,   0 };
static const FontMetrics F_GOTHIC_24      = { FONT_KEY_GOTHIC_24,             10, 24,  9,  4,  49 };
static const FontMetrics F_GOTHIC_28_BOLD = { FONT_KEY_GOTHIC_28_BOLD,        10, 28, 11,  5,  69 };
static const FontMetrics F_ROBOTO_21      = { FONT_KEY_ROBOTO_CONDENSED_21,    6, 21, 10,  5,  62 };
static const FontMetrics F_ROBOTO_49      = { FONT_KEY_ROBOTO_BOLD_SUBSET_49, 14, 49, 28, 14,   0 };
// Measured in the emulator (text layout advances, ink from a screenshot).
static const FontMetrics F_BITHAM_42_BOLD = { FONT_KEY_BITHAM_42_BOLD,        12, 42, 29, 10,   0 };
#endif

// Sizes for the decimal time on the digital display, largest first, ending
// in NULL: pick_time_font() takes the first that fits.
static const FontMetrics *const TIME_LECO[] = {
#if LARGE_SCREEN
  &F_LECO_60,
#endif
  &F_LECO_42, &F_LECO_38_BOLD, &F_LECO_36_BOLD, &F_LECO_32_BOLD, &F_LECO_28_LIGHT, &F_LECO_20_BOLD, NULL,
};
#if LARGE_SCREEN
// Gothic stops at 28 px, so the Gothic family's time uses Bitham 42 Bold,
// the largest sans-serif system font (digits only), where it fits.
static const FontMetrics *const TIME_GOTHIC[] = { &F_BITHAM_42_BOLD, &F_GOTHIC_28_BOLD, NULL };
static const FontMetrics *const TIME_ROBOTO[] = { &F_ROBOTO_49, &F_ROBOTO_21, NULL };
#endif

// The fonts of one family. [0] is for small dials and screens (and for the
// dial while a Timeline Quick View shrinks it), [1] for large ones.
typedef struct {
  const FontMetrics *const *time;  // decimal time on the digital display
  const FontMetrics *numerals[2];  // dial numerals
  const FontMetrics *text[2];      // date and normal time
} FontFamily;

// Indexed by FONT_*; the values must match the Font options in config.js.
static const FontFamily FAMILIES[FONT_COUNT] = {
  // FONT_DEFAULT
  { TIME_LECO,   { &F_LECO_20_BOLD,   &F_LECO_28_LIGHT  }, { &F_GOTHIC_18, &F_GOTHIC_24_BOLD } },
#if LARGE_SCREEN
  // FONT_GOTHIC: bold time and numerals; regular date and normal time, so the time stands out.
  { TIME_GOTHIC, { &F_GOTHIC_24_BOLD, &F_GOTHIC_28_BOLD }, { &F_GOTHIC_18, &F_GOTHIC_24 } },
  // FONT_ROBOTO: Roboto Bold has digits only, so everything but the time is Roboto Condensed.
  { TIME_ROBOTO, { &F_ROBOTO_21,      &F_ROBOTO_21      }, { &F_ROBOTO_21, &F_ROBOTO_21 } },
#endif
};

static const FontFamily *font_family(void) {
  return &FAMILIES[s_settings.font];  // settings_load/inbox_received keep it < FONT_COUNT
}

// Height of the digits' ink.
static int font_ink(const FontMetrics *f) {
  return f->line_h - f->ink_top;
}

// Width of "H:MM" / "H:MM:SS" with 1 or 2 hour digits.
static int big_text_width(const FontMetrics *f, int hour_digits, bool with_seconds) {
  int digits = hour_digits + 2 + (with_seconds ? 2 : 0);
  int colons = with_seconds ? 2 : 1;
  return digits * f->digit_w + colons * f->colon_w;
}

// The largest of the family's time sizes that fits; the smallest if none does.
static const FontMetrics *pick_time_font(const FontFamily *fam, int max_width, int hour_digits,
                                         bool with_seconds) {
  const FontMetrics *const *size = fam->time;
  while (size[1] && big_text_width(*size, hour_digits, with_seconds) > max_width) {
    size++;
  }
  return *size;
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
static void draw_small(GContext *ctx, const char *text, const FontMetrics *f, int x, int w, int ink_y) {
  graphics_draw_text(ctx, text, fonts_get_system_font(f->key),
                     GRect(x, ink_y - f->ink_top, w, f->line_h + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Fixed-width cells per character so digits don't jiggle as they change.
static void draw_big(GContext *ctx, const char *text, const FontMetrics *f, int center_x, int ink_y) {
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

// The digital display shows the day's progress around the edge of the
// screen, in ten sections, one per decimal hour, clockwise from midnight at
// the top: a frame on rectangular screens, a ring on round ones.

#if !defined(PBL_ROUND)
// Length of a frame of thickness t along the inside of rectangle o, measured
// along its centre line.
static int frame_length(GRect o, int t) {
  return 2 * (o.size.w + o.size.h) - 4 * t;
}

// Fill the part of that frame from `from` to `to` along its length,
// clockwise from the top centre. The frame is five straight runs: top right,
// right, bottom, left, top left. The runs that end in a corner include its
// square, so every pixel belongs to exactly one run.
static void fill_frame_span(GContext *ctx, GRect o, int t, int from, int to) {
  const int left = o.origin.x, top = o.origin.y;
  const int right = left + o.size.w, bottom = top + o.size.h, cx = left + o.size.w / 2;
  const int len[5] = { right - cx, o.size.h - t, o.size.w - t, o.size.h - 2 * t, cx - left };
  int start = 0;  // where the current run starts along the frame
  for (int i = 0; i < 5; i++) {
    const int a = from > start ? from - start : 0;               // the part of this run to fill
    const int z = to - start < len[i] ? to - start : len[i];
    if (z > a) {
      GRect r;
      switch (i) {
        case 0:  r = GRect(cx + a, top, z - a, t); break;              // rightwards
        case 1:  r = GRect(right - t, top + t + a, t, z - a); break;   // down
        case 2:  r = GRect(right - t - z, bottom - t, z - a, t); break; // leftwards
        case 3:  r = GRect(left, bottom - t - z, t, z - a); break;     // up
        default: r = GRect(left + a, top, z - a, t); break;           // rightwards
      }
      graphics_fill_rect(ctx, r, 0, GCornerNone);
    }
    start += len[i];
  }
}

// Rectangular screens: a frame of thickness t just inside the screen edge.
static void draw_day_frame(GContext *ctx, GRect bounds, uint32_t ds, int t, GColor accent) {
  const GRect o = grect_inset(bounds, GEdgeInsets(2));
  const int len = frame_length(o, t);
  const int done = (int)(ds * (uint32_t)len / 100000u);  // at most ~10^8: fits 32 bits
  const int gap = t / 2;                                 // between sections
#if defined(PBL_BW)
  // Black & white has no grey: the rest of the day is a thin line along the
  // middle of the frame instead.
  const GRect mid = grect_inset(o, GEdgeInsets(t / 2));
  const int mid_len = frame_length(mid, 1);
#else
  // An accent as dark as the track would disappear: use a light track then.
  const GColor track = gcolor_equal(accent, COLOR_TRACK) ? GColorLightGray : COLOR_TRACK;
#endif
  for (int i = 0; i < 10; i++) {
    const int from = i * len / 10 + gap / 2;
    const int to = (i + 1) * len / 10 - (gap - gap / 2);
#if defined(PBL_BW)
    graphics_context_set_fill_color(ctx, COLOR_MAIN);
    fill_frame_span(ctx, mid, 1, from * mid_len / len, to * mid_len / len);
#else
    graphics_context_set_fill_color(ctx, track);
    fill_frame_span(ctx, o, t, from, to);
#endif
    if (done > from) {
      graphics_context_set_fill_color(ctx, accent);
      fill_frame_span(ctx, o, t, from, done < to ? done : to);
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

// `inline`: no "unused function" warning on the watches that never call it.
static inline int iabs(int v) {
  return v < 0 ? -v : v;
}

#if !defined(PBL_ROUND)
// Square dial. Along the ray at `angle`, the edge of a square with half-side
// `half` is half / max(|sin|, |cos|) away from the centre: `half` straight up,
// half * 1.41 towards a corner. max(|sin|, |cos|) is never below 0.7, so the
// division is safe.
static int square_reach(int32_t angle, int half) {
  const int32_t s = iabs(sin_lookup(angle));
  const int32_t co = iabs(cos_lookup(angle));
  return (int)(half * TRIG_MAX_RATIO / (s > co ? s : co));
}

// How far along the ray at `angle` a box of half_w x half_h can be centred
// and still lie inside the square of half-side `half`: limited by the side
// edges through sin, and by the top and bottom edges through cos.
static int square_fit(int32_t angle, int half, int half_w, int half_h) {
  const int32_t s = iabs(sin_lookup(angle));
  const int32_t co = iabs(cos_lookup(angle));
  int d = 2 * half;  // more than any real limit
  if (s > 0 && (half - half_w) * TRIG_MAX_RATIO / s < d) {
    d = (half - half_w) * TRIG_MAX_RATIO / s;
  }
  if (co > 0 && (half - half_h) * TRIG_MAX_RATIO / co < d) {
    d = (half - half_h) * TRIG_MAX_RATIO / co;
  }
  return d;
}
#endif

#if LARGE_SCREEN

// Distance from the dial centre to the middle of the date (above) and of the
// normal time (below): 2/5 of the radius, or nearer the centre where needed
// to stay 4 px clear of every numeral (letters such as W and d reach above
// the digits), but not over the centre cap of radius cap. 0 if no distance
// works; the text is then left out.
// num holds the numeral centres relative to the dial centre. The date box is
// tested against all ten numerals. The normal time below is never wider than
// the widest date, and on both dials the numerals below mirror those above
// with labels no wider, so this covers it too.
static int dial_text_offset(int r, const GPoint num[10], const FontMetrics *nf, const FontMetrics *tf,
                            int cap) {
  const int half_w = tf->day_w / 2 + 4;
  const int half_h = font_ink(tf) / 2 + 4;
  const int n_half_h = font_ink(nf) / 2;
  for (int off = r * 2 / 5; off >= cap + half_h; off--) {
    bool clear = true;
    for (int h = 0; h < 10 && clear; h++) {
      const int n_half_w = (int)strlen(DIAL_LABELS[h]) * nf->digit_w / 2;
      if (iabs(num[h].x) < half_w + n_half_w && iabs(num[h].y + off) < half_h + n_half_h) {
        clear = false;
      }
    }
    if (clear) {
      return off;
    }
  }
  return 0;
}
#endif

static void draw_analog(GContext *ctx, GRect b, DecimalTime d, const struct tm *lt, Frame f) {
  // Fit the dial into the visible area (it shrinks for a Timeline Quick View).
  const int size = b.size.w < b.size.h ? b.size.w : b.size.h;
  const int r = size / 2 - 2;
  const GPoint c = GPoint(b.origin.x + b.size.w / 2, b.origin.y + b.size.h / 2);
  const bool xl = r >= 95;  // Time 2 and Round 2 sized dials
#if !defined(PBL_ROUND)
  // A square of the same half-side r: ticks reach out to its edge, numerals
  // move out towards its corners. The hands keep their round-dial lengths.
  const bool square = s_settings.dial == DIAL_SQUARE;
#endif

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
#if !defined(PBL_ROUND)
    if (square) {
      // Pulled in by half the width, so a wide tick's rounded end stays inside.
      const int outer = square_reach(a, r) - width / 2;
      graphics_draw_line(ctx, polar(c, a, outer - len), polar(c, a, outer));
      continue;
    }
#endif
    graphics_draw_line(ctx, polar(c, a, r - len), polar(c, a, r));
  }

  // Numerals 10, 1 .. 9 just inside the hour marks: on a circle, or on the
  // square each as far out as its box fits (num[] is relative to c).
  const FontFamily *fam = font_family();
  const FontMetrics *nf = fam->numerals[xl];
  const int nf_ink = font_ink(nf);
  GPoint num[10];
  for (int h = 0; h < 10; h++) {
    const int32_t a = (int32_t)h * TRIG_MAX_ANGLE / 10;
#if !defined(PBL_ROUND)
    if (square) {
      const int half_w = (int)strlen(DIAL_LABELS[h]) * nf->digit_w / 2;
      num[h] = polar(GPoint(0, 0), a, square_fit(a, r - hour_len - 5, half_w, nf_ink / 2));
      continue;
    }
#endif
    num[h] = polar(GPoint(0, 0), a, r - hour_len - 5 - nf_ink / 2);
  }
  GFont numeral_font = fonts_get_system_font(nf->key);
  graphics_context_set_text_color(ctx, COLOR_MAIN);
  for (int h = 0; h < 10; h++) {
    const GPoint p = GPoint(c.x + num[h].x, c.y + num[h].y);
    graphics_draw_text(ctx, DIAL_LABELS[h], numeral_font,
                       GRect(p.x - 30, p.y - nf_ink / 2 - nf->ink_top, 60, nf->line_h + 4),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }

  // Date above the centre, normal time below, if there is room
  // (not on the smallest dials, e.g. 144 px wide during a Quick View).
  if (r >= 62) {
    const FontMetrics *small = fam->text[xl];
    const int small_ink = font_ink(small);
#if LARGE_SCREEN
    // The default layout is unchanged. Other families can be larger, so
    // their text moves inward to clear the numerals, or is left out.
    const int off = fam == &FAMILIES[FONT_DEFAULT] ? r * 2 / 5
                                                   : dial_text_offset(r, num, nf, small, xl ? 7 : 5);
#else
    const int off = r * 2 / 5;
#endif
    graphics_context_set_text_color(ctx, COLOR_SUB);
    if (f.date && off > 0) {
      char date[24];
      strftime(date, sizeof(date), "%a %d", lt);
      draw_small(ctx, date, small, c.x - r, 2 * r, c.y - off - small_ink / 2);
    }
    if (f.normal_time && off > 0) {
      char normal[8];
      draw_small(ctx, format_normal_time(normal, sizeof(normal), lt), small,
                 c.x - r, 2 * r, c.y + off - small_ink / 2);
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
  const FontFamily *fam = font_family();
  const FontMetrics *small = fam->text[xl];
  const int small_ink = font_ink(small);
  const bool ten = s_settings.hour_zero_ten;
  const int hour = (ten && d.h == 0) ? 10 : d.h;

  // The day's progress around the edge; the text keeps clear of it. The
  // frame and ring are drawn on the full screen: a Quick View simply covers
  // their bottom part.
#if defined(PBL_ROUND)
  const int ring = xl ? 10 : 7;
  draw_day_ring(ctx, full, d.ds, ring, f.accent);
  const int time_w = full.size.w - 2 * (ring + 8) - 12;
  const int edge = 4;  // the ring curves away above and below the text
#else
  const int ring = xl ? 8 : 6;
  draw_day_frame(ctx, full, d.ds, ring, f.accent);
  const int edge = 2 + ring + 2;  // the frame's inset and thickness, and 2 px of black
  // The time's character cells may reach that far: the digits' own side
  // spacing leaves a few more pixels of black.
  const int time_w = full.size.w - 2 * edge;
#endif

  // Size for two hour digits all day when "10" is on, so the font stays put.
  const FontMetrics *big = pick_time_font(fam, time_w, ten ? 2 : 1, f.seconds);
  const int big_ink = font_ink(big);
  const int gap = xl ? 12 : 8;

  // Stack: date / decimal time / normal time, centred vertically.
  // First add up the height of what will be shown, then draw top to bottom.
  bool show_date = f.date, show_normal = f.normal_time;
  int block = big_ink + (show_date ? small_ink + gap : 0) + (show_normal ? gap + small_ink : 0);
  if (block > b.size.h - 2 * edge) {  // not enough room (Quick View): decimal time only
    show_date = show_normal = false;
    block = big_ink;
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
  read_choice(iter, MESSAGE_KEY_Font, &s_settings.font, FONT_COUNT - 1);
  read_choice(iter, MESSAGE_KEY_Dial, &s_settings.dial, DIAL_SQUARE);

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
  // Buffer sizes in bytes: a full settings message (9 integers) is about
  // 100 bytes, and the watch never sends anything, so the outbox can stay tiny.
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