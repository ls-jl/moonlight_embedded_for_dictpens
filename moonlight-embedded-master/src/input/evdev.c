/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "evdev.h"

#include "keyboard.h"
#include "touch_utils.h"

#include "../loop.h"

#include "libevdev/libevdev.h"
#include <Limelight.h>

#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#ifdef __linux__
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#include <math.h>

#ifndef input_event_sec
#define input_event_sec time.tv_sec
#define input_event_usec time.tv_usec
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define int16_to_le(val) val
#else
#define int16_to_le(val) ((((val) >> 8) & 0x00FF) | (((val) << 8) & 0xFF00))
#endif

struct input_abs_parms {
  int min, max;
  int flat;
  int avg;
  int range, diff;
};

#define TOUCHPAD_MAX_SLOTS ML_TOUCH_MAX_SLOTS

struct touchpad_slot {
  bool active;
  bool xValid;
  bool yValid;
  bool positionValid;
  bool lastPositionValid;
  int trackingId;
  int x, y;
  int lastX, lastY;
  int startX, startY;
  struct timeval downTime;
};

struct input_device {
  struct libevdev *dev;
  bool is_keyboard;
  bool is_mouse;
  bool is_touchscreen;
  int rotate;
  struct mapping* map;
  int key_map[KEY_CNT];
  int abs_map[ABS_CNT];
  int hats_state[3][2];
  int fd;
  dev_t rdev;
  char modifiers;
  #ifdef __linux__
  __s32 mouseDeltaX, mouseDeltaY, mouseVScroll, mouseHScroll;
  #else
  int32_t mouseDeltaX, mouseDeltaY, mouseVScroll, mouseHScroll;
  #endif
  int32_t touchMinX, touchMaxX, touchMinY, touchMaxY;
  struct timeval btnDownTime;
  int touchSlot;
  bool touchDirectUnsupported;
  bool touchpadMode;
  bool touchHasMtSlots;
  int touchOffsetX, touchOffsetY;
  struct ml_touch_slot touchSlots[ML_TOUCH_MAX_SLOTS];
  int touchMouseSlot;
  bool touchMouseDown;
  struct touchpad_slot touchpadSlots[TOUCHPAD_MAX_SLOTS];
  int touchpadGestureMaxFingers;
  bool touchpadGestureMoved;
  bool touchpadAvgValid;
  int touchpadLastAvgX, touchpadLastAvgY;
  int touchpadScrollRemainderX, touchpadScrollRemainderY;
  bool touchpadDragging;
  bool touchpadLastTapValid;
  struct timeval touchpadLastTapTime;
  short controllerId;
  int haptic_effect_id;
  int buttonFlags;
  unsigned char leftTrigger, rightTrigger;
  short leftStickX, leftStickY;
  short rightStickX, rightStickY;
  bool gamepadModified;
  bool resyncing;
  bool mouseEmulation;
  pthread_t meThread;
  struct input_abs_parms xParms, yParms, rxParms, ryParms, zParms, rzParms;
  struct input_abs_parms leftParms, rightParms, upParms, downParms;
};

#define HAT_UP 1
#define HAT_RIGHT 2
#define HAT_DOWN 4
#define HAT_LEFT 8
static const int hat_constants[3][3] = {{HAT_UP | HAT_LEFT, HAT_UP, HAT_UP | HAT_RIGHT}, {HAT_LEFT, 0, HAT_RIGHT}, {HAT_LEFT | HAT_DOWN, HAT_DOWN, HAT_DOWN | HAT_RIGHT}};

#define set_hat(flags, flag, hat, hat_flag) flags = (hat & hat_flag) == hat_flag ? flags | flag : flags & ~flag

#define TOUCH_CLICK_RADIUS 10
#define TOUCH_CLICK_DELAY 100000 // microseconds
#define TOUCH_MOUSE_REFERENCE 10000
#define TOUCHPAD_MOTION_MULTIPLIER 1
#define TOUCHPAD_DRAG_TAP_WINDOW_MS 350
#define TOUCHPAD_SCROLL_STEP 35
#define MAX_GENERATED_MAPPING_NAME 256

// How long the Start button must be pressed to toggle mouse emulation
#define MOUSE_EMULATION_LONG_PRESS_TIME 750
// How long between polling the gamepad to send virtual mouse input
#define MOUSE_EMULATION_POLLING_INTERVAL 50000
// Determines how fast the mouse will move each interval
#define MOUSE_EMULATION_MOTION_MULTIPLIER 3
// Determines the maximum motion amount before allowing movement
#define MOUSE_EMULATION_DEADZONE 2

// Limited by number of bits in activeGamepadMask
#define MAX_GAMEPADS 16

static struct input_device* devices = NULL;
static int numDevices = 0;
static int assignedControllerIds = 0;

static short* currentKey;
static short* currentHat;
static short* currentHatDir;
static short* currentAbs;
static bool* currentReverse;

static bool grabbingDevices;
static bool mouseEmulationEnabled;
static bool configuredTouchDevice;
static bool configuredTouchDeviceValid;
static dev_t configuredTouchRdev;
static char configuredTouchPath[PATH_MAX];
static int configuredTouchRotation = -1;
static int configuredTouchOffsetX;
static int configuredTouchOffsetY;

static bool waitingToExitOnModifiersUp = false;

int evdev_gamepads = 0;

