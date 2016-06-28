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

  this.beaufort_from_ms = function(speed_ms) {
    if (speed_ms < 5.5) {
      if (speed_ms < 1.6) {
        if (speed_ms < 0.3) {
          return 0;
        } else {
          return 1;
        }
      } else {
        if (speed_ms < 3.4) {
          return 2;
        } else {
          return 3;
        }
      }
    } else {
      if (speed_ms < 17.2) {
        if (speed_ms < 10.8) {
          if (speed_ms < 8) {
            return 4;
          } else {
            return 5;
          }
        } else {
          if (speed_ms < 13.9) {
            return 6;
          } else {
            return 7;
          }
        }
      } else {
        if (speed_ms < 24.5) {
          if (speed_ms < 20.8) {
            return 8;
          } else {
            return 9;
          }
        } else {
          if (speed_ms < 28.5) {
            return 10;
          } else {
            if (speed_ms < 32.7) {
              return 11;
            } else {
              return 12;
            }
          }
        }
      }
    }
  };
  
  this._getWeatherF_IO = function(coords) {
    var url = 'https://api.forecast.io/forecast/' + this._apiKey + '/' +
      coords.latitude + ',' + coords.longitude + '?exclude=currently,minutely,daily,alerts,flag&units=si&extend=hourly';

    console.log('weather: Contacting forecast.io... ' + url);
    // console.log(url);

    this._xhrWrapper(url, 'GET', function(req) {
      console.log('weather: Got API response!');
      if(req.status == 200) {
        
        // Send the hourly data as 7 messages of 24 hours
        var json = JSON.parse(req.response);
        var data = json.hourly.data;
        var day = 0;
        var data_list = [day];
        var next_time = data[0].time;
        for (var ii in data) {
          //var time = data[ii].time;
        
          
          // "time":1467068400,     - not required, we know the sequence.  1 byte to confirm, if seperate messages
          //                          increases by 3600 each hour
          
          // 5 bytes/hour * 168 hours = 840 bytes.   Or 120 bytes/day.
          // "precipIntensity":0,   - mm/hour, 1 byte (= 1 inch!)
          // "precipProbability":0, - 0->1, scale to 1 byte
          // "temperature":10.88,   - 1 byte, no decimal 0 is -50.
          // "windSpeed":4.22,      - 16 directions * 12bft = 128 options.  1 byte.
          // "windBearing":236,
          // "cloudCover":0.27,     - 0->1, scale to 1 byte

          // If results are missing, create empty results
          while (next_time < data[ii].time) {
            next_time += 3600;
            data_list.push(0);
            data_list.push(0);
            data_list.push(0);
            data_list.push(0);
            data_list.push(0);
            
            if (data_list.length == (24 * 5)) {
              var message = {
                'FIOW_REPLY': 1,
                'FIOW_DATA': data_list
              };
              Pebble.sendAppMessage(message);            
              day += 1;
              data_list = [day];
            }            
          }
          
          next_time = data[ii].time + 3600;
          
          if (data[ii].precipIntensity > 255) {
            data_list.push(255);
          }
          else {
            data_list.push(Math.ceil(data[ii].precipIntensity));
          }
          data_list.push(Math.floor(data[ii].precipProbability * 255 ));
          
          data_list.push(Math.round(data[ii].temperature) + 50);
          var wind_val = Math.round(data[ii].windBearing / 22.5) % 16;
          wind_val += 16 * this.beaufort_from_ms(data[ii].windSpeed);
          data_list.push(wind_val);
          data_list.push(Math.floor(data[ii].cloudCover * 255 ));
          
          if (data_list.length == (24 * 5)) {
            var message3 = {
              'FIOW_REPLY': 1,
              'FIOW_DATA': data_list
            };
            Pebble.sendAppMessage(message3);            
            day += 1;
            data_list = [day];
          }
        }
  
        // Send the location information
        var message2 = {
          'FIOW_REPLY': 1,
        };

        url = 'http://nominatim.openstreetmap.org/reverse?format=json&lat=' + coords.latitude + '&lon=' + coords.longitude;
        this._xhrWrapper(url, 'GET', function(req) {
          if(req.status == 200) {
            var json = JSON.parse(req.response);
            var city = json.address.village || json.address.town || json.address.city || json.address.county || '';
            message2['FIOW_NAME'] = city;
            Pebble.sendAppMessage(message2);
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