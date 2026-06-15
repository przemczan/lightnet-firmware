# LightnetCore

**Portable. No Arduino/FastLED. Compiles on a plain host C++ compiler.**

The single source of truth for animation logic, shared by:

- **Panel** (ATmega328) — `LightnetPanel` drives the LED from `currentColor()`
- **Controller** (ESP, incl. `SIM_MODE`) — `SimPanel` runs one `AnimationPlayer` per virtual panel,
  and the controller-side scene engine (`Controller`) drives the bus
- **Native tests** — `test/test_panel_anim`, `test/test_compositor`, `test/test_scene_player`, etc.
- **Mobile** (Kotlin Multiplatform) — via two C ABIs (NDK on Android, cinterop on iOS)

## Rules

- **Nothing here may `#include <Arduino.h>`, `<FastLED.h>`, or any hardware/RTOS header.** The native
  build (`pio test -e native`) enforces this — a stray Arduino include breaks it immediately.
- Time is a **parameter**: players/engines take `now` explicitly; they never call `millis()`.
- Output is **pulled, not pushed**: the player computes `currentColor()`; the engine emits packets
  via `IPacketSink`. Neither owns hardware.

## Modules

| Folder | Contents |
|---|---|
| `Common/` | Shared protocol/animation types used by both `Panel/` and `Controller/`: `ProtocolMeta`, `ProtocolTypes`, `LightnetConfig`, `Palette`, `ColorRef`, `AnimationTypes`, `UserColors`, `SpscByteQueue`. |
| `Panel/` | `AnimationPlayer` (panel-side layer compositor) + `ColorCompose`. Used by the AVR panel, `SimPanel`, and the `panel_core` C ABI. |
| `Controller/` | The controller-side scene engine: `ScenePlayer`, `AnimationScheduler`, `AnimationRunner`, `SceneParser`, topology/selector/field primitives (`TopologyIndex`, `PanelSelector`, `PanelField`, `PanelGeometry`, `TagResolver`). Decoupled from hardware via `IPacketSink` / `IPaletteResolver` / `ITopologyProvider` / `ITagResolver`. The AVR panel never pulls this in. |
| `CApi/` | C ABIs (`panel_core_c.h/.cpp`, `controller_core_c.h/.cpp`) + CMake build exposing `Panel/` and `Controller/` to non-PlatformIO consumers (mobile NDK/cinterop, host smoke tests). Excluded from firmware builds via `library.json` `srcFilter`. |

`Core/library.json` builds `Common/` + `Panel/` + `Controller/` as one PlatformIO library
(`CApi` excluded); unused subtrees (e.g. `Controller` in the panel build) are dropped by the
linker's `--gc-sections`.