static int env_int(const char *name, int fallback) {
  const char *value = getenv(name);
  if (!value || !value[0])
    return fallback;

  char *end = NULL;
  long parsed = strtol(value, &end, 10);
  if (end == value || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
    return fallback;

  return (int)parsed;
}

static int normalize_rotation(int rotation, int fallback) {
  switch (rotation) {
  case 0:
  case 90:
  case 180:
  case 270:
    return rotation;
  default:
    return fallback;
  }
}

void evdev_configure_touch_from_env(void) {
  const char *path = getenv("MOONLIGHT_RK_TOUCH_DEVICE");
  configuredTouchDevice = path && path[0];
  configuredTouchDeviceValid = false;
  configuredTouchRdev = 0;
  configuredTouchPath[0] = '\0';
  configuredTouchRotation = normalize_rotation(env_int("MOONLIGHT_RK_TOUCH_ROTATION", -1), -1);
  configuredTouchOffsetX = env_int("MOONLIGHT_RK_TOUCH_OFFSET_X", 0);
  configuredTouchOffsetY = env_int("MOONLIGHT_RK_TOUCH_OFFSET_Y", 0);

  if (!configuredTouchDevice)
    return;

  snprintf(configuredTouchPath, sizeof(configuredTouchPath), "%s", path);
  struct stat st;
  if (stat(path, &st) == 0) {
    configuredTouchRdev = st.st_rdev;
    configuredTouchDeviceValid = true;
    fprintf(stderr, "Touch config: device=%s rdev=%llu rotation=%d offset=%d,%d\n",
            configuredTouchPath,
            (unsigned long long)configuredTouchRdev,
            configuredTouchRotation,
            configuredTouchOffsetX,
            configuredTouchOffsetY);
  } else {
    fprintf(stderr, "Touch config: unable to stat %s: %s; touchscreen input disabled\n",
            configuredTouchPath, strerror(errno));
  }
}

static bool configured_touch_matches(dev_t rdev) {
  if (!configuredTouchDevice)
    return false;

  struct stat st;
  if (stat(configuredTouchPath, &st) == 0) {
    configuredTouchRdev = st.st_rdev;
    configuredTouchDeviceValid = true;
  }
  return configuredTouchDeviceValid && configuredTouchRdev == rdev;
}

#define ACTION_MODIFIERS (MODIFIER_SHIFT|MODIFIER_ALT|MODIFIER_CTRL)
#define QUIT_KEY KEY_Q
#define QUIT_BUTTONS (PLAY_FLAG|BACK_FLAG|LB_FLAG|RB_FLAG)

static bool (*handler) (struct input_event*, struct input_device*);

static bool evdev_has_keyboard_keys(struct libevdev* evdev) {
  return libevdev_has_event_code(evdev, EV_KEY, KEY_Q) ||
         libevdev_has_event_code(evdev, EV_KEY, KEY_A) ||
         libevdev_has_event_code(evdev, EV_KEY, KEY_Z) ||
         libevdev_has_event_code(evdev, EV_KEY, KEY_1) ||
         (libevdev_has_event_code(evdev, EV_KEY, KEY_ENTER) &&
          libevdev_has_event_code(evdev, EV_KEY, KEY_SPACE));
}

static void init_mapping_indices(struct mapping* map) {
  memset(&map->abs_leftx, -1, offsetof(struct mapping, next) - offsetof(struct mapping, abs_leftx));
}

static short mapped_key_index(struct input_device* dev, int code) {
  if (code < 0 || code >= KEY_CNT)
    return -1;
  return dev->key_map[code];
}

static short mapped_abs_index(struct input_device* dev, int code) {
  if (code < 0 || code >= ABS_CNT)
    return -1;
  return dev->abs_map[code];
}

static short first_mapped_key(struct input_device* dev, const int* codes, int count) {
  for (int i = 0; i < count; i++) {
    short index = mapped_key_index(dev, codes[i]);
    if (index >= 0)
      return index;
  }
  return -1;
}

static struct mapping* evdev_generate_default_mapping(struct input_device* dev, const char* name) {
  struct mapping* map = calloc(1, sizeof(struct mapping));
  if (map == NULL) {
    fprintf(stderr, "Not enough memory\n");
    exit(EXIT_FAILURE);
  }

  strncpy(map->guid, "generated", sizeof(map->guid) - 1);
  strncpy(map->name, name ? name : "Linux evdev gamepad", sizeof(map->name) - 1);
  init_mapping_indices(map);

  const int a_buttons[] = { BTN_A, BTN_0, BTN_TRIGGER };
  const int b_buttons[] = { BTN_B, BTN_1, BTN_THUMB };
  const int x_buttons[] = { BTN_X, BTN_2, BTN_THUMB2 };
  const int y_buttons[] = { BTN_Y, BTN_3, BTN_TOP };
  const int lb_buttons[] = { BTN_TL, BTN_4, BTN_TOP2 };
  const int rb_buttons[] = { BTN_TR, BTN_5, BTN_PINKIE };
  const int lt_buttons[] = { BTN_TL2, BTN_6, BTN_BASE };
  const int rt_buttons[] = { BTN_TR2, BTN_7, BTN_BASE2 };
  const int back_buttons[] = { BTN_SELECT, BTN_8, BTN_BASE3 };
  const int start_buttons[] = { BTN_START, BTN_9, BTN_BASE4 };
  const int guide_buttons[] = { BTN_MODE, BTN_BASE5 };
  const int ls_buttons[] = { BTN_THUMBL, BTN_BASE6 };
  const int rs_buttons[] = { BTN_THUMBR };

  map->btn_a = first_mapped_key(dev, a_buttons, sizeof(a_buttons) / sizeof(a_buttons[0]));
  map->btn_b = first_mapped_key(dev, b_buttons, sizeof(b_buttons) / sizeof(b_buttons[0]));
  map->btn_x = first_mapped_key(dev, x_buttons, sizeof(x_buttons) / sizeof(x_buttons[0]));
  map->btn_y = first_mapped_key(dev, y_buttons, sizeof(y_buttons) / sizeof(y_buttons[0]));
  map->btn_leftshoulder = first_mapped_key(dev, lb_buttons, sizeof(lb_buttons) / sizeof(lb_buttons[0]));
  map->btn_rightshoulder = first_mapped_key(dev, rb_buttons, sizeof(rb_buttons) / sizeof(rb_buttons[0]));
  map->btn_lefttrigger = first_mapped_key(dev, lt_buttons, sizeof(lt_buttons) / sizeof(lt_buttons[0]));
  map->btn_righttrigger = first_mapped_key(dev, rt_buttons, sizeof(rt_buttons) / sizeof(rt_buttons[0]));
  map->btn_back = first_mapped_key(dev, back_buttons, sizeof(back_buttons) / sizeof(back_buttons[0]));
  map->btn_start = first_mapped_key(dev, start_buttons, sizeof(start_buttons) / sizeof(start_buttons[0]));
  map->btn_guide = first_mapped_key(dev, guide_buttons, sizeof(guide_buttons) / sizeof(guide_buttons[0]));
  map->btn_leftstick = first_mapped_key(dev, ls_buttons, sizeof(ls_buttons) / sizeof(ls_buttons[0]));
  map->btn_rightstick = first_mapped_key(dev, rs_buttons, sizeof(rs_buttons) / sizeof(rs_buttons[0]));

#ifdef BTN_DPAD_UP
  const int dpup_buttons[] = { BTN_DPAD_UP };
  const int dpdown_buttons[] = { BTN_DPAD_DOWN };
  const int dpleft_buttons[] = { BTN_DPAD_LEFT };
  const int dpright_buttons[] = { BTN_DPAD_RIGHT };
  map->btn_dpup = first_mapped_key(dev, dpup_buttons, sizeof(dpup_buttons) / sizeof(dpup_buttons[0]));
  map->btn_dpdown = first_mapped_key(dev, dpdown_buttons, sizeof(dpdown_buttons) / sizeof(dpdown_buttons[0]));
  map->btn_dpleft = first_mapped_key(dev, dpleft_buttons, sizeof(dpleft_buttons) / sizeof(dpleft_buttons[0]));
  map->btn_dpright = first_mapped_key(dev, dpright_buttons, sizeof(dpright_buttons) / sizeof(dpright_buttons[0]));
#endif

  map->abs_leftx = mapped_abs_index(dev, ABS_X);
  map->abs_lefty = mapped_abs_index(dev, ABS_Y);

  short absRx = mapped_abs_index(dev, ABS_RX);
  short absRy = mapped_abs_index(dev, ABS_RY);
  short absZ = mapped_abs_index(dev, ABS_Z);
  short absRz = mapped_abs_index(dev, ABS_RZ);
  short absBrake = mapped_abs_index(dev, ABS_BRAKE);
  short absGas = mapped_abs_index(dev, ABS_GAS);

  if (absRx >= 0 && absRy >= 0) {
    map->abs_rightx = absRx;
    map->abs_righty = absRy;
  } else if (absZ >= 0 && absRz >= 0 && (absBrake >= 0 || absGas >= 0 || map->btn_lefttrigger >= 0 || map->btn_righttrigger >= 0)) {
    map->abs_rightx = absZ;
    map->abs_righty = absRz;
  }

  if (absBrake >= 0)
    map->abs_lefttrigger = absBrake;
  else if (absZ >= 0 && map->abs_rightx != absZ)
    map->abs_lefttrigger = absZ;

  if (absGas >= 0)
    map->abs_righttrigger = absGas;
  else if (absRz >= 0 && map->abs_righty != absRz)
    map->abs_righttrigger = absRz;

  if (libevdev_has_event_code(dev->dev, EV_ABS, ABS_HAT0X) &&
      libevdev_has_event_code(dev->dev, EV_ABS, ABS_HAT0Y)) {
    map->hat_dpright = 0;
    map->hat_dpleft = 0;
    map->hat_dpup = 0;
    map->hat_dpdown = 0;
    map->hat_dir_dpright = HAT_RIGHT;
    map->hat_dir_dpleft = HAT_LEFT;
    map->hat_dir_dpup = HAT_UP;
    map->hat_dir_dpdown = HAT_DOWN;
  }

  int vendor = libevdev_get_id_vendor(dev->dev);
  int product = libevdev_get_id_product(dev->dev);
  if (vendor == 0x054c && (product == 0x05c4 || product == 0x09cc)) {
    map->btn_a = mapped_key_index(dev, BTN_A);
    map->btn_b = mapped_key_index(dev, BTN_B);
    map->btn_x = mapped_key_index(dev, BTN_Y);
    map->btn_y = mapped_key_index(dev, BTN_X);
    map->btn_leftshoulder = mapped_key_index(dev, BTN_TL);
    map->btn_rightshoulder = mapped_key_index(dev, BTN_TR);
    map->btn_lefttrigger = mapped_key_index(dev, BTN_TL2);
    map->btn_righttrigger = mapped_key_index(dev, BTN_TR2);
    map->btn_back = mapped_key_index(dev, BTN_SELECT);
    map->btn_start = mapped_key_index(dev, BTN_START);
    map->btn_guide = mapped_key_index(dev, BTN_MODE);
    map->btn_leftstick = mapped_key_index(dev, BTN_THUMBL);
    map->btn_rightstick = mapped_key_index(dev, BTN_THUMBR);
    map->abs_leftx = mapped_abs_index(dev, ABS_X);
    map->abs_lefty = mapped_abs_index(dev, ABS_Y);
    map->abs_rightx = mapped_abs_index(dev, ABS_Z);
    map->abs_righty = mapped_abs_index(dev, ABS_RZ);
    map->abs_lefttrigger = mapped_abs_index(dev, ABS_RX);
    map->abs_righttrigger = mapped_abs_index(dev, ABS_RY);
    fprintf(stderr, "Using Sony DualShock 4 fallback gamepad mapping for %s\n", map->name);
  }

  fprintf(stderr, "Generated fallback gamepad mapping for %s\n", map->name);
  return map;
}

static void evdev_touch_bounds(struct input_device* dev, int code, int* min, int* max) {
  if (libevdev_has_event_code(dev->dev, EV_ABS, code)) {
    *min = libevdev_get_abs_minimum(dev->dev, code);
    *max = libevdev_get_abs_maximum(dev->dev, code);
  }
}

static void evdev_transform_touch(struct input_device* dev, struct ml_touch_slot* slot,
                                  float* outX, float* outY) {
  ml_touch_transform(slot->x, slot->y,
                     dev->touchMinX, dev->touchMaxX,
                     dev->touchMinY, dev->touchMaxY,
                     dev->touchOffsetX, dev->touchOffsetY,
                     dev->rotate, outX, outY);
}

static void evdev_send_touch_mouse_fallback(struct input_device* dev, int slotIndex,
                                            unsigned char eventType, float x, float y) {
  if (dev->touchMouseSlot < 0 && eventType != LI_TOUCH_EVENT_UP)
    dev->touchMouseSlot = slotIndex;
  if (dev->touchMouseSlot != slotIndex)
    return;

  short mouseX = (short)(x * TOUCH_MOUSE_REFERENCE);
  short mouseY = (short)(y * TOUCH_MOUSE_REFERENCE);
  LiSendMousePositionEvent(mouseX, mouseY, TOUCH_MOUSE_REFERENCE, TOUCH_MOUSE_REFERENCE);

  if (eventType != LI_TOUCH_EVENT_UP && !dev->touchMouseDown) {
    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
    dev->touchMouseDown = true;
  } else if (eventType == LI_TOUCH_EVENT_UP && dev->touchMouseDown) {
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    dev->touchMouseDown = false;
    dev->touchMouseSlot = -1;
  }
}

static int timeval_diff_ms(struct timeval* newer, struct timeval* older) {
  struct timeval elapsedTime;
  timersub(newer, older, &elapsedTime);
  return elapsedTime.tv_sec * 1000 + elapsedTime.tv_usec / 1000;
}

static int touchpad_active_count(struct input_device* dev) {
  int count = 0;
  for (int i = 0; i < TOUCHPAD_MAX_SLOTS; i++) {
    if (dev->touchpadSlots[i].active)
      count++;
  }
  return count;
}

static struct touchpad_slot* touchpad_first_active_slot(struct input_device* dev) {
  for (int i = 0; i < TOUCHPAD_MAX_SLOTS; i++) {
    if (dev->touchpadSlots[i].active)
      return &dev->touchpadSlots[i];
  }
  return NULL;
}

static void touchpad_update_max_fingers(struct input_device* dev) {
  int count = touchpad_active_count(dev);
  if (count > dev->touchpadGestureMaxFingers)
    dev->touchpadGestureMaxFingers = count;
}

static void touchpad_press_left_if_needed(struct input_device* dev) {
  if (!dev->touchpadDragging) {
    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
    dev->touchpadDragging = true;
  }
}

static void touchpad_release_left_if_needed(struct input_device* dev) {
  if (dev->touchpadDragging) {
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    dev->touchpadDragging = false;
  }
}

static void touchpad_reset_gesture(struct input_device* dev) {
  dev->touchpadGestureMaxFingers = 0;
  dev->touchpadGestureMoved = false;
  dev->touchpadAvgValid = false;
  dev->touchpadScrollRemainderX = 0;
  dev->touchpadScrollRemainderY = 0;
}

static void touchpad_send_click(int button) {
  LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, button);
  usleep(TOUCH_CLICK_DELAY);
  LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);
}

