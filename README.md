# Lightnet Firmware

Firmware for a self-discovering tree network of addressable LED panels.

The network is made up of two device types connected by a point-to-point UART relay:

- **Controller** — ESP32-class (ESP8266 dropped — no spare UART for the trunk, and RAM was already tight). Runs WiFi, panel discovery, a WebSocket API for low-latency triggers, and an HTTP REST API for appearance control, scene management, and firmware updates. Discoverable on the local network as `lightnet-<chipid>.local`.
- **Panel** — ATmega328P/PB. Drives a single APA102/SK9822 LED. Every panel is a store-and-forward repeater with one parent edge and up to 3 child edges; it registers into the tree during discovery and then runs animations locally with zero per-frame traffic.

Panels connect to each other through physical edges (triangular panels by default). The controller drives a depth-first walk over the relay itself during discovery, assigning each panel a sequential index.

---


## Setup

Clone with submodules to get the twiboot bootloader:

```bash
git clone --recurse-submodules https://github.com/przemczan/lightnet-firmware.git
cd lightnet-firmware
```

Or if you already cloned without submodules:

```bash
git submodule update --init --recursive
```

Copy the example config files before your first build (defaults work out of the box):

```bash
cp src/controller.config.hpp.example src/controller.config.hpp
cp src/panel.config.hpp.example       src/panel.config.hpp
```

---

## Quick start

```bash
# Build controller (Lolin S2 Mini)
pio run -e controller_s2_mini

# Build + upload via USB
pio run -e controller_s2_mini -t upload

# Build + upload over WiFi (OTA)
pio run -e controller_s2_mini -t upload --upload-port lightnet-XXXX.local

# Build panel firmware (ATmega328PB)
pio run -e panel_atmega328pb

# Build + upload panel via USBasp
pio run -e panel_atmega328pb -t upload

# Serial monitor (57600 baud)
pio device monitor -e controller_wemos_d1_mini_pro

# Run native host-side unit tests (no device needed)
pio test -e native
```

**Panel bootloader** — Use precompiled or compile your own:
```bash
# Flash precompiled bootloader (fastest)
avrdude -c usbasp -p m328pb -U flash:w:twiboot/pre-compiled_bootloaders/pre-compiled_atmega328pb_16mhz_twiboot.hex:i

# Or compile from source
pio run -e atmega328pb_bootloader -t upload
```

See [docs/ota.md](docs/ota.md) for full bootloader setup and panel OTA process.

---

## Documentation

| Document | Contents |
|---|---|
| [docs/getting-started.md](docs/getting-started.md) | PlatformIO environments, config files, build/upload commands |
| [docs/hardware.md](docs/hardware.md) | Pin assignments for controllers and panels, topology rules, fuses |
| [docs/architecture.md](docs/architecture.md) | Physical topology, library structure, I²C protocol, animation framework internals, discovery sequence, controller boot |
| [docs/ota.md](docs/ota.md) | Panel OTA (twiboot bootloader — precompiled + compilation), serial firmware upload, update flow |
| [docs/api.md](docs/api.md) | WebSocket binary protocol + full HTTP API reference (appearance, palettes, scenes, animations, firmware) |
| [docs/animations/index.md](docs/animations/index.md) | Animation system overview — panel-local types, controller runners, scene model |
| [docs/animations/scene-authoring.md](docs/animations/scene-authoring.md) | Scene authoring guide — layers, steps, panel selectors, directionality, palettes, examples |
| [docs/testing.md](docs/testing.md) | Native host-side unit tests, what's covered, how to add new suites, MinGW setup |

---

## Panel SRAM constraints (ATmega328P/PB)

The ATmega328P/PB has **2 KB SRAM**, shared statically between the relay stack
(`EdgeUartTransport`, `PacketFramer`/`EdgeFrameReceiver`, discovery) and `AnimationPlayer`
(`MAX_ANIM_SLOTS`, the one constant that scales freely). See
[docs/hardware.md § Panel SRAM budget & `MAX_ANIM_SLOTS`](docs/hardware.md) for the full
consumer-by-consumer breakdown and sizing guidance — kept there rather than duplicated here so it
doesn't go stale in two places at once.

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
