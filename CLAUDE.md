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
  LightnetPanelEdge State machine per edge: IDLE→WELCOME_SENT→BOOTING→READY
                    updateEdgeState(state, ts) — ISR path, forwards to pinger ring buffer
                    processEdgeState()         — main-loop path, drains pinger ring buffer
  LightnetPinger    GPIO ping pulses for edge detection; two types distinguished by pulse width:
                    HANDSHAKE (500 µs, welcome/ACK) and DONE (2000 µs, subtree complete).
                    Each pinger owns an 8-entry ring buffer: updateState() enqueues from the
                    ISR, processState() drains and decodes transitions in the main loop.
                    busIsDisabled is static (shared) — set during ping() so all pingers drop
                    ISR samples while any pulse is being driven, preventing self-detection.
  Protocol          Packet definitions, CRC validation, protocol version (v3)

Controller/     Controller-only
  PanelsInitializer    Discovery: assigns panel indices, builds edge graph
  PanelsController     Commands to individual panels over I²C (color/brightness/reset)
                       enterBootloader(address) — sends PACKET_ENTER_BOOTLOADER
  Panel / Edge         Data model for discovered topology
  TwibootClient        twiboot host protocol over raw Wire (bypasses LNBus)
                       connect(), programFlash(), verifyFlash(), startApp()
  PanelFlasher         Non-blocking OTA orchestrator; state machine driven by main loop
                       ENTER_BL → WAIT_BL → FLASHING → VERIFY → NEXT_PANEL
  FirmwareUpdateServer HTTP endpoints: POST /api/firmware/panels, GET /api/firmware/status
  SerialFirmwareReceiver  Receives firmware binary over USB serial (LNFW framing + CRC-16)

Panel/          Panel-only (ATmega)
  LightnetPanel    Main panel state machine; handles I²C commands, edge registration
  RGBController    FastLED wrapper for a single WS2812-style LED (pin PD5)
  BootloaderBridge EEPROM layout + prepareAndReset() for twiboot entry

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
- `BOOTLOADER_ENTRY_TOKEN = 0xB0` — shared constant used by both sides of PACKET_ENTER_BOOTLOADER
- Packet types: init pull, register edge, turn on/off, set color, set brightness, fetch state, reset, animations, enter bootloader

**MessageApi** (`MessageApi/MessageApi.hpp`) — Binary WebSocket protocol between external app and controller.
- Separate header+payload CRC scheme with a nonce
- Commands: TOGGLE, SET_BRIGHTNESS, SET_COLOR, GET_PANELS_STATES, GET_EDGES_LIST
- Internal `Message` struct wraps payloads with a `clientId` for WebSocket routing

### Animation Framework

The animation system supports **panel-local animations** (computed on ATmega328) and **controller-computed animations** (computed on ESP32/ESP8266), synchronized via I²C General Call for ±2.5 µs jitter-free timing.

#### Synchronization via General Call

General Call (I²C address 0x00) broadcasts simultaneously to all panels:
- **PACKET_ANIMATION_START**: Synchronously launches queued animations on matching `group_id`
- **PACKET_ANIMATION_UPDATE_PARAMS**: Sends reactive triggers (music beats) to all panels in a group
- Packets sent **twice** (300 µs apart) with seq_id duplicate guard to ensure reliable delivery without ACK
- Requires: ATmega28 `TWAR |= 0x01` to enable GCIE bit for General Call reception

#### Protocol Extensions (5 new packet types)

| Packet Type | Dir | Size | Use |
|---|---|---|---|
| PACKET_ANIMATION_PREPARE | C→P | 21 B | Unicast: buffer animation parameters, arm for group start |
| PACKET_ANIMATION_START | General Call | 7 B | Broadcast: fire all animations with matching group_id |
| PACKET_ANIMATION_CONTROL | C→P | 6 B | Unicast: STOP, PAUSE, RESUME, CLEAR_QUEUE commands |
| PACKET_ANIMATION_UPDATE_PARAMS | General Call | 10 B | Broadcast: update reactive animation parameters (triggers, speed) |
| PACKET_FETCH_ANIM_STATE | C→P | — | Unicast: query animation status from panel (response: 11 B) |

#### Panel-Local Animation Types (8 types, zero per-frame I²C traffic)

| Type | Computation | Use Case |
|---|---|---|
| SOLID | None | Hold color + brightness |
| FADE | Linear brightness interpolation | Simple fade in/out |
| TRANSITION | Linear RGB + brightness interpolation | Cross-fade between colors |
| BREATHE | Parabolic brightness envelope | Pulsing/breathing effect |
| PULSE | 3-phase (rise/hold/fall) brightness | Impact/beat flash |
| BLINK | Binary on/off at period | Flashing indicator |
| HUE_CYCLE | 6-step integer HSV→RGB rotation | Rainbow color cycle |
| STROBE | Binary flash at frequency (Hz) | High-speed strobe |
| REACTIVE | Decay model triggered by General Call | Music beat response (zero inter-beat I²C) |

