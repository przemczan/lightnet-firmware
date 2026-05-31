# Lightnet Firmware

Firmware for a self-discovering tree network of addressable LED panels.

The network is made up of two device types that speak over I²C:

- **Controller** — ESP8266 or ESP32. Runs WiFi, panel discovery, a WebSocket API for low-latency triggers, and an HTTP REST API for appearance control, scene management, and firmware updates. Discoverable on the local network as `lightnet-<chipid>.local`.
- **Panel** — ATmega328P/PB. Drives a single WS2812 LED. Registers itself into the tree by pinging its physical edges, receives an I²C address from the controller, then runs animations locally with zero per-frame traffic.

Panels connect to each other through physical edges (triangular panels by default). The controller walks the tree during discovery, assigns addresses, and from then on is the only I²C master.

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

---

## Quick start

```bash
# Build controller (Wemos D1 Mini / ESP8266)
pio run -e controller_wemos

# Build + upload via USB
pio run -e controller_wemos -t upload

# Build + upload over WiFi (OTA)
pio run -e controller_wemos -t upload --upload-port lightnet-XXXX.local

# Build panel firmware (ATmega328PB)
pio run -e panel_atmega328pb

# Serial monitor (57600 baud)
pio device monitor -e controller_wemos

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
| [docs/architecture.md](docs/architecture.md) | Physical topology, library structure, I²C protocol, animation framework internals, discovery sequence, controller boot |
| [docs/ota.md](docs/ota.md) | Panel OTA (twiboot bootloader — precompiled + compilation), serial firmware upload, update flow |
| [docs/api.md](docs/api.md) | WebSocket binary protocol + full HTTP API reference (appearance, palettes, scenes, animations, firmware) |
| [docs/animations.md](docs/animations.md) | Scene structure, animation types, palettes, color references, sequencing, HTTP API usage, examples |
| [docs/testing.md](docs/testing.md) | Native host-side unit tests, what's covered, how to add new suites, MinGW setup |

---

## Panel SRAM constraints (ATmega328P/PB)

The ATmega328P/PB has **2 KB SRAM**. Three build-time constants share that budget and must be sized together:

| Constant | Location | Controls |
|---|---|---|
| `TWI_BUFFER_SIZE` | `platformio.ini` `build_flags_panel` | Size of each of the 4 Wire/TWI static buffers |
| `INCOMING_BUFFER_SIZE` | `LightnetPanel.hpp` | Size of each of the 2 circular packet queues |
| `Protocol::MAX_PACKET_SIZE` | `Protocol.hpp` | Largest packet in the protocol; sets the minimum safe `TWI_BUFFER_SIZE` |

**Rule: `TWI_BUFFER_SIZE` ≥ `MAX_PACKET_SIZE` (currently 80).**  
A packet larger than `TWI_BUFFER_SIZE` is silently truncated — the CRC still validates, so the corrupted payload reaches the handler and corrupts state. Keep them equal.

> `BUFFER_SIZE` and `BUFFER_LENGTH` are **not** used by MiniCore's Wire library. Only `TWI_BUFFER_SIZE` matters.

### Known-good SRAM budget

| Allocation | Size |
|---|---|
| Wire/TWI buffers (`TWI_BUFFER_SIZE=80` × 4) | 320 B |
| Packet queues (`INCOMING_BUFFER_SIZE=100` × 2 + overhead) | ~240 B |
| `AnimationPlayer` (queue × 4 + palette × 16 + vars) | ~190 B |
| `LNPanel` other fields | ~30 B |
| 3 × `LightnetPanelEdge` + `LightnetPinger` | ~125 B |
| Arduino Serial ring buffers | ~128 B |
| Stack + heap metadata | ~200 B |
| **Total** | **~1233 B** |

Leaves ~800 bytes of headroom. If you see panels crashing mid-init, stopping after a few I²C packets, or printing garbage on serial — reduce `TWI_BUFFER_SIZE` or `INCOMING_BUFFER_SIZE` first.

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
