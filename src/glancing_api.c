#include <pebble.h>
#include "glancing_api.h"

// Enable debugging of glancing, currently just vibrate on glancing
#define DEBUG

typedef enum {
  GLANCE_STATE_NOT_ACTIVE = 0,      // User is probably not looking at the watch
  GLANCE_STATE_PENDING_ACTIVE = 1,  // Waiting in the active zone               
  GLANCE_STATE_ACTIVE = 2,   // User might well be looking at the watch
  GLANCE_STATE_NEW_ACTIVE = 2,          // User might well be looking at the watch
  GLANCE_STATE_OLD_ACTIVE = 3,          // User might well be looking at the watch
  GLANCE_STATE_IDLE_ACTIVE = 4,     // Activity expired, but not yet idle
  GLANCE_STATE_MAYBE_ROLL = 5,     // Left active, maybe on the way to rolling
  GLANCE_STATE_ROLL = 6,     // User might be part way through a wrist roll
  GLANCE_STATE_ROLL_IDLE = 7,     // User might be part way through a wrist roll to reactivate
} GlanceFSMState;

typedef enum {
  GLANCE_INPUT_DROPPED_ZONE = 0,
  GLANCE_INPUT_ACTIVE_ZONE = 1,
  GLANCE_INPUT_ROLL_ZONE = 2,
  GLANCE_INPUT_UNKNOWN_ZONE = 3,
  GLANCE_INPUT_SHORT_TIMER_EXPIRED = 4,
  GLANCE_INPUT_LONG_TIMER_EXPIRED = 5,
  GLANCE_INPUT_ROLL_TIMER_EXPIRED = 6,
  GLANCE_INPUT_ACTIVATION_TIMER_EXPIRED = 7,
} GlanceFSMInput;

static GlanceFSMState fsm_state = GLANCE_STATE_NOT_ACTIVE;

static bool discard_sample = false;

#define SET_TIMER(TIMER, MS, CURR_TIME) TIMER = (CURR_TIME + MS)
#define TIMER_ACTIVE(TIMER, CURR_TIME) (TIMER > CURR_TIME)
#define TIMER_EXPIRED(TIMER, CURR_TIME) ((TIMER != 0) && (TIMER <= CURR_TIME))
#define RESET_TIMER(TIMER) TIMER = 0

int32_t activation_timer_duration = 500;    // Min time in active zone before triggering
static uint64_t activation_timer = 0;  // Expiry time in milliseconds

int32_t new_active_timer_duration = 5000;   // Time to sit in active zone with light on and fast polling
static uint64_t new_active_timer = 0;  // Expiry time in milliseconds.  When running, the light may be on.

int32_t old_active_timer_duration = 15000;  // Time to linger in active zone with nothing happening
static uint64_t old_active_timer = 0;  // Expiry time in milliseconds.  When running, the watch may be being looked at.

int32_t roll_timer_duration = 1000;         // Time allowed to return from roll
static uint64_t roll_timer = 0;        // Expiry time in milliseconds

void glancing_service_update_timers(int32_t active_timer1, int32_t active_timer2, int32_t rolla_timer) {
  new_active_timer_duration = active_timer1;
  old_active_timer_duration = active_timer2;
}

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
glancing_zone dropped_zone = {
  .x_segment = { 800, 1000},
  .y_segment = { -500, 500},
  .z_segment = { -800, 800}
};

// arm horizontal, screen facing away from user, essentially wrist was rotated away from user
glancing_zone roll_zone = {
  .x_segment = { -600, 600},
  .y_segment = { 600, 1200},  // Lower Y was originally 850 - now easier to hit at low sample rate
  .z_segment = { -500, 500}
};

GlanceZone current_zone = GLANCE_ZONE_NONE;

static void prv_accel_handler(AccelData *data, uint32_t num_samples);

// Default to an empty callback when state changes
static void noop_glance_result_callback(GlanceResult *data) {}
static GlanceResultHandler configured_glance_result_callback = noop_glance_result_callback;

// Default initial state to inactive
static GlanceResult glance_data = {.result = GLANCE_OUTPUT_IDLE};

static bool light_on_when_active = false;
static bool allow_flick_backlight_when_inactive = false;
// the time duration of the fade out
static const int32_t LIGHT_FADE_TIME_MS = 500;

typedef struct time_ms_t {
  time_t sec;
  uint16_t ms;

  uint64_t milliseconds;
} time_ms_t;

