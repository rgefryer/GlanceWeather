#include <pebble.h>
#include "glancing_api.h"

// Enable debugging of glancing, currently just vibrate on glancing
#define DEBUG

typedef enum {
  GLANCE_STATE_NOT_ACTIVE = 0,   // User is probably not looking at the watch
  GLANCE_STATE_ACTIVE = 1,       // User might well be looking at the watch
  GLANCE_STATE_ACTIVE_ROLL = 2,  // User might be part way through a wrist roll
} GlanceFSMState;

typedef enum {
  GLANCE_INPUT_INACTIVE_ZONE = 0,
  GLANCE_INPUT_ACTIVE_ZONE = 1,
  GLANCE_INPUT_ROLL_ZONE = 2,
  GLANCE_INPUT_SHORT_TIMER_EXPIRED = 3,
  GLANCE_INPUT_LONG_TIMER_EXPIRED = 4,
  GLANCE_INPUT_ROLL_TIMER_EXPIRED = 5,
} GlanceFSMInput;

GlanceFSMState fsm_state = GLANCE_STATE_NOT_ACTIVE;
uint64_t short_timer = 0;  // Expiry time in milliseconds.  When running, the light may be on.
uint64_t long_timer = 0;   // Expiry time in milliseconds.  When running, the watch may be being looked at.
uint64_t roll_timer = 0;   // Expiry time in milliseconds

#define SET_TIMER(TIMER, MS, CURR_TIME) TIMER = (CURR_TIME + MS)
#define TIMER_ACTIVE(TIMER, CURR_TIME) (TIMER > CURR_TIME)
#define TIMER_EXPIRED(TIMER, CURR_TIME) ((TIMER != 0) && (TIMER <= CURR_TIME))
#define RESET_TIMER(TIMER) TIMER = 0

#define SHORT_TIMER_DURATION_MS   5000
#define LONG_TIMER_DURATION_MS   30000
#define ROLL_TIMER_DURATION_MS     500

#ifndef WITHIN
#define WITHIN(n, min, max) ((n) >= (min) && (n) <= (max))
#endif

#define WITHIN_ACCELEROMETER_ZONE(zone, data) ( \
  WITHIN((data).x, (zone).x_segment.start, (zone).x_segment.end) && \
  WITHIN((data).y, (zone).y_segment.start, (zone).y_segment.end) && \
  WITHIN((data).z, (zone).z_segment.start, (zone).z_segment.end) \
  )

typedef struct segment {
  int start;
  int end;
} segment;

typedef struct glancing_zone {
  segment x_segment;
  segment y_segment;
  segment z_segment;
} glancing_zone;

// watch tilted towards user, screen pointed toward user
glancing_zone active_zone = {
  .x_segment = { -500, 500},
  .y_segment = { -900, 200},
  .z_segment = { -1100, 0}
};

// arm hanging downward, select button pointing toward ground
glancing_zone inactive_zone = {
  .x_segment = { 800, 1000},
  .y_segment = { -500, 500},
  .z_segment = { -800, 800}
};

// arm horizontal, screen facing away from user, essentially wrist was rotated away from user
glancing_zone roll_zone = {
  .x_segment = { -600, 600},
  .y_segment = { 850, 1200},
  .z_segment = { -500, 500}
};

// Default to an empty callback when state changes
static void noop_glance_result_callback(GlancingData *data) {}
static GlancingDataHandler configured_glance_result_callback = noop_glance_result_callback;

// Default initial state to inactive
static GlancingData glance_data = {.result = GLANCE_OUTPUT_IDLE};

static bool light_on_when_active = false;
static bool allow_flick_backlight_when_inactive = false;
// the time duration of the fade out
static const int32_t LIGHT_FADE_TIME_MS = 500;

typedef struct time_ms_t {
  time_t sec;
  uint16_t ms;

  uint64_t milliseconds;
} time_ms_t;

static inline void send_glance_output(GlanceResult result) {
  configured_glance_result_callback(&result);
}

