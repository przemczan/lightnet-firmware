# OTA & Firmware Updates

## Panel OTA (twiboot)

Panels use a custom fork of `orempel/twiboot` stored in `firmware/twiboot_for_arduino/`. The upstream repo was tried but produced SRAM-initialisation issues on software jump entry and WDT bootloops on hardware reset — the fork addresses both.

> **ATmega88P has no bootloader** — twiboot OTA applies only to ATmega328PB and ATmega328P panels. The 88P must be flashed directly via USBasp.

### Bootloader layout

- Lives in the **4 KB boot section** at `0x7C00` (ATmega328PB/P)
- TWI address: `0x29` (compiled in via `-DTWI_ADDRESS=0x29`)
- Stays in bootloader only when EEPROM byte 510 contains `0xB007` (cleared after first read)

### Entry sequence (controller side)

1. `PanelsController::enterBootloader(address)` sends `PACKET_ENTER_BOOTLOADER` (type 201, token `0xB0`)
2. Panel `BootloaderBridge::prepareAndReset()`:
   - Writes `0xB007` to EEPROM[510]
   - Disables TWI and PCINT interrupts
   - Zeroes SRAM `0x0100–0x04FF`
   - Software-jumps to `0x7C00`
3. Bootloader reads EEPROM[510]: if `0xB007` → stays resident (clears magic); otherwise → jumps to app immediately

> **Warning:** Do **not** send `[0x01, 0x00]` (`CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER`) to the twiboot fork. It interprets this as a re-entry trigger and WDT-resets the panel, causing a bootloop.

### Writing firmware (controller side)

`TwibootClient` uses raw `Wire` (bypasses `LNBus`):

```cpp
twibootClient->connect(panelAddress);   // sends CMD_WAIT (0x00) to verify presence
twibootClient->writePage(addr, data, 128);  // 2 × 64-byte chunks
twibootClient->startApp();
```

`PanelFlasher` reads `/panel_fw.bin` from SPIFFS 128 bytes at a time — the full binary is never buffered in RAM.

### HTTP upload flow

```
POST /api/firmware/panels  [raw .bin body]
  → streams to /panel_fw.bin on SPIFFS
  → triggers PanelFlasher::startFlashing()
  → non-blocking flash loop in main() case 1

GET /api/firmware/status
  → {"state":"flashing","panel":3,"total":12,"progress":45}
```

After all panels are flashed the **controller must be restarted** — `PanelsInitializer` needs to re-run discovery so panels get new I²C addresses.

---

## Serial Firmware Upload (PC → Controller)

`SerialFirmwareReceiver` listens on the existing 57600-baud Serial port. This allows updating panels without WiFi.

### Frame format

```
┌──────────────────┬─────────────┬───────────────┬──────────────┐
│ Magic (4 B)      │ Size (4 B)  │ Data (N B)    │ CRC-16 (2 B) │
│ 'L' 'N' 'F' 'W' │ little-end. │ firmware .bin │ little-end.  │
└──────────────────┴─────────────┴───────────────┴──────────────┘
```

Controller replies:
- `READY\n` — after the header is received
- `OK\n` — after CRC validates and binary is saved to SPIFFS
- `ERR:<message>\n` — on any failure

### Usage

```bash
pip install pyserial
python tools/flash_panels_serial.py <port> <firmware.bin>
```

Once the binary is saved the controller starts flashing panels exactly as with the HTTP upload path.

---

## Controller Self-Update (ArduinoOTA)

`ArduinoOTA` is initialised after WiFi connects. Standard ports: 8266 (ESP8266), 3232 (ESP32). No password — intended for a trusted local network.

```bash
# Upload directly over WiFi
pio run -e initializer_esp32 --target upload --upload-port lightnet-XXXX.local
```

The hostname matches the mDNS name (`lightnet-<chipid>`). The `.local` suffix requires mDNS to be working on the network (standard on most OS setups).

---

## Build Post-Processing

A post-build script (`tools/generate_bin.py`) automatically produces both `.hex` and `.bin` for all panel environments. The `.bin` output is what the controller uses for OTA panel flashing — upload this file via `POST /api/firmware/panels`.

---

## Next Steps

- [API Reference](api.md) — Firmware update endpoints (`/api/firmware/panels`, `/api/firmware/status`)
- [Getting Started](getting-started.md) — Build commands and initial flashing
- [Troubleshooting](troubleshooting.md) — OTA failure recovery