static inline void send_glance_output(GlanceOutput output) {
  glance_data.result = output;
  glance_data.event = GLANCE_EVENT_OUTPUT;
  configured_glance_result_callback(&glance_data);
}

static inline void send_glance_zone(GlanceZone zone) {
  glance_data.zone = zone;
  glance_data.event = GLANCE_EVENT_ZONE;
  configured_glance_result_callback(&glance_data);
}

static inline bool is_glancing(uint64_t reading_time_ms) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "is_glancing");
  return TIMER_ACTIVE(new_active_timer, reading_time_ms);
}

static void store_current_time(time_ms_t *time_data)
{
  time_ms(&time_data->sec, &time_data->ms);  
  time_data->milliseconds = time_data->ms;
  time_data->milliseconds += 1000 * time_data->sec; 
}

// Light interactive timer to save power by not turning on light in ambient sunlight
bool holding_light_on = false;
static void keep_light_on_while_active_internal(void *data) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "keep_light_on_while_active");
  
  if (!light_on_when_active) {
    return;
  }
  
  time_ms_t current_time;
  store_current_time(&current_time);

  if (is_glancing(current_time.milliseconds)) { 
    app_timer_register(LIGHT_FADE_TIME_MS, keep_light_on_while_active_internal, data);
    light_enable_interaction();
    holding_light_on = true;
  } else {
    // no control over triggering fade-out from API
    // so just turn light off for now
    light_enable(false);
    holding_light_on = false;
  }
}

static void keep_light_on_while_active() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "keep_light_on_while_active");
  
  if (holding_light_on || !light_on_when_active) {
    return;
  }
  
  keep_light_on_while_active_internal(NULL);
}

bool prefer_fast_sampling = false;
bool slow_sampling_active = false;
bool fast_sampling_active = false;
uint32_t sample_duration_ms = 0;

// Setup motion accel handler with low sample rate
// 10hz with buffer for 7 samples for 0.7 second poll rate
static void start_slow_accelerometer_sampling(void *data) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "start_slow_accelerometer_sampling");
 
  if (slow_sampling_active) {
    return;
  }
  
  if (fast_sampling_active) {
    accel_service_set_samples_per_update(7);
    fast_sampling_active = false;  
  }
  else {
    accel_data_service_subscribe(7, prv_accel_handler);
  }
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
  slow_sampling_active = true;  
  discard_sample = true;
  sample_duration_ms = 1000 / 10;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Exit start_slow_accelerometer_sampling");
}

// Setup motion accel handler with high sample rate
// 25hz with buffer for 5 samples for 0.2 second poll rate
static void start_fast_accelerometer_sampling(void *data) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "start_fast_accelerometer_sampling");
  if (fast_sampling_active) {
    return;
  }

  if (slow_sampling_active) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "accel_service_set_samples_per_update");
    accel_service_set_samples_per_update(5);
    slow_sampling_active = false;
  }
  else {
    accel_data_service_subscribe(5, prv_accel_handler);
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "accel_service_set_sampling_rate");
  accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
  fast_sampling_active = true;
  discard_sample = true;
  sample_duration_ms = 1000 / 25;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Exit start_fast_accelerometer_sampling");
}

