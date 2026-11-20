
// Phone side: Clay builds the settings page (gear icon in the Pebble app)
// from config.js and sends the chosen values to the watch.
var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');
var clayComponents = require('./clay-components');

// We open the page and receive its result ourselves (Clay's documented
// "autoHandleEvents: false" mode) so the page also works in the emulator.
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });
// The accent colour grid and the Restore defaults / Save row.
clayComponents.forEach(function (component) {
  clay.registerComponent(component);
});
 
// In the emulator (Pebble.platform === 'pypkjs') Clay sends its page through
// Pebble's old helper page on Amazon S3, which has been offline for years, so
// the browser shows an error instead of the settings. Give the emulator the
// page as a data: URL instead, and let the page read the return address that
// pebble-tool adds as ?return_to=... (the one job the old helper page did).
// On a phone the URL is left exactly as Clay makes it.
var CLAY_EMULATOR_HELPER = 'http://clay.pebble.com.s3-website-us-west-2.amazonaws.com/#';
var RETURN_TO_PLACEHOLDER = /(["'])\$\$RETURN_TO\$\$\1/;
var READ_RETURN_TO =
  '(function () {' +
  ' var m = /[?&]return_to=([^&]*)/.exec(location.search);' +
  " return m ? decodeURIComponent(m[1]) : 'pebblejs://close#';" +
  ' })()';
 
function emulatorUrl(url) {
  if (url.indexOf(CLAY_EMULATOR_HELPER) !== 0) {
    return url;
  }
  var html = decodeURIComponent(url.substring(CLAY_EMULATOR_HELPER.length));
  // A function as the replacement, so "$" in the page is never treated specially.
  html = html.replace(RETURN_TO_PLACEHOLDER, function () { return READ_RETURN_TO; });
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}
 
Pebble.addEventListener('showConfiguration', function () {
  try {
    // What Clay's automatic mode does just before opening the page: the page
    // needs the watch's platform to decide which options to show.
    clay.meta.activeWatchInfo = Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo();
    clay.meta.accountToken = Pebble.getAccountToken();
    clay.meta.watchToken = Pebble.getWatchToken();
 
    var url = clay.generateUrl();
    Pebble.openURL(Pebble.platform === 'pypkjs' ? emulatorUrl(url) : url);
  } catch (e) {
    // Shows up in `pebble logs`, instead of the page silently never opening.
    console.log('Decimal Time: could not open the settings page: ' + e + (e && e.stack ? '\n' + e.stack : ''));
  }
});
 
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) {
    return;  // closed without saving
  }
  try {
    Pebble.sendAppMessage(clay.getSettings(e.response), function () {
      console.log('Decimal Time: settings sent to the watch');
    }, function (err) {
      console.log('Decimal Time: sending settings failed: ' + JSON.stringify(err));
    });
  } catch (err) {
    console.log('Decimal Time: could not read the settings page result: ' + err);
  }
});
 