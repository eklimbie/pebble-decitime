// Phone side: Clay builds the settings page (gear icon in the Pebble app)
// from config.js and sends the chosen values to the watch.
var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');

var clay = new Clay(clayConfig, customClay);
