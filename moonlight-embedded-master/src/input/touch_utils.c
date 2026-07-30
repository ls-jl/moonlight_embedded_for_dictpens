#include "touch_utils.h"

#include <string.h>

static float clamp_unit(float value) {
  if (value < 0.0f)
    return 0.0f;
  if (value > 1.0f)
    return 1.0f;
  return value;
}

float ml_touch_normalize_axis(int value, int minimum, int maximum, int offset) {
  int active_minimum = minimum + offset;
  if (active_minimum < minimum)
    active_minimum = minimum;
  if (active_minimum > maximum)
    active_minimum = maximum;
  if (maximum <= active_minimum)
    return 0.0f;

  return clamp_unit((float)(value - active_minimum) / (float)(maximum - active_minimum));
}

int ml_touchpad_normalize_axis(int value, int minimum, int maximum, int offset) {
  float normalized = ml_touch_normalize_axis(value, minimum, maximum, offset);
  return (int)(normalized * ML_TOUCHPAD_REFERENCE + 0.5f);
}

void ml_touch_transform(int x, int y,
                        int min_x, int max_x, int min_y, int max_y,
                        int offset_x, int offset_y, int rotation,
                        float *out_x, float *out_y) {
  float normalized_x = ml_touch_normalize_axis(x, min_x, max_x, offset_x);
  float normalized_y = ml_touch_normalize_axis(y, min_y, max_y, offset_y);

  switch (rotation) {
  case 90:
    *out_x = normalized_y;
    *out_y = 1.0f - normalized_x;
    break;
  case 180:
    *out_x = 1.0f - normalized_x;
    *out_y = 1.0f - normalized_y;
    break;
  case 270:
    *out_x = 1.0f - normalized_y;
    *out_y = normalized_x;
    break;
  default:
    *out_x = normalized_x;
    *out_y = normalized_y;
    break;
  }
}

bool ml_touch_device_allowed(bool has_touch_capability, bool has_gamepad_capability,
                             bool explicit_device, bool device_matches) {
  if (!has_touch_capability)
    return false;
  if (explicit_device)
    return device_matches;
  return !has_gamepad_capability;
}

void ml_touch_slot_reset(struct ml_touch_slot *slot) {
  memset(slot, 0, sizeof(*slot));
  slot->tracking_id = -1;
}

void ml_touch_slot_begin(struct ml_touch_slot *slot, int tracking_id) {
  ml_touch_slot_reset(slot);
  slot->active = true;
  slot->tracking_id = tracking_id;
}

void ml_touch_slot_update(struct ml_touch_slot *slot, bool is_x, int value) {
  if (!slot->active)
    ml_touch_slot_begin(slot, 0);

  if (is_x) {
    slot->x = value;
    slot->x_valid = true;
  } else {
    slot->y = value;
    slot->y_valid = true;
  }
  slot->dirty = true;
}

void ml_touch_slot_end(struct ml_touch_slot *slot) {
  if (!slot->active && !slot->sent_down)
    return;

  slot->active = false;
  slot->up_pending = true;
  slot->dirty = false;
}
