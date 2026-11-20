// Runs inside the generated settings page (Clay injects it with toString(),
// so it must be self-contained and plain ES5).
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

    if (style) {
      style.on('change', update);
    }
    if (accent) {
      accent.on('change', update);
    }
    update();

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