static void touchpad_begin_slot(struct input_device* dev, int slotIndex, int trackingId, struct timeval eventTime) {
  if (slotIndex < 0 || slotIndex >= TOUCHPAD_MAX_SLOTS)
    return;

  int oldCount = touchpad_active_count(dev);
  struct touchpad_slot* slot = &dev->touchpadSlots[slotIndex];
  memset(slot, 0, sizeof(*slot));
  slot->active = true;
  slot->trackingId = trackingId;
  slot->downTime = eventTime;

  if (oldCount == 0) {
    touchpad_reset_gesture(dev);
    if (dev->touchpadLastTapValid &&
        timeval_diff_ms(&eventTime, &dev->touchpadLastTapTime) <= TOUCHPAD_DRAG_TAP_WINDOW_MS) {
      touchpad_press_left_if_needed(dev);
      dev->touchpadLastTapValid = false;
    }
  } else if (dev->touchpadDragging) {
    touchpad_release_left_if_needed(dev);
  }

  touchpad_update_max_fingers(dev);
}

static void touchpad_update_slot_axis(struct input_device* dev, int slotIndex, bool isX, int value) {
  if (slotIndex < 0 || slotIndex >= TOUCHPAD_MAX_SLOTS)
    return;

  struct touchpad_slot* slot = &dev->touchpadSlots[slotIndex];
  if (!slot->active) {
    struct timeval zero = { 0, 0 };
    touchpad_begin_slot(dev, slotIndex, 0, zero);
  }

  bool wasValid = slot->positionValid;
  if (isX) {
    slot->x = ml_touchpad_normalize_axis(value, dev->touchMinX, dev->touchMaxX,
                                         dev->touchOffsetX);
    slot->xValid = true;
  } else {
    slot->y = ml_touchpad_normalize_axis(value, dev->touchMinY, dev->touchMaxY,
                                         dev->touchOffsetY);
    slot->yValid = true;
  }

  slot->positionValid = slot->xValid && slot->yValid;
  if (!wasValid && slot->positionValid) {
    slot->startX = slot->x;
    slot->startY = slot->y;
    slot->lastPositionValid = false;
  }
}

static void touchpad_mark_moved_if_needed(struct input_device* dev, struct touchpad_slot* slot) {
  if (!slot || !slot->positionValid)
    return;

  int deltaX = slot->x - slot->startX;
  int deltaY = slot->y - slot->startY;
  if (deltaX * deltaX + deltaY * deltaY >= TOUCH_CLICK_RADIUS * TOUCH_CLICK_RADIUS)
    dev->touchpadGestureMoved = true;
}

static void touchpad_add_scroll_delta(struct input_device* dev, int deltaX, int deltaY) {
  dev->touchpadScrollRemainderY += -deltaY;
  int vTicks = dev->touchpadScrollRemainderY / TOUCHPAD_SCROLL_STEP;
  if (vTicks != 0) {
    dev->mouseVScroll += vTicks;
    dev->touchpadScrollRemainderY -= vTicks * TOUCHPAD_SCROLL_STEP;
  }

  dev->touchpadScrollRemainderX += deltaX;
  int hTicks = dev->touchpadScrollRemainderX / TOUCHPAD_SCROLL_STEP;
  if (hTicks != 0) {
    dev->mouseHScroll += hTicks;
    dev->touchpadScrollRemainderX -= hTicks * TOUCHPAD_SCROLL_STEP;
  }
}

static void touchpad_flush(struct input_device* dev) {
  int count = touchpad_active_count(dev);
  if (count <= 0)
    return;

  touchpad_update_max_fingers(dev);

  if (dev->touchpadGestureMaxFingers >= 2 && count >= 2) {
    int sumX = 0;
    int sumY = 0;
    int validCount = 0;
    for (int i = 0; i < TOUCHPAD_MAX_SLOTS && validCount < 2; i++) {
      struct touchpad_slot* slot = &dev->touchpadSlots[i];
      if (slot->active && slot->positionValid) {
        touchpad_mark_moved_if_needed(dev, slot);
        sumX += slot->x;
        sumY += slot->y;
        validCount++;
      }
    }

    if (validCount == 2) {
      int avgX = sumX / 2;
      int avgY = sumY / 2;
      if (dev->touchpadAvgValid) {
        int deltaX = avgX - dev->touchpadLastAvgX;
        int deltaY = avgY - dev->touchpadLastAvgY;
        if (deltaX != 0 || deltaY != 0) {
          dev->touchpadGestureMoved = true;
          touchpad_add_scroll_delta(dev, deltaX, deltaY);
        }
      }
      dev->touchpadLastAvgX = avgX;
      dev->touchpadLastAvgY = avgY;
      dev->touchpadAvgValid = true;
    }
  } else if (dev->touchpadGestureMaxFingers == 1 && count == 1) {
    struct touchpad_slot* slot = touchpad_first_active_slot(dev);
    if (slot && slot->positionValid) {
      touchpad_mark_moved_if_needed(dev, slot);
      if (slot->lastPositionValid) {
        int deltaX = slot->x - slot->lastX;
        int deltaY = slot->y - slot->lastY;
        if (deltaX != 0 || deltaY != 0) {
          dev->mouseDeltaX += deltaX * TOUCHPAD_MOTION_MULTIPLIER;
          dev->mouseDeltaY += deltaY * TOUCHPAD_MOTION_MULTIPLIER;
        }
      }
    }
  }

  for (int i = 0; i < TOUCHPAD_MAX_SLOTS; i++) {
    struct touchpad_slot* slot = &dev->touchpadSlots[i];
    if (slot->active && slot->positionValid) {
      slot->lastX = slot->x;
      slot->lastY = slot->y;
      slot->lastPositionValid = true;
    }
  }
}

static void touchpad_finish_gesture(struct input_device* dev, struct timeval eventTime) {
  if (dev->touchpadDragging) {
    touchpad_release_left_if_needed(dev);
  } else if (!dev->touchpadGestureMoved) {
    if (dev->touchpadGestureMaxFingers >= 2) {
      touchpad_send_click(BUTTON_RIGHT);
      dev->touchpadLastTapValid = false;
    } else if (dev->touchpadGestureMaxFingers == 1) {
      touchpad_send_click(BUTTON_LEFT);
      dev->touchpadLastTapTime = eventTime;
      dev->touchpadLastTapValid = true;
    }
  } else {
    dev->touchpadLastTapValid = false;
  }

  touchpad_reset_gesture(dev);
}

static void touchpad_release_slot(struct input_device* dev, int slotIndex, struct timeval eventTime) {
  if (slotIndex < 0 || slotIndex >= TOUCHPAD_MAX_SLOTS)
    return;

  struct touchpad_slot* slot = &dev->touchpadSlots[slotIndex];
  if (!slot->active)
    return;

  touchpad_mark_moved_if_needed(dev, slot);
  slot->active = false;
  slot->xValid = false;
  slot->yValid = false;
  slot->positionValid = false;
  slot->lastPositionValid = false;
  slot->trackingId = -1;

  if (touchpad_active_count(dev) == 0)
    touchpad_finish_gesture(dev, eventTime);
  else
    dev->touchpadAvgValid = false;
}

static void touchpad_release_all(struct input_device* dev, struct timeval eventTime) {
  bool hadActive = false;
  for (int i = 0; i < TOUCHPAD_MAX_SLOTS; i++) {
    if (dev->touchpadSlots[i].active) {
      touchpad_mark_moved_if_needed(dev, &dev->touchpadSlots[i]);
      dev->touchpadSlots[i].active = false;
      dev->touchpadSlots[i].xValid = false;
      dev->touchpadSlots[i].yValid = false;
      dev->touchpadSlots[i].positionValid = false;
      dev->touchpadSlots[i].lastPositionValid = false;
      dev->touchpadSlots[i].trackingId = -1;
      hadActive = true;
    }
  }

  if (hadActive)
    touchpad_finish_gesture(dev, eventTime);
}

static void evdev_begin_direct_slot(struct input_device* dev, int slotIndex, int trackingId) {
  if (slotIndex < 0 || slotIndex >= ML_TOUCH_MAX_SLOTS)
    return;
  ml_touch_slot_begin(&dev->touchSlots[slotIndex], trackingId);
}

static void evdev_release_direct_slot(struct input_device* dev, int slotIndex) {
  if (slotIndex < 0 || slotIndex >= ML_TOUCH_MAX_SLOTS)
    return;
  ml_touch_slot_end(&dev->touchSlots[slotIndex]);
}

static void evdev_release_all_direct_slots(struct input_device* dev) {
  for (int i = 0; i < ML_TOUCH_MAX_SLOTS; i++)
    evdev_release_direct_slot(dev, i);
}

static void evdev_start_mouse_fallback_if_needed(struct input_device* dev) {
  if (!dev->touchDirectUnsupported || dev->touchMouseSlot >= 0)
    return;

  for (int i = 0; i < ML_TOUCH_MAX_SLOTS; i++) {
    struct ml_touch_slot* slot = &dev->touchSlots[i];
    if (!slot->active || !slot->x_valid || !slot->y_valid)
      continue;

    float x;
    float y;
    evdev_transform_touch(dev, slot, &x, &y);
    evdev_send_touch_mouse_fallback(dev, i, LI_TOUCH_EVENT_DOWN, x, y);
    break;
  }
}