#### Animation State (22 bytes, packed struct)

Each panel queues up to 4 animations (`AnimationState queue[4]` = 88 bytes SRAM):
```cpp
struct AnimationState {
    uint8_t  animType;       // enum 0-8
    uint8_t  group_id;       // 1-254 (0 reserved)
    uint8_t  flags;          // LOOP | PINGPONG | EASING
    uint8_t  transitionMs;   // crossfade duration 0-255ms
    uint16_t durationMs;     // animation duration (0=infinite)
    uint16_t startMs;        // millis() snapshot at start
    ColorRGB colorFrom, colorTo;  // 3 bytes each
    uint8_t  brightnessFrom, brightnessTo;
    uint8_t  param1, param2; // type-specific parameters
};  // 22 bytes with __attribute__((__packed__))
```

#### Animation Groups

Panels can run multiple **independent** animations simultaneously on non-overlapping `group_id` values (1-254):
```
PREPARE(group=1, BREATHE) → panels A,B,C
PREPARE(group=2, BLINK)  → panels D,E
GENERAL CALL START(seq=1, group=1)  ← only A,B,C start
GENERAL CALL START(seq=2, group=2)  ← only D,E start
```

#### Panel-Side Implementation

**AnimationPlayer** (`Panel/AnimationPlayer.hpp/.cpp`):
- Manages 4-deep animation queue per panel
- **tick()** called every main loop (internally gated at 16 ms = 60 fps)
- Responds to PREPARE, START (General Call), CONTROL, UPDATE_PARAMS packets
- Computes frame locally with integer math only (no FPU on ATmega)
- Progress interpolation uses q8 fixed-point: `progress_q8 = (elapsed << 8) / duration`
- Reactive decay: `reactiveLevel -= (decayRate * elapsedMs / 1000)` per tick

#### Controller-Side Implementation

**AnimationScheduler** (`Controller/AnimationScheduler.hpp/.cpp`):
- Manages active controller-computed animation runners
- Maintains per-panel state tracking for in-memory status queries (no I²C polling needed)
- `playOnPanels()`: sends PREPARE unicast sequence, then General Call START twice
- `triggerGroup()`: sends General Call UPDATE_PARAMS for reactive triggers
- Frame gating: 16.67 ms (60 fps), processes only active runners

**AnimationRunner** base class with subtypes:
- **WaveRunner**: 3-wide brightness envelope across topology-ordered panels
- **RippleRunner**: Distance-based phase expansion from origin panel
- **ChaseRunner**: Single lit panel traversing edge graph
- Delta optimization: only send I²C when brightness changes (max ~3 panels per frame = 600 µs)

#### Bandwidth Budget

| Scenario | I²C / Frame |
|---|---|
| 30 panels, all BREATHE (panel-local) | **0 µs** during animation |
| 30 panels, REACTIVE, 120 BPM music | **~140 µs per beat** (between beats: 0 µs) |
| 3-wide wave, controller-computed | **≤ 600 µs** (delta optimized) |
| Setup: PREPARE × 30 panels + 2 General Calls | **~6.2 ms** one-time |
| Full status poll (30 panels) | **~11.7 ms** rare, on-demand only |

### Discovery Sequence (Controller Boot)

Ping handshake per edge (3 pulses total):
1. Parent sends **HANDSHAKE** (500 µs) — welcome ping
2. Child replies **HANDSHAKE** (500 µs) — ACK; immediately enters `REGISTER_STATE_BEGIN` and calls `LNBus.begin(0x78)`
3. Child sends **DONE** (2000 µs) — signals its entire subtree has finished registering

Panel PCINT ISR calls `LNPanel.updateEdgesStates((PINB >> 1) & 0x07, TCNT1)` directly — bit i of the first argument is the level of edge i (PB1/PB2/PB3 → edges 0/1/2). Each `LightnetPinger` owns its own 8-entry ring buffer; the ISR enqueues samples and `LightnetPanel::run()` drains them via `processEdgeStates()` at the top of every iteration. Timer1 runs free at prescaler 8 (0.5 µs/tick) for pulse-duration validation. Controller uses `micros()*2` for the same unit scale.

