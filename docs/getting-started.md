# Firmware: Getting Started

## Build Environments

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

## Common Commands

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

## Initial Setup

1. Clone the `lightnet-firmware` repository
2. Install [PlatformIO](https://platformio.org/) and ensure board drivers are available
3. For controller: connect via USB, select the appropriate environment, and run `pio run -e <env> --target upload`
4. After first boot, the controller opens a WiFi captive portal named **"Lightnet-Controller"** — connect and configure WiFi credentials
5. Once connected, the controller is reachable at `lightnet-<chipid>.local`

---

## Next Steps

- [Hardware](hardware.md) — Pin assignments and panel connectivity
- [Architecture](architecture.md) — Source tree and internal design
- [OTA & Updates](ota.md) — Panel OTA and controller self-update
