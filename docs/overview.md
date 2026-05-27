# Lightnet Firmware Overview

Lightnet is embedded firmware for a tree network of addressable-LED panels. A single ESP8266/ESP32 **controller** discovers and drives up to 100 ATmega **panels** over I²C. The controller exposes WiFi APIs; panels run animations locally after a single setup packet.

## Controller vs Panel

Two entirely different binaries are compiled from one source tree. The preprocessor flag `LIGHTNET_TARGET_CONTROLLER` (set in `platformio.ini`) selects the target — the unused half is eliminated entirely at compile time.

| Target | MCU | Role |
|---|---|---|
| Controller | ESP8266 / ESP32 | Discovery, WiFi, HTTP + WebSocket API, animation scheduling |
| Panel | ATmega328P / 328PB / 88P | Local animation playback, LED output, I²C slave |

## Discovery & Topology

Panels form a **tree** rooted at the controller. Each panel has up to 3 physical edge connectors. On boot, the controller discovers the network by pinging each edge via GPIO — panels respond and register over I²C, receiving sequential addresses. After discovery, all traffic uses I²C.

```
Controller
├── Panel A (edge 0)
│   ├── Panel B (edge 1)
│   └── Panel C (edge 2)
└── Panel D (edge 1)
    └── Panel E (edge 0)
```

## Animation System

- **Panel-local animations** (BREATHE, PULSE, REACTIVE, etc.) run entirely on the ATmega with zero per-frame I²C traffic after a single PREPARE packet.
- **Controller runners** (WAVE, RIPPLE, CHASE) are computed on the ESP each frame and send per-panel brightness over I²C.
- **Scenes** are multi-layer JSON containers stored on SPIFFS, played back by the controller's `ScenePlayer`.
- **Palettes** are 16-stop RGB gradients. **Groups** are synchronisation units — panels in the same group fire simultaneously (±2.5 µs jitter via I²C General Call).

## External Interfaces

- **WebSocket** at `ws://lightnet-<chipid>.local/ws` — binary, low-latency, for real-time panel control and reactive beat triggers.
- **HTTP** at `http://lightnet-<chipid>.local` — JSON REST API for appearance, scene management, palettes, panel control, and firmware updates.
- **mDNS** as `lightnet-<chipid>.local`, service `_lightnet._tcp`.

## Active Branches

| Branch | State |
|---|---|
| `master` | Stable — animation framework v3, no HTTP animation API |
| `scenes` | In progress — Protocol v4, palette/brightness/base-colors, full scene player and HTTP API |

Code specific to the `scenes` branch is marked *(scenes)* throughout the docs.

## Documentation

- [Getting Started](getting-started.md) — Build environments, PlatformIO commands, initial setup
- [Hardware](hardware.md) — Pin assignments, physical topology, panel fuses
- [Architecture](architecture.md) — Source tree, library structure, I²C protocol, internals
- [API Reference](api.md) — WebSocket binary protocol and all HTTP endpoints
- [Animations & Scenes](animations.md) — Animation types, palettes, scenes, color references, examples
- [OTA & Updates](ota.md) — Panel twiboot OTA, serial upload, controller ArduinoOTA
- [Troubleshooting](troubleshooting.md) — Debug macros, common issues
