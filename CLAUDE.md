# CLAUDE.md

Guidance for Claude Code working in this repository.

---

## What this project is

Lightnet is embedded firmware for a tree network of addressable-LED panels. A single ESP8266/ESP32 **controller** discovers and drives up to 100 **panels** (ATmega328/ATmega88) over I²C. The controller exposes WiFi APIs; panels run animations locally after a single setup packet.

Two distinct binaries are compiled from one source tree. `LIGHTNET_TARGET_CONTROLLER` (set in `platformio.ini`) selects the target; the preprocessor eliminates the unused half entirely.

---

## Docs

| Document | Contents |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | Physical topology, library structure, I²C protocol, animation framework internals, discovery sequence, controller boot |
| [`docs/firmware.md`](docs/firmware.md) | PlatformIO environments, pin assignments, panel OTA (twiboot), serial upload, controller ArduinoOTA, debugging |
| [`docs/api.md`](docs/api.md) | WebSocket binary protocol (MessageApi) + all HTTP endpoints, request/response format |
| [`docs/animations.md`](docs/animations.md) | Scenes, layers, animation types, palettes, color references, HTTP API usage, examples |
| [`animation-system-plan.md`](animation-system-plan.md) | Full design document for the scenes branch — architectural decisions, struct layouts, verification checklist |

---

## Build quick-reference

```bash
pio run -e initializer_wemos              # build controller (Wemos D1 Mini)
pio run -e initializer_wemos -t upload    # build + upload via USB
pio run -e panel_atmega328pb -t upload    # build + upload panel via USBasp
pio run -e initializer_wemos -t upload --upload-port lightnet-XXXX.local  # OTA
pio device monitor -e initializer_wemos   # serial monitor (57600 baud)
```

Environments: `initializer_esp8266` / `initializer_wemos` / `initializer_esp32` for the controller; `panel_nanoatmega328` / `panel_atmega328pb` / `panel_atmega328p` / `panel_atmega88p` for panels. See [`docs/firmware.md`](docs/firmware.md#1-build-environments) for full details.

There are no automated tests.

---

## Active branches

| Branch | State |
|---|---|
| `master` | Stable — animation framework v3, no HTTP animation API |
| `scenes` | In progress — Protocol v4, palette/brightness/base-colors, appearance HTTP API, scene player |

Code on the `scenes` branch is marked *(scenes)* throughout the docs.

---

## Key facts for coding

- **Single entry point**: `src/main.cpp` — includes `controller/main.h` or `panel/main.hpp` based on `LIGHTNET_TARGET_CONTROLLER`.
- **I²C protocol version**: v3 on master, v4 on scenes (`VERSION` constant in `Common/Protocol.hpp`). Changing the protocol **requires flashing both controller and all panels together**.
- **`animScheduler->tick(millis())`** must be called in the main loop `case 1` — it was accidentally missing on master and was added on the scenes branch.
- **SPIFFS** is mounted inside `AppServer` during `setupWiFi()` on master. On `scenes`, it is hoisted to `case 0` so `AppearanceStore` can read `/config/appearance.json` before the WiFi captive portal can block.
- **`USE_LIGHT_WS2812`** build flag switches `RGBController` to the `light_ws2812` path (no FastLED dependency). Only set for `panel_atmega88p`.
- **`BOOTLOADER_ENTRY_TOKEN = 0xB0`** — both sides of `PACKET_ENTER_BOOTLOADER` must agree on this value. Do not send `CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER` (bytes `0x01 0x00`) to the twiboot fork — it WDT-resets the panel.
- **`busIsDisabled` in `LightnetPinger` is static (shared)** — set while any ping pulse is being driven so all pingers drop ISR samples during that window, preventing self-detection.
- **Debug macros** (`PRINTLN`, `PRINTKV`, `PRINTF`) are no-ops when `DEBUG=0`. Serial baud is 57600 everywhere.