static GlanceFSMState glance_fsm(GlanceFSMState state, GlanceFSMInput input, uint64_t time_of_input_ms) {
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "glance_fsm(%d, %d)", state, input);

  APP_LOG(APP_LOG_LEVEL_DEBUG, "fsm_input %d", input);
  
  GlanceFSMState new_state = state;

  switch (state) {

    // Watch is idle, we don't expect anyone to be looking at it,
    // no timers running, sampling rate is slow.
    case GLANCE_STATE_NOT_ACTIVE:
      switch (input) {

        case GLANCE_INPUT_ACTIVE_ZONE:
          new_state = GLANCE_STATE_PENDING_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_PENDING_ACTIVE");
          prefer_fast_sampling = true;
          SET_TIMER(activation_timer, activation_timer_duration, time_of_input_ms);
          break;

        case GLANCE_INPUT_DROPPED_ZONE:
        case GLANCE_INPUT_ROLL_ZONE:
        case GLANCE_INPUT_UNKNOWN_ZONE:
        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
        case GLANCE_INPUT_ROLL_TIMER_EXPIRED:
        case GLANCE_INPUT_ACTIVATION_TIMER_EXPIRED:
          break;
      }
      break;

    // In Active zone, waiting for a timeout to decide we're really active.
    // activation_timer running, sampling rate is fast.
    case GLANCE_STATE_PENDING_ACTIVE:
      switch (input) {

        case GLANCE_INPUT_ROLL_ZONE:
        case GLANCE_INPUT_DROPPED_ZONE:
        case GLANCE_INPUT_UNKNOWN_ZONE:
          new_state = GLANCE_STATE_NOT_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NOT_ACTIVE");
          RESET_TIMER(activation_timer);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ACTIVE_ZONE:
        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
        case GLANCE_INPUT_ROLL_TIMER_EXPIRED:
          break;

        case GLANCE_INPUT_ACTIVATION_TIMER_EXPIRED:
          new_state = GLANCE_STATE_NEW_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NEW_ACTIVE");
          SET_TIMER(new_active_timer, new_active_timer_duration, time_of_input_ms);
          SET_TIMER(old_active_timer, old_active_timer_duration, time_of_input_ms);
          send_glance_output(GLANCE_OUTPUT_ACTIVE);
          keep_light_on_while_active();
          break;
      }
      break;

    // Watch has recently gone active.  
    // Initially, new_active_timer is running, and sampling rate is fast.
    // Later, old_active_timer is running, and sampling rate is slow.
    case GLANCE_STATE_ACTIVE:
      switch (input) {
        case GLANCE_INPUT_DROPPED_ZONE:
          new_state = GLANCE_STATE_NOT_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NOT_ACTIVE");
          send_glance_output(GLANCE_OUTPUT_IDLE);
          RESET_TIMER(new_active_timer);
          RESET_TIMER(old_active_timer);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ROLL_ZONE:
          new_state = GLANCE_STATE_ROLL;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_ROLL");
          SET_TIMER(roll_timer, roll_timer_duration, time_of_input_ms);
          prefer_fast_sampling = true;
          break;

        case GLANCE_INPUT_UNKNOWN_ZONE:
          new_state = GLANCE_STATE_MAYBE_ROLL:
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_ROLL");
          SET_TIMER(roll_timer, roll_timer_duration, time_of_input_ms);
          break;

        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
          SET_TIMER(old_active_timer, old_active_timer_duration, time_of_input_ms);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
          if ((current_zone == GLANCE_ZONE_ACTIVE) || (current_zone == GLANCE_ZONE_NONE)) {
            new_state = GLANCE_STATE_IDLE_ACTIVE;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_IDLE_ACTIVE");
          }
          send_glance_output(GLANCE_OUTPUT_IDLE);
          break;
        
        case GLANCE_INPUT_ACTIVE_ZONE:
        case GLANCE_INPUT_ROLL_TIMER_EXPIRED:
        case GLANCE_INPUT_ACTIVATION_TIMER_EXPIRED:
         break;
      }
      break;

    // Watch is idle (active state has timed out) but still in the 
    // active zone.  No timers are running.  sampling_rate is slow.
    case GLANCE_STATE_IDLE_ACTIVE:
      switch (input) {
        case GLANCE_INPUT_DROPPED_ZONE:
          new_state = GLANCE_STATE_NOT_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NOT_ACTIVE");
          break;

        case GLANCE_INPUT_ROLL_ZONE:
          SET_TIMER(roll_timer, roll_timer_duration, time_of_input_ms);
          prefer_fast_sampling = true;
          new_state = GLANCE_STATE_ROLL_IDLE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_ROLL_IDLE");
          break;

        case GLANCE_INPUT_UNKNOWN_ZONE:
        case GLANCE_INPUT_ACTIVE_ZONE:
        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
        case GLANCE_INPUT_ROLL_TIMER_EXPIRED:
        case GLANCE_INPUT_ACTIVATION_TIMER_EXPIRED:
          break;
      }
      break;

    // Watch has shifted from active, maybe about to be rolled.
    // roll_timer is running.  old_active_timer or new_active_timer may be running.  sampling_rate may be fast or slow.
    case GLANCE_STATE_MAYBE_ROLL:
      switch (input) {
        case GLANCE_INPUT_DROPPED_ZONE:
          new_state = GLANCE_STATE_NOT_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NOT_ACTIVE");
          send_glance_output(GLANCE_OUTPUT_IDLE);
          RESET_TIMER(new_active_timer);
          RESET_TIMER(old_active_timer);
          RESET_TIMER(roll_active_timer);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ROLL_ZONE:
          new_state = GLANCE_STATE_ROLL;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_ROLL");
          RESET_TIMER(old_active_timer);
          prefer_fast_sampling = true;
          break;

        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
          SET_TIMER(old_active_timer, old_active_timer_duration, time_of_input_ms);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ACTIVE_ZONE:
        case GLANCE_INPUT_ROLL_TIMER_EXPIRED:
        
        
        case GLANCE_INPUT_UNKNOWN_ZONE:
        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
        case GLANCE_INPUT_ACTIVATION_TIMER_EXPIRED:
         break;
      }
      break;
    
    
    // Watch has been rolled from active, hopefully waiting to be rolled back.
    // roll_timer is running.  new_active_timer may be running.  sampling_rate is fast.
    case GLANCE_STATE_ROLL:
      switch (input) {
        case GLANCE_INPUT_DROPPED_ZONE:
        case GLANCE_INPUT_ROLL_TIMER_EXPIRED:
          new_state = GLANCE_STATE_NOT_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NOT_ACTIVE");
          send_glance_output(GLANCE_OUTPUT_IDLE);
          RESET_TIMER(roll_timer);
          RESET_TIMER(new_active_timer);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ACTIVE_ZONE:
          RESET_TIMER(roll_timer);
          SET_TIMER(new_active_timer, new_active_timer_duration, time_of_input_ms);
          new_state = GLANCE_STATE_NEW_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NEW_ACTIVE");
          keep_light_on_while_active();
          send_glance_output(GLANCE_OUTPUT_ROLL);
          break;

        case GLANCE_INPUT_ROLL_ZONE:
        case GLANCE_INPUT_UNKNOWN_ZONE:
        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
        case GLANCE_INPUT_ACTIVATION_TIMER_EXPIRED:
          break;
      } 
      break;
    
    // Watch has been rolled from idle, hopefully waiting to be rolled back.
    // roll_timer is running.  sampling_rate is fast.
    case GLANCE_STATE_ROLL_IDLE:
      switch (input) {
        case GLANCE_INPUT_DROPPED_ZONE:
          new_state = GLANCE_STATE_NOT_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NOT_ACTIVE");
          RESET_TIMER(roll_timer);
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ROLL_TIMER_EXPIRED:
          new_state = GLANCE_STATE_IDLE_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_IDLE_ACTIVE");
          prefer_fast_sampling = false;
          break;

        case GLANCE_INPUT_ACTIVE_ZONE:
          RESET_TIMER(roll_timer);
          SET_TIMER(new_active_timer, new_active_timer_duration, time_of_input_ms);
          new_state = GLANCE_STATE_NEW_ACTIVE;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "new_state = GLANCE_STATE_NEW_ACTIVE");
          keep_light_on_while_active();
          send_glance_output(GLANCE_OUTPUT_ACTIVE);
          break;

        case GLANCE_INPUT_ROLL_ZONE:
        case GLANCE_INPUT_UNKNOWN_ZONE:
        case GLANCE_INPUT_SHORT_TIMER_EXPIRED:
        case GLANCE_INPUT_LONG_TIMER_EXPIRED:
        case GLANCE_INPUT_ACTIVATION_TIMER_EXPIRED:
          break;
      } 
      break;
  }

  //APP_LOG(APP_LOG_LEVEL_DEBUG, "glance_fsm returns %d", new_state);
  return new_state;
}