static void evdev_flush_touch(struct input_device* dev) {
  for (int i = 0; i < ML_TOUCH_MAX_SLOTS; i++) {
    struct ml_touch_slot* slot = &dev->touchSlots[i];
    if (!slot->x_valid || !slot->y_valid) {
      if (slot->up_pending)
        ml_touch_slot_reset(slot);
      continue;
    }

    unsigned char eventType = 0;
    if (slot->up_pending)
      eventType = LI_TOUCH_EVENT_UP;
    else if (slot->active && !slot->sent_down)
      eventType = LI_TOUCH_EVENT_DOWN;
    else if (slot->active && slot->dirty)
      eventType = LI_TOUCH_EVENT_MOVE;
    else
      continue;

    float x;
    float y;
    evdev_transform_touch(dev, slot, &x, &y);

    if (!dev->touchDirectUnsupported) {
      int ret = LiSendTouchEvent(eventType,
                                 slot->tracking_id >= 0 ? (uint32_t)slot->tracking_id : (uint32_t)i,
                                 x,
                                 y,
                                 eventType == LI_TOUCH_EVENT_UP ? 0.0f : 1.0f,
                                 0.0f,
                                 0.0f,
                                 LI_ROT_UNKNOWN);
      if (ret == LI_ERR_UNSUPPORTED) {
        dev->touchDirectUnsupported = true;
        evdev_send_touch_mouse_fallback(dev, i, eventType, x, y);
      }
    } else {
      evdev_send_touch_mouse_fallback(dev, i, eventType, x, y);
    }

    if (eventType == LI_TOUCH_EVENT_DOWN)
      slot->sent_down = true;
    if (eventType == LI_TOUCH_EVENT_UP)
      ml_touch_slot_reset(slot);
    else
      slot->dirty = false;
  }

  evdev_start_mouse_fallback_if_needed(dev);
}

static void evdev_cancel_touch_input(struct input_device* dev) {
  if (!dev->is_touchscreen)
    return;

  if (dev->touchpadMode) {
    touchpad_release_left_if_needed(dev);
    for (int i = 0; i < TOUCHPAD_MAX_SLOTS; i++)
      memset(&dev->touchpadSlots[i], 0, sizeof(dev->touchpadSlots[i]));
    dev->touchpadLastTapValid = false;
    touchpad_reset_gesture(dev);
    return;
  }

  if (!dev->touchDirectUnsupported) {
    LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL_ALL, 0, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, LI_ROT_UNKNOWN);
  }
  if (dev->touchMouseDown)
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
  dev->touchMouseDown = false;
  dev->touchMouseSlot = -1;
  for (int i = 0; i < ML_TOUCH_MAX_SLOTS; i++)
    ml_touch_slot_reset(&dev->touchSlots[i]);
}

static int evdev_get_map(int* map, int length, int value) {
  for (int i = 0; i < length; i++) {
    if (value == map[i])
      return i;
  }
  return -1;
}

static bool evdev_init_parms(struct input_device *dev, struct input_abs_parms *parms, int code) {
  int abs = evdev_get_map(dev->abs_map, ABS_MAX, code);

  if (abs >= 0) {
    parms->flat = libevdev_get_abs_flat(dev->dev, abs);
    parms->min = libevdev_get_abs_minimum(dev->dev, abs);
    parms->max = libevdev_get_abs_maximum(dev->dev, abs);
    if (parms->flat == 0 && parms->min == 0 && parms->max == 0)
      return false;

    parms->avg = (parms->min+parms->max)/2;
    parms->range = parms->max - parms->avg;
    parms->diff = parms->max - parms->min;
  }
  return true;
}

static void evdev_remove(int devindex) {
  numDevices--;

  printf("Input device removed: %s (player %d)\n", libevdev_get_name(devices[devindex].dev), devices[devindex].controllerId + 1);

  if (devices[devindex].controllerId >= 0) {
    assignedControllerIds &= ~(1 << devices[devindex].controllerId);
    LiSendMultiControllerEvent(devices[devindex].controllerId, assignedControllerIds, 0, 0, 0, 0, 0, 0, 0);
  }
  if (devices[devindex].mouseEmulation) {
    devices[devindex].mouseEmulation = false;
    pthread_join(devices[devindex].meThread, NULL);
  }
  evdev_cancel_touch_input(&devices[devindex]);

  libevdev_free(devices[devindex].dev);
  loop_remove_fd(devices[devindex].fd);
  close(devices[devindex].fd);

  if (devindex != numDevices && numDevices > 0)
    memcpy(&devices[devindex], &devices[numDevices], sizeof(struct input_device));
}

static short evdev_convert_value(struct input_event *ev, struct input_device *dev, struct input_abs_parms *parms, bool reverse) {
  if (parms->max == 0 && parms->min == 0) {
    fprintf(stderr, "Axis not found: %d\n", ev->code);
    return 0;
  }

  if (abs(ev->value - parms->avg) < parms->flat)
    return 0;
  else if (ev->value > parms->max)
    return reverse?SHRT_MIN:SHRT_MAX;
  else if (ev->value < parms->min)
    return reverse?SHRT_MAX:SHRT_MIN;
  else if (reverse)
    return (long long)(parms->max - (ev->value<parms->avg?parms->flat*2:0) - ev->value) * (SHRT_MAX-SHRT_MIN) / (parms->max-parms->min-parms->flat*2) + SHRT_MIN;
  else
    return (long long)(ev->value - (ev->value>parms->avg?parms->flat*2:0) - parms->min) * (SHRT_MAX-SHRT_MIN) / (parms->max-parms->min-parms->flat*2) + SHRT_MIN;
}

static unsigned char evdev_convert_value_byte(struct input_event *ev, struct input_device *dev, struct input_abs_parms *parms, char halfaxis) {
  if (parms->max == 0 && parms->min == 0) {
    fprintf(stderr, "Axis not found: %d\n", ev->code);
    return 0;
  }

  if (halfaxis == 0) {
    if (abs(ev->value-parms->min)<parms->flat)
      return 0;
    else if (ev->value>parms->max)
      return UCHAR_MAX;
    else
      return (ev->value - parms->flat - parms->min) * UCHAR_MAX / (parms->diff - parms->flat);
  } else {
    short val = evdev_convert_value(ev, dev, parms, false);
    if (halfaxis == '-' && val < 0)
      return -(int)val * UCHAR_MAX / (SHRT_MAX-SHRT_MIN);
    else if (halfaxis == '+' && val > 0)
      return (int)val * UCHAR_MAX / (SHRT_MAX-SHRT_MIN);
    else
      return 0;
  }
}

static bool evdev_update_short(short *target, short value) {
  if (*target == value)
    return false;

  *target = value;
  return true;
}

static bool evdev_update_byte(unsigned char *target, unsigned char value) {
  if (*target == value)
    return false;

  *target = value;
  return true;
}

void *HandleMouseEmulation(void* param)
{
  struct input_device* dev = (struct input_device*) param;

  while (dev->mouseEmulation) {
    usleep(MOUSE_EMULATION_POLLING_INTERVAL);

    short rawX;
    short rawY;

    // Determine which analog stick is currently receiving the strongest input
    if ((uint32_t)abs(dev->leftStickX) + abs(dev->leftStickY) > (uint32_t)abs(dev->rightStickX) + abs(dev->rightStickY)) {
      rawX = dev->leftStickX;
      rawY = dev->leftStickY;
    } else {
      rawX = dev->rightStickX;
      rawY = dev->rightStickY;
    }

    float deltaX;
    float deltaY;

    // Produce a base vector for mouse movement with increased speed as we deviate further from center
    deltaX = pow((float)rawX / 32767.0f * MOUSE_EMULATION_MOTION_MULTIPLIER, 3);
    deltaY = pow((float)rawY / 32767.0f * MOUSE_EMULATION_MOTION_MULTIPLIER, 3);

    // Enforce deadzones
    deltaX = fabs(deltaX) > MOUSE_EMULATION_DEADZONE ? deltaX - MOUSE_EMULATION_DEADZONE : 0;
    deltaY = fabs(deltaY) > MOUSE_EMULATION_DEADZONE ? deltaY - MOUSE_EMULATION_DEADZONE : 0;

    if (deltaX != 0 || deltaY != 0)
      LiSendMouseMoveEvent(deltaX, -deltaY);
  }

  return NULL;
}

#define SET_BTN_FLAG(x, y) supportedButtonFlags |= (x >= 0) ? y : 0

static void send_controller_arrival(struct input_device *dev) {
  unsigned char type = LI_CTYPE_UNKNOWN;
  unsigned int supportedButtonFlags = 0;
  unsigned short capabilities = 0;

  switch (libevdev_get_id_vendor(dev->dev)) {
  case 0x045e: // Microsoft
    type = LI_CTYPE_XBOX;
    break;
  case 0x054c: // Sony
    type = LI_CTYPE_XBOX;
    fprintf(stderr, "Reporting Sony gamepad as Xbox controller for host compatibility\n");
    break;
  case 0x057e: // Nintendo
    type = LI_CTYPE_NINTENDO;
    break;
  }

  const char* name = libevdev_get_name(dev->dev);
  if (name && type == LI_CTYPE_UNKNOWN) {

    // Try to guess based on the name
    if (strstr(name, "Xbox") || strstr(name, "X-Box") || strstr(name, "XBox") || strstr(name, "XBOX")) {
      type = LI_CTYPE_XBOX;
    }
  }

  SET_BTN_FLAG(dev->map->btn_a, A_FLAG);
  SET_BTN_FLAG(dev->map->btn_b, B_FLAG);
  SET_BTN_FLAG(dev->map->btn_x, X_FLAG);
  SET_BTN_FLAG(dev->map->btn_y, Y_FLAG);
  SET_BTN_FLAG(dev->map->btn_back, BACK_FLAG);
  SET_BTN_FLAG(dev->map->btn_start, PLAY_FLAG);
  SET_BTN_FLAG(dev->map->btn_guide, SPECIAL_FLAG);
  SET_BTN_FLAG(dev->map->btn_leftstick, LS_CLK_FLAG);
  SET_BTN_FLAG(dev->map->btn_rightstick, RS_CLK_FLAG);
  SET_BTN_FLAG(dev->map->btn_leftshoulder, LB_FLAG);
  SET_BTN_FLAG(dev->map->btn_rightshoulder, RB_FLAG);
  SET_BTN_FLAG(dev->map->btn_misc1, MISC_FLAG);
  SET_BTN_FLAG(dev->map->btn_paddle1, PADDLE1_FLAG);
  SET_BTN_FLAG(dev->map->btn_paddle2, PADDLE2_FLAG);
  SET_BTN_FLAG(dev->map->btn_paddle3, PADDLE3_FLAG);
  SET_BTN_FLAG(dev->map->btn_paddle4, PADDLE4_FLAG);
  SET_BTN_FLAG(dev->map->btn_touchpad, TOUCHPAD_FLAG);

  if (dev->map->abs_lefttrigger >= 0 && dev->map->abs_righttrigger >= 0)
    capabilities |= LI_CCAP_ANALOG_TRIGGERS;

  // TODO: Probe for this properly
  capabilities |= LI_CCAP_RUMBLE;

  LiSendControllerArrivalEvent(dev->controllerId, assignedControllerIds, type,
                               supportedButtonFlags, capabilities);
}

