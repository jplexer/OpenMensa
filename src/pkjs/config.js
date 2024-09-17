var Clay = require("./clay.js");
var CONFIG_KEY = "config";
var OPENMENSA_ID = "openmensaID";
var clayConfig = [
    {
        "type": "section",
        "items": [
          {
            "type": "heading",
            "defaultValue": "Please enter your canteen's ID as listed on OpenMensa (https://openmensa.org/c/ID)"
          },
          {
            "type": "input",
            "appKey": "openmensaID",
            "label": "OpenMensa ID",
            "attributes": {
              "placeholder": "ID",
              "type": "number"
            }
          }
        ]
      },
      {
        "type": "submit",
        "defaultValue": "Save"
      }
];

var clay = new Clay(clayConfig);

// Fired when the configuration is saved
Pebble.addEventListener("webviewclosed", function (e) {
  if (e && !e.response) {
    return;
  }

  var configData = JSON.parse(e.response);
  var configValues = Object.keys(configData).reduce(function (result, key) {
    result[key] = configData[key].value;
    return result;
  }, {});

  localStorage.setItem(CONFIG_KEY, JSON.stringify(configValues));
});

function getConfig() {
  return JSON.parse(localStorage.getItem(CONFIG_KEY));
}

module.exports = {
  OPENMENSA_ID,
  getConfig
};