# Firmware Hardware

## Physical Topology

Panels form a **tree structure** rooted at the controller. Each panel has up to 3 edges (physical connectors); edges carry both power and a single-wire ping line. The controller discovers the network by sequentially pinging each edge via GPIO, triggering PCINT interrupts on the receiving ATmega.

```
Controller
├── Panel A (edge 0)
│   ├── Panel B (edge 1)
│   └── Panel C (edge 2)
└── Panel D (edge 1)
    └── Panel E (edge 0)
```

After the ping handshake completes, all communication uses **I²C** (`LightnetBus`) carrying structured `Protocol` packets. Panels are assigned sequential indices during discovery and use those indices as I²C addresses for all subsequent unicast traffic.

---

## Pin Assignments

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

## Panel Fuses

### ATmega328PB / 328P

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

### ATmega88P

```
lfuse = 0xF7  — 16 MHz external full-swing crystal
hfuse = 0xD7  — SPIEN, EESAVE, BOOTRST=1 (app start, no bootloader)
efuse = 0xFC  — BOD 4.3 V
```

The ATmega88P has no bootloader — twiboot OTA does **not** apply to this target. Panel firmware must be flashed directly via USBasp.

---

## Next Steps

- [Getting Started](getting-started.md) — Build and flash commands
- [Architecture](architecture.md) — Software structure and I²C protocol
- [OTA & Updates](ota.md) — Panel OTA via twiboot
