// Custom Clay components for the settings page, registered in index.js.
// Clay copies these into the page as text, so templates and styles are plain
// strings. Templates use Clay's {{ }} syntax, the same as its built-in ones.

// A radio group drawn as a grid of colour squares, two per row, with an
// optional "Custom" square centred on a row of its own below them. It reuses
// Clay's radiogroup value handling, so messageKey, defaultValue and
// serializeValueAs work as they do for type: 'radiogroup'.
//   options:      [{ label, value, color }]  one square each, color is CSS
//   customOption: { label, value, colorFrom }
//     colorFrom is the messageKey of a 'color' item; the square shows that
//     item's colour and follows it as it changes. That item's row is also
//     moved in here, right under the squares and above the description.
var swatchGroup = {
  name: 'swatchgroup',
  manipulator: 'radiogroup',
  defaults: {
    label: '',
    options: [],
    customOption: null,
    serializeValueAs: 'string',
    description: ''
  },
  template:
    '<div class="component component-swatches">' +
      '<span class="label">{{{label}}}</span>' +
      '<div class="swatch-grid">' +
        '{{each options}}' +
          '<label class="swatch">' +
            '<input type="radio" value="{{this.value}}" name="clay-{{clayId}}" />' +
            '<span class="swatch-chip" style="background-color: {{this.color}}"></span>' +
            '<span class="swatch-name">{{{this.label}}}</span>' +
          '</label>' +
        '{{/each}}' +
        '{{if customOption}}' +
          '<label class="swatch swatch-custom">' +
            '<input type="radio" value="{{customOption.value}}" name="clay-{{clayId}}" />' +
            '<span class="swatch-chip"></span>' +
            '<span class="swatch-name">{{{customOption.label}}}</span>' +
          '</label>' +
        '{{/if}}' +
      '</div>' +
      '{{if description}}' +
        '<div class="description swatch-description">{{{description}}}</div>' +
      '{{/if}}' +
    '</div>',
  // Runs in the page (Clay copies it with toString()), so plain ES5 only.
  // The colour item is built after this one, so wait for the whole page.
  initialize: function (minified, clay) {
    var self = this;
    var custom = self.config.customOption;
    if (!custom || !custom.colorFrom) {
      return;
    }
    clay.on(clay.EVENTS.AFTER_BUILD, function () {
      var source = clay.getItemByMessageKey(custom.colorFrom);
      var root = self.$element[0];
      var chip = root.querySelector('.swatch-custom .swatch-chip');
      if (!source || !chip) {
        return;
      }
      // Put the colour row under the squares. Clay only draws separator lines
      // between rows directly in a section, so it gets none here. Its own
      // show/hide (custom-clay.js) and picker keep working after the move.
      root.insertBefore(source.$element[0], root.querySelector('.swatch-description'));
      function paint() {
        // A colour item's value is 0xRRGGBB as a number.
        chip.style.backgroundColor = '#' + ('00000' + (source.get() >>> 0).toString(16)).slice(-6);
      }
      source.on('change', paint);
      paint();
    });
  },
  style: [
    '.component-swatches > .label { display: block; padding-bottom: 0.7rem; }',
    '.component-swatches input { position: absolute; opacity: 0; pointer-events: none; }',
    '.component-swatches .swatch-grid {',
    '  display: grid; grid-template-columns: repeat(2, 5rem); gap: 0.8rem 2rem;',
    '  justify-content: center; padding-bottom: 0.7rem;',
    '}',
    '.component-swatches .swatch {',
    '  display: flex; flex-direction: column; align-items: center; padding: 0;',
    '  -webkit-tap-highlight-color: transparent; cursor: pointer;',
    '}',
    // The ring is spaced from the square by a band of the section colour,
    // so it reads clearly even around the red square.
    '.component-swatches .swatch-chip {',
    '  display: block; width: 5rem; height: 5rem; border-radius: 0.25rem;',
    '  box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.3);',
    '}',
    '.component-swatches .swatch input:checked + .swatch-chip {',
    '  box-shadow: 0 0 0 3px #484848, 0 0 0 6px #ff4700;',
    '}',
    '.component-swatches .swatch-name { font-size: 0.9em; }',
    '.component-swatches .swatch .swatch-name { padding-top: 0.45rem; color: #a4a4a4; }',
    '.component-swatches .swatch input:checked ~ .swatch-name { color: #ffffff; }',
    // "Custom" spans both columns of its row and sits in the middle.
    '.component-swatches .swatch-custom { grid-column: 1 / -1; justify-self: center; }',
    // The moved colour row: this block already has the section's side padding.
    '.component-swatches .component-color > label { padding-left: 0; padding-right: 0; }',
    '.component-swatches .description { padding-left: 0; padding-right: 0; }'
  ].join('\n')
};

// "Restore defaults" and "Save" side by side, Save on the right. Use this in
// place of a 'submit' item. defaultValue is the restore button's label (Clay
// sets it through the 'button' manipulator), and on('click') on this item
// listens to the restore button. Save is a normal submit button.
//   submitLabel: text on the Save button
var saveBar = {
  name: 'savebar',
  manipulator: 'button',
  defaults: {
    submitLabel: 'Save',
    description: ''
  },
  template:
    '<div class="component component-savebar">' +
      '<div class="savebar-row">' +
        '<button type="button" data-manipulator-target></button>' +
        '<button type="submit">{{{submitLabel}}}</button>' +
      '</div>' +
      '{{if description}}' +
        '<div class="description">{{{description}}}</div>' +
      '{{/if}}' +
    '</div>',
  style: [
    '.component-savebar .savebar-row { display: flex; gap: 0.75rem; }',
    // Clay gives buttons min-width: 12rem, too wide for two on a phone.
    '.component-savebar button { flex: 1 1 0; min-width: 0; margin: 0 0 0.7rem; }',
    '.component-savebar .description { padding-left: 0; padding-right: 0; }'
  ].join('\n')
};

module.exports = [swatchGroup, saveBar];
