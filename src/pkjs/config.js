// Settings page for Decimal Time. Values must match the watch side:
//   Style       0 digital, 1 analog
//   HourZero    0 show "0:43", 1 show "10:43" (digital only)
//   Seconds,
//   NormalTime,
//   Date        0 never, 1 on wrist flick, 2 always
//   Accent      0 white, 1 grey, 2 red, 3 blue, 4 custom
//   AccentCustom colour picker, used when Accent is custom
// The watch accepts integers only, so every radiogroup/select below must keep
// serializeValueAs: 'integer' (Clay would otherwise send the value as text,
// and the watch would ignore it).

var SHOW_OPTIONS = [
  { label: 'Never', value: '0' },
  { label: 'On wrist flick', value: '1' },
  { label: 'Always', value: '2' }
];

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
    items: [
      {
        type: 'heading',
        defaultValue: 'Show'
      },
      {
        type: 'text',
        defaultValue: 'A wrist flick shows items set to "On wrist flick" for 100 decimal seconds (86.4 s).'
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
        type: 'radiogroup',
        messageKey: 'Accent',
        label: 'Accent',
        description: 'Hour marks and second hand on the dial; the day bar on the digital display.',
        defaultValue: '1',
        serializeValueAs: 'integer',
        options: [
          { label: 'White', value: '0' },
          { label: 'Grey', value: '1' },
          { label: 'Red', value: '2' },
          { label: 'Blue', value: '3' },
          { label: 'Custom', value: '4' }
        ]
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
    type: 'submit',
    defaultValue: 'Save'
  }
];