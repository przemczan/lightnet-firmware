---
icon: material/bug-outline
---

# Troubleshooting

Serial debug macros, common user-facing symptoms, and how to inspect panel state over WebSocket.

## Serial debugging

`DEBUG=1` is set globally in `common_env_data.build_flags`, so the firmware is verbose by default. All debug macros compile to **no-ops** when `DEBUG=0`, so there is no cost in production builds.

| Macro | Expands to (DEBUG=1) | Use |
|---|---|---|
| `PRINTLN(s)` | `Serial.println(s)` | String message |
| `PRINTKV(k, v)` | `Serial.print(k); Serial.println(v)` | Key + value |
| `PRINTF(fmt, ...)` | `Serial.printf(fmt, ...)` | Formatted output |

Serial baud rate is **57600** on every environment.

```bash
pio device monitor -e controller_wemos
```

### Per-frame panel LED log

For per-frame LED output logging on the panel, set `RGBC_DEBUG 1` in `Panel/RGBController.cpp`. ATmega only; adds ~200 bytes of flash.

### Inspecting panel state from a client

The fastest way to confirm a panel's live state is the `GET_PANELS_STATES` WebSocket command (type 5). The packet structure and CRC algorithm live in [API Reference](api.md). A minimal browser-side example:

```javascript
const ws = new WebSocket('ws://lightnet-XXXX.local/ws');
ws.binaryType = 'arraybuffer';

ws.onopen = () => {
  // Build PacketMeta (14 bytes): header (7) + headerCRC (2) + payloadCRC (2) + payloadSize (2)
  // type=5, protocolVersion=0x0001, nonce=arbitrary, payload empty
  // Compute CRC-16/IBM over the 7-byte header and send.
  ws.send(packetBuffer);
};

ws.onmessage = (e) => {
  // Response is PANELS_STATES (type 6).
  // Parse: length (uint16) then N × (panelIndex, state, r, g, b, brightness).
  const view = new DataView(e.data);
  const n = view.getUint16(16, true);
  // ...
};
```

The matching HTTP endpoint is `GET /api/panels`.

---

## Common user-facing issues

| Symptom | What to check |
|---|---|
| Captive portal never appears on first boot | Power for 20 s before connecting; forget the `Lightnet-Controller` SSID and reconnect |
| Captive portal opens every boot | Wi-Fi credentials didn't save — go through the portal again, ensure you tap "Save" |
| Controller boots but `lightnet-XXXX.local` is unreachable | Try the IP shown in serial monitor; on Windows install Bonjour Print Services; on iOS allow local-network access |
| `GET /api/panels` returns empty | Discovery timed out (5 s). Check edge wiring, panel power, and that panels are actually flashed |
| Some panels missing after discovery | Edge cable on the missing branch, or panel flash. Power-cycle the controller to retry discovery |
| Animations look glitchy with many panels | I²C bus quality — shorten cable runs or reduce panel count |
| Panel stuck in bootloop after OTA | Power-cycle once. If it persists, reflash that panel directly with USBasp (see [OTA & Updates](ota.md)) |

If something doesn't fit any row above, search the serial log around the moment the symptom appears — every state transition and protocol packet is traced when `DEBUG=1`.

---

- [Architecture](architecture.md) — discovery sequence and internal systems
- [OTA & Updates](ota.md) — panel flashing and bootloader details
- [API Reference](api.md) — querying state over HTTP / WebSocket
