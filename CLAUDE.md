# CLAUDE.md

Guidance for Claude Code working in this repository.

---

## What this project is

Lightnet is embedded firmware for a tree network of addressable-LED panels. A single ESP8266/ESP32 **controller** discovers and drives up to 100 **panels** (ATmega328) over I²C. The controller exposes WiFi APIs; panels run animations locally after a single setup packet.

Two distinct binaries are compiled from one source tree. `LIGHTNET_TARGET_CONTROLLER` (set in `platformio.ini`) selects the target; the preprocessor eliminates the unused half entirely.

---

## Docs

| Document | Contents |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | Physical topology, library structure, I²C protocol, animation framework internals, discovery sequence, controller boot |
| [`docs/firmware.md`](docs/firmware.md) | PlatformIO environments, pin assignments, panel OTA (twiboot), serial upload, controller ArduinoOTA, debugging |
| [`docs/api.md`](docs/api.md) | WebSocket binary protocol (WebsocketApi) + all HTTP endpoints, request/response format |
| [`docs/animations/scene-authoring.md`](docs/animations/scene-authoring.md) | **Scene authoring guide** — every scene/layer/step prop, topology, panel targeting (selectors/tags), directionality (`source`), colours/palettes, logical root, and an example-scene library |
| [`docs/animations/`](docs/animations/index.md) | Animation reference: [`concepts`](docs/animations/concepts.md) (model/palettes/timing), [`types`](docs/animations/types.md) (per-type & runner params), [`api`](docs/animations/api.md) (HTTP/WS + examples) |
| [`docs/testing.md`](docs/testing.md) | Native host-side unit tests, what's covered, how to add new tests, MinGW setup |

---

## Build quick-reference

```bash
pio run -e controller_wemos              # build controller (Wemos D1 Mini)
pio run -e controller_wemos -t upload    # build + upload via USB
pio run -e panel_atmega328pb -t upload    # build + upload panel via USBasp
pio run -e controller_wemos -t upload --upload-port lightnet-XXXX.local  # OTA
pio device monitor -e controller_wemos   # serial monitor (57600 baud)
```

