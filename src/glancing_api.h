#pragma once

#include <pebble.h>

typedef enum {
  GLANCING_INACTIVE = 0,   // Definitely not looking
  GLANCING_ACTIVE = 1,     // Watch Face in view
  GLANCING_TIMEDOUT = 2,   // Active, but timeout expired
  GLANCING_ROCK = 3,       // Active, user rocked their wrist
} GlanceState;

typedef struct GlancingData {
  GlanceState state;
} GlancingData;

typedef void (*GlancingDataHandler)(GlancingData *data);

void glancing_service_subscribe(int timeout_ms, bool control_backlight, 
                                bool legacy_flick_backlight, GlancingDataHandler handler);

void glancing_service_unsubscribe();
