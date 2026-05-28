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
    | `panel_nanoatmega328` | Arduino Nano (ATmega328) | Arduino serial | Arduino | Via controller serial bridge |
    | `panel_atmega328pb` | ATmega328PB | USBasp | twiboot at `0x7C00` | `-D` flag preserves bootloader on erase |
    | `panel_atmega328p` | ATmega328P | USBasp | twiboot at `0x7C00` | Same binary as 328PB |

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
hfuse = 0xD0  — SPIEN, EESAVE, BOOTRST (boot from twiboot)
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
```

!!! info "Post-build `.bin` generation"
    The post-build hook `tools/generate_bin.py` automatically emits both `.hex` and `.bin` for every panel environment. Upload the `.bin` to the controller via `POST /api/firmware/panels` — no manual conversion needed.

---

- [Hardware](hardware.md) — pin assignments and panel connectivity
- [Architecture](architecture.md) — source tree and internal design
- [OTA & Updates](ota.md) — panel updates over I²C and controller self-update
