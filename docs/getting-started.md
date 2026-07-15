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
    | `LIGHTNET_TRUNK_BAUD` | `500000UL` | Relay trunk UART baud — must match the panels' setting; flash both sides together after changing |
    | `CONFIG_PORTAL_TIMEOUT` | `120` | Seconds the Wi-Fi captive portal stays open before timeout |
    | `SERVER_PORT` | `80` | HTTP server port |

    **Debug sub-switches** — all default to `1` when `DEBUG=1` is set in `platformio.ini`. Uncomment and set to `0` to silence a specific area:

    | Symbol | Area silenced |
    |---|---|
    | `DEBUG_API` | WebSocket / HTTP API logs |
    | `DEBUG_RGB_CTRL` | LED controller logs |
    | `DEBUG_LIGHTNET_BUS` | Bus packet logs — one line per packet sent/received (type + panel index, valid/invalid); also gates the sim panel dispatch logs (`LightnetBus`, `SIM_MODE`) |
    | `DEBUG_FLASHER` | OTA / panel flash logs |
    | `DEBUG_DISCOVERY` | Panel discovery logs |
    | `DEBUG_INIT` | Startup / init logs |
    | `DEBUG_DEMO` | Demo logs |

    `DEBUG_LIGHTNET_BUS_PACKET_CONTENT` additionally dumps every packet's raw bytes. It is the one sub-switch that defaults to `0` — set it to `1` explicitly when you need packet content.

    Pin assignments (`CONTROLLER_TRUNK_RX_PIN`/`CONTROLLER_TRUNK_TX_PIN` for the relay trunk's `Serial1`, etc.) have platform-specific defaults in `src/controller/config.hpp` and only need overriding for custom hardware.

=== "panel.config.hpp"

    Located at `src/panel.config.hpp`, included by `src/panel/config.hpp`. The panel build is
    bare-metal (no `framework = arduino` — see [Architecture](architecture.md)); with `DEBUG=1`
    its debug output goes out a bit-banged, TX-only UART on PD7 (`Panel/DebugSerial.hpp`,
    57600 8N1, overridable via `DEBUG_SERIAL_BAUD`), since USART0 is owned by the relay trunk.
    Edge pins/count are fixed by
    `Panel/EdgeUartTransport.hpp` (3 edges, matching the schematic's mux/USART wiring), not
    configurable per-build the way the old GPIO ping-pulse edges were.

    | Symbol | Default | Description |
    |---|---|---|
    | `LIGHTNET_TRUNK_BAUD` | `500000UL` | Edge-link UART baud — must match the controller's setting and the relay bootloader (re-burn the bootloader after changing); use exact 16 MHz UBRR divisors (2000000, 1000000, 500000, 250000, …) for 0% baud error |
    | `DEBUG_SERIAL_BAUD` | `57600UL` | Baud of the bit-banged debug UART on PD7 — match your monitor's baud if overridden |

---

## PlatformIO environments

All environments are defined in `platformio.ini`.

=== "Controller"

    | Environment | Board | Notes |
    |---|---|---|
    | `controller_esp32` | ESP32 DevKit | USB upload at 460800 baud |
    | `controller_s2_mini` | Lolin S2 Mini (ESP32-S2) | USB upload at 460800 baud |
    | `controller_esp32_c3` | Unbranded ESP32-C3-MINI-1 clone (UART bridge) | USB upload at 460800 baud |
    | `controller_esp32_sim` | ESP32 DevKit (sim) | Host-side sim — no hardware; fabricates a virtual panel tree directly (`Sim/PanelsInitializerSim.cpp`), no wire protocol involved |
    | `controller_s2_mini_sim` | Lolin S2 Mini (sim) | Same as `_sim` above |
    | `controller_esp32_c3_sim` | ESP32-C3-MINI-1 clone (sim) | Same as `_sim` above |

    !!! note "ESP8266 controller targets are retired"
        Dropped: it doesn't meet the relay design's requirements (no spare hardware UART for the
        trunk, and RAM was already tight). See `platformio.ini`.

    All controller environments use `lib_ldf_mode = chain+`, ESPAsyncWebServer, and ESPAsyncWiFiManager. `*_sim` targets define `SIM_MODE`: the scene engine and `PanelsController` stay on `ControllerPacketSink`/`LNBus` (routed to `SimPanelManager`) since sim panels don't speak the relay's UART protocol, while real hardware uses `ControllerRelayPacketSink` over `Serial1` instead — see [Architecture](architecture.md) §3.

    **MQTT / Home Assistant** is available on all controller targets (`LIGHTNET_MQTT=1`, ESP32-class only now). Enable it via `PATCH /api/mqtt` after the controller is on the network. By default the controller **auto-discovers** the broker (`_mqtt._tcp` mDNS, then `homeassistant.local` / `hassio.local`); set a manual broker host to skip discovery. Home Assistant discovers Lightnet entities automatically when its MQTT integration uses the same broker. See [`docs/api.md`](api.md) §2.9 for topic layout and discovery modes.

=== "Native tests"

    | Environment | Purpose |
    |---|---|
    | `native` | Host-side unit tests — pure C++ logic, no Arduino (`pio test -e native`) |

=== "Panel"

    | Environment | Board | Uploader | Bootloader | Notes |
    |---|---|---|---|---|
    | `panel_atmega328_via_controller` | ATmega328P | Custom serial via controller | — | Upload `.bin` over the controller's 57600-baud serial port |
    | `panel_atmega328pb` | ATmega328PB | USBasp | relay bootloader at `0x7000` | Every upload chip-erases (wiping any resident bootloader — re-burn via the bootloader envs when OTA testing needs it): skipping the erase with avrdude's `-D` corrupts every re-flash, since ISP writes can only clear bits and chip erase is the only erase ISP has. **Bare-metal** (hardware redesign plan §10/§11) — no `framework = arduino`. Both panel and controller have cut over to the relay protocol; builds clean but is not yet bench-validated on real hardware. |
    | `panel_atmega328p` | ATmega328P | USBasp | relay bootloader at `0x7000` | Same binary as 328PB |

=== "Bootloader (one-time)"

    | Environment | Purpose |
    |---|---|
    | `atmega328p_bootloader` | Flash fuses + burn the relay's own OTA bootloader (`lib/Lightnet/Panel/bootloader/`) onto a 328P panel — see [`docs/ota.md`](ota.md) |
    | `atmega328pb_bootloader` | Same, for 328PB panels |

---

## Panel fuses (ATmega328PB / 328P)

!!! warning "Flash fuses through the bootloader environment"
    Wrong fuse values can lock the microcontroller. Use `pio run -e atmega328p_bootloader -t fuses` (or the `pb` variant) — don't set them by hand unless you know exactly what you're doing. If ISP stops responding to a chip right after a fuse write (works fine before, dead after — on old *and* brand-new chips), suspect an invalid `CKSEL`/clock-source fuse before anything else: with no working system clock, the target can't run the SPI programming state machine at all, so even reading the device signature times out. Standard ISP cannot recover from this — either inject an external clock signal into XTAL1 (often enough to revive ISP long enough to rewrite fuses) or use high-voltage/parallel programming, which doesn't depend on the target's own clock.

```
lfuse = 0xF7  — 328P: Full Swing Crystal Oscillator (0.4-20 MHz, rail-to-rail XTAL2 swing, robust
                to board noise), slowest/safest start-up ramp
lfuse = 0xF7  — 328PB: Full Swing isn't defined on this variant's datasheet — same byte value
                selects the Low Power Crystal Oscillator (8-16 MHz band) instead, same start-up ramp
hfuse = 0xD8  — SPIEN, EESAVE, BOOTRST (4 KB boot section at 0x7000)
efuse = 0xFD  — 328P: BOD 2.7 V
efuse = 0xF5  — 328PB: same BOD 2.7 V, but bit 3 is unused-and-must-read-0 on this variant instead
                of the usual AVR unused-bits-read-1 convention; avrdude accepts 0xFD with a
                deprecation warning today but will eventually hard-error on it
```

328P and 328PB share the same `lfuse`/`hfuse` bytes but take different `efuse` values (`panel_fuses_328` for 328P; `env:panel_atmega328pb` / `env:atmega328pb_bootloader` override `efuse` for 328PB in `platformio.ini`) — don't copy one variant's fuse bytes onto the other.

One-time sequence per panel:

```bash
pio run -e atmega328p_bootloader -t fuses    # set fuses
pio run -e atmega328p_bootloader -t upload   # burn the relay bootloader
pio run -e panel_atmega328pb  -t upload      # burn panel application
```

After this, future panel updates are wireless via the controller — see [OTA & Updates](ota.md).

!!! warning "Every ISP app upload wipes the bootloader — re-burn it"
    `panel_atmega328p`/`panel_atmega328pb` chip-erase before writing (ISP has no partial erase, and
    a flash write can only clear bits, never set them, so skipping erase corrupts the app the
    moment a different image is written over the old one). A full chip erase also wipes whatever
    was at `0x7000`. If you ever flash the app again over ISP (bench testing, recovery), re-run the
    `atmega328p_bootloader -t upload` step afterward, or the panel won't boot into the app at all.

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
