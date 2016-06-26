var ForecastIoWeather = function() {
  
  this._apiKey    = '';

  var conditions = {
    ClearSky        : 0,
    FewClouds       : 1,
    ScatteredClouds : 2,
    BrokenClouds    : 3,
    ShowerRain      : 4,
    Rain            : 5,
    Thunderstorm    : 6,
    Snow            : 7,
    Mist            : 8,
    Unknown         : 1000,
  };

  this._xhrWrapper = function(url, type, callback) {
    var xhr = new XMLHttpRequest();
    xhr.onload = function () {
      callback(xhr);
    };
    xhr.open(type, url);
    xhr.send();
  };

  this._getWeatherF_IO = function(coords) {
    var url = 'https://api.forecast.io/forecast/' + this._apiKey + '/' +
      coords.latitude + ',' + coords.longitude + '?exclude=minutely,hourly,daily,alerts,flag&units=si';

    console.log('weather: Contacting forecast.io...');
    // console.log(url);

    this._xhrWrapper(url, 'GET', function(req) {
      console.log('weather: Got API response!');
      if(req.status == 200) {
        var json = JSON.parse(req.response);

        var condition = json.currently.icon;
        if(condition === 'clear-day' || condition === 'clear-night'){
          condition = conditions.ClearSky;
        }
        else if(condition === 'partly-cloudy-day' || condition === 'partly-cloudy-night'){
          condition = conditions.FewClouds;
        }
        else if(condition === 'cloudy'){
          condition = conditions.BrokenClouds;
        }
        else if(condition === 'rain'){
          condition = conditions.Rain;
        }
        else if(condition === 'thunderstorm'){
          condition = conditions.Thunderstorm;
        }
        else if(condition === 'snow' || condition === 'sleet'){
          condition = conditions.Thunderstorm;
        }
        else if(condition === 'fog'){
          condition = conditions.Mist;
        }
        else {
          condition = conditions.Unknown;
        }

        var message = {
          'FIOW_REPLY': 1,
          'FIOW_TEMPK': Math.round(json.currently.temperature + 273.15),
          'FIOW_DESCRIPTION': json.currently.summary,
          'FIOW_DAY': json.currently.icon.indexOf("-day") > 0 ? 1 : 0,
          'FIOW_CONDITIONCODE':condition
        };

        url = 'http://nominatim.openstreetmap.org/reverse?format=json&lat=' + coords.latitude + '&lon=' + coords.longitude;
        this._xhrWrapper(url, 'GET', function(req) {
          if(req.status == 200) {
            var json = JSON.parse(req.response);
            var city = json.address.village || json.address.town || json.address.city || json.address.county || '';
            message['FIOW_NAME'] = city;
            Pebble.sendAppMessage(message);
          } else {
            // console.log('weather: Error fetching data (HTTP Status: ' + req.status + ')');
          }
        }.bind(this));

      } else {
        console.log('weather: Error fetching data (HTTP Status: ' + req.status + ')');
        Pebble.sendAppMessage({ 'FIOW_BADKEY': 1 });
      }
    }.bind(this));
  };

  this._getWeather = function(coords) {
    this._getWeatherF_IO(coords);
  };

  this._onLocationSuccess = function(pos) {
    console.log('weather: Location success');
    this._getWeather(pos.coords);
  };

  this._onLocationError = function(err) {
    console.log('weather: Location error');
    Pebble.sendAppMessage({
      'FIOW_LOCATIONUNAVAILABLE': 1
    });
  };

  this.appMessageHandler = function(dict, options) {
    console.log('weather: in appMessageHandler');
    if(dict.payload['FIOW_REQUEST']) {

      console.log('weather: Got fetch request from C app');

      this._apiKey = '';

      if(options && 'apiKey' in options){
        this._apiKey = options['apiKey'];
      }
      else if(dict.payload && 'FIOW_APIKEY' in dict.payload){
        this._apiKey = dict.payload['FIOW_APIKEY'];
      }

      var location = undefined;
      if(options && 'location' in options){
        location = options['location'];
      }
      else if(dict.payload && 'FIOW_LATITUDE' in dict.payload && 'FIOW_LONGITUDE' in dict.payload){
        location = { 'latitude' : dict.payload['FIOW_LATITUDE'] / 100000, 'longitude' : dict.payload['FIOW_LONGITUDE'] / 100000};
      }

      if(location) {
        console.log('weather: use user defined location');
        this._getWeather(location);
      }
      else {
        navigator.geolocation.getCurrentPosition(
          this._onLocationSuccess.bind(this),
          this._onLocationError.bind(this), {
            timeout: 15000,
            maximumAge: 60000
        });
      }
    }
    else {
      console.log('weather: unexpected payload');
    }
  };
};

var weather = new ForecastIoWeather();

Pebble.addEventListener('appmessage', function(e) {
  console.log('weather: appmessage received');
  weather.appMessageHandler(e);
});

Pebble.addEventListener('ready', function() {
  console.log('weather: PebbleKit JS ready.');

  // Update s_js_ready on watch
  Pebble.sendAppMessage({'JSReady': 1});
});