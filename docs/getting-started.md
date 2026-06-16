---
icon: material/rocket-launch-outline
---

# Build & Flash Reference

This page is the PlatformIO reference: every environment, the fuses, and the day-to-day commands. For a guided first-time walkthrough, see the hub's **[Get Started](../getting-started/index.md)** instead.

## Repository

```bash
git clone https://github.com/przemczan/lightnet-firmware.git
cd lightnet-firmware
```

The same source tree builds both controller and panel binaries — the active PlatformIO environment selects which one.

---

## Configuration

Before building, copy the example config files and edit them to match your hardware:

```bash
cp src/controller.config.hpp.example src/controller.config.hpp
cp src/panel.config.hpp.example       src/panel.config.hpp
```

Both files ship with sane defaults, so no changes are required to do a first build. The `*.config.hpp` files are gitignored — keep per-device settings there without touching the tracked `*.example` files.

=== "controller.config.hpp"

    Located at `src/controller.config.hpp`, included by `src/controller/config.hpp`.

    | Symbol | Default | Description |
    |---|---|---|
    | `DEMO_MODE` | `0` | Set to `1` to run the built-in light demo on startup |
    | `CONFIG_PORTAL_TIMEOUT` | `120` | Seconds the Wi-Fi captive portal stays open before timeout |
    | `SERVER_PORT` | `80` | HTTP server port |

    **Debug sub-switches** — all default to `1` when `DEBUG=1` is set in `platformio.ini`. Uncomment and set to `0` to silence a specific area:

    | Symbol | Area silenced |
    |---|---|
    | `DEBUG_API` | WebSocket / HTTP API logs |
    | `DEBUG_RGB_CTRL` | LED controller logs |
    | `DEBUG_LIGHTNET_BUS` | I²C bus logs |
    | `DEBUG_FLASHER` | OTA / panel flash logs |
    | `DEBUG_DISCOVERY` | Panel discovery / ping logs |
    | `DEBUG_INIT` | Startup / init logs |
    | `DEBUG_DEMO` | Demo logs |

    Pin assignments (`INITIALIZER_EDGE_PIN_NO`, `IIC_SDA_PIN`, etc.) have platform-specific defaults in `src/controller/config.hpp` and only need overriding for custom hardware.

=== "panel.config.hpp"

    Located at `src/panel.config.hpp`, included by `src/panel/config.hpp`.

    | Symbol | Default | Description |
    |---|---|---|
    | `NUMBER_OF_EDGES` | `3` | Physical edges on the panel (3–5) |
    | `EDGE_1_PIN` … `EDGE_5_PIN` | 9–13 | Arduino pin for each edge output |

    !!! note "6-edge panels"
        Six edges require a separate pin-change ISR — not yet supported.

    Panel builds have `DEBUG=0` by default. To enable, set `DEBUG=1` for your panel environment in `platformio.ini`, then uncomment `DEBUG_RGB_CTRL` in this file.

---

## PlatformIO environments

All environments are defined in `platformio.ini`.

=== "Controller"

    | Environment | Board | Notes |
    |---|---|---|
    | `controller_esp8266` | ESP-12E (ESP8266) | USB upload at 230400 baud |
    | `controller_wemos` | Wemos D1 Mini Pro (ESP8266) | USB upload at 460800 baud |
    | `controller_esp32` | ESP32 DevKit | USB upload at 460800 baud |

    All controller environments use `lib_ldf_mode = chain+`, FastLED, ESPAsyncWebServer, and ESPAsyncWiFiManager.

=== "Panel"

    | Environment | Board | Uploader | Bootloader | Notes |
    |---|---|---|---|---|
    | `panel_atmega328p_via_controller` | ATmega328P | Custom serial via controller | — | Upload `.bin` over the controller's 57600-baud serial port |
    | `panel_atmega328pb` | ATmega328PB | USBasp | twiboot at `0x7000` | `-D` flag preserves bootloader on erase |
    | `panel_atmega328p` | ATmega328P | USBasp | twiboot at `0x7000` | Same binary as 328PB |

=== "Bootloader (one-time)"

    | Environment | Purpose |
    |---|---|
    | `atmega328p_bootloader` | Flash fuses + burn twiboot bootloader onto a 328P panel |
    | `atmega328pb_bootloader` | Same, for 328PB panels |

---

## Panel fuses (ATmega328PB / 328P)

!!! warning "Flash fuses through the bootloader environment"
    Wrong fuse values can lock the microcontroller. Use `pio run -e atmega328p_bootloader -t fuses` — don't set them by hand unless you know exactly what you're doing.

```
lfuse = 0xF7  — 16 MHz external full-swing crystal
hfuse = 0xD8  — SPIEN, EESAVE, BOOTRST (4 KB boot section at 0x7000)
efuse = 0xFC  — BOD 4.3 V
```

One-time sequence per panel:

```bash
pio run -e atmega328p_bootloader -t fuses    # set fuses
pio run -e atmega328p_bootloader -t upload   # burn twiboot
pio run -e panel_atmega328pb  -t upload      # burn panel application
```

After this, future panel updates are wireless via the controller. See [OTA & Updates](ota.md).

---

## Common commands

```bash
# Build only
pio run -e controller_esp32

# Build + upload over USB
pio run -e controller_wemos -t upload

# Upload over Wi-Fi (controller OTA via ArduinoOTA + mDNS)
pio run -e controller_wemos -t upload --upload-port lightnet-XXXX.local

# Serial monitor — 57600 baud everywhere
pio device monitor -e controller_wemos

# Build everything
pio run

# Run native host-side unit tests (no device needed — see Testing)
pio test -e native
```

!!! info "Post-build `.bin` generation"
    The post-build hook `tools/generate_bin.py` automatically emits both `.hex` and `.bin` for every panel environment. Upload the `.bin` to the controller via `POST /api/firmware/panels` — no manual conversion needed.

---

- [Hardware](hardware.md) — pin assignments and panel connectivity
- [Architecture](architecture.md) — source tree and internal design
- [OTA & Updates](ota.md) — panel updates over I²C and controller self-update
- [Testing](testing.md) — native unit tests and how to add new ones
