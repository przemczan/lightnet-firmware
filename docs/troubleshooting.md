---
icon: material/bug-outline
---

# Troubleshooting

Serial debug macros, common user-facing symptoms, and how to inspect panel state over WebSocket.

## Serial debugging

`DEBUG=0` by default in `src/controller.config.hpp` / `src/panel.config.hpp` (copied from the `*.example` files). Set `#define DEBUG 1` in your config header to enable verbose serial output. All debug macros compile to **no-ops** when `DEBUG=0`, so there is no cost in production builds.

| Macro | Expands to (DEBUG=1) | Use |
|---|---|---|
| `PRINTLN(s)` | `Serial.println(s)` | String message |
| `PRINTKV(k, v)` | `Serial.print(k); Serial.println(v)` | Key + value |
| `PRINTF(fmt, ...)` | `Serial.printf(fmt, ...)` | Formatted output |

Serial baud rate is **57600** on every environment.

```bash
pio device monitor -e controller_wemos_d1_mini_pro
```

### Per-frame panel LED log

For per-frame LED output logging on the panel, set `RGBC_DEBUG 1` in `lib/Lightnet/Panel/RGBController.cpp`. ATmega only; adds ~200 bytes of flash.

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
  // Parse: length (uint16) then N × (panelIndex uint16, state uint8, r uint8, g uint8, b uint8). 7 bytes per entry.
  const view = new DataView(e.data);
  const n = view.getUint16(16, true);
  // ...
};
```

The matching HTTP endpoint is `GET /api/panels`.

---

## Panel ISP programming issues

### "Target doesn't answer" (avrdude)

`avrdude` reports something like `Target doesn't answer. 1` (or `initialization failed`) when
flashing a panel over USBasp (`pio run -e panel_atmega328p -t upload` /
`panel_atmega328pb`). This might mean the target AVR's core clock isn't running at all, so it can never
service the ISP (SPI) handshake — ISP inherently needs the target's own clock to respond, unlike a
bootloader-based UART upload.

The near-universal cause on these boards is a fuse/crystal mismatch: `panel_fuses_328` in
`platformio.ini` sets the low fuse to select an external crystal oscillator (`0xF7` for the 328P's
Full Swing Crystal Oscillator, `0xFF`/Low Power Crystal for the 328PB variant — see that section's
own comment). If `X2` (the 16MHz crystal) isn't actually oscillating — missing, dead, a cold
solder joint, or wrong load caps — the chip is fused to wait on a clock source that never arrives,
and it's effectively bricked from ISP's point of view even though the silicon itself is fine.

**Fix — inject an external clock**: a second, known-good AVR can generate a clock signal for the
bricked target to run on, just long enough to rewrite its fuses back to something that works.

1. Flash `tools/recovery/ckout_divider.cpp` (via `pio run -e donor_ckout_slowclock -t upload`) onto
   a spare ATmega328P. It divides the donor's own 16MHz system clock by 16 (`CLKPR`) before it
   reaches the `CKOUT`/PB0 pin — a clean ~1MHz square wave is easier to couple in and less likely
   to fight the bricked panel's own dead oscillator amplifier bias than the raw 16MHz would be.
2. Enable the donor's own `CKOUT` fuse so PB0 actually outputs the system clock — it's low-fuse bit
   6 (active-low), so cleared relative to `panel_fuses_328`'s normal `0xF7`: write lfuse `0xB7` on
   the donor specifically (`avrdude ... -U lfuse:w:0xB7:m`), not on the bricked target.
3. Feed the donor's `CKOUT`/PB0 output into the bricked target's `XTAL1` pin through a coupling
   capacitor (a small ceramic, a few nF–10s of nF works well) — this is `X2`'s pin 1/PB6 on
   `Panel.png`.
4. With the injected clock running, `avrdude` should be able to talk to the target again. Rewrite
   its fuses back to the correct crystal-based values (`panel_fuses_328`, or the matching 328PB
   override) and reflash normally, then remove the donor clock and confirm the panel boots on its
   own crystal.

---

## Common user-facing issues

| Symptom | What to check |
|---|---|
| Captive portal never appears on first boot | Power for 20 s before connecting; forget the `Lightnet-Controller` SSID and reconnect |
| Captive portal opens every boot | Wi-Fi credentials didn't save — go through the portal again, ensure you tap "Save" |
| Controller boots but `lightnet-XXXX.local` is unreachable | Try the IP shown in serial monitor; on Windows install Bonjour Print Services; on iOS allow local-network access |
| `GET /api/panels` returns empty | Discovery timed out (5 s). Check edge wiring, panel power, and that panels are actually flashed |
| Some panels missing after discovery | Edge cable on the missing branch, or panel flash. Power-cycle the controller to retry discovery |
| Animations look glitchy with many panels | Relay edge quality — check inter-panel cable connections, especially on deep branches |
| Panel stuck in bootloop after OTA | Power-cycle once. If it persists, reflash that panel directly with USBasp (see [OTA & Updates](ota.md)) |

If something doesn't fit any row above, search the serial log around the moment the symptom appears — every state transition and protocol packet is traced when `DEBUG=1`.

---

- [Architecture](architecture.md) — discovery sequence and internal systems
- [OTA & Updates](ota.md) — panel flashing and bootloader details
- [API Reference](api.md) — querying state over HTTP / WebSocket
