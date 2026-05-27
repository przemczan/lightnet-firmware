# Troubleshooting

## Debugging

### Debug macros

`DEBUG=1` is set globally in `common_env_data.build_flags`. Override with `DEBUG=0` in environments where code size matters (e.g. `panel_atmega88p`).

| Macro | Expands to (DEBUG=1) | Use |
|---|---|---|
| `PRINTLN(s)` | `Serial.println(s)` | String message |
| `PRINTKV(k, v)` | `Serial.print(k); Serial.println(v)` | Key-value pair |
| `PRINTF(fmt, ...)` | `Serial.printf(fmt, ...)` | Formatted output |

All macros compile to **no-ops** when `DEBUG=0`.

Serial baud rate: **57600** on all environments.

```bash
pio device monitor -e initializer_wemos
```

### Watching panel I²C traffic

Set `RGBC_DEBUG 1` in `Panel/RGBController.cpp` for per-frame LED output logging (ATmega only; adds ~200 bytes flash).

### Inspecting panel state via WebSocket

Query all panel states with a `GET_PANELS_STATES` command (type 5):

```javascript
const ws = new WebSocket('ws://lightnet-XXXX.local/ws');
ws.binaryType = 'arraybuffer';

ws.onopen = () => {
  // Build PacketMeta (14 bytes): header (7) + headerCRC (2) + payloadCRC (2) + payloadSize (2)
  // type=5, protocolVersion=0x0001, nonce=arbitrary, payload empty
  // ... compute CRC-16/IBM over the 7-byte header and send
  ws.send(packetBuffer);
};

ws.onmessage = (e) => {
  // Response is PANELS_STATES (type 6)
  // Parse: length (uint16) then N × (panelIndex, state, r, g, b, brightness)
  const view = new DataView(e.data);
  const n = view.getUint16(16, true); // after 14-byte PacketMeta + 2-byte offset
  // ...
};
```

See [API Reference](api.md) for the full packet structure and CRC algorithm.

---

## Common Issues

| Symptom | Likely cause |
|---|---|
| Panel doesn't respond after `ENTER_BOOTLOADER` | EEPROM byte 510 was not written — check `BootloaderBridge::prepareAndReset()` |
| Panel WDT-resets in bootloader loop | `CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER` (`0x01 0x00`) was sent to the twiboot fork — see [OTA & Updates](ota.md) |
| All panels fail to start animations | `animScheduler->tick(millis())` missing from main loop `case 1` |
| Palette/appearance not restored on reboot | SPIFFS not mounted before `AppearanceStore::loadAndApply()` — check that SPIFFS mount is hoisted to `case 0` (scenes branch only) |
| Panels discovered but animations don't sync | General Call START sent only once — must be sent **twice** (300 µs apart) with `seq_id` duplicate guard |
| Controller opens captive portal every boot | WiFi credentials not saved — complete the "Lightnet-Controller" portal setup |
| `GET /api/panels` returns empty array | Discovery timed out (5 s) — check edge wiring and panel power |

---

## Next Steps

- [Architecture](architecture.md) — Understand internal systems and the discovery sequence
- [OTA & Updates](ota.md) — Panel flashing and bootloader details
- [API Reference](api.md) — Query panel states and diagnose via HTTP/WebSocket