static inline bool is_glancing(uint64_t reading_time_ms) {
  return TIMER_ACTIVE(short_timer, reading_time_ms);
}

// Light interactive timer to save power by not turning on light in ambient sunlight
static void keep_light_on_while_active(void *data) {
  time_ms_t current_time;
  store_current_time(&current_time);

  if (is_glancing(current_time.milliseconds)) { 
    app_timer_register(LIGHT_FADE_TIME_MS, keep_light_on_while_active, data);
    light_enable_interaction();
  } else {
    // no control over triggering fade-out from API
    // so just turn light off for now
    light_enable(false);
  }
}

bool prefer_fast_sampling = false;
bool slow_sampling_active = false;
bool fast_sampling_active = false;
uint32_t sample_duration_ms = 0;

// Setup motion accel handler with low sample rate
// 10hz with buffer for 10 samples for 1 second update rate
static void start_slow_accelerometer_sampling()
{
  if (slow_sampling_active) {
    return;
  }

  if (fast_sampling_active) {
    accel_data_service_unsubscribe();
    fast_sampling_active = false;
  }

  accel_data_service_subscribe(10, prv_accel_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
  slow_sampling_active = true;  
  sample_duration_ms = 1000 / 10;
}

// Setup motion accel handler with high sample rate
// 25hz with buffer for 5 samples for 0.2 second update rate
static void start_fast_accelerometer_sampling()
{
  if (fast_sampling_active) {
    return;
  }

  if (slow_sampling_active) {
    accel_data_service_unsubscribe();
    slow_sampling_active = false;
  }
  
  accel_data_service_subscribe(5, prv_accel_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
  fast_sampling_active = true;
  sample_duration_ms = 1000 / 25;
}

static void store_current_time(time_ms_t *time_data)
{
  time_ms(&time_data->sec, &time_data->ms);  
  time_data->milliseconds = time_data->ms;
  time_data->milliseconds += 1000 * time_data->sec; 
}

static GlanceFsmState glance_fsm(GlanceFsmState state, GlanceFSMInput input, uint64_t time_of_input_ms) {

  GlanceFsmState = new_state = state;

  switch (state) {
    case GLANCE_STATE_NOT_ACTIVE:
      if (input == GLANCE_INPUT_ACTIVE_ZONE) {
        new_state = GLANCE_STATE_ACTIVE;
        SET_TIMER(short_timer, SHORT_TIMER_DURATION_MS, time_of_input_ms);
        SET_TIMER(long_timer, LONG_TIMER_DURATION_MS, time_of_input_ms);
        prefer_fast_sampling = true;
        send_glance_output(GLANCE_OUTPUT_ACTIVE);
      }
      break;

    case GLANCE_STATE_ACTIVE:
      switch (input) {
        case GLANCE_INPUT_INACTIVE_ZONE:
        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
          new_state = GLANCE_STATE_NOT_ACTIVE;
          send_glance_output(GLANCE_OUTPUT_IDLE);
          RESET_TIMER(roll_timer);
          RESET_TIMER(short_timer);
          RESET_TIMER(long_timer);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ROLL_ZONE:
          new_state = GLANCE_STATE_ACTIVE_ROLL;
          SET_TIMER(roll_timer, ROLL_TIMER_DURATION_MS, time_of_input_ms);
          break;

        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
          prefer_fast_sampling = false;
          break;
      }
      break;

    case GLANCE_STATE_ACTIVE_ROLL:
      switch (input) {
        case GLANCE_INPUT_INACTIVE_ZONE:
        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
          new_state = GLANCE_STATE_NOT_ACTIVE;
          send_glance_output(GLANCE_OUTPUT_IDLE);
          RESET_TIMER(roll_timer);
          RESET_TIMER(short_timer);
          RESET_TIMER(long_timer);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ACTIVE_ZONE:
          RESET_TIMER(roll_timer);
          new_state = GLANCE_STATE_ACTIVE;
          send_glance_output(GLANCE_OUTPUT_ROLL);
          SET_TIMER(short_timer, SHORT_TIMER_DURATION_MS, time_of_input_ms);
          SET_TIMER(long_timer, LONG_TIMER_DURATION_MS, time_of_input_ms);
          prefer_fast_sampling = true;
          break;

        case GLANCE_INPUT_ROLL_TIMER_EXPIRED:
          new_state = GLANCE_STATE_ACTIVE;
          break;

        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
          new_state = GLANCE_STATE_ACTIVE;
          prefer_fast_sampling = false;
          break;
      }
      break;
  }

  return state;
}


static void process_accelerometer_reading(AccelData *reading, uint64_t reading_time_ms) {

  if (WITHIN_ACCELEROMETER_ZONE(active_zone, *reading)) {
    fsm_state = glance_fsm(fsm_state, GLANCE_INPUT_ACTIVE_ZONE, reading_time_ms);
  }
  else if (WITHIN_ACCELEROMETER_ZONE(inactive_zone, *reading)) { 
    fsm_state = glance_fsm(fsm_state, GLANCE_INPUT_INACTIVE_ZONE, reading_time_ms);
  }
  else if (WITHIN_ACCELEROMETER_ZONE(roll_zone, *reading)) { 
    fsm_state = glance_fsm(fsm_state, GLANCE_INPUT_ROLL_ZONE, reading_time_ms);
  }

  if (TIMER_EXPIRED(roll_timer, reading_time_ms)) {
    RESET_TIMER(roll_timer);
    fsm_state = glance_fsm(fsm_state, GLANCE_INPUT_ROLL_TIMER_EXPIRED, reading_time_ms);
  }
  if (TIMER_EXPIRED(short_timer, reading_time_ms)) {
    RESET_TIMER(short_timer);
    fsm_state = glance_fsm(fsm_state, GLANCE_INPUT_SHORT_TIMER_EXPIRED, reading_time_ms);
  }
  if (TIMER_EXPIRED(long_timer, reading_time_ms)) {
    RESET_TIMER(long_timer);
    fsm_state = glance_fsm(fsm_state, GLANCE_INPUT_LONG_TIMER_EXPIRED, reading_time_ms);
  }
}


static void prv_accel_handler(AccelData *data, uint32_t num_samples) {
  static bool unglanced = true;
  uint32_t active_count = 0;
  time_ms_t current_time;
  store_current_time(&current_time);

  uint64_t input_time_ms = current_time.milliseconds - (num_samples * sample_duration_ms);

  for (uint32_t i = 0; i < num_samples; i++) {
    process_accelerometer_reading(&(data[i]), input_time_ms);
    input_time_ms += sample_duration_ms;
  }

  // Update the sampling speed
  if (prefer_fast_sampling) {
    start_fast_accelerometer_sampling();
  }
  else {
    start_slow_accelerometer_sampling();
  }
}

static void tap_event_handler(AccelAxisType axis, int32_t direction) {
  time_ms_t current_time;
  store_current_time(&current_time);

  // If not glancing, optionally allow "flick backlight" behaviour
  if (!is_glancing(current_time.milliseconds)) {
    if (allow_flick_backlight_when_inactive) {
      light_enable_interaction();
    } else {
      light_enable(false);
    }
  }
}

void glancing_service_subscribe(bool control_backlight, 
                                bool legacy_flick_backlight, GlancingDataHandler handler) {
  configured_glance_result_callback = handler;

  start_slow_accelerometer_sampling()
 
  allow_flick_backlight_when_inactive = legacy_flick_backlight;
  light_on_when_active = control_backlight;

  if (light_on_when_active) {
    // Setup tap service to support or disable flick to light behavior
    accel_tap_service_subscribe(tap_event_handler);
  }
}

void glancing_service_unsubscribe() {
  if (sampling_active) {
    accel_data_service_unsubscribe();
  }
  if (light_on_when_active) {
    accel_tap_service_unsubscribe();
  }
}
