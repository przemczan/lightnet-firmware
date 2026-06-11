---
icon: material/api
---

# External API Reference

The controller exposes two external interfaces on port 80:

- **WebSocket** at `ws://lightnet-<chipid>.local/ws` — binary, low-latency, for real-time panel control and reactive triggers
- **HTTP** at `http://lightnet-<chipid>.local` — JSON, for discovery, appearance control, scene management, and firmware updates

The controller is discoverable via mDNS as `lightnet-<chipid>.local` with service `_lightnet._tcp`.

!!! tip "Choosing the right interface"
    Use **HTTP** for setup, configuration, and state queries. Use **WebSocket** for interactive control and reactive beat triggers — it cuts round-trip latency from ~5 ms to sub-millisecond.

---

## Table of Contents

1. [WebSocket Binary API](#1-websocket-binary-api)
   - [Packet structure](#11-packet-structure)
   - [Commands](#12-commands) — TOGGLE (1), SET_COLOR (3), GET_PANELS_STATES (5), GET_EDGES_LIST (4), ANIMATION_TRIGGER (8), SET_MIRROR (10), PING (11)
   - [Responses](#13-responses) — PANELS_STATES (6), EDGES_LIST (7), MIRROR_BATCH (9), PONG (12)
   - [Sending a command — worked example](#14-sending-a-command-worked-example)
2. [HTTP API](#2-http-api)
   - [Appearance](#21-appearance)
   - [Palettes](#22-palettes)
   - [Scenes](#23-scenes)
   - [Animations](#24-animations)
   - [Firmware updates](#25-firmware-updates)
   - [Panels](#26-panels)
   - [Topology](#27-topology-logical-root-panel-tags)
   - [Configuration](#28-configuration)
   - [State](#29-state)
3. [Error handling](#3-error-handling)

---

## 1. WebSocket Binary API

Connect to `ws://lightnet-<chipid>.local/ws`. All frames are binary. The protocol is defined in `lib/Lightnet/Controller/API/websocket/WebsocketApi.hpp` (namespace `WebsocketApi`).

### 1.1 Packet structure

Every frame has a **fixed 14-byte header** (`PacketMeta`) followed by a variable payload.

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 1 B | `type` | uint8 | Packet type (see commands / responses below) |
| 1 | 2 B | `protocolVersion` | uint16 | Must be `0x0001` |
| 3 | 4 B | `nonce` | uint32 | `micros()` at send time — used for ordering |
| 7 | 2 B | `headerCrc` | uint16 | CRC-16 of bytes 0–6 |
| 9 | 2 B | `payloadCrc` | uint16 | CRC-16 of the payload bytes |
| 11 | 2 B | `payloadSize` | uint16 | Payload length in bytes |
| 13+ | N B | payload | — | Varies per type |

Bytes 0–6 (`type` + `protocolVersion` + `nonce`) form the inner `PacketHeader` struct that is covered by `headerCrc`.

**CRC algorithm**: CRC-16/IBM (reflected, poly `0xA001`, init `0xFFFF`) — same function throughout the firmware (`Utils/Crc.hpp`).

!!! warning "Malformed frames are silently dropped"
    The controller validates header CRC, payload CRC, and protocol version. There is no error response — if a command has no effect, check these three fields first.

### 1.2 Commands

Commands are sent **client → controller**. The controller does not reply unless a response is specified.

---

#### TOGGLE (type 1)

Turn a panel's LED on or off.

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 1 B | `address` | uint8 | Panel index assigned during discovery |
| 1 | 1 B | `state` | uint8 | `1` = on, `0` = off |

---

#### SET_COLOR (type 3)

Set a panel's LED colour directly.

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 1 B | `address` | uint8 | Panel index |
| 1 | 1 B | `r` | uint8 | Red channel |
| 2 | 1 B | `g` | uint8 | Green channel |
| 3 | 1 B | `b` | uint8 | Blue channel |

Running animations overwrite this on the next tick.

---

#### GET_PANELS_STATES (type 5)

Request the current state (on/off, colour) of all discovered panels. The controller replies with a PANELS_STATES response on the same client connection.

No payload.

---

#### GET_EDGES_LIST (type 4)

Request the full edge connectivity graph. The controller replies with an EDGES_LIST response.

No payload.

---

#### ANIMATION_TRIGGER (type 8)

Fire a low-latency reactive beat trigger. The controller broadcasts `PACKET_ANIMATION_UPDATE_PARAMS` via I²C General Call to all panels running a REACTIVE animation in the specified group. Round-trip from WebSocket frame to panels lighting up is typically under 5 ms.

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 1 B | `groupId` | uint8 | Animation group to trigger (1–254) |
| 1 | 1 B | `value` | uint8 | Peak level — `0` = off, `255` = full brightness flash |

No response is sent. For music sync at 120 BPM, fire this every 500 ms.

---

#### SET_MIRROR (type 10)

Enable or disable `MIRROR_BATCH` streaming for this client connection. Mirroring is **off by default** — the controller only sends `MIRROR_BATCH` frames to clients that have explicitly opted in.

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 1 B | `enabled` | uint8 | `1` = enable streaming, `0` = disable |

When `enabled=1`, the controller immediately unicasts a **state snapshot** — a single `MIRROR_BATCH` containing the last-seen packet for each panel/type combination — before the live stream begins. This brings the client's visualizer to the correct current state without waiting for the next animation cycle.

When `enabled=0`, the controller stops sending `MIRROR_BATCH` frames to this client, saving network bandwidth and send-queue capacity.

No response is sent.

---

#### PING (type 11)

Liveness check. The controller replies immediately with a `PONG` on the same connection. Clients use this to confirm a WebSocket connection that reports as open is actually being served by a live controller — useful for periodic "is this device online" checks, since a TCP connection can remain in a connected state after the remote end has gone silent.

No payload.

---

### 1.3 Responses

Responses are sent **controller → client** in reply to query commands.

---

#### PANELS_STATES (type 6)

Sent in reply to GET_PANELS_STATES. `payloadSize = 2 + N×7`.

**Payload header (2 bytes):**

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 2 B | `length` | uint16 | Number of panel state entries (N) |

**Per entry × N (7 bytes each):**

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 2 B | `panelIndex` | uint16 | Panel index |
| 2 | 1 B | `state` | uint8 | `1` = on, `0` = off |
| 3 | 1 B | `color_r` | uint8 | Red channel |
| 4 | 1 B | `color_g` | uint8 | Green channel |
| 5 | 1 B | `color_b` | uint8 | Blue channel |

---

#### EDGES_LIST (type 7)

Sent in reply to GET_EDGES_LIST. `payloadSize = 2 + N×8`.

**Payload header (2 bytes):**

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 2 B | `length` | uint16 | Total number of edge entries (N) |

**Per entry × N (8 bytes each):**

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 2 B | `panelIndex` | uint16 | Source panel index |
| 2 | 2 B | `edgeIndex` | uint16 | Edge slot on that panel (0–2) |
| 4 | 2 B | `connectedPanelIndex` | uint16 | Peer panel index (`0` if unoccupied) |
| 6 | 2 B | `connectedEdgeIndex` | uint16 | Peer edge slot (`0` if unoccupied) |

---

#### MIRROR_BATCH (type 9)

Streamed **controller → client** at up to ~30 fps once the client has sent `SET_MIRROR(1)`. Each frame is a coalesced batch of all outbound I²C packets captured since the previous flush. The mobile app uses these to drive its per-panel `AnimationPlayer` for real-time preview.

**Payload header (6 bytes):**

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 4 B | `controllerMillis` | uint32 | `millis()` on the controller at flush time (LE) |
| 4 | 2 B | `count` | uint16 | Number of records that follow (LE) |

**Per record × count:**

| Offset | Size | Field | Type | Description |
|---|---|---|---|---|
| 0 | 1 B | `address` | uint8 | I²C target panel index; `0` = General Call (all panels) |
| 1 | 1 B | `type` | uint8 | `Protocol::packetType_t` value |
| 2 | 1 B | `size` | uint8 | Byte length of `packet` |
| 3 | N B | `packet` | bytes | Raw packet including 5-byte `PacketMeta` header |

All records in one frame share the same `controllerMillis` timestamp. The first frame received after enabling mirroring is a **state snapshot** (last-seen value per panel/type) rather than a live-stream flush — process it identically.

---

#### PONG (type 12)

Sent immediately in reply to `PING`, on the same connection.

No payload.

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

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/appearance` | — | `{"brightness":N,"baseColors":["#..","#..","#.."],"palette":"..."}` |
| `PATCH` | `/api/appearance` | Any subset of the three fields | `{}` |

Persists to `/config/appearance.json` atomically and broadcasts the updated value to all panels immediately.

---

### 2.2 Palettes

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/palettes` | — | `{"rainbow":{...},"lava":{...},...}` — map of name → Palette JSON |
| `GET` | `/api/palettes/:name` | — | Palette JSON |
| `POST` | `/api/palettes` | Palette JSON | `{}` |
| `DELETE` | `/api/palettes/:name` | — | `403` if built-in |

Palette JSON format:
```json
{
  "schemaVersion": 1,
  "name": "my-palette",
  "stops": [
    [0, "#000000"],
    [128, "#FF4400"],
    [255, "#FFFFFF"]
  ]
}
```

---

### 2.3 Scenes

#### Scene library

| Method | Path | Body | Response |
|---|---|---|---|
| `POST` | `/api/scenes` | Scene JSON | `{}` — saves to `/scenes/<name>.json` |
| `GET` | `/api/scenes` | — | `[{"name":"sunset","size":412},...]` |
| `GET` | `/api/scenes/:name` | — | Scene JSON (file passthrough) |
| `DELETE` | `/api/scenes/:name` | — | `{}` |

Scene names: 1–18 chars, `[a-zA-Z0-9_-]`.

#### Playback

| Method | Path | Body | Response |
|---|---|---|---|
| `POST` | `/api/scenes/play` | Full scene JSON body — stored under the reserved name `Current`, then played by name | `{}` |
| `POST` | `/api/scenes/:name/play` | — (plays stored scene by name) | `{}` |
| `POST` | `/api/scenes/stop` | — | `{}` |
| `POST` | `/api/scenes/speed` | `{"speed": <float>}` — set playback speed multiplier [0.1, 10.0] | `{"ok":true,"speed":2.0}` |
| `GET` | `/api/scenes/status` | — | See below |

Status response while playing:
```json
{
  "playing": true,
  "scene": "sunset",
  "loop": true,
  "layers": 2,
  "speed": 1.0
}
```

Status when idle: `{"playing": false}`

---

### 2.4 Animations

#### One-shot animation

Plays a single-layer animation directly without saving to disk. Use this for notifications that overlay an ambient scene on a free group ID.

The body is a **flat object** — step fields (`type`/`runner`, `color`, `duration`, `params`, etc.) sit at the root level alongside `group` and `panels`. There is no `"sequence"` wrapper.

```http
POST /api/animations/play
Content-Type: application/json

{
  "group": 250,
  "panels": "all",
  "type": "PULSE",
  "colorFrom": "#000000",
  "colorTo": "#FF0000",
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

`group` accepts a numeric ID **or** the string name used in the scene JSON (e.g. `"group": "layer5"`). The name is resolved against the currently playing scene; `group_not_found` is returned if no loaded layer has that name.

Response: `200 {}`

!!! tip
    Use the WebSocket `ANIMATION_TRIGGER` (type 8) when latency matters — HTTP adds ~5 ms compared to the sub-millisecond path through the WebSocket handler.

---

### 2.5 Firmware updates

#### Upload and flash panel firmware

```http
POST /api/firmware/panels
Content-Type: application/octet-stream
[raw binary body — panel firmware .bin file]
```

The body is streamed directly to `/panel_fw.bin` to avoid buffering in RAM. Once the upload completes, the controller starts flashing panels over I²C one by one in discovery order.

**Responses:**

| Code | Body | Meaning |
|---|---|---|
| `200` | `{"status":"flashing","panels":N}` | Upload accepted, flashing started |
| `409` | `{"error":"flash already in progress"}` | Another flash is already running |
| `422` | `{"error":"<msg>"}` | Write failure or other error |
| `507` | `{"error":"filesystem write failed"}` | Filesystem full or unavailable |

!!! warning "Restart required after flashing"
    After flashing all panels, the **controller must be restarted** so `PanelsInitializer` can re-run discovery with the new panel firmware.

#### Flash status

```http
GET /api/firmware/status
```

```json
{
  "state": "flashing",
  "panel": 3,
  "total": 12,
  "progress": 45
}
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

---

### 2.6 Panels

Direct per-panel control. These endpoints bypass the animation system — any running animation continues to compute and will overwrite the values on its next frame.

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/panels` | — | `[{"address":N,"on":true,"color":"#RRGGBB"},...]` |
| `GET` | `/api/panels/edges` | — | `[{"panel":N,"edge":N,"connectedPanel":N,"connectedEdge":N},...]` |
| `PUT` | `/api/panels/:address/on` | `{"value":1}` | `{}` |
| `PUT` | `/api/panels/:address/color` | `{"color":"#FF0000"}` | `{}` |

`:address` is the panel's I²C index as returned by `GET /api/panels`.

`GET /api/panels` fetches the live state of each discovered panel over I²C. Panels that do not respond are omitted from the array. `connectedPanel` and `connectedEdge` in the edges response are `0` when an edge slot is unoccupied.

These are the HTTP equivalents of the WebSocket `TOGGLE`, `SET_COLOR`, `GET_PANELS_STATES`, and `GET_EDGES_LIST` commands.

---

### 2.7 Topology (logical root + panel tags)

Per-device topology configuration that scene **panel selectors** resolve against (see [Scene Authoring → Targeting panels](animations/scene-authoring.md#6-targeting-panels-the-panels-field) and [§10 Per-device topology config](animations/scene-authoring.md#10-per-device-topology-config-logical-root--tags)). Stored in `/config/topology.json`, written atomically.

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/topology` | — | `{"logicalRoot":5,"tags":{"1":["accent"],"5":["accent"]}}` |
| `PUT` | `/api/topology/root` | `{"logicalRoot":N}` (1–255, or 0 to reset to the physical root) | `{"ok":true,"logicalRoot":N}` |
| `GET` | `/api/panel-tags` | — | `{"1":["accent","left"],"5":["accent"]}` |
| `PUT` | `/api/panel-tags` | tag map (whole-map replace) | `{}` |

- **Logical root** re-centres the rooted topology view (`depth`/`subtree`/canonical order and the default runner `source:root`). Setting it persists *and* restarts a playing scene so the new rooting applies immediately. A `logicalRoot` that doesn't exist on this device falls back to the physical root.
- **Tags** are validated `[a-zA-Z0-9_-]`, 1–15 chars; a scene targets them with `"panels":"tag:<name>"`. `PUT /api/panel-tags` replaces the entire map.

---

### 2.8 Configuration

Persistent app-level settings. Stored in `/config/configuration.json` and written atomically with a 5-second deferred-write window.

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/configuration` | — | `{"powerStateOnBoot":0}` |
| `PATCH` | `/api/configuration` | `{"powerStateOnBoot":int}` | `{}` |

**`powerStateOnBoot`** — controls the global power state after a reboot:

| Value | Constant | Behavior |
|---|---|---|
| `0` | `POWER_ALWAYS_ON` (default) | Always boot with panels on |
| `1` | `POWER_ALWAYS_OFF` | Always boot with panels off |
| `2` | `POWER_LAST_STATE` | Restore the last persisted `isOn` value |

Values outside `0–2` return `422`.

---

### 2.9 State

Runtime app state — power state and the most recently played scene's name. Persisted in
`/config/app_state.json` with a 5-second deferred-write window. The initial `isOn` value on boot
is derived from `powerStateOnBoot` (see §2.7); `lastPlayedScene` is set whenever a scene is played
via `POST /api/scenes/play` or `POST /api/scenes/:name/play` (inline plays are recorded under the
reserved name `Current`, see §2.3 Scenes).

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/api/state` | — | `{"isOn":true,"lastPlayedScene":"sunset"}` |
| `POST` | `/api/state/power` | `{"isOn":bool}` | `{"isOn":bool}` |

- Setting `isOn: false` stops all animations, clears panel animation queues, and turns all panels off. Scene and animation play endpoints return `409 system_off` while off.
- Setting `isOn: true` turns all panels back on and re-broadcasts the current appearance (brightness, palette, base colors).

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
| `500 {"error":"..."}` | Filesystem read/write failed |
| `507 {"error":"..."}` | Insufficient storage (firmware upload endpoint only) |

### WebSocket

!!! warning "Silent drops"
    The controller silently drops frames that fail validation (size, CRC, version). There is no error response. If a command has no visible effect, verify:

    1. `protocolVersion` is `0x0001`
    2. Header CRC covers the first 7 bytes (`type` + `protocolVersion` + `nonce`)
    3. Payload CRC covers only the payload bytes (not `PacketMeta`)
    4. Panel address is a valid panel index (0-based, returned by GET_PANELS_STATES)
