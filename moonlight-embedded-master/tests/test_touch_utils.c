#include "touch_utils.h"

#include <assert.h>
#include <math.h>

static void assert_close(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}

static void test_axis_normalization(void) {
  assert_close(ml_touch_normalize_axis(0, 0, 1000, 100), 0.0f);
  assert_close(ml_touch_normalize_axis(100, 0, 1000, 100), 0.0f);
  assert_close(ml_touch_normalize_axis(550, 0, 1000, 100), 0.5f);
  assert_close(ml_touch_normalize_axis(1200, 0, 1000, 100), 1.0f);
  assert(ml_touchpad_normalize_axis(550, 0, 1000, 100) == 2048);
}

static void test_rotations(void) {
  float x;
  float y;

  ml_touch_transform(25, 75, 0, 100, 0, 100, 0, 0, 0, &x, &y);
  assert_close(x, 0.25f);
  assert_close(y, 0.75f);

  ml_touch_transform(25, 75, 0, 100, 0, 100, 0, 0, 90, &x, &y);
  assert_close(x, 0.75f);
  assert_close(y, 0.75f);

  ml_touch_transform(25, 75, 0, 100, 0, 100, 0, 0, 180, &x, &y);
  assert_close(x, 0.75f);
  assert_close(y, 0.25f);

  ml_touch_transform(25, 75, 0, 100, 0, 100, 0, 0, 270, &x, &y);
  assert_close(x, 0.25f);
  assert_close(y, 0.25f);
}

static void test_device_selection(void) {
  assert(ml_touch_device_allowed(true, false, false, false));
  assert(!ml_touch_device_allowed(true, true, false, false));
  assert(ml_touch_device_allowed(true, true, true, true));
  assert(!ml_touch_device_allowed(true, false, true, false));
  assert(!ml_touch_device_allowed(false, false, false, false));
}

static void test_multitouch_slot(void) {
  struct ml_touch_slot slot;
  ml_touch_slot_reset(&slot);
  assert(!slot.active);
  assert(slot.tracking_id == -1);

  ml_touch_slot_begin(&slot, 42);
  ml_touch_slot_update(&slot, true, 123);
  ml_touch_slot_update(&slot, false, 456);
  assert(slot.active);
  assert(slot.tracking_id == 42);
  assert(slot.x_valid && slot.y_valid && slot.dirty);
  assert(slot.x == 123 && slot.y == 456);

  slot.sent_down = true;
  ml_touch_slot_end(&slot);
  assert(!slot.active);
  assert(slot.up_pending);

  ml_touch_slot_reset(&slot);
  assert(!slot.up_pending && !slot.sent_down);
}

int main(void) {
  test_axis_normalization();
  test_rotations();
  test_device_selection();
  test_multitouch_slot();
  return 0;
}