Discovery flow:
1. `PanelsInitializer::start()` — sets up I²C as master, attaches CHANGE interrupt on edge pin
2. `PanelsInitializer::boot()` called every loop — drives `LightnetPanelEdge` state machine, pulls 0x78 every 25 ms while `STATE_BOOTING`
3. Panel detects HANDSHAKE via pinger ring buffer → replies HANDSHAKE → enters `STATE_REGISTER_EDGES` → `LNBus.begin(0x78)`
4. Controller pull delivers `PACKET_INITIALIZATION_PULL`; panel responds with `PacketRegisterEdge` (panel index + edge index)
5. Panel repeats steps 3–4 for each of its non-parent edges, then sends DONE to its parent
6. Controller detects DONE on its edge → `isReady()` returns true (5 s boot timeout)

### Controller WiFi / API Startup

After discovery completes (`isReady() == true`):
1. Sends `PacketPanelConfiguration` to all panels (gamma correction, color temp)
2. Runs self-test fade sequence
3. Initialises WiFi via `AsyncWiFiManager` (auto-connect or config portal named "Lightnet-Controller")
4. Starts `AsyncWebServer` on port 80, `MessageServer` (WebSocket), `AppServer` (SPIFFS static)
5. Starts `FirmwareUpdateServer` (HTTP firmware upload endpoints)
6. Starts `ArduinoOTA` for controller self-update over WiFi
7. MDNS as `lightnet-<chipid>.local` with service `_lightnet._tcp`
8. Main loop: `ArduinoOTA.handle()` + `SerialFirmwareReceiver::run()` + `PanelFlasher::run()` + `messageHandler->handleIncommingMessages()` + `MDNS.update()` (ESP8266 only)

### OTA Firmware Updates

#### Panel OTA flow

Panels run [twiboot](https://github.com/orempel/twiboot) in the 4 KB boot section at `0x7000`
(fuses: `lfuse=0xF7` for 16 MHz external full-swing crystal or `0xE2` for internal 8 MHz RC;
`hfuse=0xD0` — BOOTSZ=00, boot section at 0x7000, BOOTRST, EESAVE;
`efuse=0xFC` — BOD 4.3 V). Two EEPROM bytes coordinate the handoff:

| EEPROM byte | Content |
|---|---|
| `[0]` | Panel I²C address (written before OTA reset, read by twiboot as TWI slave address) |
| `[1]` | `0x42` = enter bootloader; `0xFF` = start app |

twiboot source lives in `firmware/twiboot/` (not the upstream repo). Key build settings:
- `F_CPU=16000000` (matches 16 MHz crystal; use 8000000 with internal 8 MHz oscillator)
- `BOOTLOADER_ADDRESS=0x7C00` (1 KB section, matches `hfuse=0xD4` BOOTSZ=10)
- `TIMEOUT_MS=200` — must exit quickly so the panel app starts before the controller's
  discovery window expires (~400 ms after panel power-on)
- The same hex (`firmware/twiboot/.pio/build/atmega328p/firmware.hex`) works on both
  ATmega328P and ATmega328PB
- Flash via `cd firmware/twiboot && pio run -e atmega328p --target upload`

`PACKET_ENTER_BOOTLOADER` (value 201, token `0xB0`) triggers `BootloaderBridge::prepareAndReset()`:
writes the two EEPROM bytes then resets via watchdog. Token mismatch → silently ignored.

`TwibootClient` (controller side) uses raw `Wire` — **bypasses LNBus entirely** since twiboot
does not speak the Lightnet Protocol. Command bytes (`CMD_WRITE_FLASH=0x02`, etc.) must match
the compiled twiboot binary; they are documented in `TwibootClient.hpp`.

`PanelFlasher` reads firmware from SPIFFS `/panel_fw.bin` one 128-byte page at a time
(never buffers the whole image in RAM). After all panels are flashed, the controller must
be restarted so `PanelsInitializer` can re-run discovery.

Panel firmware builds produce both `.hex` and `.bin` automatically (via `tools/generate_bin.py`
post-build script). The same `panel_atmega328pb` binary runs on both 328P and 328PB panels.
Panel USBasp uploads use `upload_flags = -D` (no chip erase) to preserve twiboot at `0x7C00`.

#### Serial firmware upload framing (PC → controller)

`SerialFirmwareReceiver` listens on the existing 57600-baud Serial port:

```
[4B magic 'L','N','F','W'] [4B size LE] [size bytes data] [2B CRC-16 LE]
```

Controller responds `READY\n` after the header, then `OK\n` or `ERR:…\n` after the CRC check.
Use `tools/flash_panels_serial.py` as the PC-side sender (`pip install pyserial` required).

#### Controller self-update (ArduinoOTA)

ArduinoOTA runs on the standard port (3232 for ESP32, 8266 for ESP8266). Hostname matches
the MDNS name (`lightnet-<chipid>`). No password — intended for a trusted local network.

```bash
pio run -e initializer_esp32 --target upload --upload-port lightnet-XXXX.local
```

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
