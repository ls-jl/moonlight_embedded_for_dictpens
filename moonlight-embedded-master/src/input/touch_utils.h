#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ML_TOUCH_MAX_SLOTS 10
#define ML_TOUCHPAD_REFERENCE 4095

struct ml_touch_slot {
  bool active;
  bool sent_down;
  bool x_valid;
  bool y_valid;
  bool dirty;
  bool up_pending;
  int tracking_id;
  int x;
  int y;
};

float ml_touch_normalize_axis(int value, int minimum, int maximum, int offset);
int ml_touchpad_normalize_axis(int value, int minimum, int maximum, int offset);
void ml_touch_transform(int x, int y,
                        int min_x, int max_x, int min_y, int max_y,
                        int offset_x, int offset_y, int rotation,
                        float *out_x, float *out_y);
bool ml_touch_device_allowed(bool has_touch_capability, bool has_gamepad_capability,
                             bool explicit_device, bool device_matches);
void ml_touch_slot_reset(struct ml_touch_slot *slot);
void ml_touch_slot_begin(struct ml_touch_slot *slot, int tracking_id);
void ml_touch_slot_update(struct ml_touch_slot *slot, bool is_x, int value);
void ml_touch_slot_end(struct ml_touch_slot *slot);
