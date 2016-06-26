#pragma once

#include <pebble.h>

#define FORECASTIO_WEATHER_BUFFER_SIZE 32

//! Possible statuses of the weather library
typedef enum {
  //! Weather library has not yet initiated a fetch
  ForecastIOWeatherStatusNotYetFetched = 0,
  //! Bluetooth is disconnected
  ForecastIOWeatherStatusBluetoothDisconnected,
  //! Weather data fetch is in progress
  ForecastIOWeatherStatusPending,
  //! Weather fetch failed
  ForecastIOWeatherStatusFailed,
  //! Weather fetched and available
  ForecastIOWeatherStatusAvailable,
  //! API key was bad
  ForecastIOWeatherStatusBadKey,
  //! Location not available
  ForecastIOWeatherStatusLocationUnavailable
} ForecastIOWeatherStatus;

//! Possible weather conditions
typedef enum {
  ForecastIOWeatherConditionClearSky = 0,
  ForecastIOWeatherConditionFewClouds,
  ForecastIOWeatherConditionScatteredClouds,
  ForecastIOWeatherConditionBrokenClouds,
  ForecastIOWeatherConditionShowerRain,
  ForecastIOWeatherConditionRain,
  ForecastIOWeatherConditionThunderstorm,
  ForecastIOWeatherConditionSnow,
  ForecastIOWeatherConditionMist,
  ForecastIOWeatherConditionUnknown = 1000
} ForecastIOWeatherConditionCode;

//! Struct containing weather data
typedef struct {
  //! Weather conditions string e.g: "Sky is clear"
  char description[FORECASTIO_WEATHER_BUFFER_SIZE];
  //! Name of the location from the weather feed
  char name[FORECASTIO_WEATHER_BUFFER_SIZE];
  //! Temperature in degrees Kelvin
  int16_t temp_k;
  int16_t temp_c;
  int16_t temp_f;
  //! day or night ?
  bool day;
  //! Condition code (see ForecastIOWeatherConditionCode values)
  ForecastIOWeatherConditionCode condition;
  //! Date that the data was received
  time_t timestamp;
} ForecastIOWeatherInfo;

//! Possible weather providers
typedef enum {
  //! OpenWeatherMap
  ForecastIOWeatherProviderOpenWeatherMap      = 0,
  //! WeatherUnderground
  ForecastIOWeatherProviderWeatherUnderground  = 1,
  //! Forecast.io
  ForecastIOWeatherProviderForecastIo          = 2,

  ForecastIOWeatherProviderUnknown             = 1000,
} ForecastIOWeatherProvider;

//! Struct containing coordinates
typedef struct {
  //! Latitude of the coordinates x 100000 (eg : 42.123456 -> 4212345)
  int32_t latitude; 
  //! Longitude of the coordinates x 100000 (eg : -12.354789 -> -1235478)
  int32_t longitude;
} ForecastIOWeatherCoordinates;

#define FORECASTIO_WEATHER_GPS_LOCATION (ForecastIOWeatherCoordinates){.latitude=0xFFFFFFFF,.longitude=0xFFFFFFFF}

//! Callback for a weather fetch
//! @param info The struct containing the weather data
//! @param status The current ForecastIOWeatherStatus, which may have changed.
typedef void(ForecastIOWeatherCallback)(ForecastIOWeatherInfo *info, ForecastIOWeatherStatus status);

//! Initialize the weather library. The data is fetched after calling this, and should be accessed
//! and stored once the callback returns data, if it is successful.
void forecast_io_weather_init();

//! Initialize the weather API key
//! @param api_key The API key for your weather provider.
void forecast_io_weather_set_api_key(const char *api_key);

//! Initialize the weather provider
//! @param provider The selected weather provider (default is OWM)
void forecast_io_weather_set_provider(ForecastIOWeatherProvider provider);

//! Initialize the weather location if you don't want to use the GPS
//! @param coordinates The coordinates (default is FORECASTIO_WEATHER_GPS_LOCATION)
void forecast_io_weather_set_location(const ForecastIOWeatherCoordinates coordinates);

//! Important: This uses the AppMessage system. You should only use AppMessage yourself
//! either before calling this, or after you have obtained your weather data.
//! @param callback Callback to be called once the weather.
//! @return true if the fetch message to PebbleKit JS was successful, false otherwise.
bool forecast_io_weather_fetch(ForecastIOWeatherCallback *callback);

//! Deinitialize and free the backing ForecastIOWeatherInfo.
void forecast_io_weather_deinit();

//! Peek at the current state of the weather library. You should check the ForecastIOWeatherStatus of the
//! returned ForecastIOWeatherInfo before accessing data members.
//! @return ForecastIOWeatherInfo object, internally allocated.
//! If NULL, forecast_io_weather_init() has not been called.
ForecastIOWeatherInfo* forecast_io_weather_peek();

//! Save the current state of the weather library
//! @param key The key to write to.
void forecast_io_weather_save(const uint32_t key);

//! Load the state of the weather library from persistent storage
//! @param key The key to read from.
void forecast_io_weather_load(const uint32_t key);