static bool evdev_handle_event(struct input_event *ev, struct input_device *dev) {
  bool gamepadModified = false;

  switch (ev->type) {
  case EV_SYN:
    if (ev->code != SYN_REPORT)
      break;
    if (dev->is_touchscreen) {
      if (dev->touchpadMode)
        touchpad_flush(dev);
      else
        evdev_flush_touch(dev);
    }

    if (dev->mouseDeltaX != 0 || dev->mouseDeltaY != 0) {
      switch (dev->rotate) {
      case 90:
        LiSendMouseMoveEvent(dev->mouseDeltaY, -dev->mouseDeltaX);
        break;
      case 180:
        LiSendMouseMoveEvent(-dev->mouseDeltaX, -dev->mouseDeltaY);
        break;
      case 270:
        LiSendMouseMoveEvent(-dev->mouseDeltaY, dev->mouseDeltaX);
        break;
      default:
        LiSendMouseMoveEvent(dev->mouseDeltaX, dev->mouseDeltaY);
        break;
      }
      dev->mouseDeltaX = 0;
      dev->mouseDeltaY = 0;
    }
    if (dev->mouseVScroll != 0) {
      LiSendScrollEvent(dev->mouseVScroll);
      dev->mouseVScroll = 0;
    }
    if (dev->mouseHScroll != 0) {
      LiSendHScrollEvent(dev->mouseHScroll);
      dev->mouseHScroll = 0;
    }
    if (dev->gamepadModified) {
      if (dev->controllerId < 0) {
        for (int i = 0; i < MAX_GAMEPADS; i++) {
          if ((assignedControllerIds & (1 << i)) == 0) {
            assignedControllerIds |= (1 << i);
            dev->controllerId = i;
            printf("Assigned %s as player %d\n", libevdev_get_name(dev->dev), i+1);
            break;
          }
        }
        //Use id 0 when too many gamepads are connected
        if (dev->controllerId < 0)
          dev->controllerId = 0;

        // Send controller arrival event to the host
        send_controller_arrival(dev);
      }
      // Send event only if mouse emulation is disabled.
      if (dev->mouseEmulation == false)
        LiSendMultiControllerEvent(dev->controllerId, assignedControllerIds, dev->buttonFlags, dev->leftTrigger, dev->rightTrigger, dev->leftStickX, dev->leftStickY, dev->rightStickX, dev->rightStickY);
      dev->gamepadModified = false;
    }
    break;
  case EV_KEY:
    if (ev->code > KEY_MAX)
      return true;
    if (ev->code < sizeof(keyCodes)/sizeof(keyCodes[0])) {
      char modifier = 0;
      switch (ev->code) {
      case KEY_LEFTSHIFT:
      case KEY_RIGHTSHIFT:
        modifier = MODIFIER_SHIFT;
        break;
      case KEY_LEFTALT:
      case KEY_RIGHTALT:
        modifier = MODIFIER_ALT;
        break;
      case KEY_LEFTCTRL:
      case KEY_RIGHTCTRL:
        modifier = MODIFIER_CTRL;
        break;
      case KEY_LEFTMETA:
      case KEY_RIGHTMETA:
        modifier = MODIFIER_META;
        break;
      }
      if (modifier != 0) {
        if (ev->value)
          dev->modifiers |= modifier;
        else
          dev->modifiers &= ~modifier;
      }

      // After the quit key combo is pressed, quit once all keys are raised
      if ((dev->modifiers & ACTION_MODIFIERS) == ACTION_MODIFIERS &&
          ev->code == QUIT_KEY && ev->value != 0) {
        waitingToExitOnModifiersUp = true;
        return true;
      } else if (waitingToExitOnModifiersUp && dev->modifiers == 0)
        return false;

      if (keyCodes[ev->code] != 0) {
        short code = 0x80 << 8 | keyCodes[ev->code];
        LiSendKeyboardEvent(code, ev->value?KEY_ACTION_DOWN:KEY_ACTION_UP, dev->modifiers);
      }
    } else {
      int mouseCode = 0;
      int gamepadCode = 0;
      int index = dev->key_map[ev->code];

      switch (ev->code) {
      case BTN_LEFT:
        mouseCode = BUTTON_LEFT;
        break;
      case BTN_MIDDLE:
        mouseCode = BUTTON_MIDDLE;
        break;
      case BTN_RIGHT:
        mouseCode = BUTTON_RIGHT;
        break;
      case BTN_SIDE:
        mouseCode = BUTTON_X1;
        break;
      case BTN_EXTRA:
        mouseCode = BUTTON_X2;
        break;
      case BTN_TOUCH:
        if (dev->touchpadMode) {
          struct timeval eventTime;
          eventTime.tv_sec = ev->input_event_sec;
          eventTime.tv_usec = ev->input_event_usec;
          if (!dev->touchHasMtSlots) {
            if (ev->value == 1)
              touchpad_begin_slot(dev, 0, 0, eventTime);
            else
              touchpad_release_slot(dev, 0, eventTime);
          } else if (ev->value == 0) {
            touchpad_release_all(dev, eventTime);
          }
        } else if (!dev->touchHasMtSlots) {
          if (ev->value == 1)
            evdev_begin_direct_slot(dev, 0, 0);
          else
            evdev_release_direct_slot(dev, 0);
        } else if (ev->value == 0) {
          evdev_release_all_direct_slots(dev);
        }
        break;
      default:
        if (dev->map == NULL)
          break;
        else if (index == dev->map->btn_a)
          gamepadCode = A_FLAG;
        else if (index == dev->map->btn_x)
          gamepadCode = X_FLAG;
        else if (index == dev->map->btn_y)
          gamepadCode = Y_FLAG;
        else if (index == dev->map->btn_b)
          gamepadCode = B_FLAG;
        else if (index == dev->map->btn_dpup)
          gamepadCode = UP_FLAG;
        else if (index == dev->map->btn_dpdown)
          gamepadCode = DOWN_FLAG;
        else if (index == dev->map->btn_dpright)
          gamepadCode = RIGHT_FLAG;
        else if (index == dev->map->btn_dpleft)
          gamepadCode = LEFT_FLAG;
        else if (index == dev->map->btn_leftstick)
          gamepadCode = LS_CLK_FLAG;
        else if (index == dev->map->btn_rightstick)
          gamepadCode = RS_CLK_FLAG;
        else if (index == dev->map->btn_leftshoulder)
          gamepadCode = LB_FLAG;
        else if (index == dev->map->btn_rightshoulder)
          gamepadCode = RB_FLAG;
        else if (index == dev->map->btn_start)
          gamepadCode = PLAY_FLAG;
        else if (index == dev->map->btn_back)
          gamepadCode = BACK_FLAG;
        else if (index == dev->map->btn_guide)
          gamepadCode = SPECIAL_FLAG;
        else if (index == dev->map->btn_misc1)
          gamepadCode = MISC_FLAG;
        else if (index == dev->map->btn_paddle1)
          gamepadCode = PADDLE1_FLAG;
        else if (index == dev->map->btn_paddle2)
          gamepadCode = PADDLE2_FLAG;
        else if (index == dev->map->btn_paddle3)
          gamepadCode = PADDLE3_FLAG;
        else if (index == dev->map->btn_paddle4)
          gamepadCode = PADDLE4_FLAG;
        else if (index == dev->map->btn_touchpad)
          gamepadCode = TOUCHPAD_FLAG;
      }

      if (mouseCode != 0) {
        LiSendMouseButtonEvent(ev->value?BUTTON_ACTION_PRESS:BUTTON_ACTION_RELEASE, mouseCode);
        gamepadModified = false;
      } else if (gamepadCode != 0) {
        int oldButtonFlags = dev->buttonFlags;
        if (ev->value) {
          dev->buttonFlags |= gamepadCode;
          dev->btnDownTime.tv_sec = ev->input_event_sec;
          dev->btnDownTime.tv_usec = ev->input_event_usec;
        } else
          dev->buttonFlags &= ~gamepadCode;
        gamepadModified = oldButtonFlags != dev->buttonFlags;

        if (mouseEmulationEnabled && gamepadCode == PLAY_FLAG && ev->value == 0) {
          struct timeval eventTime;
          eventTime.tv_sec = ev->input_event_sec;
          eventTime.tv_usec = ev->input_event_usec;
          struct timeval elapsedTime;
          timersub(&eventTime, &dev->btnDownTime, &elapsedTime);
          int holdTimeMs = elapsedTime.tv_sec * 1000 + elapsedTime.tv_usec / 1000;
          if (holdTimeMs >= MOUSE_EMULATION_LONG_PRESS_TIME) {
            if (dev->mouseEmulation) {
              dev->mouseEmulation = false;
              pthread_join(dev->meThread, NULL);
              dev->meThread = 0;
              printf("Mouse emulation disabled for controller %d.\n", dev->controllerId);
            } else {
              dev->mouseEmulation = true;
              pthread_create(&dev->meThread, NULL, HandleMouseEmulation, dev);
              printf("Mouse emulation enabled for controller %d.\n", dev->controllerId);
            }
            // clear gamepad state.
            LiSendMultiControllerEvent(dev->controllerId, assignedControllerIds, 0, 0, 0, 0, 0, 0, 0);
          }
        } else if (dev->mouseEmulation) {
          char action = ev->value ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE;
          switch (gamepadCode) {
            case A_FLAG:
              LiSendMouseButtonEvent(action, BUTTON_LEFT);
              break;
            case B_FLAG:
              LiSendMouseButtonEvent(action, BUTTON_RIGHT);
              break;
            case X_FLAG:
              LiSendMouseButtonEvent(action, BUTTON_MIDDLE);
              break;
            case LB_FLAG:
              LiSendMouseButtonEvent(action, BUTTON_X1);
              break;
            case RB_FLAG:
              LiSendMouseButtonEvent(action, BUTTON_X2);
              break;
          }
        }
      } else if (dev->map != NULL && index == dev->map->btn_lefttrigger) {
        gamepadModified = evdev_update_byte(&dev->leftTrigger, ev->value ? UCHAR_MAX : 0);
      } else if (dev->map != NULL && index == dev->map->btn_righttrigger) {
        gamepadModified = evdev_update_byte(&dev->rightTrigger, ev->value ? UCHAR_MAX : 0);
      } else {
        if (dev->map != NULL)
          fprintf(stderr, "Unmapped button: %d\n", ev->code);

        gamepadModified = false;
      }
    }
    break;
  case EV_REL:
    switch (ev->code) {
      case REL_X:
        dev->mouseDeltaX += ev->value;
        break;
      case REL_Y:
        dev->mouseDeltaY += ev->value;
        break;
      case REL_HWHEEL:
        dev->mouseHScroll += ev->value;
        break;
      case REL_WHEEL:
        dev->mouseVScroll += ev->value;
        break;
    }
    break;
  case EV_ABS:
    if (ev->code > ABS_MAX)
      return true;
    if (dev->is_touchscreen) {
      switch (ev->code) {
      case ABS_MT_SLOT:
        dev->touchSlot = ev->value;
        break;
      case ABS_MT_TRACKING_ID:
        if (dev->touchpadMode) {
          struct timeval eventTime;
          eventTime.tv_sec = ev->input_event_sec;
          eventTime.tv_usec = ev->input_event_usec;
          if (ev->value >= 0)
            touchpad_begin_slot(dev, dev->touchSlot, ev->value, eventTime);
          else
            touchpad_release_slot(dev, dev->touchSlot, eventTime);
        } else {
          if (ev->value >= 0)
            evdev_begin_direct_slot(dev, dev->touchSlot, ev->value);
          else
            evdev_release_direct_slot(dev, dev->touchSlot);
        }
        break;
      case ABS_MT_POSITION_X:
      case ABS_X:
        if (dev->touchpadMode) {
          int slot = ev->code == ABS_X ? 0 : dev->touchSlot;
          touchpad_update_slot_axis(dev, slot, true, ev->value);
          break;
        }
        {
          int slot = ev->code == ABS_X ? 0 : dev->touchSlot;
          if (slot >= 0 && slot < ML_TOUCH_MAX_SLOTS)
            ml_touch_slot_update(&dev->touchSlots[slot], true, ev->value);
        }
        break;
      case ABS_MT_POSITION_Y:
      case ABS_Y:
        if (dev->touchpadMode) {
          int slot = ev->code == ABS_Y ? 0 : dev->touchSlot;
          touchpad_update_slot_axis(dev, slot, false, ev->value);
          break;
        }
        {
          int slot = ev->code == ABS_Y ? 0 : dev->touchSlot;
          if (slot >= 0 && slot < ML_TOUCH_MAX_SLOTS)
            ml_touch_slot_update(&dev->touchSlots[slot], false, ev->value);
        }
        break;
      }
      break;
    }

    if (dev->map == NULL)
      break;

    int index = dev->abs_map[ev->code];
    int hat_index = (ev->code - ABS_HAT0X) / 2;
    int hat_dir_index = (ev->code - ABS_HAT0X) % 2;
    int oldButtonFlags = dev->buttonFlags;

    switch (ev->code) {
    case ABS_HAT0X:
    case ABS_HAT0Y:
    case ABS_HAT1X:
    case ABS_HAT1Y:
    case ABS_HAT2X:
    case ABS_HAT2Y:
    case ABS_HAT3X:
    case ABS_HAT3Y:
      dev->hats_state[hat_index][hat_dir_index] = ev->value < 0 ? -1 : (ev->value == 0 ? 0 : 1);
      int hat_state = hat_constants[dev->hats_state[hat_index][1] + 1][dev->hats_state[hat_index][0] + 1];
      if (hat_index == dev->map->hat_dpup)
        set_hat(dev->buttonFlags, UP_FLAG, hat_state, dev->map->hat_dir_dpup);
      if (hat_index == dev->map->hat_dpdown)
        set_hat(dev->buttonFlags, DOWN_FLAG, hat_state, dev->map->hat_dir_dpdown);
      if (hat_index == dev->map->hat_dpright)
        set_hat(dev->buttonFlags, RIGHT_FLAG, hat_state, dev->map->hat_dir_dpright);
      if (hat_index == dev->map->hat_dpleft)
        set_hat(dev->buttonFlags, LEFT_FLAG, hat_state, dev->map->hat_dir_dpleft);
      gamepadModified = oldButtonFlags != dev->buttonFlags;
      break;
    default:
      if (index == dev->map->abs_leftx)
        gamepadModified = evdev_update_short(&dev->leftStickX, evdev_convert_value(ev, dev, &dev->xParms, dev->map->reverse_leftx));
      else if (index == dev->map->abs_lefty)
        gamepadModified = evdev_update_short(&dev->leftStickY, evdev_convert_value(ev, dev, &dev->yParms, !dev->map->reverse_lefty));
      else if (index == dev->map->abs_rightx)
        gamepadModified = evdev_update_short(&dev->rightStickX, evdev_convert_value(ev, dev, &dev->rxParms, dev->map->reverse_rightx));
      else if (index == dev->map->abs_righty)
        gamepadModified = evdev_update_short(&dev->rightStickY, evdev_convert_value(ev, dev, &dev->ryParms, !dev->map->reverse_righty));
      else
        gamepadModified = false;

      if (index == dev->map->abs_lefttrigger) {
        gamepadModified |= evdev_update_byte(&dev->leftTrigger, evdev_convert_value_byte(ev, dev, &dev->zParms, dev->map->halfaxis_lefttrigger));
      }
      if (index == dev->map->abs_righttrigger) {
        gamepadModified |= evdev_update_byte(&dev->rightTrigger, evdev_convert_value_byte(ev, dev, &dev->rzParms, dev->map->halfaxis_righttrigger));
      }

      if (index == dev->map->abs_dpright) {
        int oldFlags = dev->buttonFlags;
        if (evdev_convert_value_byte(ev, dev, &dev->rightParms, dev->map->halfaxis_dpright) > 127)
          dev->buttonFlags |= RIGHT_FLAG;
        else
          dev->buttonFlags &= ~RIGHT_FLAG;

        gamepadModified |= oldFlags != dev->buttonFlags;
      }
      if (index == dev->map->abs_dpleft) {
        int oldFlags = dev->buttonFlags;
        if (evdev_convert_value_byte(ev, dev, &dev->leftParms, dev->map->halfaxis_dpleft) > 127)
          dev->buttonFlags |= LEFT_FLAG;
        else
          dev->buttonFlags &= ~LEFT_FLAG;

        gamepadModified |= oldFlags != dev->buttonFlags;
      }
      if (index == dev->map->abs_dpup) {
        int oldFlags = dev->buttonFlags;
        if (evdev_convert_value_byte(ev, dev, &dev->upParms, dev->map->halfaxis_dpup) > 127)
          dev->buttonFlags |= UP_FLAG;
        else
          dev->buttonFlags &= ~UP_FLAG;

        gamepadModified |= oldFlags != dev->buttonFlags;
      }
      if (index == dev->map->abs_dpdown) {
        int oldFlags = dev->buttonFlags;
        if (evdev_convert_value_byte(ev, dev, &dev->downParms, dev->map->halfaxis_dpdown) > 127)
          dev->buttonFlags |= DOWN_FLAG;
        else
          dev->buttonFlags &= ~DOWN_FLAG;

        gamepadModified |= oldFlags != dev->buttonFlags;
      }
    }
  }

  if (gamepadModified && (dev->buttonFlags & QUIT_BUTTONS) == QUIT_BUTTONS) {
    LiSendMultiControllerEvent(dev->controllerId, assignedControllerIds, 0, 0, 0, 0, 0, 0, 0);
    return false;
  }

  dev->gamepadModified |= gamepadModified;
  return true;
}

