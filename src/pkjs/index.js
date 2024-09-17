require('pebblejs');
var UI = require('pebblejs/ui');
var {OPENMENSA_ID, getConfig} = require('./config');

var openmensaID;

Pebble.addEventListener('ready', function () {
console.log("App is running");

try {
  openmensaID = getConfig()[OPENMENSA_ID];
}
catch (e) {
  console.error(e);
  var card = new UI.Card({
    title: "No canteen ID",
    subtitle: "Please set your canteen ID in the settings"
  });
  card.show();
  return;
}

var xhr = new XMLHttpRequest();
xhr.open('GET', `https://openmensa.org/api/v2/canteens/${openmensaID}/days`, true);
xhr.responseType = 'json';

xhr.onload = function() {
  if (xhr.status >= 200 && xhr.status < 300) {
    var days = xhr.response;
    var menu = new UI.Menu({
      sections: [{
        items: parseDays(days)
      }]
    });
    menu.on('select', function(e) {
      var day = e.item.title.substring(0, 2);
      var month = e.item.title.substring(3, 5);
      var year = e.item.title.substring(6, 10);
      getMeals(year + "-" + month + "-" + day);
    });
    menu.show();
  } else {
    console.error('Request failed with status: ' + xhr.status);
    var card = new UI.Card({
      title: "Unable to fetch data",
      subtitle: "Please try again later",
    });
    card.show();
  }
};

xhr.onerror = function() {
  console.error('Request failed');
  var card = new UI.Card({
    title: "Unable to fetch data",
    subtitle: "Please try again later",
  });
  card.show();
};

xhr.send();

});

var parseDays = function(days) {
  var items = [];
  for (var i = 0; i < days.length; i++) {
    var day = days[i];
    var year = day.date.substring(0, 4);
    var month = day.date.substring(5, 7);
    var d = day.date.substring(8, 10);
    var date = d + "." + month + "." + year;
    items.push({
      title: date
    });
  }

  return items;
}

var parseMeals = function(meals) {
  var items = [];
  for (var i = 0; i < meals.length; i++) {
    var meal = meals[i];
    var mealName = meal.name;
    var mealNotes = Array.isArray(meal.notes) ? meal.notes.join(', ') : meal.notes;
    var image = "IMAGE_POT"

    if (mealName.toLowerCase().includes("vegan") || mealNotes.toLowerCase().includes("vegan")) {
      image = "IMAGE_VEGAN";
    }

    if (mealName.toLowerCase().includes("vegetarisch") || mealName.toLowerCase().includes("vegetarian") || mealNotes.toLowerCase().includes("vegetarisch") || mealNotes.toLowerCase().includes("vegetarian")) {
      image = "IMAGE_VEGETARIAN";
    }

    //If the meal name starts with "VEGAN:", "VEGETARIAN:" or "VEGETARISCH:", remove the prefix
    if (mealName.toLowerCase().startsWith("vegan:")) {
      mealName = mealName.substring(6).trim();
    }
    if (mealName.toLowerCase().startsWith("vegetarian:")) {
      mealName = mealName.substring(11).trim();
    }
    if (mealName.toLowerCase().startsWith("vegetarisch:")) {
      mealName = mealName.substring(12).trim();
    }

      items.push({
        title: mealName,
        icon: image,
        subtitle: meal.prices.students + "€"
      });
  }
  return items;
}

var parseMealInfo = function(meals) {
  var items = [];
  for (var i = 0; i < meals.length; i++) {
    var meal = meals[i];
      items.push({
        title: meal.name,
        price: meal.prices.students,
        notes: meal.notes
      });
    
  }
  return items;
}

function getMeals(date) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', `https://openmensa.org/api/v2/canteens/${openmensaID}/days/${date}/meals`, true);
  xhr.responseType = 'json';

  xhr.onload = function() {
    if (xhr.status >= 200 && xhr.status < 300) {
      var meals = xhr.response;
      var parsedMeals = parseMeals(meals);
      var mealInfo = parseMealInfo(meals);
      var menu = new UI.Menu({
        sections: [{
          items: parsedMeals
        }]
      });
      menu.on('select', function(e) {
        showMealCard(mealInfo[e.itemIndex]);
      });
      menu.show();
    } else {
      console.error('Request failed with status: ' + xhr.status);
      var card = new UI.Card({
        title: "Unable to fetch data",
        subtitle: "Please try again later",
      });
      card.show();
    }
  };

  xhr.onerror = function() {
    console.error('Request failed');
    var card = new UI.Card({
      title: "Unable to fetch data",
      subtitle: "Please try again later",
    });
    card.show();
  };

  xhr.send();
}

function showMealCard(meal) {
  var mealName = meal.title;

  //if the meal name starts with "VEGAN:", "VEGETARIAN:" or "VEGETARISCH:", remove the prefix
  if (mealName.startsWith("VEGAN:")) {
    mealName = mealName.substring(6).trim();
  }
  if (mealName.startsWith("VEGETARIAN:")) {
    mealName = mealName.substring(11).trim();
  }
  if (mealName.startsWith("VEGETARISCH:")) {
    mealName = mealName.substring(12).trim();
  }

  var card = new UI.Card({
    title: mealName,
    subtitle: meal.price + "€",
    body: meal.notes,
    scrollable: true
  });
  card.show();
}