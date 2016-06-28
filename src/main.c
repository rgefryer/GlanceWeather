#include <pebble.h>
#include "glancing_api.h"
#include "get_weather.h"
#include <pebble-events/pebble-events.h>

/*
Next up...

- Weather displays
  - Today
  - Next 3 days

- Main watch display
  - Age of forecast
  - Make it look nice!
  
- Config
  - API key
  - Frequency of updates

*/

static char active_str[] = "ACTIVE";
static char inactive_str[] = "IDLE";
static char rolled_str[] = "  ROLLED";

static Window *window;

static TextLayer *time_text_layer;
char time_string[] = "00:00:00";

static TextLayer *weather_text_layer;

static bool seconds_mode = false;
static TextLayer *glance_text_layer;
char glance_string[16] = "IDLE";
static TextLayer *zone_text_layer;
char zone_string[16] = "NONE";
uint16_t roll_count = 0;

char battery_string[16] = "100%";
static TextLayer *battery_text_layer;

char bt_string[16] = "BTOK";
static TextLayer *bluetooth_text_layer;


static GlanceOutput state = GLANCE_OUTPUT_IDLE;

void tick_handler(struct tm *tick_time, TimeUnits units_changed){
  if (seconds_mode) {
    strftime(time_string, sizeof(time_string), 
        clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S", tick_time);
  } else {
    // Format only with hour:minute
    strftime(time_string, sizeof(time_string), 
        clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  }
  layer_mark_dirty(text_layer_get_layer(time_text_layer));
}

void glancing_callback(GlanceResult *data) {
  time_t current_time;
 
  switch (data->event) {
    case GLANCE_EVENT_OUTPUT:
      switch (data->result) {
        case GLANCE_OUTPUT_ACTIVE:
          seconds_mode = true;
          tick_timer_service_subscribe((SECOND_UNIT), tick_handler);
          //Kick the tick_handler for instant update
          current_time = time(NULL);
          tick_handler(localtime(&current_time),SECOND_UNIT);
          strncpy(glance_string, active_str, sizeof(glance_string) - 1);
          //window_set_background_color(window, GColorGreen); // Green for active
          break;

        case GLANCE_OUTPUT_ROLL:
          roll_count += 1;
          strncpy(glance_string, rolled_str, sizeof(glance_string) - 1);
          glance_string[0] = '0' + roll_count;
          //window_set_background_color(window, GColorBlue);  // Blue for timedout
          break;

        case GLANCE_OUTPUT_IDLE:
        default:
          roll_count = 0;
          seconds_mode = false;
          //Kick the tick_handler for instant update
          current_time = time(NULL);
          tick_handler(localtime(&current_time),MINUTE_UNIT);
          tick_timer_service_subscribe((MINUTE_UNIT), tick_handler);
          strncpy(glance_string, inactive_str, sizeof(glance_string) - 1);
          //window_set_background_color(window, GColorRed);  // Red for inactive
          break;
      }
      layer_mark_dirty(text_layer_get_layer(glance_text_layer));
      state = data->result;
      break;

    case GLANCE_EVENT_ZONE:
      switch (data->zone) {
        case GLANCE_ZONE_INACTIVE:
          strncpy(zone_string, "INACTIVE", sizeof(zone_string) - 1);
          break;
          
        case GLANCE_ZONE_ACTIVE:
          strncpy(zone_string, "ACTIVE", sizeof(zone_string) - 1);
          break;
          
        case GLANCE_ZONE_ROLL:
          strncpy(zone_string, "ROLL", sizeof(zone_string) - 1);
          break;
          
        case GLANCE_ZONE_NONE:
          strncpy(zone_string, "NONE", sizeof(zone_string) - 1);
          break;

      }
      layer_mark_dirty(text_layer_get_layer(zone_text_layer));
      break;
  }
}

char *weather_status = "NotYetFetched";

static void weather_callback(ForecastIOWeatherInfo *info, ForecastIOWeatherStatus status) {
  switch(status) {
    case ForecastIOWeatherStatusAvailable:
    {
      /*
      static char s_buffer[256];
      snprintf(s_buffer, sizeof(s_buffer),
        "Temperature (K/C/F): %d/%d/%d\n\nName:\n%s\n\nDescription:\n%s",
        info->temp_k, info->temp_c, info->temp_f, info->name, info->description);
      text_layer_set_text(s_text_layer, s_buffer);
      */
      weather_status = "Available";
    }
      break;
    case ForecastIOWeatherStatusNotYetFetched:
      weather_status = "NotYetFetched";
      break;
    case ForecastIOWeatherStatusBluetoothDisconnected:
      weather_status = "BluetoothDisconnected";
      break;
    case ForecastIOWeatherStatusPending:
      weather_status = "Pending";
      break;
    case ForecastIOWeatherStatusFailed:
      weather_status = "Failed";
      break;
    case ForecastIOWeatherStatusBadKey:
      weather_status = "BadKey";
      break;
    case ForecastIOWeatherStatusLocationUnavailable:
      weather_status = "LocationUnavailable";
      break;
  }
  text_layer_set_text(weather_text_layer, weather_status); 
}

static void handle_battery(BatteryChargeState charge_state) {

  if (charge_state.is_charging) {
    snprintf(battery_string, sizeof(battery_string), "...");
  } else {
    snprintf(battery_string, sizeof(battery_string), "%d%%", charge_state.charge_percent);
  }
  text_layer_set_text(battery_text_layer, battery_string);
}

static void handle_bluetooth(bool connected) {
  text_layer_set_text(bluetooth_text_layer, connected ? "BTOK" : "NOBT");
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const GPoint center = grect_center_point(&bounds);

  weather_text_layer = text_layer_create(GRect (0, center.y - 30, bounds.size.w, 32)); 
  text_layer_set_text(weather_text_layer, weather_status);
  text_layer_set_font(weather_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(weather_text_layer, GColorWhite);
  text_layer_set_background_color(weather_text_layer, GColorClear);
  text_layer_set_text_alignment(weather_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(weather_text_layer));
  
  time_text_layer = text_layer_create(GRect (0, center.y, bounds.size.w, 32)); 
  text_layer_set_text(time_text_layer, time_string);
  text_layer_set_font(time_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(time_text_layer, GColorWhite);
  text_layer_set_background_color(time_text_layer, GColorClear);
  text_layer_set_text_alignment(time_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(time_text_layer));

  glance_text_layer = text_layer_create(GRect (0, center.y + 30, bounds.size.w, 32)); 
  text_layer_set_text(glance_text_layer, glance_string);
  text_layer_set_font(glance_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(glance_text_layer, GColorWhite);
  text_layer_set_background_color(glance_text_layer, GColorClear);
  text_layer_set_text_alignment(glance_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(glance_text_layer));

  zone_text_layer = text_layer_create(GRect (0, center.y - 60, bounds.size.w, 32)); 
  text_layer_set_text(zone_text_layer, zone_string);
  text_layer_set_font(zone_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(zone_text_layer, GColorWhite);
  text_layer_set_background_color(zone_text_layer, GColorClear);
  text_layer_set_text_alignment(zone_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(zone_text_layer));

  battery_text_layer = text_layer_create(GRect (0, center.y - 90, bounds.size.w / 2, 32)); 
  text_layer_set_text(battery_text_layer, battery_string);
  text_layer_set_font(battery_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(battery_text_layer, GColorWhite);
  text_layer_set_background_color(battery_text_layer, GColorClear);
  text_layer_set_text_alignment(battery_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(battery_text_layer));

  bluetooth_text_layer = text_layer_create(GRect (bounds.size.w / 2, center.y - 90, bounds.size.w / 2, 32)); 
  text_layer_set_text(bluetooth_text_layer, bt_string);
  text_layer_set_font(bluetooth_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(bluetooth_text_layer, GColorWhite);
  text_layer_set_background_color(bluetooth_text_layer, GColorClear);
  text_layer_set_text_alignment(bluetooth_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(bluetooth_text_layer));

  // Force time update
  time_t current_time = time(NULL);
  struct tm *current_tm = localtime(&current_time);
  tick_handler(current_tm, MINUTE_UNIT);
  
  // Setup tick time handler
  tick_timer_service_subscribe((MINUTE_UNIT), tick_handler);

  battery_state_service_subscribe(handle_battery);

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = handle_bluetooth
  });  
  
  // Enable Glancing with normal 5 second timeout, takeover backlight
  glancing_service_subscribe(true, false, glancing_callback);
  
  handle_battery(battery_state_service_peek());  
}

static void window_unload(Window *window) {
  text_layer_destroy(time_text_layer);
  text_layer_destroy(weather_text_layer);
  text_layer_destroy(glance_text_layer);
  text_layer_destroy(zone_text_layer);
  text_layer_destroy(battery_text_layer);
  text_layer_destroy(bluetooth_text_layer);
}

static void init(void) {
  window = window_create();
  window_set_background_color(window, GColorRed);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(window, true);
  
  forecast_io_weather_init(weather_callback);
  forecast_io_weather_set_api_key("ddb8191c20d47e3cd47c91912e5c200c");
  events_app_message_open();
}

static void deinit(void) {
  window_destroy(window);
  forecast_io_weather_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
