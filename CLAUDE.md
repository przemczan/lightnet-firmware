# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This project uses **PlatformIO**. All build, upload, and monitor commands are issued via the `pio` CLI or the PlatformIO IDE extension (recommended VS Code extension: `platformio.platformio-ide`).

### Environments

| Environment | Target | Board |
|---|---|---|
| `initializer_esp8266` | Controller | ESP-12E (ESP8266) |
| `initializer_wemos` | Controller | Wemos D1 Mini Pro (ESP8266) |
| `initializer_esp32` | Controller | ESP32 DevKit |
| `panel_nanoatmega328` | Panel | Arduino Nano (ATmega328) |
| `panel_atmega328pb` | Panel | ATmega328PB (USBasp) |
| `panel_atmega328p` | Panel | ATmega328P (USBasp) |

### Common Commands

```bash
# Build a specific environment
pio run -e initializer_esp32

# Build and upload
pio run -e initializer_wemos --target upload

# Serial monitor (57600 baud on most envs)
pio device monitor -e initializer_wemos

# Build all environments
pio run
```

There are no automated tests in this project.

## Architecture Overview

The firmware targets **two distinct device types** compiled from a single source tree, selected at build time by the `LIGHTNET_TARGET_CONTROLLER` preprocessor flag (set via `build_flags` in `platformio.ini`).

- `src/main.cpp` — single entry point that includes either `controller/main.h` or `panel/main.hpp`
- Controller build: `LIGHTNET_TARGET_CONTROLLER` defined → ESP8266/ESP32 target
- Panel build: flag absent → ATmega328 target

### Physical Topology

Panels form a **tree structure** rooted at the controller. Panels connect to each other via physical edges (one panel per edge). The controller discovers the network by sequentially pinging each panel's edges via GPIO, triggering PCINT interrupts on the receiving AVR. I²C (via `LightnetBus`) carries all structured packets after initial edge detection.

### Library Structure (`lib/Lightnet/`)

```
Common/         Shared between controller and panel
  LightnetBus       I²C wrapper (send/receive Protocol packets, IRQ callbacks)
  LightnetPanelEdge State machine per edge: IDLE→WELLCOME_SENT→BOOTING→READY
  LightnetPinger    GPIO ping pulse (100µs) for edge presence detection via PCINT
  Protocol          Packet definitions, CRC validation, protocol version (v3)

Controller/     Controller-only
  PanelsInitializer  Discovery: assigns panel indices, builds edge graph
  PanelsController   Commands to individual panels over I²C (color/brightness/reset)
  Panel / Edge       Data model for discovered topology

Panel/          Panel-only (ATmega)
  LightnetPanel  Main panel state machine; handles I²C commands, edge registration
  RGBController  FastLED wrapper for a single WS2812-style LED (pin PD5)

MessageApi/     WebSocket API (controller only, ESP)
  MessageServer  AsyncWebSocket on port 80; queues incoming binary frames
  MessageHandler Decodes MessageApi packets, dispatches to PanelsController
  MessageApi     Binary protocol structs (toggle, set color/brightness, state query)

AppServer/      Serves static web app from SPIFFS (controller only)
Utils/
  CircularQueue  FIFO byte buffer used for incoming I²C and WebSocket queues
  List           Simple linked list
  Crc            CRC-16 used in both Protocol and MessageApi headers
  Mem            memcpy/memset shims
  Macros         Convenience macros
  Debug          PRINT/PRINTLN macros gated on DEBUG flag
  Gamma          Gamma correction table for LED output
WebSockets/     Vendored WebSockets library
```

### Two Binary Protocols

**Protocol** (`Common/Protocol.hpp`) — I²C packets exchanged between controller and panels.
- Fixed binary structs with `__attribute__((__packed__))`
- Header CRC validated on every receive
- Version field (`VERSION = 3`) checked on every packet
- Packet types: init pull, register edge, turn on/off, set color, set brightness, fetch state, reset

**MessageApi** (`MessageApi/MessageApi.hpp`) — Binary WebSocket protocol between external app and controller.
- Separate header+payload CRC scheme with a nonce
- Commands: TOGGLE, SET_BRIGHTNESS, SET_COLOR, GET_PANELS_STATES, GET_EDGES_LIST
- Internal `Message` struct wraps payloads with a `clientId` for WebSocket routing

### Discovery Sequence (Controller Boot)

1. `PanelsInitializer::start()` — sets up I²C as master, configures interrupt pin
2. `PanelsInitializer::boot()` called every loop — sends pings to currently active edge pin
3. Panel wakes on PCINT interrupt → `LNPanel.updateEdgesStates()` → responds with welcome ping
4. Controller detects response → sends `PACKET_INITIALIZATION_PULL` with new panel index over I²C
5. Panel registers all its edges back via `PACKET_REGISTER_EDGE`
6. Controller iterates discovered edges and repeats for each child panel
7. `isReady()` returns true when discovery completes (timeout 5 s after last panel)

### Controller WiFi / API Startup

After discovery completes (`isReady() == true`):
1. Sends `PacketPanelConfiguration` to all panels (gamma correction, color temp)
2. Runs self-test fade sequence
3. Initialises WiFi via `AsyncWiFiManager` (auto-connect or config portal named "Lightnet-Controller")
4. Starts `AsyncWebServer` on port 80, `MessageServer` (WebSocket), `AppServer` (SPIFFS static)
5. MDNS as `lightnet-<chipid>.local` with service `_lightnet._tcp`
6. Main loop: `messageHandler->handleIncommingMessages()` + `MDNS.update()` (ESP8266 only)

### Pin Assignments

Controller pins differ per target:

| Signal | ESP8266 | ESP32 |
|---|---|---|
| Edge ping out | GPIO 13 | GPIO 12 |
| Edge interrupt in | GPIO 12 | GPIO 13 |
| Status LED | GPIO 2 | GPIO 2 |
| I²C SDA | GPIO 4 | GPIO 4 |
| I²C SCL | GPIO 5 | GPIO 5 |
| Panel power | GPIO 14 | GPIO 21 |

Panel (ATmega): edges on pins 9/10/11 (PB1/PB2/PB3, PCINT1/2/3); LED data on PD5.

### Debugging

`DEBUG=1` is set globally in `common_env_data.build_flags`. All debug output uses `PRINTLN`/`PRINTKV`/`PRINTF` macros from `Utils/Debug.hpp` — these compile to no-ops when `DEBUG=0`. Serial baud rate is 57600.