static bool evdev_handle_mapping_event(struct input_event *ev, struct input_device *dev) {
  int index, hat_index;
  switch (ev->type) {
  case EV_KEY:
    index = dev->key_map[ev->code];
    if (currentKey != NULL) {
      if (ev->value)
        *currentKey = index;
      else if (*currentKey != -1 && index == *currentKey)
        return false;
    }
    break;
  case EV_ABS:
    hat_index = (ev->code - ABS_HAT0X) / 2;
    if (hat_index >= 0 && hat_index < 4) {
      int hat_dir_index = (ev->code - ABS_HAT0X) % 2;
      dev->hats_state[hat_index][hat_dir_index] = ev->value < 0 ? -1 : (ev->value == 0 ? 0 : 1);
    }
    if (currentAbs != NULL) {
      struct input_abs_parms parms;
      evdev_init_parms(dev, &parms, ev->code);

      if (ev->value > parms.avg + parms.range/2) {
        *currentAbs = dev->abs_map[ev->code];
        *currentReverse = false;
      } else if (ev->value < parms.avg - parms.range/2) {
        *currentAbs = dev->abs_map[ev->code];
        *currentReverse = true;
      } else if (ev->code == *currentAbs)
        return false;
    } else if (currentHat != NULL) {
      if (hat_index >= 0 && hat_index < 4) {
        *currentHat = hat_index;
        *currentHatDir = hat_constants[dev->hats_state[hat_index][1] + 1][dev->hats_state[hat_index][0] + 1];
        return false;
      }
    }
    break;
  }
  return true;
}

void evdev_scan_devices(struct mapping* mappings, bool verbose, int rotate) {
  char path[64];
  for (int i = 0; i < 64; i++) {
    snprintf(path, sizeof(path), "/dev/input/event%d", i);
    if (access(path, R_OK | W_OK) == 0)
      evdev_create(path, mappings, verbose, rotate);
  }
}

static void evdev_drain(void) {
  for (int i = 0; i < numDevices; i++) {
    struct input_event ev;
    while (libevdev_next_event(devices[i].dev, LIBEVDEV_READ_FLAG_NORMAL, &ev) >= 0);
  }
}