Environments: `controller_esp8266` / `controller_wemos` / `controller_esp32` for the controller; `panel_nanoatmega328` / `panel_atmega328pb` / `panel_atmega328p` for panels. See [`docs/firmware.md`](docs/firmware.md#1-build-environments) for full details.

## Tests

Native host-side unit tests cover the pure C++ utilities (no Arduino, no hardware). Run via PlatformIO:

```bash
pio test -e native                       # all suites
pio test -e native -f test_simplejson    # single suite
```

On Windows, MinGW GCC must be on `PATH` (typically `C:\msys64\mingw64\bin`).

Current suites: `test_simplejson`, `test_http_url`, `test_palette_parser`, `test_panel_graph`, `test_topology`, `test_panel_selector`, `test_panel_selector_parser`, `test_panel_field`, `test_panel_geometry`, `test_runner_math`, `test_compositor`, `test_panel_anim`, `test_spsc_queue`. When fixing a bug in a pure-logic module, add a regression test under `test/test_*/test_main.cpp`. See [`docs/testing.md`](docs/testing.md) for what's testable natively vs. what needs a device.

---


## API changes

Whenever you add, remove, or rename an HTTP endpoint, update **all** of the following:

- `openapi.json` — path keys and any referenced URLs
- `docs/api.md` — endpoint table
- `docs/architecture.md` — server route summary table
- `docs/animations/api.md` — if the endpoint is appearance/animation-related
- Any inline doc comments in source that reference the path (e.g. `AppearanceStore.hpp`)
- `lightnet-mobile` client (`LightnetHttpClient.kt`) if the endpoint is consumed there

---

## Live device tools

### api-shell (`tools/api-shell/`)

Node.js WebSocket client for sending commands to a live controller. Run interactively:

```bash
cd tools/api-shell && node api-shell.js <controller-ip>
```

For one-shot queries from Claude Code, inline the protocol directly — `node -e "..."` from `tools/api-shell/` (so `require('ws')` resolves). The CRC is **CRC-16/IBM** (reflected, poly `0xA001`, init `0xFFFF`). Nonce must be `Date.now() % 0x100000000` (not raw `Date.now()` — overflows `writeUInt32LE`).

Packet types: `TOGGLE=1`, `SET_BRIGHTNESS=2`, `SET_COLOR=3`, `GET_EDGES_LIST=4`, `GET_PANELS_STATES=5`, `PANELS_STATES=6`, `EDGES_LIST=7`.

Response payload for `EDGES_LIST`: `u16 count` followed by `count × 8 bytes` (`panel u16`, `edge u16`, `connectedPanel u16`, `connectedEdge u16`). `connectedPanel=0` means unconnected.

### mirror-dump.js (`tools/api-shell/mirror-dump.js`)

Diagnostic tool that connects to a live controller and prints a summary of each incoming `MIRROR_BATCH` frame — how many records of each I²C type arrived per batch, plus which panel addresses received `PREPARE` and `START` packets.

```bash
cd tools/api-shell && node mirror-dump.js <controller-ip> [seconds]
# default capture window: 15 s
```

Use this to verify the mirror pipeline is alive, check that general-call START packets are arriving (addr=0), and confirm that panel-local animations (PREPARE+START) and runner animations (SET_COLOR) are both being captured.

---

## Packet mirroring (live preview)

The controller captures every outbound I²C packet into `PacketMirror`. Clients that opt in receive `MIRROR_BATCH` WebSocket frames so the mobile app can render a real-time preview without polling.

### Firmware side

| File | Role |
|---|---|
| `lib/Lightnet/Controller/API/websocket/PacketMirror.cpp/.hpp` | Captures records into a live-stream ring and a persistent snapshot; `flushTo()` broadcasts the ring, `flushSnapshotTo()` unicasts the snapshot to one client |
| `src/controller/main.cpp` — `mirrorOutboundPacket()` | Plain function registered via `LNBus.setOnPacketSent()`; forwards every outbound packet to `PacketMirror::capture()` |
| `src/controller/MirrorService.hpp` / `serviceMirror()` in `main.cpp` | Flush gate (~30 fps); also drains `pendingSnapshotClientId` to unicast the snapshot to newly-enabled clients |
| `lib/Lightnet/Controller/API/websocket/WebsocketServer` — `ClientSettings` | Per-client `mirroringEnabled` flag; registered on connect, cleared on disconnect |

**Opt-in streaming**: mirroring is disabled by default for each client connection. The client sends `SET_MIRROR(enabled=1)` to start receiving `MIRROR_BATCH` frames, and `SET_MIRROR(enabled=0)` to stop. This keeps bandwidth and CPU usage zero for clients that don't need live preview (e.g. a control-only UI).

**Snapshot on enable**: `PacketMirror` maintains a second buffer alongside the live ring — a snapshot of the last-seen packet for each `(address, type[, group_id])` combination. When a client enables mirroring, a one-shot `MIRROR_BATCH` containing the snapshot is unicast to that client before the live stream begins, so the preview is correct immediately rather than waiting for the next full animation cycle.

Snapshotted types: `PACKET_SET_GLOBAL_BRIGHTNESS`, `PACKET_SET_BASE_COLORS`, `PACKET_SET_PALETTE`, `PACKET_TURN_ON_OFF`, `PACKET_ANIMATION_PREPARE`, `PACKET_ANIMATION_START`. `PACKET_SET_COLOR` is not snapshotted (60 fps stream, self-heals within one live frame).

**Power-off / power-on**: `PacketMirror::clearSnapshot()` is called when the controller turns off, so stale animation state is not replayed to clients that connect while it is off. On power-on, `AnimationService::resumeScene()` restarts the last-loaded scene from the beginning using data preserved in `ScenePlayer` (all scene state survives `stop()` in memory; `lCount > 0` is the resume guard).

**Sim mode**: `LightnetBusSim.cpp::sendPacket()` invokes `onPacketSentCallback` so the mirror pipeline works identically in SIM_MODE — outbound packets reach `PacketMirror::capture()` and the mobile live preview works without real hardware.

**Wire format of `MIRROR_BATCH` payload**:
```
u32 controllerMillis   (LE)
u16 count
count × { u8 address, u8 type, u8 size, u8[size] packet }
```
`address=0` means general call (all panels). `type` is `Protocol::packetType_t`. `packet` includes the full `PacketMeta` header (5 bytes: type + protocolVersion + headerCrc).

**Key**: all records in one flush share a single `controllerMillis` timestamp. Runner animations (SET_COLOR per panel at 60fps) and panel-local animations (PREPARE + START once) both go through the same path.

---

## Scene portability (topology selectors, directionality, tags)

Scenes pick panels and give runners direction by **resolving against the discovered panel tree
at play time**, so one scene adapts to devices with different panel counts/wiring. Authoring:
[`docs/animations/scene-authoring.md`](docs/animations/scene-authoring.md) (topology §2,
selectors §6, directionality §8, logical root/tags §10).

**Pure, natively-tested core** (`lib/Lightnet/Controller/Topology/`, no Arduino):

| File | Role |
|---|---|
| `TopologyIndex.hpp` | Rooted view of the tree (depth, parent, leaf/branch, subtree, neighbours, canonical order, multi-source hop-distance). Built from a generic link list; **parameterised by a start node** → re-rooting is just a rebuild. |
| `PanelSelector.hpp` + `PanelSelectorParser.hpp` | The `panels` grammar (`all`/indices/`exclude`, graph selectors `root`/`leaves`/`depth:a-b`/`subtree:N`/`fraction`/…, `tag:<name>`, `any`/`all`/`not`) compiled to a compact RPN program; `resolveSelector(sel, topo, out, ITagResolver*)`. |
| `PanelField.hpp` | Runner directionality: per-panel **hop-distance coord** from a `source` (`root`/`leaves`/`panel:N`, `reverse`). Envelopes are pure in `Animations/RunnerMath.hpp`. |
| `PanelGeometry.hpp` | **Geometric** runner directionality (`source:geometric` + `angle`): planar (x,y) layout of the tree from regular-polygon geometry (faithful port of mobile `PanelsLayoutService`, anchored at the lowest panel index = visualizer frame), then `computeGeometricField()` projects centroids onto the chosen axis. No protocol change. |
| `TagResolver.hpp` | `ITagResolver` + the single `isValidTagName`/`TAG_NAME_MAX` shared by parser, store, and endpoint. |

**Device side**: `Topology/TopologyConfigStore` persists `/config/topology.json` (logical root +
panel tags) and is the `ITagResolver`. `Scenes/SceneTopology` owns the panel-tree views
(`PanelGraph` + `TopologyIndex` + `PanelGeometry`), the logical root, the tag resolver, and
selector resolution (`rebuild()` + `resolvePanels()`). `ScenePlayer` holds one `SceneTopology`
(rebuilt in `loadAndPlay`/`resume`),
delegates `setLogicalRoot()` / `setTagResolver()` to it, and reads its views in `fireStep`. Endpoints
live in `API/http/TopologyServer` (`GET /api/topology`, `PUT /api/topology/root`,
`GET/PUT /api/panel-tags`), wired in `main.cpp` case 0.

- **Controller-side, no protocol change**: runners (incl. directionality math) run on the ESP in
  float — they are **not** part of the shared `Core/Anim` panel-local animation math (which mobile
  also runs via the C ABI). `N`/edges/adjacency come from existing discovery data.
- **Backward compatible**: v2 `panels` forms map onto the RPN; legacy `originPanel` → `source:panel:N`;
  v2 WAVE/CHASE default to `source:root` (slight visual change — design §6.4). `source:geometric`
  requires `schemaVersion: 3` (current `SCENE_SCHEMA_VERSION`); `SceneTopology::rebuild()` builds the
  `PanelGeometry` alongside `topo`, and `ScenePlayer::fireStep` branches on it.
- Native suites: `test_panel_graph`, `test_topology`, `test_panel_selector`, `test_panel_selector_parser`,
  `test_panel_field`, `test_panel_geometry`, `test_runner_math`.

---

## Key facts for coding

- **Single entry point**: `src/main.cpp` — includes `controller/main.hpp` or `panel/main.hpp` based on `LIGHTNET_TARGET_CONTROLLER`.
- **I²C protocol version**: v6 (`VERSION` constant in `Common/Protocol.hpp`). Changing the protocol **requires flashing both controller and all panels together**.
- **`animScheduler->tick(millis())`** must be called in the main loop `case 1`.
- **LittleFS** is mounted in `case 0` before the WiFi captive portal starts, so `AppearanceStore` can read `/config/appearance.json`.

- **`BOOTLOADER_ENTRY_TOKEN = 0xB0`** — both sides of `PACKET_ENTER_BOOTLOADER` must agree on this value. Do not send `CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER` (bytes `0x01 0x00`) to the twiboot fork — it WDT-resets the panel.
- **`busIsDisabled` in `LightnetPinger` is static (shared)** — set while any ping pulse is being driven so all pingers drop ISR samples during that window, preventing self-detection.
- **Debug macros** (`PRINTLN`, `PRINTKV`, `PRINTF`) are no-ops when `DEBUG=0`. Serial baud is 57600 everywhere.
