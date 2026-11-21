// Settings page for Decimal Time. Values must match the watch side:
//   Style       0 digital, 1 analog
//   HourZero    0 show "0:43", 1 show "10:43" (digital only)
//   Seconds,
//   NormalTime,
//   Date        0 never, 1 on wrist flick, 2 always
//   Accent      0 white, 1 grey, 2 red, 3 blue, 4 custom
//   AccentCustom colour picker, used when Accent is custom
//   Font        0 default, 1 Gothic, 2 Roboto (Time 2 and Round 2)
//   Dial        0 round, 1 square (rectangular watches, analog style)
// The watch accepts integers only, so every radiogroup/swatchgroup/imagegroup/
// select with a messageKey must keep serializeValueAs: 'integer' (Clay would
// otherwise send the value as text, and the watch would ignore it).
//
// Options with a `preview` get their picture from previews.js (made by
// tools/screenshots.py) for the watch the page is opened for; see index.js.

var SHOW_OPTIONS = [
  { label: 'Never', value: '0' },
  { label: 'On wrist flick', value: '1' },
  { label: 'Always', value: '2' }
];

// "Watch face" is one choice on the page but two settings on the watch, Style
// and Dial. The picker has no messageKey; custom-clay.js keeps it and the
// hidden Style and Dial items in step.
var FACE_DIGITAL = { label: 'Digital', value: 'digital', preview: 'digital' };
var FACE_ROUND = { label: 'Analog round', value: 'analog-round', preview: 'analog-round' };
var FACE_SQUARE = { label: 'Analog square', value: 'analog-square', preview: 'analog-square' };

module.exports = [
  {
    type: 'heading',
    defaultValue: 'Decimal Time'
  },
  {
    type: 'text',
    defaultValue: '10 hours a day, 100 minutes an hour, 100 seconds a minute. Noon is 5:00.'
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Display'
      },
      {
        // Rectangular watches: digital, round dial or square dial.
        type: 'imagegroup',
        id: 'Face',
        label: 'Watch face',
        capabilities: ['RECT'],
        columns: 3,
        defaultValue: 'analog-round',
        options: [FACE_DIGITAL, FACE_ROUND, FACE_SQUARE]
      },
      {
        // Round watches have no square dial. Only one of the two is built.
        type: 'imagegroup',
        id: 'Face',
        label: 'Watch face',
        capabilities: ['ROUND'],
        columns: 2,
        shape: 'round',
        defaultValue: 'analog-round',
        options: [FACE_DIGITAL, FACE_ROUND]
      },
      {
        // Hidden on the page (custom-clay.js): set by the Watch face picker.
        type: 'radiogroup',
        messageKey: 'Style',
        label: 'Style',
        defaultValue: '1',
        serializeValueAs: 'integer',
        options: [
          { label: 'Analog (ten-hour dial)', value: '1' },
          { label: 'Digital', value: '0' }
        ]
      },
      {
        // Hidden on the page (custom-clay.js): set by the Watch face picker.
        type: 'radiogroup',
        messageKey: 'Dial',
        label: 'Dial shape',
        capabilities: ['RECT'],
        defaultValue: '0',
        serializeValueAs: 'integer',
        options: [
          { label: 'Round', value: '0' },
          { label: 'Square', value: '1' }
        ]
      },
      {
        type: 'radiogroup',
        messageKey: 'HourZero',
        label: 'First hour of the day',
        description: 'How the digital display shows the hour after midnight. The dial always has 10 at the top.',
        defaultValue: '0',
        serializeValueAs: 'integer',
        options: [
          { label: '0  (0:43)', value: '0' },
          { label: '10  (10:43)', value: '1' }
        ]
      }
    ]
  },
  {
    type: 'section',
    // Pebble Time 2 and Round 2 only. Clay requires every listed capability,
    // so "Time 2 or Round 2" is written as "none of the older watches".
    capabilities: ['NOT_PLATFORM_APLITE', 'NOT_PLATFORM_BASALT', 'NOT_PLATFORM_CHALK',
      'NOT_PLATFORM_DIORITE', 'NOT_PLATFORM_FLINT'],
    items: [
      {
        type: 'heading',
        defaultValue: 'Font'
      },
      {
        // Each choice shows the date and time as the watch draws them in it.
        // No label: the section heading already says "Font".
        type: 'imagegroup',
        messageKey: 'Font',
        description: 'Used for all text: time, date, normal time and dial numerals. ' +
          'The default is the original look: LECO for the time and numerals, Gothic for the date. ' +
          'Gothic has no large size, so its digital time is in Bitham.',
        columns: 1,
        defaultValue: '0',
        serializeValueAs: 'integer',
        options: [
          { label: 'Default (LECO and Gothic)', value: '0', preview: 'font-0' },
          { label: 'Gothic', value: '1', preview: 'font-1' },
          { label: 'Roboto', value: '2', preview: 'font-2' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Show'
      },
      {
        type: 'text',
        defaultValue: 'A wrist flick shows items set to "On wrist flick" for 50 decimal seconds (43.2 s).'
      },
      {
        type: 'select',
        messageKey: 'Seconds',
        label: 'Seconds',
        description: 'Always on uses more battery: the face redraws every decimal second.',
        defaultValue: '1',
        serializeValueAs: 'integer',
        options: SHOW_OPTIONS
      },
      {
        type: 'select',
        messageKey: 'NormalTime',
        label: 'Normal time',
        defaultValue: '1',
        serializeValueAs: 'integer',
        options: SHOW_OPTIONS
      },
      {
        type: 'select',
        messageKey: 'Date',
        label: 'Date',
        defaultValue: '2',
        serializeValueAs: 'integer',
        options: SHOW_OPTIONS
      }
    ]
  },
  {
    type: 'section',
    capabilities: ['COLOR'],
    items: [
      {
        type: 'heading',
        defaultValue: 'Accent colour'
      },
      {
        // Custom component (clay-components.js): a 2x2 grid of colour squares
        // with a "Custom" square centred below, showing the AccentCustom colour.
        // The AccentCustom row below is moved up under the squares on the page.
        // Each color is what the watch draws for that choice, see
        // accent_color() in decimal_time.c.
        type: 'swatchgroup',
        messageKey: 'Accent',
        label: 'Accent',
        description: 'Hour marks and second hand on the dial; the day\'s progress around the edge of the digital display.',
        defaultValue: '1',
        serializeValueAs: 'integer',
        options: [
          { label: 'White', value: '0', color: '#FFFFFF' },  // GColorWhite
          { label: 'Grey', value: '1', color: '#AAAAAA' },   // GColorLightGray
          { label: 'Red', value: '2', color: '#FF0000' },    // GColorRed
          { label: 'Blue', value: '3', color: '#0055FF' }    // GColorBlueMoon
        ],
        customOption: { label: 'Custom', value: '4', colorFrom: 'AccentCustom' }
      },
      {
        type: 'color',
        messageKey: 'AccentCustom',
        label: 'Custom colour',
        defaultValue: '00AAFF',
        sunlight: true
      }
    ]
  },
  {
    // Custom component (clay-components.js): "Restore defaults" and "Save" side
    // by side. defaultValue is the restore button's label; its click is handled
    // in custom-clay.js. No messageKey, so nothing extra is sent to the watch.
    type: 'savebar',
    id: 'RestoreDefaults',
    defaultValue: 'Restore defaults',
    submitLabel: 'Save',
    description: 'Restore defaults puts every setting back to its default. Press Save to apply, or close the page to keep your current settings.'
  }
];