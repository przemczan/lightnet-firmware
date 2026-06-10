# LightnetCore

**Portable. No Arduino/FastLED. Compiles on a plain host C++ compiler.**

The single source of truth for animation logic, shared by:

- **Panel** (ATmega328) — `LightnetPanel` drives the LED from `currentColor()`
- **Controller** (ESP, incl. `SIM_MODE`) — `SimPanel` runs one `AnimationPlayer` per virtual panel
- **Native tests** — `test/test_panel_anim`, `test/test_compositor`
- **Mobile** (Kotlin Multiplatform) — via a C ABI (NDK on Android, cinterop on iOS)

## Rules

- **Nothing here may `#include <Arduino.h>`, `<FastLED.h>`, or any hardware/RTOS header.** The native
  build (`pio test -e native`) enforces this — a stray Arduino include breaks it immediately.
- Time is a **parameter**: the player takes `uint16_t now` (16-bit ms, wraps ~65.5 s); it never calls
  `millis()`.
- Output is **pulled, not pushed**: the player computes `currentColor()`; the platform reads it and
  drives the LED. The player owns no output driver.

## Modules

| Folder | Contents |
|---|---|
| `Anim/` | `AnimationPlayer` (panel-side layer compositor) + its pure deps: `AnimationTypes`, `ColorCompose`, `ColorRef`, `Palette`, `LightnetConfig`, and `ProtocolTypes` (the host-compilable subset of the wire protocol). |
| `CApi/` | C ABI (`anim_core_c.h/.cpp`) + CMake build exposing `Anim/` to non-PlatformIO consumers (mobile NDK/cinterop, host tests). **Not** part of the PlatformIO library (`library.json` `srcDirs` is `["Anim"]` only) — firmware builds never see it. |

Future portable controller logic (scene/topology) moves here as additional modules.
