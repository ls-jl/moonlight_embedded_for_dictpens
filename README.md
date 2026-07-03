# Moonlight Embedded Miniapp for RK3562

This project packages a Rockchip RK3562 build of Moonlight Embedded into a Youdao/Falcon miniapp. The miniapp provides a settings page for host pairing and stream options, then enters a blank frame page where the bundled Moonlight binary takes over the DRM framebuffer directly.

## What Is Included

- Falcon miniapp UI for pairing hosts, selecting a host, and configuring stream options.
- Bundled RK3562 Moonlight runtime under `assets/moonlight-rk3562`.
- Native miniapp JSAPI module under `jsapi/src/jsapi_rgbframe`.
- RK DRM/MPP/RGA Moonlight backend changes under `moonlight-embedded-master`.
- Prebuilt runtime snapshots under `build-rk3562-from-pve`.

## Runtime Behavior

1. `src/pages/index/index.vue` shows the vertical settings page.
2. Host pairing data is stored in the miniapp data directory, not hardcoded.
3. Starting a stream navigates to `src/pages/frame/frame.vue`.
4. The frame page copies the bundled Moonlight runtime into the miniapp data directory and starts Moonlight as a child process.
5. Moonlight uses the RK DRM backend to claim the framebuffer plane.
6. Leaving the frame page stops Moonlight and releases the framebuffer.
7. If Moonlight exits unexpectedly, the frame page watchdog returns to the settings page.

## Current Stream Defaults

- App: `Desktop`
- Codec: H.264
- Default host field: `192.168.100.120`
- Default bitrate: `1500 Kbps`
- Bitrate slider range: `500 Kbps` to `25000 Kbps`
- Packet size: `1024`
- Remote mode: `yes`
- FPS: `30` or `60`
- Orientation modes:
  - portrait
  - portrait flipped
  - landscape
  - landscape flipped
- Input modes:
  - view only
  - direct touch screen
  - touchpad-style relative mouse
  - USB OTG gamepad

## RK-Specific Moonlight Work

The RK backend is in `moonlight-embedded-master/src/video/rk.c`.

Implemented behavior includes:

- MPP hardware decoding.
- DRM plane presentation.
- DRM plane rotation when supported.
- RGA hardware rotation fallback for `90`, `180`, and `270` degree output.
- No CPU rotation fallback.
- For `90` and `270` degree rotation, the requested stream size is swapped internally and rotated back to the target screen size.

The input changes are mainly in `moonlight-embedded-master/src/input/evdev.c`:

- Touch screen mode.
- Touchpad mode with relative mouse behavior.
- Gamepad hotplug rescan.
- DualShock 4 fallback mapping.
- Sony gamepad reporting as an Xbox controller for host compatibility.
- Reduced controller event spam by only sending changed controller state.

The native miniapp bridge is in `jsapi/src/jsapi_rgbframe/JSRgbFramePlayer.cpp`:

- Starts and stops the Moonlight process.
- Copies bundled runtime files into the writable app data directory.
- Keeps the screen awake while streaming.
- Switches RK USB OTG into host mode for input mode.
- Re-applies Wi-Fi and USB runtime power settings during streaming.
- Passes stream options from miniapp JS to Moonlight CLI arguments.

## Project Layout

```text
.
├── assets/moonlight-rk3562/        # Runtime files bundled into the miniapp
├── build-rk3562-from-pve/          # Local copy of PVE-built RK3562 artifacts
├── jsapi/                          # Native rgbframe JSAPI module
├── libs/                           # Miniapp native libraries
├── moonlight-embedded-master/      # Forked Moonlight Embedded source
├── src/pages/index/                # Settings and pairing page
├── src/pages/frame/                # Blank streaming lifecycle page
├── tools/                          # Miniapp docs and toolchain archives
├── package.json                    # Falcon miniapp scripts
└── icon.png                        # Miniapp icon
```

## Build Miniapp Package

```bash
npm run build:bin
```

This generates:

