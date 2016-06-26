#pragma once

#include <pebble.h>

typedef enum {
  GLANCE_OUTPUT_IDLE = 0,
  GLANCE_OUTPUT_ACTIVE = 1,
  GLANCE_OUTPUT_ROLL = 2,
} GlanceOutput;

typedef struct GlanceResult {
  GlanceOutput result;
} GlanceResult;

typedef void (*GlancingDataHandler)(GlanceResult *data);

// control_backlight - switch on light while the glance is active
// legacy_flick_backlight - allow "flick backlight" when the glance is inactive
void glancing_service_subscribe(bool control_backlight, 
                                bool legacy_flick_backlight, GlancingDataHandler handler);

void glancing_service_unsubscribe();