static GlanceOutputState output_state = GLANCE_OUTPUT_IDLE;
static void set_output_state(GlanceOutputState new_state) {
  if (new_state != output_state) {
    output_state = new_state;
    send_glance_output(new_state);
  }
}

typedef enum {
  GLANCE_STATE3_IDLE = 0,      // User is probably not looking at the watch
  GLANCE_STATE3_NEW_ACTIVE = 2,          // User might well be looking at the watch
  GLANCE_STATE3_OLD_ACTIVE = 3,          // User might well be looking at the watch
  GLANCE_STATE3_IDLE_ACTIVE = 4,     // Activity expired, but not yet idle
} GlanceFSMState3;

typedef enum {
  GLANCE_INPUT3_DROPPED = 0,
  GLANCE_INPUT3_ROLLED = 1,
  GLANCE_INPUT3_ACTIVE = 2,
  GLANCE_INPUT3_SHORT_TIMER_EXPIRED = 3,
  GLANCE_INPUT3_LONG_TIMER_EXPIRED = 4
} GlanceFSMInput3;

static GlanceFSMState3 state3 = GLANCE_STATE3_IDLE;
static void glance_fsm3(GlanceFSMInput3 input, uint64_t time_of_input_ms) {
  GlanceFSMState3 new_state = state3;
  
  if (input == GLANCE_INPUT3_DROPPED) {
    new_state = GLANCE_STATE3_IDLE;
    RESET_TIMER(new_active_timer);
    RESET_TIMER(old_active_timer);
    set_output_state(GLANCE_OUTPUT_IDLE);
    prefer_fast_sampling = false;
  }
  
  else if (input == GLANCE_INPUT3_ROLLED) {
    new_state = GLANCE_STATE3_NEW_ACTIVE;
    if ((state3 == GLANCE_STATE3_IDLE) || (state3 == GLANCE_STATE3_IDLE_ACTIVE)) {
      set_output_state(GLANCE_OUTPUT_ACTIVE);
    else {
      send_glance_output(GLANCE_OUTPUT_ROLL);
    }
    RESET_TIMER(new_active_timer);
    RESET_TIMER(old_active_timer);
    SET_TIMER(new_active_timer, new_active_timer_duration, time_of_input_ms);
    keep_light_on_while_active();
    prefer_fast_sampling = true;
  }
  
  else if ((input == GLANCE_INPUT3_ACTIVE) && (state3 == GLANCE_STATE3_IDLE)) {
    new_state = GLANCE_STATE3_NEW_ACTIVE;
    RESET_TIMER(new_active_timer);
    RESET_TIMER(old_active_timer);
    SET_TIMER(new_active_timer, new_active_timer_duration, time_of_input_ms);
    set_output_state(GLANCE_OUTPUT_ACTIVE);
    keep_light_on_while_active();
    prefer_fast_sampling = true;
  }
    
  else if ((input == GLANCE_INPUT3_SHORT_TIMER_EXPIRED) && (state3 == GLANCE_STATE3_NEW_ACTIVE)) {
    new_state = GLANCE_STATE3_OLD_ACTIVE;
    SET_TIMER(old_active_timer, old_active_timer_duration, time_of_input_ms);
    set_output_state(GLANCE_OUTPUT_ACTIVE);
    prefer_fast_sampling = false;
  }
    
  else if ((input == GLANCE_INPUT3_LONG_TIMER_EXPIRED) && (state3 == GLANCE_STATE3_OLD_ACTIVE)) {
    new_state = GLANCE_STATE3_IDLE_ACTIVE;
    set_output_state(GLANCE_OUTPUT_IDLE);
    prefer_fast_sampling = false;
  }
  
  state3 = new_state;
}  


typedef enum {
  GLANCE_STATE2_NONE = 0,      // User is probably not looking at the watch
  GLANCE_STATE2_ROLL = 6,     // User might be part way through a wrist roll
} GlanceFSMState2;

typedef enum {
  GLANCE_INPUT2_DROPPED_ZONE = 0,
  GLANCE_INPUT2_ACTIVE_ZONE = 1,
  GLANCE_INPUT2_ROLL_ZONE = 2,
  GLANCE_INPUT2_UNKNOWN_ZONE = 3,
  GLANCE_INPUT2_SHORT_TIMER_EXPIRED = 3,
  GLANCE_INPUT2_LONG_TIMER_EXPIRED = 4
  GLANCE_INPUT2_ROLL_TIMER_EXPIRED = 6,
  GLANCE_INPUT2_ACTIVATION_TIMER_EXPIRED = 7,
} GlanceFSMInput2;

GlanceFSMState2 state2 = GLANCE_STATE2_NONE;
static void glance_fsm2(GlanceFSMInput2 input, uint64_t time_of_input_ms) {
  GlanceFSMState2 new_state = state2;
  
  switch (input) {
    case GLANCE_INPUT2_DROPPED_ZONE:
      // Hand dropped - reset everything
      RESET_TIMER(roll_timer);
      RESET_TIMER(activation_timer);
      new_state = GLANCE_STATE2_NONE
      glance_fsm3(GLANCE_INPUT3_DROPPED, time_of_input_ms);
      break;
    
    case GLANCE_INPUT2_ACTIVE_ZONE:
      if (TIMER_ACTIVE(roll_timer, time_of_input_ms) and (state2 == GLANCE_STATE2_ROLL)) {
        // Complete roll
        RESET_TIMER(roll_timer);
        glance_fsm3(GLANCE_INPUT3_ROLLED, time_of_input_ms);
      }
      else {
        // Start activation timer
        SET_TIMER(activation_timer, activation_timer_duration, time_of_input_ms);
      }
      break;
    
    case GLANCE_INPUT2_ROLL_ZONE:
      RESET_TIMER(activation_timer);
      if (TIMER_ACTIVE(roll_timer, time_of_input_ms)) {
        // Flag that we reached the roll zone
        new_state = GLANCE_STATE2_ROLL;
      }
      else if (current_zone == GLANCE_ZONE_ACTIVE) {
        SET_TIMER(roll_timer, roll_timer_duration, time_of_input_ms);
        new_state = GLANCE_STATE2_ROLL;
        prefer_fast_sampling = true;
      }
      break;

    case GLANCE_INPUT2_UNKNOWN_ZONE:
      RESET_TIMER(activation_timer);
      if (current_zone == GLANCE_ZONE_ACTIVE) {
        SET_TIMER(roll_timer, roll_timer_duration, time_of_input_ms);
        prefer_fast_sampling = true;
        new_state = GLANCE_STATE2_NONE;
      }  
      break;

    case GLANCE_INPUT2_ROLL_TIMER_EXPIRED:
      prefer_fast_sampling = false;
      new_state = GLANCE_STATE2_NONE;
      break;
  
    case GLANCE_INPUT2_ACTIVATION_TIMER_EXPIRED:
      prefer_fast_sampling = false;
      glance_fsm3(GLANCE_INPUT3_ACTIVE, time_of_input_ms);
      break;
    
    case GLANCE_INPUT3_SHORT_TIMER_EXPIRED:
      glance_fsm3(GLANCE_INPUT2_SHORT_TIMER_EXPIRED, time_of_input_ms);
      break;

    case GLANCE_INPUT3_LONG_TIMER_EXPIRED:
      glance_fsm3(GLANCE_INPUT2_LONG_TIMER_EXPIRED, time_of_input_ms);   
      break;
  
  state2 = new_state;
}  


static void process_accelerometer_reading(AccelData *reading, uint64_t reading_time_ms) {

  // Start by testing if the zone is unchanged (for efficiency)
  bool zone_not_changed = false;
  switch (current_zone) {
    case GLANCE_ZONE_ACTIVE:
      if (WITHIN_ACCELEROMETER_ZONE(active_zone, *reading)) {
        zone_not_changed = true;
        // APP_LOG(APP_LOG_LEVEL_DEBUG, "Still ACTIVE");
      }
      break;

    case GLANCE_ZONE_INACTIVE:
      if (WITHIN_ACCELEROMETER_ZONE(dropped_zone, *reading)) {
        zone_not_changed = true;
        // APP_LOG(APP_LOG_LEVEL_DEBUG, "Still INACTIVE");
      }
      break;

    case GLANCE_ZONE_ROLL:
      if (WITHIN_ACCELEROMETER_ZONE(roll_zone, *reading)) {
        zone_not_changed = true;
        // APP_LOG(APP_LOG_LEVEL_DEBUG, "Still ROLLED");
      }
      break;

    case GLANCE_ZONE_NONE:
      break;
  }

  // If we haven't spotted that the zone is unchanged, try the other possibilities.
  // Avoid repeating the test done above.
  if (!zone_not_changed) {
    if ((current_zone != GLANCE_ZONE_ACTIVE) && WITHIN_ACCELEROMETER_ZONE(active_zone, *reading)) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Gone ACTIVE");
      glance_fsm2(GLANCE_INPUT2_ACTIVE_ZONE, reading_time_ms);
      current_zone = GLANCE_ZONE_ACTIVE;
      send_glance_zone(current_zone);      
    }
    else if ((current_zone != GLANCE_ZONE_INACTIVE) && WITHIN_ACCELEROMETER_ZONE(dropped_zone, *reading)) { 
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Gone INACTIVE");
      glance_fsm2(GLANCE_INPUT2_DROPPED_ZONE, reading_time_ms);
      current_zone = GLANCE_ZONE_INACTIVE;
      send_glance_zone(current_zone);
    }
    else if ((current_zone != GLANCE_ZONE_ROLL) && WITHIN_ACCELEROMETER_ZONE(roll_zone, *reading)) { 
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Gone ROLL");
      glance_fsm2(GLANCE_INPUT2_ROLL_ZONE, reading_time_ms);
      current_zone = GLANCE_ZONE_ROLL;
      send_glance_zone(current_zone);
    }
    else if (current_zone != GLANCE_ZONE_NONE) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Gone NONE");
      glance_fsm2(GLANCE_INPUT2_UNKNOWN_ZONE, reading_time_ms);
      current_zone = GLANCE_ZONE_ROLL;
      send_glance_zone(current_zone);
    }
  }
  //else {
  //  APP_LOG(APP_LOG_LEVEL_DEBUG, "Zone unchanged");
  //}

  // Create inputs for timer experies
  if (TIMER_EXPIRED(roll_timer, reading_time_ms)) {
    RESET_TIMER(roll_timer);
    glance_fsm2(GLANCE_INPUT2_ROLL_TIMER_EXPIRED, reading_time_ms);
  }
  if (TIMER_EXPIRED(new_active_timer, reading_time_ms)) {
    RESET_TIMER(new_active_timer);
    glance_fsm2(GLANCE_INPUT2_SHORT_TIMER_EXPIRED, reading_time_ms);
  }
  if (TIMER_EXPIRED(old_active_timer, reading_time_ms)) {
    RESET_TIMER(old_active_timer);
    glance_fsm2(GLANCE_INPUT2_LONG_TIMER_EXPIRED, reading_time_ms);
  }
  if (TIMER_EXPIRED(activation_timer, reading_time_ms)) {
    RESET_TIMER(activation_timer);
    glance_fsm2(GLANCE_INPUT2_ACTIVATION_TIMER_EXPIRED, reading_time_ms);
  }
}

