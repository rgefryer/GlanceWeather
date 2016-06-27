#pragma once

#include <pebble.h>

typedef enum {
  GLANCE_OUTPUT_IDLE = 0,
  GLANCE_OUTPUT_ACTIVE = 1,
  GLANCE_OUTPUT_ROLL = 2,
} GlanceOutput;

typedef enum {
  GLANCE_ZONE_INACTIVE = 0,
  GLANCE_ZONE_ACTIVE = 1,
  GLANCE_ZONE_ROLL = 2,
  GLANCE_ZONE_NONE = 3,
} GlanceZone;

typedef enum {
  GLANCE_EVENT_ZONE = 0,
  GLANCE_EVENT_OUTPUT = 1,
} GlanceEvent;

typedef struct GlanceResult {
  GlanceEvent event;
  GlanceOutput result;
  GlanceZone zone;
} GlanceResult;

typedef void (*GlanceResultHandler)(GlanceResult *data);

// control_backlight - switch on light while the glance is active
// legacy_flick_backlight - allow "flick backlight" when the glance is inactive
void glancing_service_subscribe(bool control_backlight, 
                                bool legacy_flick_backlight, GlanceResultHandler handler);

void glancing_service_unsubscribe();