static int evdev_handle(int fd) {
  for (int i=0;i<numDevices;i++) {
    if (devices[i].fd == fd) {
      int rc;
      struct input_event ev;
      while ((rc = libevdev_next_event(devices[i].dev, LIBEVDEV_READ_FLAG_NORMAL, &ev)) >= 0) {
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
          if (!devices[i].resyncing) {
            fprintf(stderr, "Input event queue overflow on %s; resyncing\n", libevdev_get_name(devices[i].dev));
            devices[i].resyncing = true;
          }
          if (!handler(&ev, &devices[i]))
            return LOOP_RETURN;
        } else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
          devices[i].resyncing = false;
          if (!handler(&ev, &devices[i]))
            return LOOP_RETURN;
        }
      }
      if (rc == -ENODEV) {
        evdev_remove(i);
      } else if (rc != -EAGAIN && rc < 0) {
        fprintf(stderr, "Error: %s\n", strerror(-rc));
        exit(EXIT_FAILURE);
      }
    }
  }
  return LOOP_OK;
}

void evdev_create(const char* device, struct mapping* mappings, bool verbose, int rotate) {
  int fd = open(device, O_RDWR|O_NONBLOCK);
  if (fd <= 0) {
    fprintf(stderr, "Failed to open device %s\n", device);
    fflush(stderr);
    return;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    fprintf(stderr, "Failed to stat device %s\n", device);
    close(fd);
    return;
  }
  for (int i = 0; i < numDevices; i++) {
    if (devices[i].rdev == st.st_rdev) {
      if (verbose)
        printf("Input device %s already added\n", device);
      close(fd);
      return;
    }
  }

  struct libevdev *evdev = libevdev_new();
  libevdev_set_fd(evdev, fd);
  const char* name = libevdev_get_name(evdev);

  int16_t guid[8] = {0};
  guid[0] = int16_to_le(libevdev_get_id_bustype(evdev));
  int16_t vendor = libevdev_get_id_vendor(evdev);
  int16_t product = libevdev_get_id_product(evdev);
  if (vendor && product) {
    guid[2] = int16_to_le(vendor);
    guid[4] = int16_to_le(product);
    guid[6] = int16_to_le(libevdev_get_id_version(evdev));
  } else
    strncpy((char*) &guid[2], name, 11);

  char str_guid[33];
  char* buf = str_guid;
  for (int i = 0; i < 16; i++)
    buf += sprintf(buf, "%02x", ((unsigned char*) guid)[i]);

  struct mapping* default_mapping = NULL;
  struct mapping* xwc_mapping = NULL;
  while (mappings != NULL) {
    if (strncmp(str_guid, mappings->guid, 32) == 0) {
      if (verbose)
        printf("Detected %s (%s) on %s as %s\n", name, str_guid, device, mappings->name);

      break;
    } else if (strncmp("default", mappings->guid, 32) == 0)
      default_mapping = mappings;
    else if (strncmp("xwc", mappings->guid, 32) == 0)
      xwc_mapping = mappings;

    mappings = mappings->next;
  }

  if (mappings == NULL && strstr(name, "Xbox 360 Wireless Receiver") != NULL)
    mappings = xwc_mapping;

  bool is_keyboard = evdev_has_keyboard_keys(evdev);
  bool is_mouse = libevdev_has_event_type(evdev, EV_REL) || libevdev_has_event_code(evdev, EV_KEY, BTN_LEFT);
  bool has_touch_capability =
    (libevdev_has_event_code(evdev, EV_KEY, BTN_TOUCH) ||
     libevdev_has_event_code(evdev, EV_ABS, ABS_MT_TRACKING_ID)) &&
    ((libevdev_has_event_code(evdev, EV_ABS, ABS_X) &&
      libevdev_has_event_code(evdev, EV_ABS, ABS_Y)) ||
     (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) &&
      libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y)));

  // This classification logic comes from SDL
  bool is_accelerometer =
    ((libevdev_has_event_code(evdev, EV_ABS, ABS_X) &&
      libevdev_has_event_code(evdev, EV_ABS, ABS_Y) &&
      libevdev_has_event_code(evdev, EV_ABS, ABS_Z)) ||
     (libevdev_has_event_code(evdev, EV_ABS, ABS_RX) &&
      libevdev_has_event_code(evdev, EV_ABS, ABS_RY) &&
      libevdev_has_event_code(evdev, EV_ABS, ABS_RZ))) &&
    !libevdev_has_event_type(evdev, EV_KEY);
  bool has_gamepad_axis =
    (libevdev_has_event_code(evdev, EV_ABS, ABS_X) &&
     libevdev_has_event_code(evdev, EV_ABS, ABS_Y)) ||
    (libevdev_has_event_code(evdev, EV_ABS, ABS_HAT0X) &&
     libevdev_has_event_code(evdev, EV_ABS, ABS_HAT0Y)) ||
    libevdev_has_event_code(evdev, EV_ABS, ABS_RX) ||
    libevdev_has_event_code(evdev, EV_ABS, ABS_RY) ||
    libevdev_has_event_code(evdev, EV_ABS, ABS_RZ) ||
    libevdev_has_event_code(evdev, EV_ABS, ABS_THROTTLE) ||
    libevdev_has_event_code(evdev, EV_ABS, ABS_RUDDER) ||
    libevdev_has_event_code(evdev, EV_ABS, ABS_WHEEL) ||
    libevdev_has_event_code(evdev, EV_ABS, ABS_GAS) ||
    libevdev_has_event_code(evdev, EV_ABS, ABS_BRAKE);
  bool has_gamepad_button =
    libevdev_has_event_code(evdev, EV_KEY, BTN_TRIGGER) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_A) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_B) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_X) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_Y) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_1) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_GAMEPAD) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_TL) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_TR) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_SELECT) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_START) ||
    libevdev_has_event_code(evdev, EV_KEY, BTN_MODE);
  bool is_gamepad = has_gamepad_button && (has_gamepad_axis || !is_keyboard);
  bool touch_device_matches = configured_touch_matches(st.st_rdev);
  bool is_touchscreen = ml_touch_device_allowed(has_touch_capability, is_gamepad,
                                                configuredTouchDevice, touch_device_matches);

  if (has_touch_capability && !is_touchscreen && verbose) {
    fprintf(stderr, "Ignoring touch capability on %s (%s): %s\n",
            name, device,
            configuredTouchDevice ? "not configured physical touchscreen" : "gamepad touch surface");
  }

  if (is_accelerometer) {
    if (verbose)
      printf("Ignoring accelerometer: %s\n", name);
    libevdev_free(evdev);
    close(fd);
    return;
  }

  if (!is_keyboard && !is_mouse && !is_touchscreen && !is_gamepad) {
    if (verbose)
      printf("Ignoring non-input event device: %s on %s\n", name, device);
    libevdev_free(evdev);
    close(fd);
    return;
  }

  if (is_gamepad) {
    evdev_gamepads++;

    if (mappings == NULL) {
      fprintf(stderr, "No external mapping available for %s (%s) on %s\n", name, str_guid, device);
      mappings = default_mapping;
    }
  } else {
    if (verbose)
      printf("Not mapping %s as a gamepad\n", name);
    mappings = NULL;
  }

  int dev = numDevices;
  numDevices++;

  if (devices == NULL) {
    devices = malloc(sizeof(struct input_device));
  } else {
    devices = realloc(devices, sizeof(struct input_device)*numDevices);
  }

  if (devices == NULL) {
    fprintf(stderr, "Not enough memory\n");
    exit(EXIT_FAILURE);
  }

  memset(&devices[dev], 0, sizeof(devices[0]));
  devices[dev].fd = fd;
  devices[dev].rdev = st.st_rdev;
  devices[dev].dev = evdev;
  devices[dev].map = mappings;
  /* Set unused evdev indices to -2 to avoid aliasing with the default -1 in our mappings */
  memset(&devices[dev].key_map, -2, sizeof(devices[dev].key_map));
  memset(&devices[dev].abs_map, -2, sizeof(devices[dev].abs_map));
  devices[dev].is_keyboard = is_keyboard;
  devices[dev].is_mouse = is_mouse;
  devices[dev].is_touchscreen = is_touchscreen;
  devices[dev].rotate = configuredTouchRotation >= 0 ? configuredTouchRotation : rotate;
  devices[dev].touchSlot = 0;
  devices[dev].touchMouseSlot = -1;
  devices[dev].touchOffsetX = configuredTouchOffsetX;
  devices[dev].touchOffsetY = configuredTouchOffsetY;
  for (int i = 0; i < ML_TOUCH_MAX_SLOTS; i++)
    ml_touch_slot_reset(&devices[dev].touchSlots[i]);
  for (int i = 0; i < TOUCHPAD_MAX_SLOTS; i++)
    devices[dev].touchpadSlots[i].trackingId = -1;
  devices[dev].touchMinX = 0;
  devices[dev].touchMaxX = 1;
  devices[dev].touchMinY = 0;
  devices[dev].touchMaxY = 1;

  int nbuttons = 0;
  /* Count joystick buttons first like SDL does */
  for (int i = BTN_JOYSTICK; i < KEY_MAX; ++i) {
    if (libevdev_has_event_code(devices[dev].dev, EV_KEY, i))
      devices[dev].key_map[i] = nbuttons++;
  }
  for (int i = 0; i < BTN_JOYSTICK; ++i) {
    if (libevdev_has_event_code(devices[dev].dev, EV_KEY, i))
      devices[dev].key_map[i] = nbuttons++;
  }

  int naxes = 0;
  for (int i = 0; i < ABS_MAX; ++i) {
    /* Skip hats */
    if (i == ABS_HAT0X)
      i = ABS_HAT3Y;
    else if (libevdev_has_event_code(devices[dev].dev, EV_ABS, i))
      devices[dev].abs_map[i] = naxes++;
  }

  if (is_gamepad && devices[dev].map == NULL)
    devices[dev].map = evdev_generate_default_mapping(&devices[dev], name);

  devices[dev].controllerId = -1;
  devices[dev].haptic_effect_id = -1;

  if (devices[dev].is_touchscreen) {
    const char* touch_mode = getenv("MOONLIGHT_RK_TOUCH_MODE");
    devices[dev].touchpadMode = touch_mode != NULL && strcmp(touch_mode, "touchpad") == 0;
    devices[dev].touchHasMtSlots = libevdev_has_event_code(devices[dev].dev, EV_ABS, ABS_MT_SLOT) ||
                                   libevdev_has_event_code(devices[dev].dev, EV_ABS, ABS_MT_TRACKING_ID);
    evdev_touch_bounds(&devices[dev], ABS_X, &devices[dev].touchMinX, &devices[dev].touchMaxX);
    evdev_touch_bounds(&devices[dev], ABS_Y, &devices[dev].touchMinY, &devices[dev].touchMaxY);
    evdev_touch_bounds(&devices[dev], ABS_MT_POSITION_X, &devices[dev].touchMinX, &devices[dev].touchMaxX);
    evdev_touch_bounds(&devices[dev], ABS_MT_POSITION_Y, &devices[dev].touchMinY, &devices[dev].touchMaxY);
    int activeMinX = devices[dev].touchMinX + devices[dev].touchOffsetX;
    int activeMinY = devices[dev].touchMinY + devices[dev].touchOffsetY;
    if (activeMinX < devices[dev].touchMinX)
      activeMinX = devices[dev].touchMinX;
    if (activeMinX > devices[dev].touchMaxX)
      activeMinX = devices[dev].touchMaxX;
    if (activeMinY < devices[dev].touchMinY)
      activeMinY = devices[dev].touchMinY;
    if (activeMinY > devices[dev].touchMaxY)
      activeMinY = devices[dev].touchMaxY;
    fprintf(stderr,
            "Touch input %s on %s rdev=%llu raw=%d..%d x %d..%d active=%d..%d x %d..%d slots=%d rotate=%d mode=%s\n",
            name,
            device,
            (unsigned long long)devices[dev].rdev,
            devices[dev].touchMinX,
            devices[dev].touchMaxX,
            devices[dev].touchMinY,
            devices[dev].touchMaxY,
            activeMinX,
            devices[dev].touchMaxX,
            activeMinY,
            devices[dev].touchMaxY,
            devices[dev].touchHasMtSlots ? ML_TOUCH_MAX_SLOTS : 1,
            devices[dev].rotate,
            devices[dev].touchpadMode ? "touchpad" : "screen");
  } else if (is_gamepad) {
    fprintf(stderr, "Gamepad input %s on %s ready\n", name, device);
  } else if (is_keyboard) {
    fprintf(stderr, "Keyboard input %s on %s ready\n", name, device);
  }

  if (devices[dev].map != NULL) {
    bool valid = evdev_init_parms(&devices[dev], &(devices[dev].xParms), devices[dev].map->abs_leftx);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].yParms), devices[dev].map->abs_lefty);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].zParms), devices[dev].map->abs_lefttrigger);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].rxParms), devices[dev].map->abs_rightx);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].ryParms), devices[dev].map->abs_righty);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].rzParms), devices[dev].map->abs_righttrigger);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].leftParms), devices[dev].map->abs_dpleft);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].rightParms), devices[dev].map->abs_dpright);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].upParms), devices[dev].map->abs_dpup);
    valid &= evdev_init_parms(&devices[dev], &(devices[dev].downParms), devices[dev].map->abs_dpdown);
    if (!valid)
      fprintf(stderr, "Mapping for %s (%s) on %s is incorrect\n", name, str_guid, device);
  }

  if (grabbingDevices && (is_keyboard || is_mouse || is_touchscreen)) {
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
      fprintf(stderr, "EVIOCGRAB failed with error %d\n", errno);
    }
  }

  loop_add_fd(devices[dev].fd, &evdev_handle, POLLIN);
}

