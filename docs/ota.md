---
icon: material/cloud-download-outline
---

# OTA & Firmware Updates

Three update paths: panel OTA over the relay trunk (triggered from the controller), serial firmware upload, and controller self-update over ArduinoOTA. Real (non-SIM) controller hardware only — there is no I²C wire to any panel to flash over, and sim panels don't implement a bootloader protocol, so OTA doesn't exist under `SIM_MODE`.

## Panel OTA (relay)

Panels run a from-scratch bootloader speaking the relay's own `PacketMeta`/CRC-16 framing:
[`lib/Lightnet/Panel/bootloader/RelayBootloader.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/lib/Lightnet/Panel/bootloader/RelayBootloader.cpp),
driven from the controller side by
[`RelayBootloaderClient`](https://github.com/przemczan/lightnet-firmware/blob/master/lib/Lightnet/Controller/OTA/RelayBootloaderClient.cpp).

!!! warning "Unvalidated hardware, no read-back verify"
    Builds clean end to end (panel bootloader image + controller driver + `PanelFlasher`) but
    nothing here has been exercised on real silicon — no bench spike has run. There is also no
    post-write flash read-back verification — each chunk's own CRC-16 (checked before it ever
    reaches `boot_page_fill()`) is the integrity guarantee instead; see `RelayBootloaderClient`'s
    class comment for why a read-back pass would need its own new packet type.

### Getting the bootloader

```bash
# Build + burn (one-time, per panel)
pio run -e atmega328pb_bootloader -t fuses    # set fuses (see getting-started.md)
pio run -e atmega328pb_bootloader -t upload   # burn via USBasp

# Hex file is in `.pio/build/atmega328pb_bootloader/firmware.hex`
```

- Lives in the **4 KB boot section** at `0x7000` (ATmega328PB/P)
- Stays resident only when EEPROM byte 510 contains `0xB007` (cleared once read)
- Listens/replies on the one edge that leads back toward the controller — persisted to EEPROM
  by `BootloaderBridge::prepareAndReset()` immediately before the jump, alongside this panel's
  own assigned index (for filtering)

### Entry sequence

```mermaid
sequenceDiagram
  participant C as Controller
  participant P as Panel App
  participant BL as RelayBootloader (0x7000)

  C->>P: PACKET_ENTER_BOOTLOADER (type 201, token 0xB0)
  P->>P: Write assigned index + parent edge + 0xB007 to EEPROM
  P->>P: Disable TWI/PCINT interrupts, zero SRAM 0x0100-0x04FF
  P->>BL: Software jump to 0x7000
  BL->>BL: Read EEPROM magic == 0xB007 -> stay resident, clear magic
  C->>BL: PACKET_BOOTLOADER_PING -> PONG (verify presence)
  loop per 128-byte page (2x 64-byte chunks)
    C->>BL: PACKET_BOOTLOADER_WRITE_CHUNK -> WRITE_ACK
  end
  C->>BL: PACKET_BOOTLOADER_START_APP (fire-and-forget)
  BL->>P: Jump to application
```

Design (hardware redesign plan §8 step 5): only one panel is ever resident in its bootloader at a
time, and every other panel keeps running its normal application — full `PanelDiscovery`/
`PanelRouter`, already discovered and connected. So OTA traffic reaches the target exactly like
any other addressed setup packet already does — flooded downstream through unmodified
intermediate panels, filtered by `targetPanelIndex` at the destination — with **no `PanelRouter`
changes needed anywhere**. The bootloader itself does no relaying, which is also why its own
children are transiently unreachable for the duration of the flash — an accepted cost, not a bug
(see the warning below about the controller restart).

New wire packets (`Core/Common/ProtocolTypes.hpp`, protocol v12): `PACKET_BOOTLOADER_PING`/`PONG`
(presence + chip info), `PACKET_BOOTLOADER_WRITE_CHUNK`/`WRITE_ACK` (64-byte chunks — two per
128-byte flash page, carrying their own CRC-16 since `headerCrc` covers only `PacketHeader`, not
payload), `PACKET_BOOTLOADER_START_APP`. These are a deliberately frozen contract: the resident
bootloader does not validate `protocolVersion` (flashing is how a version mismatch gets resolved),
via `PacketFramer`'s `validateProtocolVersion` constructor parameter.

### How the controller drives it

`ControllerRelayPacketSink::requestReply()` — the same "send a unicast, block for a matching reply
routed back up the trunk" primitive `PanelsController::fetchState()` and the turn-on/off/panel-
configuration acks use (see [Architecture §8.7](architecture.md)) — sends `PACKET_BOOTLOADER_PING`
and `PACKET_BOOTLOADER_WRITE_CHUNK` and waits for `PONG`/`WRITE_ACK`. `PACKET_BOOTLOADER_START_APP`
is fire-and-forget: the panel commits its last page and jumps to the application immediately, so
no reply is ever coming. `PanelFlasher`'s `WAIT_BL`/`FLASHING` states call this through
`RelayBootloaderClient`, going straight from the last page's `WRITE_ACK` to `startApp()` — no
read-back verify pass.

`PanelFlasher` reads `/panel_fw.bin` from LittleFS 128 bytes at a time — the full binary is never
buffered in RAM.

### Flashing order and reboot safety

`PanelFlasher` flashes **leaves first, root last** — `currentPanelAddress()` walks the discovered
panel list back to front. `getPanels()` is ordered by discovery (pre-order DFS: a panel is always
discovered before any of its descendants — a child can only be found by probing through an
already-registered parent), so walking it in reverse visits every panel *after* all of its own
descendants, and the root last of all. This is a deliberate reboot-safety property, not an
arbitrary ordering choice.

**Why order matters at all.** A protocol-version bump means a panel that hasn't been reflashed
yet only accepts discovery/application traffic stamped with the `protocolVersion` it was compiled
with — everything except `PACKET_RESET_DEVICE`/`PACKET_ENTER_BOOTLOADER` is silently rejected on a
mismatch (`Protocol::isVersionExemptType()`). If the **controller** reboots mid-campaign — before
it has been updated itself — rediscovery runs stamped with whatever version the controller
currently has:

- **Root-last (adopted):** the controller and every *not-yet-flashed* panel still share the same
  (old) version throughout the whole campaign, since the controller isn't touched until the very
  end. A mid-campaign reboot's rediscovery walks the tree from the root outward and only stops
  being able to reach a panel once it hits one that's *already* been updated — and by construction
  (leaves → branches → root), everything past that point is also already done. The panels still
  waiting to be flashed are never the ones a reboot can drop out of discovery.
- **Root-first (discovery order — not what this code does):** the opposite happens. The first
  not-yet-flashed panel a rediscovery reaches rejects the newer probe from its now-updated parent,
  and — because discovery can't get past a non-responding edge — **its entire remaining subtree
  disappears from the discovered panel list along with it.** That's exactly the set of panels
  still needing the update, now unreachable over the relay with no recovery path short of
  re-flashing them by hand over ISP.

Ordinary application traffic (animations, scene commands) to already-flashed panels is frozen for
the same version-mismatch reason for the rest of the campaign — cosmetic, not a reachability
problem, and it resolves the moment the controller itself is updated at the end.

!!! note "Not yet built: skip-if-already-at-target-version"
    Every campaign re-flashes every panel from scratch — `PanelFlasher::startFlashing()` always
    restarts at the first (in flash order) panel, and there's currently no way for a panel to
    report which application firmware it's already running, so an interrupted-and-resumed
    campaign harmlessly but redundantly re-writes already-up-to-date panels.
    `PacketBootloaderPong.bootloaderVersion` doesn't help — that's the *bootloader's* own fixed
    version constant, not the application's. Building this would need a version marker embedded
    in the panel binary at a known flash address (the controller side already has an analogous
    `FW_VERSION`, git-describe-based, via `tools/fw_version.py` — panel builds have no equivalent
    today), `RelayBootloader.cpp` reading it back and reporting it, and `PanelFlasher` comparing
    it against the image about to be written before deciding whether to skip straight to
    `startApp()`.

### HTTP upload flow

```mermaid
sequenceDiagram
  participant Client
  participant Controller
  participant LittleFS
  participant Panels

  Client->>Controller: POST /api/firmware/panels (raw .bin body)
  Controller->>LittleFS: Stream to /panel_fw.bin
  Controller->>Client: 200 {"status":"flashing","panels":N}
  loop per panel (main loop)
    Controller->>Panels: ENTER_BOOTLOADER -> flash over the relay
    Client->>Controller: GET /api/firmware/status
    Controller->>Client: {"state":"flashing","panel":K,"total":N,"progress":P}
  end
  Note over Controller: Restart required after all panels flashed
```

!!! warning "Controller restart required"
    After all panels are flashed, **restart the controller** so `PanelsInitializer` can re-run discovery with the new panel firmware.

---

## Serial Firmware Upload (PC → Controller)

`SerialFirmwareReceiver` listens on the existing 57600-baud Serial port. This allows updating panels without WiFi.

### Frame format

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 B | Magic | ASCII `LNFW` — identifies a Lightnet firmware frame |
| 4 | 4 B | Size | Payload length in bytes, little-endian |
| 8 | N B | Data | Raw firmware `.bin` |
| 8+N | 2 B | CRC-16 | CRC-16 of the data bytes, little-endian |

Controller replies:

- `READY\n` — after the header is received
- `OK\n` — after CRC validates and binary is saved to LittleFS
- `ERR:<message>\n` — on any failure

### Usage

```bash
pip install pyserial
python tools/flash_panels_serial.py <port> <firmware.bin>
```

Once the binary is saved the controller starts flashing panels exactly as with the HTTP upload path.

---

## Controller Self-Update (ArduinoOTA)

`ArduinoOTA` is initialised after WiFi connects, on the standard ESP32 port 3232. No password — intended for a trusted local network.

```bash
# Upload directly over WiFi
pio run -e controller_esp32 --target upload --upload-port lightnet-XXXX.local
```

The hostname matches the mDNS name (`lightnet-<chipid>`). The `.local` suffix requires mDNS to be working on the network (standard on most OS setups).

---

## Build Post-Processing

!!! info "Automatic `.bin` generation"
    The post-build script `tools/generate_bin.py` automatically produces both `.hex` and `.bin` for all panel environments. Upload the `.bin` via `POST /api/firmware/panels` — no manual conversion needed.

---

- [API Reference](api.md) — Firmware update endpoints (`/api/firmware/panels`, `/api/firmware/status`)
- [Build & Flash](getting-started.md) — Build commands and initial flashing
- [Troubleshooting](troubleshooting.md) — OTA failure recovery
