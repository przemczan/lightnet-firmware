# Firmware: Building, Flashing & Hardware

Reference for PlatformIO environments, pin assignments, panel OTA, serial upload, and controller self-update.

---

## Table of Contents

1. [Build Environments](#1-build-environments)
2. [Common Commands](#2-common-commands)
3. [Pin Assignments](#3-pin-assignments)
4. [Panel OTA (twiboot)](#4-panel-ota-twiboot)
5. [Serial Firmware Upload (PC → Controller)](#5-serial-firmware-upload-pc--controller)
6. [Controller Self-Update (ArduinoOTA)](#6-controller-self-update-arduinoota)
7. [Debugging](#7-debugging)

---

## 1. Build Environments

All environments are defined in `platformio.ini`.

### Controller environments

| Environment | Board | Notes |
|---|---|---|
| `initializer_esp8266` | ESP-12E (ESP8266) | Upload via USB, 230400 baud |
| `initializer_wemos` | Wemos D1 Mini Pro (ESP8266) | Upload via USB, 460800 baud |
| `initializer_esp32` | ESP32 DevKit | Upload via USB, 460800 baud |

All controller envs use `lib_ldf_mode = chain+`, FastLED, ESPAsyncWebServer, and ESPAsyncWiFiManager.

### Panel environments

| Environment | Board | Uploader | Bootloader | Notes |
|---|---|---|---|---|
| `panel_nanoatmega328` | Arduino Nano (ATmega328) | Arduino serial | Arduino | Via controller serial bridge |
| `panel_atmega328pb` | ATmega328PB | USBasp | twiboot at `0x7C00` | `-D` flag preserves bootloader on erase |
| `panel_atmega328p` | ATmega328P | USBasp | twiboot at `0x7C00` | Same binary as 328PB |
| `panel_atmega88p` | ATmega88P/PA | USBasp | None | *scenes branch only*; uses `light_ws2812` + `DEBUG=0`; 8 KB flash |

#### ATmega88P build details

Because FastLED 3.3.2 has no pin-map for ATmega88P and the firmware (Arduino + FastLED + animation stack) doesn't fit in 8 KB anyway, this environment:
- Uses `light_ws2812` for the WS2812 LED driver (no pin-map dependency, ~200 B code)
- Sets `USE_LIGHT_WS2812` compile flag — `RGBController` switches to `ws2812_setleds()` path
- Disables all debug output (`DEBUG=0`)
- Applies maximum size optimisation: `-Os -ffunction-sections -fdata-sections -Wl,--gc-sections,--relax`

#### Panel fuses (ATmega328PB / 328P)

```
lfuse = 0xF7  — 16 MHz external full-swing crystal
hfuse = 0xD0  — SPIEN, EESAVE, BOOTRST (boot from twiboot)
efuse = 0xFC  — BOD 4.3 V
```

Flash fuses + bootloader:
```bash
pio run -e atmega328p_bootloader -t fuses
pio run -e atmega328p_bootloader -t upload
```

#### Panel fuses (ATmega88P)

```
lfuse = 0xF7  — 16 MHz external full-swing crystal
hfuse = 0xD7  — SPIEN, EESAVE, BOOTRST=1 (app start, no bootloader)
efuse = 0xFC  — BOD 4.3 V
```

---

## 2. Common Commands

```bash
# Build a specific environment
pio run -e initializer_esp32

# Build and upload
pio run -e initializer_wemos --target upload

# Upload using mDNS hostname (controller OTA)
pio run -e initializer_wemos --target upload --upload-port lightnet-XXXX.local

# Serial monitor (57600 baud)
pio device monitor -e initializer_wemos

# Flash fuses only (panel bootloader environments)
pio run -e atmega328p_bootloader -t fuses

# Build all environments
pio run
```

Post-build script (`tools/generate_bin.py`) automatically produces both `.hex` and `.bin` for all panel environments. The `.bin` is used for OTA panel flashing via the controller.

---

## 3. Pin Assignments

### Controller

| Signal | ESP8266 | ESP32 |
|---|---|---|
| Edge ping out | GPIO 13 | GPIO 12 |
| Edge interrupt in | GPIO 12 | GPIO 13 |
| Status LED (active low) | GPIO 2 | GPIO 2 |
| I²C SDA | GPIO 4 | GPIO 4 |
| I²C SCL | GPIO 5 | GPIO 5 |
| Panel power enable | GPIO 14 | GPIO 21 |

### Panel (ATmega)

| Signal | Pin | Port |
|---|---|---|
| Edge 0 | Pin 9 | PB1 / PCINT1 |
| Edge 1 | Pin 10 | PB2 / PCINT2 |
| Edge 2 | Pin 11 | PB3 / PCINT3 |
| LED data | PD5 | — |
| I²C SDA | PC4 | — |
| I²C SCL | PC5 | — |

---

## 4. Panel OTA (twiboot)

Panels use a custom fork of `orempel/twiboot` stored in `firmware/twiboot_for_arduino/`. The upstream repo was tried but produced SRAM-initialisation issues on software jump entry and WDT bootloops on hardware reset — the fork addresses both.

### Bootloader layout

- Lives in the **4 KB boot section** at `0x7C00` (ATmega328PB/P)
- TWI address: `0x29` (compiled in via `-DTWI_ADDRESS=0x29`)
- Stays in bootloader only when EEPROM byte 510 contains `0xB007` (cleared after first read)

### Entry sequence (controller side)

1. `PanelsController::enterBootloader(address)` sends `PACKET_ENTER_BOOTLOADER` (type 201, token `0xB0`)
2. Panel `BootloaderBridge::prepareAndReset()`:
   - Writes `0xB007` to EEPROM[510]
   - Disables TWI and PCINT interrupts
   - Zeroes SRAM `0x0100–0x04FF`
   - Software-jumps to `0x7C00`
3. Bootloader reads EEPROM[510]: if `0xB007` → stays resident (clears magic); otherwise → jumps to app immediately

### Writing firmware (controller side)

`TwibootClient` uses raw `Wire` (bypasses `LNBus`):

```cpp
twibootClient->connect(panelAddress);   // sends CMD_WAIT (0x00) to verify presence
twibootClient->writePage(addr, data, 128);  // 2 × 64-byte chunks
twibootClient->startApp();
```

> **Do not** send `[0x01, 0x00]` (`CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER`). The fork interprets it as a re-entry trigger and WDT-resets the panel.

`PanelFlasher` reads `/panel_fw.bin` from SPIFFS 128 bytes at a time — the full binary is never buffered in RAM.

### HTTP upload flow

```
POST /api/firmware/panels  [raw .bin body]
  → streams to /panel_fw.bin on SPIFFS
  → triggers PanelFlasher::startFlashing()
  → non-blocking flash loop in main() case 1

GET /api/firmware/status
  → {"state":"flashing","panel":3,"total":12,"progress":45}
```

After all panels are flashed the **controller must be restarted** — `PanelsInitializer` needs to re-run discovery so panels get new I²C addresses.

---

## 5. Serial Firmware Upload (PC → Controller)

`SerialFirmwareReceiver` listens on the existing 57600-baud Serial port. This allows updating panels without WiFi.

### Frame format

```
┌──────────────────┬─────────────┬───────────────┬──────────────┐
│ Magic (4 B)      │ Size (4 B)  │ Data (N B)    │ CRC-16 (2 B) │
│ 'L' 'N' 'F' 'W' │ little-end. │ firmware .bin │ little-end.  │
└──────────────────┴─────────────┴───────────────┴──────────────┘
```

Controller replies:
- `READY\n` — after the header is received
- `OK\n` — after CRC validates and binary is saved to SPIFFS
- `ERR:<message>\n` — on any failure

### Usage

```bash
pip install pyserial
python tools/flash_panels_serial.py <port> <firmware.bin>
```

Once the binary is saved the controller starts flashing panels exactly as with the HTTP upload path.

---

## 6. Controller Self-Update (ArduinoOTA)

`ArduinoOTA` is initialised after WiFi connects. Standard ports: 8266 (ESP8266), 3232 (ESP32). No password — intended for a trusted local network.

```bash
# Upload directly over WiFi
pio run -e initializer_esp32 --target upload --upload-port lightnet-XXXX.local
```

The hostname matches the mDNS name (`lightnet-<chipid>`). The `.local` suffix requires mDNS to be working on the network (standard on most OS setups).

---

## 7. Debugging

`DEBUG=1` is set globally in `common_env_data.build_flags`. Remove or override with `DEBUG=0` in environments where code size matters (e.g. `panel_atmega88p`).

### Debug macros (`Utils/Debug.hpp`)

| Macro | Expands to (DEBUG=1) | Use |
|---|---|---|
| `PRINTLN(s)` | `Serial.println(s)` | String message |
| `PRINTKV(k, v)` | `Serial.print(k); Serial.println(v)` | Key-value pair |
| `PRINTF(fmt, ...)` | `Serial.printf(fmt, ...)` | Formatted output |

All macros compile to **no-ops** when `DEBUG=0`.

Serial baud rate: **57600** on all environments.

### Watching panel I²C traffic

Set `RGBC_DEBUG 1` in `Panel/RGBController.cpp` for per-frame LED output logging (ATmega only; adds ~200 bytes flash).

### Common issues

| Symptom | Likely cause |
|---|---|
| Panel doesn't respond after `ENTER_BOOTLOADER` | EEPROM byte 510 was not written — check `BootloaderBridge::prepareAndReset()` |
| Panel WDT-resets in bootloader loop | `CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER` was sent — see [twiboot note](#4-panel-ota-twiboot) |
| All panels fail to start animations | `animScheduler->tick(millis())` missing from main loop `case 1` |
| Palette/appearance not restored on reboot | SPIFFS not mounted before `AppearanceStore::loadAndApply()` |
