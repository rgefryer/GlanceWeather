#include <pebble.h>
#include "get_weather.h"

#include <pebble-events/pebble-events.h>

static ForecastIOWeatherInfo *s_info;
static ForecastIOWeatherCallback *s_callback;
static ForecastIOWeatherStatus s_status;

static char s_api_key[33];
static ForecastIOWeatherCoordinates s_coordinates;
static ForecastIOWeatherCallback *s_weather_callback;

static AppTimer *s_timeout_timer;
static EventHandle s_event_handle;

static void timeout_timer_handler(void *);

static bool js_ready = false;
static bool pending_refresh = false;
static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *reply_tuple = dict_find(iter, MESSAGE_KEY_FIOW_REPLY);
  if(reply_tuple) {
    
    Tuple *name_tuple = dict_find(iter, MESSAGE_KEY_FIOW_NAME);
    if (name_tuple) {
      strncpy(s_info->name, name_tuple->value->cstring, FORECASTIO_WEATHER_BUFFER_SIZE);
      
      // Tell the user we're good to go
      s_status = ForecastIOWeatherStatusAvailable;
      s_callback(s_info, s_status);
      
      // Schedule another update in 30 mins
      if (!pending_refresh) {
        const int retry_interval_ms = 30 * 60 * 1000;
        app_timer_register(retry_interval_ms, timeout_timer_handler, NULL);
        pending_refresh = true;        
      }
    }

    Tuple *data_tuple = dict_find(iter, MESSAGE_KEY_FIOW_DATA);
    if (data_tuple) {
      uint8_t *data = data_tuple->value->data;
      
      uint32_t epoch_time = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      uint32_t time_key = 2 * data[4];
      uint32_t data_key = 2 * data[4] + 1;
      
      if (persist_exists(time_key)) persist_delete(time_key);
      if (persist_exists(data_key)) persist_delete(data_key);
      
      persist_write_int(time_key, epoch_time);
      persist_write_data(data_key, &data[5], 24 * 5);
    }

  }

  Tuple *err_tuple = dict_find(iter, MESSAGE_KEY_FIOW_BADKEY);
  if(err_tuple) {
    s_status = ForecastIOWeatherStatusBadKey;
    s_callback(s_info, s_status);
  }

  err_tuple = dict_find(iter, MESSAGE_KEY_FIOW_LOCATIONUNAVAILABLE);
  if(err_tuple) {
    s_status = ForecastIOWeatherStatusLocationUnavailable;
    s_callback(s_info, s_status);
  }
  
  Tuple *ready_tuple = dict_find(iter, MESSAGE_KEY_JSReady);
  if(ready_tuple) {
    js_ready = true;
    forecast_io_weather_fetch();
  }  
}

static void fail_and_callback() {
  s_status = ForecastIOWeatherStatusFailed;
  s_callback(s_info, s_status);
}

static void outbox_failed_handler(DictionaryIterator *iter, 
                                      AppMessageResult reason, void *context) {
  // Message failed before timer elapsed, reschedule for later
  if(s_timeout_timer) {
    app_timer_cancel(s_timeout_timer);
  }

  // Inform the user of the failure
  fail_and_callback();

  // Use the timeout handler to perform the same action - resend the message
  const int retry_interval_ms = 500;
  app_timer_register(retry_interval_ms, timeout_timer_handler, NULL);
}

static bool fetch() {
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if(result != APP_MSG_OK) {
    fail_and_callback();
    return false;
  }

  dict_write_uint8(out, MESSAGE_KEY_FIOW_REQUEST, 1);

  if(strlen(s_api_key) > 0)
    dict_write_cstring(out, MESSAGE_KEY_FIOW_APIKEY, s_api_key);

  if(s_coordinates.latitude != (int32_t)0xFFFFFFFF && s_coordinates.longitude != (int32_t)0xFFFFFFFF){
    dict_write_int32(out, MESSAGE_KEY_FIOW_LATITUDE, s_coordinates.latitude);
    dict_write_int32(out, MESSAGE_KEY_FIOW_LONGITUDE, s_coordinates.longitude);
  }

  result = app_message_outbox_send();
  if(result != APP_MSG_OK) {
    fail_and_callback();
    return false;
  }

  // Schedule the timeout timer
  const int interval_ms = 1000;
  s_timeout_timer = app_timer_register(interval_ms, timeout_timer_handler, NULL);  
  
  s_status = ForecastIOWeatherStatusPending;
  s_callback(s_info, s_status);
  return true;
}

static void timeout_timer_handler(void *context) {
  // Could update status here
  
  // Retry the message
  pending_refresh = false;
  fetch();
}

static void outbox_sent_handler(DictionaryIterator *iter, void *context) {
  // Successful message, the timeout is not needed anymore for this message
  app_timer_cancel(s_timeout_timer);
}

void forecast_io_weather_init(ForecastIOWeatherCallback *callback) {
  if(s_info) {
    free(s_info);
  }

  s_callback = callback;
  s_info = (ForecastIOWeatherInfo*)malloc(sizeof(ForecastIOWeatherInfo));
  s_api_key[0] = 0;
  s_coordinates = FORECASTIO_WEATHER_GPS_LOCATION;
  s_status = ForecastIOWeatherStatusNotYetFetched;
  events_app_message_request_inbox_size(200);
  events_app_message_request_outbox_size(100);
  s_event_handle = events_app_message_register_inbox_received(inbox_received_handler, NULL);
  app_message_register_outbox_sent(outbox_sent_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
}

void forecast_io_weather_set_api_key(const char *api_key) {
  if(!api_key) {
    s_api_key[0] = 0;
  }
  else {
    strncpy(s_api_key, api_key, sizeof(s_api_key));
    if (js_ready) {
      fetch();
    }
  }
}

void forecast_io_weather_set_location(const ForecastIOWeatherCoordinates coordinates){
  s_coordinates = coordinates;
}

bool forecast_io_weather_fetch() {
  if(!s_info) {
    return false;
  }

  if(!s_callback) {
    return false;
  }

  if(!bluetooth_connection_service_peek()) {
    s_status = ForecastIOWeatherStatusBluetoothDisconnected;
    s_callback(s_info, s_status);
    return false;
  }

  return fetch();
}

void forecast_io_weather_deinit() {
  if(s_info) {
    free(s_info);
    s_info = NULL;
    s_callback = NULL;
    events_app_message_unsubscribe(s_event_handle);
  }
}

ForecastIOWeatherInfo* forecast_io_weather_peek() {
  if(!s_info) {
    return NULL;
  }

  return s_info;
}

void forecast_io_weather_save(const uint32_t key){
  if(!s_info) {
    return;
  }

  persist_write_data(key, s_info, sizeof(ForecastIOWeatherInfo));
}

void forecast_io_weather_load(const uint32_t key){
  if(!s_info) {
    return;
  }

  if(persist_exists(key)){
    persist_read_data(key, s_info, sizeof(ForecastIOWeatherInfo));
  }
}
