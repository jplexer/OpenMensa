var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var messageKeys = require('message_keys');
var clay = new Clay(clayConfig);

// Global variable to cache the full meals JSON.
var cachedMeals = null;
var openmensaID = null;

Pebble.addEventListener("ready", function(e) {
  //console.log("Pebble ready");
  openmensaID = localStorage.getItem("openmensaID");
  //console.log("openmensaID:", openmensaID);
  if (!openmensaID) {
    Pebble.sendAppMessage({
        "ERROR_MSG": "Please go into the settings and set your Canteen's ID"
      }, function(e) {
        //console.log("Error message sent");
      }, function(e) {
        console.log("Error message send failed", JSON.stringify(e));
      });
    console.log("No openmensaID configured.");
    return;
  }

  fetchDayList();
});

function fetchDayList() {
  // Fetch day list for the menu
  var url = "https://openmensa.org/api/v2/canteens/" + openmensaID + "/days";
  //console.log("Fetching days from: " + url);
  
  var req = new XMLHttpRequest();
  req.onload = function() {
    if (req.status >= 200 && req.status < 300) {
      var data = JSON.parse(req.responseText);
      var activeDates = data.filter(function(day) {
        return day.closed === false;
      }).map(function(day) {
        return day.date;
      });
      if (activeDates.length > 10) { activeDates = activeDates.slice(0, 10); }
      
      // Reformat dates and compute weekdays.
      var formattedDates = activeDates.map(function(dateStr) {
        var parts = dateStr.split('-'); // [YYYY, MM, DD]
        return parts[2] + '.' + parts[1] + '.' + parts[0];
      });
      
      var weekdayNames = activeDates.map(function(dateStr) {
        var parts = dateStr.split('-');
        var dateObj = new Date(parts[0], parts[1] - 1, parts[2]);
        return dateObj.toLocaleDateString('en-US', { weekday: 'short' });
      });
      
      Pebble.sendAppMessage({
        "DAY_LIST": JSON.stringify(formattedDates)
      }, function(e) {
        //console.log("DAY_LIST sent:", JSON.stringify(formattedDates));
        Pebble.sendAppMessage({
          "WEEKDAY_LIST": JSON.stringify(weekdayNames)
        }, function(e) {
          //console.log("WEEKDAY_LIST sent:", JSON.stringify(weekdayNames));
        }, function(e) {
          console.log("WEEKDAY_LIST send failed");
        });
      }, function(e) {
        console.log("DAY_LIST send failed");
      });
    } else {
      console.log("XHR request failed: " + req.status);
    }
  };
  req.onerror = function() {
    console.log("XHR network error");
  };
  req.open("GET", url);
  req.send();
}

Pebble.addEventListener("showConfiguration", function(e) {
  var url = clay.generateUrl();
  Pebble.openURL(url);
});

Pebble.addEventListener("webviewclosed", function(e) {
  var settings = clay.getSettings(e.response);
  localStorage.setItem("openmensaID", settings[messageKeys.openmensaID]);
  openmensaID = settings[messageKeys.openmensaID];
  localStorage.setItem("MEAL_PRICE", settings[messageKeys.MEAL_PRICE]);
  Pebble.sendAppMessage({ "RELOAD_APP": 1 }, 
    function(e) {
      console.log("Reload message sent.");
    }, 
    function(e) {
      console.log("Reload message send failed", JSON.stringify(e));
    }
  );
});

// Listen for messages from the watch.
Pebble.addEventListener("appmessage", function(e) {
  if (e.payload.RELOAD_DONE === 1) {
    fetchDayList();
  } else if (e.payload.SELECTED_DATE) {
      var openmensaID = localStorage.getItem("openmensaID");
      var parts = e.payload.SELECTED_DATE.split('.');
      if(parts.length === 3) {
        var apiDate = parts[2] + '-' + parts[1] + '-' + parts[0];
        var url = "https://openmensa.org/api/v2/canteens/" + openmensaID + "/days/" + apiDate + "/meals";
        //console.log("Fetching meals from: " + url);
        var reqMeals = new XMLHttpRequest();
        reqMeals.onload = function() {
          if (reqMeals.status >= 200 && reqMeals.status < 300) {
            try {
              var fullMeals = JSON.parse(reqMeals.responseText);
              // Cache the full meals JSON.
              cachedMeals = fullMeals;

              var pricingCategory = localStorage.getItem("MEAL_PRICE") || "students";
              // Build the simplified objects.
              var simplifiedMeals = fullMeals.map(function(meal) {
                return {
                  id: meal.id,
                  name: meal.name,
                  price: meal.prices && meal.prices[pricingCategory] ? meal.prices[pricingCategory] : null
                };
              });
              // Create separate arrays.
              var mealIDs = simplifiedMeals.map(function(meal) { return meal.id; });
              var mealNames = simplifiedMeals.map(function(meal) { return meal.name; });
              var MEAL_PRICES = simplifiedMeals.map(function(meal) { 
                  return meal.price !== null ? meal.price.toFixed(2) + "€" : "N/A"; 
                });
              
              // Combine into a single dictionary payload.
              var payload = {
                "MEALS_IDS": JSON.stringify(mealIDs),
                "MEALS_NAMES": JSON.stringify(mealNames),
                "MEALS_PRICES": JSON.stringify(MEAL_PRICES)
              };

              Pebble.sendAppMessage(payload, function(e) {
                //console.log("Combined meals payload sent:", JSON.stringify(payload));
              }, function(e) {
                console.log("Combined meals payload send failed");
              });
            
            } catch(ex) {
              console.log("Error parsing meals JSON:", ex);
            }
          } else {
            console.log("Meals request failed: " + reqMeals.statusText);
          }
        };
        reqMeals.onerror = function() {
          console.log("Meals XHR network error");
        };
        reqMeals.open("GET", url);
        reqMeals.send();
      }
  }
  // If a meal was selected on the watch, look it up in the cache and send back the meal info.
  else if (e.payload.MEAL_ID !== undefined) {
    if (!cachedMeals) {
      //console.log("No cached meals available.");
      return;
    }
    var selectedMealId = e.payload.MEAL_ID;
    // Find the meal in the cached meals using the meal id.
    var mealInfo = cachedMeals.find(function(meal) {
      return meal.id === selectedMealId;
    });
    if (mealInfo) {
      var pricingCategory = localStorage.getItem("MEAL_PRICE") || "students";
      var singlePrice = (mealInfo.prices && mealInfo.prices[pricingCategory]) ?
                           mealInfo.prices[pricingCategory].toFixed(2) + "€" : "N/A";
      var notes = ""
      mealInfo.notes.forEach(function(note) {
        notes += note + ", ";
      });
      //remove trailing comma
      notes = notes.replace(/,\s*$/, "");
      var payload = {
        "MEAL_NAME": mealInfo.name,
        "MEAL_PRICE": singlePrice,
        "MEAL_NOTES": notes
      };
      Pebble.sendAppMessage(payload, function(e) {
        //console.log("Single meal payload sent:", JSON.stringify(payload));
      }, function(e) {
        console.log("Single meal payload send failed");
      });
    } else {
      console.log("Meal not found for id:", selectedMealId);
    }
  }
});