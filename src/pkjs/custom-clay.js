// Runs inside the generated settings page (Clay injects it with toString(),
// so it must be self-contained and plain ES5).
// - The "Watch face" picker sets the Style and Dial items, which are hidden.
// - "First hour of the day" only applies to the digital style.
// - The custom colour picker only applies when Accent is "Custom".
// - "Restore defaults" puts every item back to its defaultValue from config.js
//   (the page only; the watch changes when Save is pressed).
// With serializeValueAs: 'integer', get() returns a number, so compare as strings.
// Clay calls this with `this` set to the page's ClayConfig; `minified` is its
// bundled DOM helper library, unused here.
module.exports = function (minified) {
  var clayConfig = this;

  function setVisible(item, visible) {
    if (!item) {
      return;
    }
    if (visible) {
      item.show();
    } else {
      item.hide();
    }
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    var style = clayConfig.getItemByMessageKey('Style');
    var hourZero = clayConfig.getItemByMessageKey('HourZero');
    var accent = clayConfig.getItemByMessageKey('Accent');          // absent on black & white watches
    var accentCustom = clayConfig.getItemByMessageKey('AccentCustom');

    function update() {
      setVisible(hourZero, !!style && String(style.get()) === '0');
      setVisible(accentCustom, !!accent && String(accent.get()) === '4');
    }

    // White is no longer offered. A saved White matches no square, and the
    // radio group then reads as 0, which is White: show it as Custom white,
    // which looks the same on the watch.
    if (accent && accentCustom && String(accent.get()) === '0') {
      accentCustom.set(0xFFFFFF);
      accent.set(4);
    }

    if (style) {
      style.on('change', update);
    }
    if (accent) {
      accent.on('change', update);
    }
    update();

    // Watch face: one picker for two settings. Picking a face sets Style and
    // Dial; when those change some other way (the saved settings when the page
    // opens, Restore defaults) the picker follows. `syncing` stops the two
    // directions from setting each other in a loop.
    var face = clayConfig.getItemById('Face');
    var dial = clayConfig.getItemByMessageKey('Dial');              // absent on round watches
    var syncing = false;

    function faceFromSettings() {
      if (String(style.get()) === '0') {
        return 'digital';
      }
      return dial && String(dial.get()) === '1' ? 'analog-square' : 'analog-round';
    }

    function applyFace() {
      if (syncing) {
        return;
      }
      syncing = true;
      var value = face.get();
      if (dial && value !== 'digital') {
        dial.set(value === 'analog-square' ? 1 : 0);  // digital keeps the last dial shape
      }
      style.set(value === 'digital' ? 0 : 1);
      syncing = false;
    }

    function syncFace() {
      if (syncing) {
        return;
      }
      syncing = true;
      face.set(faceFromSettings());
      syncing = false;
    }

    if (face && style) {
      setVisible(style, false);
      setVisible(dial, false);
      face.on('change', applyFace);
      style.on('change', syncFace);
      if (dial) {
        dial.on('change', syncFace);
      }
      syncFace();
    }

    // item.config is the item's entry from config.js, so config.defaultValue is
    // the default even when the page opened with saved settings. set() fires
    // 'change', which re-runs update() above.
    var restore = clayConfig.getItemById('RestoreDefaults');
    if (restore) {
      restore.on('click', function () {
        clayConfig.getAllItems().forEach(function (item) {
          if (item.messageKey && typeof item.config.defaultValue !== 'undefined') {
            item.set(item.config.defaultValue);
          }
        });
      });
    }
  });
};