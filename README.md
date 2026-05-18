# Lightnet Firmware

Firmware for two device types that form a self-discovering LED panel network:

- **Controller** — ESP8266 or ESP32; runs WiFi, discovery, WebSocket API, and OTA updates
- **Panel** — ATmega328P/PB; drives one WS2812 LED, registers to the tree via edge pings

Panels connect to each other via physical edges (triangular default). The controller
initiates discovery, assigns each panel an I²C address, then communicates over I²C.

---

## Building

This project uses **PlatformIO**.

```bash
# Controller (ESP32)
pio run -e initializer_esp32

# Controller (Wemos D1 Mini / ESP8266)
pio run -e initializer_wemos

# Panel (ATmega328P via USBasp)
pio run -e panel_atmega328p

# Panel (ATmega328PB via USBasp)
pio run -e panel_atmega328pb

# All environments
pio run
```

---

## Flashing the Controller

### First flash (USB serial)

```bash
pio run -e initializer_esp32 --target upload --upload-port COM3
```

### Subsequent flashes (WiFi OTA)

Once the controller is on the network it advertises itself via mDNS as
`lightnet-<chipid>.local`. Use that hostname as the upload port:

```bash
pio run -e initializer_esp32 --target upload --upload-port lightnet-XXXX.local
```

Or set it permanently in `platformio.ini`:

```ini
[env:initializer_esp32]
upload_protocol = espota
upload_port     = lightnet-XXXX.local
```

---

## Flashing Panels

Panel firmware updates happen in two stages:

1. **First-time only (one per panel):** flash the [twiboot](https://github.com/orempel/twiboot)
   I²C bootloader via USBasp so the panel can be updated over I²C from then on.
2. **All future updates:** build panel firmware on your PC, push it to the controller
   over WiFi (HTTP) or USB serial — the controller programs every panel over I²C automatically.

### Step 1 — One-time fuse + twiboot setup per panel

#### ATmega328P

Set fuses (16 MHz external full-swing crystal, 1 KB boot section at 0x7C00, BOOTRST, BOD 4.3 V).
Fresh chips run at 1 MHz by default so `-B 32` is required for the initial ISP communication:

```
avrdude -c usbasp -p atmega328p -B 32 \
  -U lfuse:w:0xF7:m -U hfuse:w:0xD0:m -U efuse:w:0xFC:m
```

| Fuse | Value | Key bits |
|---|---|---|
| lfuse | `0xF7` | CKSEL=0111 — Full Swing Crystal; SUT=11 max startup delay |
| hfuse | `0xD0` | BOOTSZ=00 → 4 KB boot section starting at **0x7000**; BOOTRST; EESAVE |
| efuse | `0xFC` | BODLEVEL=100 → BOD **4.3 V** |

> **No external crystal?** Use `lfuse=0xE2` (internal 8 MHz RC oscillator).
> The firmware must also be built with `F_CPU=8000000L` in that case.

Flash twiboot (pre-built in this repo, see twiboot configuration note below):

```bash
cd firmware/twiboot
pio run -e atmega328p --target upload
```

#### ATmega328PB

Same fuses as 328P; use `-B 32` as fresh chips also start at 1 MHz:

```
avrdude -c usbasp -p atmega328pb -B 32 \
  -U lfuse:w:0xF7:m -U hfuse:w:0xD0:m -U efuse:w:0xFC:m
```

> **No external crystal?** Use `lfuse=0xE2` instead of `0xF7`.

Flash twiboot:

```bash
cd firmware/twiboot
pio run -e atmega328pb --target upload
```

#### twiboot configuration

twiboot reads its I²C slave address and boot mode flag from EEPROM bytes `[0]` and `[1]`
— written by the panel firmware before triggering a reboot into update mode:

| EEPROM byte | Content |
|---|---|
| `[0]` | Panel's assigned I²C address (written before OTA reset) |
| `[1]` | `0x42` = stay in bootloader; `0xFF` = start app after short timeout |

The twiboot source lives in `firmware/twiboot/` and is pre-configured with:
- `F_CPU=8000000`
- `BOOTLOADER_ADDRESS=0x7C00` (4 KB section, matches `hfuse=0xD6`)
- `TIMEOUT_MS=200` — exits to app quickly so controller discovery window is not missed
- On a fresh chip EEPROM is `0xFF` — twiboot falls back to compiled-in default address
  (`0x29`) when `EEPROM[0] == 0xFF`.

After a successful programming session twiboot clears `EEPROM[1]` to `0xFF`
before jumping to the application. If power is lost mid-transfer the flag
survives, so twiboot will re-enter programming mode on the next reset — the panel
never boots corrupted firmware.

#### First-time panel firmware flash (bootstrap)

After twiboot is on the chip, flash the Lightnet panel firmware directly via USBasp
**once** to bootstrap the panel. The `-D` flag (skip chip erase) preserves twiboot:

```bash
# from repo root — works for both 328P and 328PB targets
pio run -e panel_atmega328pb --target upload
```

`upload_flags = -D` is already set in `platformio.ini` so twiboot at `0x7C00` is never erased.
After this first flash the panel will be discovered by the controller, and all future
updates go through the controller over I²C (Step 2).

### Step 2 — Push new panel firmware (all future updates)

Build the panel firmware first:

```bash
pio run -e panel_atmega328pb
# .bin is generated automatically alongside .hex:
# .pio/build/panel_atmega328pb/firmware.bin
```

The same binary works for both ATmega328P and ATmega328PB panels — build once, flash all.

Then deliver it to the controller using one of two methods:

#### Option A — WiFi (HTTP)

```bash
curl -X POST http://lightnet-XXXX.local/api/firmware/panels \
     -H "Content-Type: application/octet-stream" \
     --data-binary @.pio/build/panel_atmega328p/firmware.bin
```

Poll progress:

```bash
curl http://lightnet-XXXX.local/api/firmware/status
```

#### Option B — USB serial

```bash
pip install pyserial   # once

python tools/flash_panels_serial.py COM3 .pio/build/panel_atmega328pb/firmware.bin
```

The script waits for the controller to finish booting (WiFi connect + OTA ready) before
sending the firmware, so it is safe to run immediately after connecting the serial port.

Both options store the binary in SPIFFS and immediately start flashing all
discovered panels sequentially over I²C. After all panels are done, **restart
the controller** (OTA or power cycle) so discovery re-runs — the updated panels
reboot and wait for a fresh welcome ping.

---

## HTTP API

| Endpoint | Method | Description |
|---|---|---|
| `/api/firmware/panels` | `POST` | Upload panel firmware binary; starts flashing immediately |
| `/api/firmware/status` | `GET` | Returns JSON with current flash state and progress |
| `/ws` | WebSocket | Binary MessageApi protocol (toggle, color, brightness, state) |
| `/` | `GET` | Serves static web app from SPIFFS |

---

## Serial Monitor

```bash
pio device monitor -e initializer_esp32   # 57600 baud
pio device monitor -e panel_atmega328p    # 57600 baud
```