static void prv_accel_handler(AccelData *data, uint32_t num_samples) {
  time_ms_t current_time;
  store_current_time(&current_time);

  uint64_t input_time_ms;

  // Efficiency when idle, but has the downside of slowing down activation
  uint32_t first_sample = 0;
  //if (fsm_state == GLANCE_STATE_NOT_ACTIVE) {
  //  first_sample = num_samples - 1;
  //  input_time_ms = current_time.milliseconds;
  //}
  //else {
    input_time_ms = current_time.milliseconds - (num_samples * sample_duration_ms);
  //}
  
  if (discard_sample) {
    first_sample = 1;
    input_time_ms = 3600;
    discard_sample = false;
  }
  
  for (uint32_t i = first_sample; i < num_samples; i++) {
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Sample %ud", i);
    process_accelerometer_reading(&(data[i]), input_time_ms);
    input_time_ms += sample_duration_ms;
  }

  // Update the sampling speed
  if (prefer_fast_sampling && slow_sampling_active) {
    app_timer_register(10, start_fast_accelerometer_sampling, NULL);
  }
  else if (!prefer_fast_sampling && fast_sampling_active) {
    app_timer_register(10, start_slow_accelerometer_sampling, NULL);
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
                                bool legacy_flick_backlight,
                                GlanceResultHandler handler) {
  configured_glance_result_callback = handler;

  start_slow_accelerometer_sampling(NULL);
 
  allow_flick_backlight_when_inactive = legacy_flick_backlight; 
  light_on_when_active = control_backlight;
  
  if (light_on_when_active) {
    // Setup tap service to support or disable flick to light behavior
    accel_tap_service_subscribe(tap_event_handler);
  } 
}

void glancing_service_update_control_backlight(bool control_backlight, 
                                bool legacy_flick_backlight) {

  bool old_light_on_when_active = light_on_when_active;
  
  allow_flick_backlight_when_inactive = legacy_flick_backlight; 
  light_on_when_active = control_backlight;
  
  if (old_light_on_when_active && !light_on_when_active) {
    accel_tap_service_unsubscribe();
  }
  
  else if (light_on_when_active && !old_light_on_when_active) {
    // Setup tap service to support or disable flick to light behavior
    accel_tap_service_subscribe(tap_event_handler);
  } 
  
}

void glancing_service_update_timers(int32_t light_time, int32_t active_time, int32_t roll_time) {

  new_active_timer_duration = light_time;
  old_active_timer_duration = active_time;
  roll_timer_duration = roll_time;
} 
  
void glancing_service_unsubscribe() {
  if (fast_sampling_active || slow_sampling_active) {
    accel_data_service_unsubscribe();
  }
  if (light_on_when_active) {
    accel_tap_service_unsubscribe();
  }
}
