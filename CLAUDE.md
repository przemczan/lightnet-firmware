# CLAUDE.md

Guidance for Claude Code working in this repository.

---

## What this project is

Lightnet is embedded firmware for a tree network of addressable-LED panels. A single ESP8266/ESP32 **controller** discovers and drives up to 100 **panels** (ATmega328) over I²C. The controller exposes WiFi APIs; panels run animations locally after a single setup packet.

Two distinct binaries are compiled from one source tree. `LIGHTNET_TARGET_CONTROLLER` (set in `platformio.ini`) selects the target; the preprocessor eliminates the unused half entirely.

---

## Docs

| Document | Contents |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | Physical topology, library structure, I²C protocol, animation framework internals, discovery sequence, controller boot |
| [`docs/firmware.md`](docs/firmware.md) | PlatformIO environments, pin assignments, panel OTA (twiboot), serial upload, controller ArduinoOTA, debugging |
| [`docs/api.md`](docs/api.md) | WebSocket binary protocol (WebsocketApi) + all HTTP endpoints, request/response format |
| [`docs/animations.md`](docs/animations.md) | Scenes, layers, animation types, palettes, color references, HTTP API usage, examples |
| [`docs/testing.md`](docs/testing.md) | Native host-side unit tests, what's covered, how to add new tests, MinGW setup |

---

## Build quick-reference

```bash
pio run -e controller_wemos              # build controller (Wemos D1 Mini)
pio run -e controller_wemos -t upload    # build + upload via USB
pio run -e panel_atmega328pb -t upload    # build + upload panel via USBasp
pio run -e controller_wemos -t upload --upload-port lightnet-XXXX.local  # OTA
pio device monitor -e controller_wemos   # serial monitor (57600 baud)
```

Environments: `controller_esp8266` / `controller_wemos` / `controller_esp32` for the controller; `panel_nanoatmega328` / `panel_atmega328pb` / `panel_atmega328p` for panels. See [`docs/firmware.md`](docs/firmware.md#1-build-environments) for full details.

## Tests

Native host-side unit tests cover the pure C++ utilities (no Arduino, no hardware). Run via PlatformIO:

```bash
pio test -e native                       # all suites
pio test -e native -f test_simplejson    # single suite
```

On Windows, MinGW GCC must be on `PATH` (typically `C:\msys64\mingw64\bin`).

Current suites: `test_simplejson`, `test_http_url`, `test_palette_parser`. When fixing a bug in a pure-logic module, add a regression test under `test/test_*/test_main.cpp`. See [`docs/testing.md`](docs/testing.md) for what's testable natively vs. what needs a device.

---


## API changes

Whenever you add, remove, or rename an HTTP endpoint, update **all** of the following:

- `openapi.json` — path keys and any referenced URLs
- `docs/api.md` — endpoint table
- `docs/architecture.md` — server route summary table
- `docs/animations/api.md` — if the endpoint is appearance/animation-related
- Any inline doc comments in source that reference the path (e.g. `AppearanceStore.hpp`)
- `lightnet-mobile` client (`LightnetHttpClient.kt`) if the endpoint is consumed there

---

## Key facts for coding

- **Single entry point**: `src/main.cpp` — includes `controller/main.h` or `panel/main.hpp` based on `LIGHTNET_TARGET_CONTROLLER`.
- **I²C protocol version**: v4 (`VERSION` constant in `Common/Protocol.hpp`). Changing the protocol **requires flashing both controller and all panels together**.
- **`animScheduler->tick(millis())`** must be called in the main loop `case 1`.
- **SPIFFS** is mounted in `case 0` before the WiFi captive portal starts, so `AppearanceStore` can read `/config/appearance.json`.

- **`BOOTLOADER_ENTRY_TOKEN = 0xB0`** — both sides of `PACKET_ENTER_BOOTLOADER` must agree on this value. Do not send `CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER` (bytes `0x01 0x00`) to the twiboot fork — it WDT-resets the panel.
- **`busIsDisabled` in `LightnetPinger` is static (shared)** — set while any ping pulse is being driven so all pingers drop ISR samples during that window, preventing self-detection.
- **Debug macros** (`PRINTLN`, `PRINTKV`, `PRINTF`) are no-ops when `DEBUG=0`. Serial baud is 57600 everywhere.
