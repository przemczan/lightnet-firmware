# External API Reference

The controller exposes two external interfaces on port 80:

- **WebSocket** at `ws://lightnet-<chipid>.local/ws` — binary, low-latency, for real-time panel control and reactive triggers
- **HTTP** at `http://lightnet-<chipid>.local` — JSON, for discovery, appearance control, scene management, and firmware updates

The controller is discoverable via mDNS as `lightnet-<chipid>.local` with service `_lightnet._tcp`.

---

## Table of Contents

1. [WebSocket Binary API](#1-websocket-binary-api)
   - [Packet structure](#11-packet-structure)
   - [Commands](#12-commands)
   - [Responses](#13-responses)
2. [HTTP API](#2-http-api)
   - [Appearance](#21-appearance)
   - [Palettes](#22-palettes)
   - [Scenes](#23-scenes)
   - [Animations](#24-animations)
   - [Firmware updates](#25-firmware-updates)
3. [Error handling](#3-error-handling)

---

## 1. WebSocket Binary API

Connect to `ws://lightnet-<chipid>.local/ws`. All frames are binary. The protocol is defined in `lib/Lightnet/Controller/API/websocket/WebsocketApi.hpp` (namespace `WebsocketApi`).

### 1.1 Packet structure

Every frame has a **fixed header + CRC + payload**:

```
PacketMeta (14 bytes total)
┌──────────────────────────────────────────────────────┐
│ PacketHeader (7 bytes)                                │
│   type            uint8   — packet type enum          │
│   protocolVersion uint16  — must be 0x0001            │
│   nonce           uint32  — micros() at send time     │
├───────────────────────────────────────────────────────┤
│ headerCrc   uint16  — CRC-16 of the 7-byte header     │
│ payloadCrc  uint16  — CRC-16 of the payload bytes     │
│ payloadSize uint16  — byte length of the payload      │
└───────────────────────────────────────────────────────┘
[payload bytes — varies per type]
```

The controller validates all three checks (header CRC, payload CRC, protocol version) and silently drops malformed frames. Use `micros()` or any monotonically increasing value for the nonce — it is not checked for uniqueness.

**CRC algorithm**: CRC-16/IBM (poly `0x8005`, init `0x0000`, no reflection) — same function used throughout the firmware (`Utils/Crc.hpp`).

### 1.2 Commands

Commands are sent **client → controller**. The controller does not reply unless a response is specified.

---

#### TOGGLE (type 1)

Turn a panel's LED on or off.

```
Payload (2 bytes)
  address  uint8   — panel I²C address (panel index assigned during discovery)
  state    uint8   — 1 = on, 0 = off
```

---

#### SET_BRIGHTNESS (type 2)

Set a panel's LED brightness directly.

```
Payload (2 bytes)
  address     uint8  — panel address
  brightness  uint8  — 0 (off) … 255 (full)
```

This bypasses the animation system. Any running animation continues to compute and will overwrite this value on its next frame.

---

#### SET_COLOR (type 3)

Set a panel's LED colour directly.

```
Payload (4 bytes)
  address  uint8  — panel address
  r        uint8
  g        uint8
  b        uint8
```

Same caveat as SET_BRIGHTNESS — running animations overwrite on next tick.

---

#### GET_PANELS_STATES (type 5)

Request the current state (on/off, colour, brightness) of all discovered panels. The controller replies with a PANELS_STATES response on the same client connection.

```
Payload: empty (0 bytes)
```

---

#### GET_EDGES_LIST (type 4)

Request the full edge connectivity graph. The controller replies with an EDGES_LIST response.

```
Payload: empty (0 bytes)
```

---

#### ANIMATION_TRIGGER (type 8) *(scenes branch)*

Fire a low-latency reactive beat trigger. The controller broadcasts `PACKET_ANIMATION_UPDATE_PARAMS` via I²C General Call to all panels running a REACTIVE animation in the specified group. Round-trip from WebSocket frame to panels lighting up is typically under 5 ms.

```
Payload (2 bytes)
  groupId  uint8  — animation group (1–254) to trigger
  value    uint8  — peak level (0–255); higher = brighter flash
```

No response is sent. For music sync at 120 BPM, fire this every 500 ms.

---

### 1.3 Responses

Responses are sent **controller → client** in reply to query commands.

---

#### PANELS_STATES (type 6)

Sent in reply to GET_PANELS_STATES.

```
Header fields: type=6, payloadSize = 2 + N×8

Payload:
  length   uint16  — number of panel state entries (N)
  [repeated N times]
    panelIndex  uint16
    state       uint8   — 1 = on, 0 = off
    color_r     uint8
    color_g     uint8
    color_b     uint8
    brightness  uint8
```

---

#### EDGES_LIST (type 7)

Sent in reply to GET_EDGES_LIST.

```
Header fields: type=7, payloadSize = 2 + N×8

Payload:
  length   uint16  — total number of edge entries (N)
  [repeated N times]
    panelIndex          uint16  — source panel
    edgeIndex           uint16  — edge slot on that panel (0–2)
    connectedPanelIndex uint16  — peer panel (0 if unconnected)
    connectedEdgeIndex  uint16  — peer edge slot (0 if unconnected)
```

---

### 1.4 Sending a command — worked example

To turn panel 3 on:

1. Build `PacketHeader`: `type=1`, `protocolVersion=0x0001`, `nonce=<micros()>`
2. Compute `headerCrc = crc16(&header, 7)`
3. Build payload: `address=3`, `state=1` (2 bytes)
4. Compute `payloadCrc = crc16(payload, 2)`
5. Assemble `PacketMeta + payload` (14 + 2 = 16 bytes total)
6. Send as a binary WebSocket frame

---

## 2. HTTP API

All endpoints return `application/json`. Successful mutations return `200 {}`. All ports are 80.

### 2.1 Appearance

*(scenes branch — see [animations.md](animations.md) for full semantics)*

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/appearance` | — | `{"brightness":N,"baseColors":["#..","#..","#.."],"palette":"..."}` |
| `PUT` | `/api/appearance` | Any subset of the three fields | `{}` |
| `GET` | `/api/brightness` | — | `{"value":N}` |
| `PUT` | `/api/brightness` | `{"value":128}` | `{}` |
| `GET` | `/api/colors` | — | `{"primary":"#..","secondary":"#..","tertiary":"#.."}` |
| `PUT` | `/api/colors` | Any subset of the three slots | `{}` |
| `GET` | `/api/palette` | — | `{"palette":"lava"}` |
| `PUT` | `/api/palette` | `{"palette":"lava"}` | `{}` |

All `PUT` endpoints persist to `/config/appearance.json` atomically and broadcast the updated value to all panels immediately.

---

### 2.2 Palettes

*(scenes branch)*

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/palettes` | — | `["rainbow","lava","ocean",...]` |
| `GET` | `/api/palettes/:name` | — | Palette JSON (synthesized from stored stops; `"userColors"` is built from current base colours) |
| `POST` | `/api/palettes` | Palette JSON | `{}` |
| `DELETE` | `/api/palettes/:name` | — | `403` if built-in |

Palette JSON format:
```json
{
  "schemaVersion": 1,
  "name": "my-palette",
  "stops": [[0,"#000000"],[128,"#FF4400"],[255,"#FFFFFF"]]
}
```

---

### 2.3 Scenes

*(scenes branch — see [animations.md](animations.md) for the full scene JSON schema)*

#### Scene library

| Method | Path | Body | Response |
|---|---|---|---|
| `POST` | `/api/scenes` | Scene JSON | `{}` — saves to `/scenes/<name>.json` |
| `GET` | `/api/scenes` | — | `[{"name":"sunset","size":412},...]` |
| `GET` | `/api/scenes/:name` | — | Scene JSON (file passthrough) |
| `DELETE` | `/api/scenes/:name` | — | `{}` |

Scene names: 1–18 chars, `[a-zA-Z0-9_-]`. (18-char limit keeps the SPIFFS path `/scenes/<name>.json` within the 31-character filesystem limit.)

#### Playback

| Method | Path | Body | Response |
|---|---|---|---|
| `POST` | `/api/scenes/play` | Full scene JSON body (inline play, not saved) | `{}` |
| `POST` | `/api/scenes/:name/play` | — (plays stored scene by name) | `{}` |
| `POST` | `/api/scenes/stop` | — | `{}` |
| `GET` | `/api/scenes/status` | — | See below |

Status response while playing:
```json
{"playing": true, "scene": "sunset", "loop": true, "layers": 2}
```

Status when idle: `{"playing": false}`

---

### 2.4 Animations

*(scenes branch)*

#### One-shot animation

Plays a single-layer animation directly without saving to SPIFFS. Use this for notifications that overlay an ambient scene on a free group ID.

The body is a **flat object** — step fields (`type`/`runner`, `color`, `duration`, `params`, etc.) sit at the root level alongside `group` and `panels`. There is no `"sequence"` wrapper.

```http
POST /api/animations/play
Content-Type: application/json

{
  "group": 250,
  "panels": "all",
  "type": "PULSE",
  "color": "#FF0000",
  "brightnessFrom": 0,
  "brightnessTo": 255,
  "duration": 600,
  "params": [64, 128, 64]
}
```

For chained steps (e.g. pulse → fade-out), use `POST /api/scenes/play` with a full scene body containing one layer with the desired sequence, and a free group ID.

Response: `200 {}`

#### Reactive trigger (HTTP alternative to WebSocket)

```http
POST /api/animations/trigger
Content-Type: application/json

{"group": 1, "value": 200}
```

Response: `200 {}`

Use the WebSocket `ANIMATION_TRIGGER` (type 8) instead when latency matters — HTTP adds ~5 ms compared to the sub-millisecond path through the WebSocket handler.

---

### 2.5 Firmware updates

These endpoints exist on all controller builds (master and scenes branches).

#### Upload and flash panel firmware

```http
POST /api/firmware/panels
Content-Type: application/octet-stream
[raw binary body — panel firmware .bin file]
```

The body is streamed directly to SPIFFS as `/panel_fw.bin` to avoid buffering in RAM. Once the upload completes, the controller starts flashing panels over I²C one by one in discovery order.

**Responses:**

| Code | Body | Meaning |
|---|---|---|
| `200` | `{"status":"flashing","panels":N}` | Upload accepted, flashing started |
| `409` | `{"error":"flash already in progress"}` | Another flash is already running |
| `422` | `{"error":"<msg>"}` | SPIFFS write failure or other error |
| `507` | `{"error":"SPIFFS write failed"}` | Filesystem full or unavailable |

After flashing all panels, the **controller must be restarted** so `PanelsInitializer` can re-run discovery with the new panel firmware.

#### Flash status

```http
GET /api/firmware/status
```

```json
{"state": "flashing", "panel": 3, "total": 12, "progress": 45}
```

State values:

| `state` | Meaning |
|---|---|
| `"idle"` | No flash in progress |
| `"connecting"` | Entering bootloader on current panel |
| `"flashing"` | Writing firmware pages |
| `"verifying"` | Reading back to verify |
| `"done"` | All panels flashed successfully |
| `"error"` | Flash failed — see `"error"` field |

On error, the response includes an additional `"error"` string field.

---

## 3. Error Handling

### HTTP

| Code | Meaning |
|---|---|
| `200 {}` | Success |
| `404 {"error":"not_found"}` | Named scene or palette does not exist |
| `409 {"error":"..."}` | Conflict — flash already running, or scene schema version too new for this firmware |
| `422 {"error":"..."}` | Validation failure — the request body is invalid; no state was changed |
| `403 {"error":"..."}` | Operation not permitted (e.g. deleting a built-in palette) |
| `500 {"error":"..."}` | SPIFFS read/write failed |
| `507 {"error":"..."}` | Insufficient storage (firmware upload endpoint only) |

### WebSocket

The controller silently drops frames that fail validation (size, CRC, version). There is no error response. If a command has no visible effect, verify:
1. `protocolVersion` is `0x0001`
2. Header CRC covers the first 7 bytes (`type` + `protocolVersion` + `nonce`)
3. Payload CRC covers only the payload bytes (not `PacketMeta`)
4. Panel address is a valid panel index (0-based, returned by GET_PANELS_STATES)
