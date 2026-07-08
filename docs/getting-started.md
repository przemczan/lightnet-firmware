---
icon: material/rocket-launch-outline
---

# Build & Flash Reference

This page is the PlatformIO reference: every environment, the fuses, and the day-to-day commands. For a guided first-time walkthrough, see the hub's **[Get Started](../getting-started/index.md)** instead. For schematics and PCB layouts, see [Hardware → Schematics & PCB](hardware.md#schematics-pcb).

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
cp platformio_local.ini.example       platformio_local.ini   # optional: USB/monitor ports
```

Both `*.config.hpp` files ship with sane defaults, so no changes are required to do a first build. The `*.config.hpp` files and `platformio_local.ini` are gitignored — keep per-device and per-machine settings there without touching the tracked `*.example` files.

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
    | `DEBUG_LIGHTNET_BUS` | Sim panel dispatch logs (`LightnetBus`, `SIM_MODE` only) |
    | `DEBUG_FLASHER` | OTA / panel flash logs |
    | `DEBUG_DISCOVERY` | Panel discovery / ping logs |
    | `DEBUG_INIT` | Startup / init logs |
    | `DEBUG_DEMO` | Demo logs |

    Pin assignments (`CONTROLLER_TRUNK_RX_PIN`/`CONTROLLER_TRUNK_TX_PIN` for the relay trunk's `Serial1`, etc.) have platform-specific defaults in `src/controller/config.hpp` and only need overriding for custom hardware.

=== "panel.config.hpp"

    Located at `src/panel.config.hpp`, included by `src/panel/config.hpp`. The panel build is
    bare-metal (no `framework = arduino` — see [Architecture](architecture.md)), with no
    non-Arduino debug UART path built yet, so `DEBUG` stays `0`. Edge pins/count are fixed by
    `Panel/EdgeUartTransport.hpp` (3 edges, matching the schematic's mux/USART wiring), not
    configurable per-build the way the old GPIO ping-pulse edges were.

---

## PlatformIO environments

All environments are defined in `platformio.ini`.

=== "Controller"

    | Environment | Board | Notes |
    |---|---|---|
    | `controller_esp32` | ESP32 DevKit | USB upload at 460800 baud |
    | `controller_s2_mini` | Lolin S2 Mini (ESP32-S2) | USB upload at 460800 baud |
    | `controller_esp32_sim` | ESP32 DevKit (sim) | Host-side sim — no hardware; fabricates a virtual panel tree directly (`Sim/PanelsInitializerSim.cpp`), no wire protocol involved |
    | `controller_s2_mini_sim` | Lolin S2 Mini (sim) | Same as `_sim` above |

    !!! note "ESP8266 controller targets are retired"
        Dropped: it doesn't meet the relay design's requirements (no spare hardware UART for the
        trunk, and RAM was already tight). See `platformio.ini`.

    All controller environments use `lib_ldf_mode = chain+`, FastLED, ESPAsyncWebServer, and ESPAsyncWiFiManager. `*_sim` targets define `SIM_MODE`: the scene engine and `PanelsController` stay on `ControllerPacketSink`/`LNBus` (routed to `SimPanelManager`) since sim panels don't speak the relay's UART protocol, while real hardware uses `ControllerRelayPacketSink` over `Serial1` instead — see [Architecture](architecture.md) §3.

    **MQTT / Home Assistant** is available on all controller targets (`LIGHTNET_MQTT=1`, ESP32-class only now). Enable it via the WiFi captive portal (MQTT section) or `PATCH /api/mqtt` after the controller is on the network. By default the controller **auto-discovers** the broker (`_mqtt._tcp` mDNS, then `homeassistant.local` / `hassio.local`); set a manual broker host to skip discovery. Home Assistant discovers Lightnet entities automatically when its MQTT integration uses the same broker. See [`docs/api.md`](api.md) §2.9 for topic layout and discovery modes.

=== "Native tests"

    | Environment | Purpose |
    |---|---|
    | `native` | Host-side unit tests — pure C++ logic, no Arduino (`pio test -e native`) |

=== "Panel"

    | Environment | Board | Uploader | Bootloader | Notes |
    |---|---|---|---|---|
    | `panel_atmega328_via_controller` | ATmega328P | Custom serial via controller | — | Upload `.bin` over the controller's 57600-baud serial port |
    | `panel_atmega328pb` | ATmega328PB | USBasp | relay bootloader at `0x7000` | `-D` flag preserves bootloader on erase. **Bare-metal** (hardware redesign plan §10/§11) — no `framework = arduino`. Both panel and controller have cut over to the relay protocol; builds clean but is not yet bench-validated on real hardware. |
    | `panel_atmega328p` | ATmega328P | USBasp | relay bootloader at `0x7000` | Same binary as 328PB |

=== "Bootloader (one-time)"

    | Environment | Purpose |
    |---|---|
    | `atmega328p_bootloader` | Flash fuses + burn the relay's own OTA bootloader (`lib/Lightnet/Panel/bootloader/`) onto a 328P panel — see [`docs/ota.md`](ota.md) |
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
pio run -e atmega328p_bootloader -t upload   # burn the relay bootloader
pio run -e panel_atmega328pb  -t upload      # burn panel application
```

After this, future panel updates are wireless via the controller. See [OTA & Updates](ota.md).

---

## Common commands

```bash
# Build only
pio run -e controller_esp32

# Build + upload over USB
pio run -e controller_s2_mini -t upload

# Upload over Wi-Fi (controller OTA via ArduinoOTA + mDNS)
pio run -e controller_s2_mini -t upload --upload-port lightnet-XXXX.local

# Serial monitor — 57600 baud everywhere
pio device monitor -e controller_s2_mini

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
- [OTA & Updates](ota.md) — panel updates over the relay and controller self-update
- [Testing](testing.md) — native unit tests and how to add new ones