```text
8001780158276510.1_0_0.amr
```

The AMR package is ignored by Git because it is a generated artifact.

## Cross Build Native Artifacts

The current workflow uses a PVE build machine with the RK3562 toolchain. Keep credentials out of the repo and provide them locally through your shell or SSH config.

Build Moonlight on the PVE machine:

```bash
cmake --build /home/pve/moonlight_embedded_rk3562_20260531_120550/moonlight-embedded-master/build-rk3562 --parallel
```

Build the JSAPI module on the PVE machine:

```bash
cmake --build /home/pve/moonlight_embedded_rk3562_20260531_120550/jsapi/build-rk3562 --parallel
```

After copying artifacts back locally, update:

```text
assets/moonlight-rk3562/moonlight
build-rk3562-from-pve/moonlight-rk3562-runtime/moonlight
libs/arm64-orange/libjsapi_rgbframe.so
libs/libjsapi_rgbframe_12345.so
```

Then rebuild the miniapp package:

```bash
npm run build:bin
```

## Install on Device

Install the AMR package with ADB when ADB is available:

```bash
adb push 8001780158276510.1_0_0.amr /userdisk/8001780158276510.1_0_0.amr
adb shell 'miniapp_cli install /userdisk/8001780158276510.1_0_0.amr'
adb shell 'miniapp_cli start 8001780158276510 --index'
```

When using SSH on the device, copy the package and run:

```bash
miniapp_cli install /userdisk/8001780158276510.1_0_0.amr
miniapp_cli start 8001780158276510 --index
```

## Device Runtime Paths

Installed package path is version-slot based. It is usually one of:

```text
/userdisk/secondary/miniapp/data/mini_app/pkg/8001780158276510/a
/userdisk/secondary/miniapp/data/mini_app/pkg/8001780158276510/b
```

Writable app data lives under:

```text
/userdisk/secondary/miniapp/data/mini_app/pkg/8001780158276510/data
```

Moonlight runtime and keys are copied under:

```text
data/moonlight/runtime
data/moonlight/keys
```

Useful logs:

```text
data/moonlight/moonlight-drm.log
/tmp/moonlight-pair.log
/tmp/moonlight_start.log
```

## Device Debug Commands

Check Moonlight and miniapp processes:

```bash
ps | grep -E 'miniapp|moonlight' | grep -v grep
```

Check screen state:

```bash
hal-screen state
```

Keep screen awake manually:

```bash
hal-screen keep
```

Check Wi-Fi power state:

```bash
iwconfig wlan0
```

Check USB OTG mode:

```bash
cat /sys/devices/platform/ff740000.usb2-phy/otg_mode
```

Switch USB mode manually if needed:

```bash
echo host > /sys/devices/platform/ff740000.usb2-phy/otg_mode
echo otg > /sys/devices/platform/ff740000.usb2-phy/otg_mode
```

Check DRM state:

```bash
cat /sys/kernel/debug/dri/0/state
```

## Troubleshooting Notes

- If the stream page becomes black and does not recover, check `moonlight-drm.log`. The frame page should return to settings if Moonlight exits.
- Heavy `Network dropped ... frames` logs usually indicate Wi-Fi quality, MTU, bitrate, or host-side congestion. The default profile uses `-remote yes -packetsize 1024` to reduce fragmentation risk.
- If the PS4 controller disconnects, check kernel logs for USB host reset and Type-C events:

```bash
dmesg | tail -n 200
```

- If input mode does not see the controller, make sure the device is in USB host mode and the controller appears under `/proc/bus/input/devices`.
- If an installed package changes from slot `a` to `b`, update direct debug paths accordingly.

## Generated and Local Files

These are build or local test outputs and should not be treated as source of truth:

- `.falcon_`
- `.falcon_tmp`
- `8001780158276510.1_0_0.amr`
- device screenshots
- local logs

The source of truth for native runtime files is the checked-in source plus the current build artifacts copied into `assets/`, `libs/`, and `build-rk3562-from-pve/`.