static void evdev_map_key(char* keyName, short* key) {
  printf("Press %s\n", keyName);
  currentKey = key;
  currentHat = NULL;
  currentAbs = NULL;
  *key = -1;
  loop_main();

  usleep(250000);
  evdev_drain();
}

static void evdev_map_abs(char* keyName, short* abs, bool* reverse) {
  printf("Move %s\n", keyName);
  currentKey = NULL;
  currentHat = NULL;
  currentAbs = abs;
  currentReverse = reverse;
  *abs = -1;
  loop_main();

  usleep(250000);
  evdev_drain();
}

static void evdev_map_hatkey(char* keyName, short* hat, short* hat_dir, short* key) {
  printf("Press %s\n", keyName);
  currentKey = key;
  currentHat = hat;
  currentHatDir = hat_dir;
  currentAbs = NULL;
  *key = -1;
  *hat = -1;
  *hat_dir = -1;
  *currentReverse = false;
  loop_main();

  usleep(250000);
  evdev_drain();
}

static void evdev_map_abskey(char* keyName, short* abs, short* key, bool* reverse) {
  printf("Press %s\n", keyName);
  currentKey = key;
  currentHat = NULL;
  currentAbs = abs;
  currentReverse = reverse;
  *key = -1;
  *abs = -1;
  *currentReverse = false;
  loop_main();

  usleep(250000);
  evdev_drain();
}

void evdev_map(char* device) {
  int fd = open(device, O_RDONLY|O_NONBLOCK);
  struct libevdev *evdev = libevdev_new();
  libevdev_set_fd(evdev, fd);
  const char* name = libevdev_get_name(evdev);

  int16_t guid[8] = {0};
  guid[0] = int16_to_le(libevdev_get_id_bustype(evdev));
  guid[2] = int16_to_le(libevdev_get_id_vendor(evdev));
  guid[4] = int16_to_le(libevdev_get_id_product(evdev));
  guid[6] = int16_to_le(libevdev_get_id_version(evdev));
  char str_guid[33];
  char* buf = str_guid;
  for (int i = 0; i < 16; i++)
    buf += sprintf(buf, "%02x", ((unsigned char*) guid)[i]);

  struct mapping map = {0};
  strncpy(map.name, name, sizeof(map.name) - 1);
  strncpy(map.guid, str_guid, sizeof(map.guid) - 1);

  libevdev_free(evdev);
  close(fd);

  handler = evdev_handle_mapping_event;

  evdev_map_abs("Left Stick Right", &(map.abs_leftx), &(map.reverse_leftx));
  evdev_map_abs("Left Stick Up", &(map.abs_lefty), &(map.reverse_lefty));
  evdev_map_key("Left Stick Button", &(map.btn_leftstick));

  evdev_map_abs("Right Stick Right", &(map.abs_rightx), &(map.reverse_rightx));
  evdev_map_abs("Right Stick Up", &(map.abs_righty), &(map.reverse_righty));
  evdev_map_key("Right Stick Button", &(map.btn_rightstick));

  evdev_map_hatkey("D-Pad Right", &(map.hat_dpright), &(map.hat_dir_dpright), &(map.btn_dpright));
  evdev_map_hatkey("D-Pad Left", &(map.hat_dpleft), &(map.hat_dir_dpleft), &(map.btn_dpleft));
  evdev_map_hatkey("D-Pad Up", &(map.hat_dpup), &(map.hat_dir_dpup), &(map.btn_dpup));
  evdev_map_hatkey("D-Pad Down", &(map.hat_dpdown), &(map.hat_dir_dpdown), &(map.btn_dpdown));

  evdev_map_key("Button X (1)", &(map.btn_x));
  evdev_map_key("Button A (2)", &(map.btn_a));
  evdev_map_key("Button B (3)", &(map.btn_b));
  evdev_map_key("Button Y (4)", &(map.btn_y));
  evdev_map_key("Back Button", &(map.btn_back));
  evdev_map_key("Start Button", &(map.btn_start));
  evdev_map_key("Special Button", &(map.btn_guide));

  bool ignored;
  evdev_map_abskey("Left Trigger", &(map.abs_lefttrigger), &(map.btn_lefttrigger), &ignored);
  evdev_map_abskey("Right Trigger", &(map.abs_righttrigger), &(map.btn_righttrigger), &ignored);

  evdev_map_key("Left Bumper", &(map.btn_leftshoulder));
  evdev_map_key("Right Bumper", &(map.btn_rightshoulder));
  mapping_print(&map);
}

void evdev_start() {
  // After grabbing, the only way to quit via the keyboard
  // is via the special key combo that the input handling
  // code looks for. For this reason, we wait to grab until
  // we're ready to take input events. Ctrl+C works up until
  // this point.
  for (int i = 0; i < numDevices; i++) {
    if ((devices[i].is_keyboard || devices[i].is_mouse || devices[i].is_touchscreen) && ioctl(devices[i].fd, EVIOCGRAB, 1) < 0) {
      fprintf(stderr, "EVIOCGRAB failed with error %d\n", errno);
    }
  }

  // Any new input devices detected after this point will be grabbed immediately
  grabbingDevices = true;

  // Handle input events until the quit combo is pressed
}

void evdev_stop() {
  evdev_drain();
  for (int i = 0; i < numDevices; i++)
    evdev_cancel_touch_input(&devices[i]);
}

void evdev_init(bool mouse_emulation_enabled) {
  handler = evdev_handle_event;
  mouseEmulationEnabled = mouse_emulation_enabled;
}

static struct input_device* evdev_get_input_device(unsigned short controller_id) {
  for (int i=0; i<numDevices; i++)
    if (devices[i].controllerId == controller_id)
      return &devices[i];

  return NULL;
}

void evdev_rumble(unsigned short controller_id, unsigned short low_freq_motor, unsigned short high_freq_motor) {
  struct input_device* device = evdev_get_input_device(controller_id);
  if (!device)
    return;

  if (device->haptic_effect_id >= 0) {
    ioctl(device->fd, EVIOCRMFF, device->haptic_effect_id);
    device->haptic_effect_id = -1;
  }

  if (low_freq_motor == 0 && high_freq_motor == 0)
    return;

  struct ff_effect effect = {0};
  effect.type = FF_RUMBLE;
  effect.id = -1;
  effect.replay.length = USHRT_MAX;
  effect.u.rumble.strong_magnitude = low_freq_motor;
  effect.u.rumble.weak_magnitude = high_freq_motor;
  if (ioctl(device->fd, EVIOCSFF, &effect) == -1)
    return;

  struct input_event event = {0};
  event.type = EV_FF;
  event.code = effect.id;
  event.value = 1;
  write(device->fd, (const void*) &event, sizeof(event));
  device->haptic_effect_id = effect.id;
}
