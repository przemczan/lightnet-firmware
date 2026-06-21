---
description: 
alwaysApply: true
---

# CLAUDE.md

Guidance for Claude Code working in this repository.

* **This code is still in heavy development**. At this point anything can be changed, backwards compatibility is NOT a concern.
* Comments MUST be timeless and describe only the present-state behavior of the code. Never reference prior implementations, previous behavior, or code history.

## IMPORTANT: Coding preferences for this project

* Put implementation in cpp files instead of hpp - unless agreed functionality requires it to be in hpp.
* For large changes (or plan execution) in the code:
  * Always test build different targets (one build for each board type is enough).
  * Always test build at least one SIM target.
* Avoid harcoded mem offsets, use casting, sizeof etc. if possible.

---

## What this project is

Lightnet is embedded firmware for a tree network of addressable-LED panels. A single ESP8266/ESP32 **controller** discovers and drives **panels** (ATmega328) over I²C — up to 32 on ESP8266, 100 on ESP32 (`LIGHTNET_MAX_PANELS` in `Core/Common/LightnetConfig.hpp`). The controller exposes WiFi APIs; panels run animations locally after a single setup packet.

Two distinct binaries are compiled from one source tree. `LIGHTNET_TARGET_CONTROLLER` (set in `platformio.ini`) selects the target; the preprocessor eliminates the unused half entirely.

### lightnet-mobile (sibling repo)

The **Android/iOS client** lives in a separate checkout beside this repo — typically `../lightnet-mobile` (e.g. `D:/Projects/Lightnet/lightnet-mobile` next to `lightnet-firmware`). It is not vendored inside this tree unless added as `third_party/lightnet-firmware` from the mobile side.

| Where to look | Contents |
|---|---|
| `composeApp/src/commonMain/` | WebSocket protocol, `LightnetDevice`, UI, scene editor, live preview |
| `composeApp/src/commonMain/.../LightnetHttpClient.kt` | HTTP client for controller REST API — update when adding/changing endpoints |
| `composeApp/src/androidMain/cpp/` | NDK `liblightnet_anim.so` — JNI over `lib/Lightnet/Core/CApi` (`panel_core` + `controller_core`) |
| `composeApp/build.gradle.kts` | Resolves firmware path: `-PlightnetFirmwareDir`, submodule `third_party/lightnet-firmware`, or sibling `../lightnet-firmware` |

Build the mobile app from that repo: `.\gradlew.bat :composeApp:assembleDebug`. See `lib/Lightnet/Core/CApi/README.md` for how the portable core is consumed cross-repo.

---

## Docs

| Document | Contents |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | Physical topology, library structure, I²C protocol, animation framework internals, discovery sequence, controller boot |
| [`docs/getting-started.md`](docs/getting-started.md) | PlatformIO environments, config files, build/upload commands |
| [`docs/hardware.md`](docs/hardware.md) | Pin assignments for controllers and panels, topology rules, fuses |
| [`docs/ota.md`](docs/ota.md) | Panel OTA (twiboot), serial upload via controller, controller ArduinoOTA |
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

Environments: `controller_esp8266` / `controller_wemos` / `controller_esp32` / `controller_s2_mini` (+ `_sim` variants) for the controller; `panel_atmega328p_via_controller` / `panel_atmega328pb` / `panel_atmega328p` for panels; `atmega328p_bootloader` / `atmega328pb_bootloader` for one-time twiboot burn. See [`docs/getting-started.md`](docs/getting-started.md#platformio-environments) for full details.

## Tests

Native host-side unit tests cover the pure C++ utilities (no Arduino, no hardware). Run via PlatformIO:

```bash
pio test -e native                       # all suites
pio test -e native -f test_simplejson    # single suite
```

On Windows, MinGW GCC must be on `PATH` (typically `C:\msys64\mingw64\bin`).

Current suites: `test_simplejson`, `test_http_url`, `test_palette_parser`, `test_palette_codec`, `test_database`, `test_config_codecs`, `test_panel_graph`, `test_topology`, `test_panel_selector`, `test_panel_selector_parser`, `test_panel_field`, `test_panel_geometry`, `test_runner_math`, `test_runner_compile`, `test_runner_spawn`, `test_compositor`, `test_panel_anim`, `test_spsc_queue`, `test_main_loop_queue`, `test_scene_player`, `test_scene_codec`, `test_scene_writer`, `test_scene_capi`. When fixing a bug in a pure-logic module, add a regression test under `test/test_*/test_main.cpp`. See [`docs/testing.md`](docs/testing.md) for what's testable natively vs. what needs a device.

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

Packet types (`WebsocketApi::packet_t` in `WebsocketApi.hpp`): `TOGGLE=1`, `SET_COLOR=3`, `GET_EDGES_LIST=4`, `GET_PANELS_STATES=5`, `PANELS_STATES=6`, `EDGES_LIST=7`, `ANIMATION_TRIGGER=8`, `MIRROR_BATCH=9`, `SET_MIRROR=10`, `PING=11`, `PONG=12`. (`SET_BRIGHTNESS=2` was removed — use appearance/global-brightness HTTP or I²C `PACKET_SET_GLOBAL_BRIGHTNESS` instead.)

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

Snapshotted types: `PACKET_SET_GLOBAL_BRIGHTNESS`, `PACKET_SET_BASE_COLORS`, `PACKET_SET_PALETTE`, `PACKET_SET_BACKGROUND`, `PACKET_TURN_ON_OFF`, `PACKET_ANIMATION_PREPARE`, `PACKET_ANIMATION_START`. `PACKET_SET_COLOR` is not snapshotted (60 fps stream, self-heals within one live frame). `PACKET_ANIMATION_CONTROL` is mirrored live but not snapshotted — a STOP invalidates matching PREPARE/START snapshot entries.

**Power-off / power-on**: `PacketMirror::clearSnapshot()` is called when the controller turns off, so stale animation state is not replayed to clients that connect while it is off. On power-on, `ScenesService::resumeScene()` restarts the last-loaded scene from the beginning using data preserved in `ScenePlayer` (all scene state survives `stop()` in memory; `lCount > 0` is the resume guard).

**Sim mode**: `lib/Lightnet/Sim/LightnetBusSim.cpp::sendPacket()` invokes `onPacketSentCallback` so the mirror pipeline works identically in SIM_MODE — outbound packets reach `PacketMirror::capture()` and the mobile live preview works without real hardware.

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

The whole scene engine is now the **shared `lib/Lightnet/Core/Controller/` library** (no
Arduino) — built into the controller and, via the scene C ABI
(`Core/CApi/controller_core_c.h`), into the mobile app so a scene can be **previewed without a
controller**. It's decoupled from hardware by three pure seams: `IPacketSink` (the bus),
`IPaletteResolver` (palettes), `ITopologyProvider` (the panel tree), plus `ITagResolver` (tags). The
AVR panel never pulls `Controller/`.

**Pure, natively-tested topology primitives** (in `lib/Lightnet/Core/Controller/`, no Arduino):

| File | Role |
|---|---|
| `TopologyIndex.hpp` | Rooted view of the tree (depth, parent, leaf/branch, subtree, neighbours, canonical order, multi-source hop-distance). Built from a generic link list; **parameterised by a start node** → re-rooting is just a rebuild. |
| `PanelSelector.hpp` + `PanelSelectorParser.hpp` | The `panels` grammar (`all`/indices/`exclude`, graph selectors `root`/`leaves`/`depth:a-b`/`subtree:N`/`fraction`/…, `tag:<name>`, `any`/`all`/`not`) compiled to a compact RPN program; `resolveSelector(sel, topo, out, ITagResolver*)`. |
| `PanelField.hpp` | Runner directionality: per-panel **hop-distance coord** from a `source` (`root`/`leaves`/`panel:N`, `reverse`). Envelopes are pure in `RunnerMath.hpp` (same `Core/Controller/` lib). |
| `PanelGeometry.hpp` | **Geometric** runner directionality (`directionality:"geometric"` + `angle`; legacy `source:"geometric"` still parses): planar (x,y) layout of the tree from regular-polygon geometry (faithful port of mobile `PanelsLayoutService`, anchored at the lowest panel index = visualizer frame), then `computeGeometricField()` projects centroids onto the chosen axis. No protocol change. |
| `TagResolver.hpp` | `ITagResolver` + the single `isValidTagName`/`TAG_NAME_MAX` shared by parser, store, and endpoint. |

**Engine side** (`lib/Lightnet/Core/Controller/`, shared): `SceneTopology` owns the panel-tree views
(`PanelGraph` + `TopologyIndex` + `PanelGeometry`), the logical root, the tag resolver, and selector
resolution (`rebuild()` + `resolvePanels()`); it pulls the tree through `ITopologyProvider`.
`ScenePlayer` holds one `SceneTopology` (rebuilt in `loadAndPlay`/`resume`), delegates
`setLogicalRoot()` / `setTagResolver()` to it, reads its views in `fireStep`, and emits packets via
`AnimationScheduler` → `IPacketSink`.

**Device glue** (`lib/Lightnet/Controller/`, implements the seams): `ControllerPacketSink` wraps
`LNBus` (ack-retry + bus pacing); `PaletteRepository` is the `IPaletteResolver`; `PanelsTopologyProvider`
flattens the live `PanelsInitializer` tree into the `ITopologyProvider` arrays;
`Topology/TopologyConfigStore` persists `/config/topology.json` and is the `ITagResolver`. Endpoints
live in `API/http/ConfigurationServer` (`GET /api/configuration`, `PATCH /api/configuration`),
wired in `main.cpp` case 0.

- **No protocol change**: runners (incl. directionality math) run in float — they are **not** part of
  the `Core/Panel` panel-local math (which mobile also runs via `panel_core`). On the controller they
  run on the ESP; on mobile they run via `controller_core` (offline scene preview, no controller).
  `N`/edges/adjacency come from discovery data (controller) or a cached/virtual tree (mobile).
- **Backward compatible**: v2 `panels` forms map onto the RPN; legacy `originPanel` → `source:panel:N`;
  v2 WAVE/CHASE default to `source:root` (slight visual change — design §6.4). Geometric
  directionality requires `schemaVersion: 3` minimum (current `SCENE_SCHEMA_VERSION` is 8);
  `SceneTopology::rebuild()` builds the `PanelGeometry` alongside `topo`, and `ScenePlayer::fireStep`
  branches on it.
- Native suites: `test_panel_graph`, `test_topology`, `test_panel_selector`, `test_panel_selector_parser`,
  `test_panel_field`, `test_panel_geometry`, `test_runner_math`, `test_runner_compile`, `test_runner_spawn`,
  `test_scene_player` (engine end-to-end via a mock `IPacketSink`), `test_scene_capi` (the scene C ABI).

---

## Key facts for coding

- **Source entry**: `src/main.cpp` selects the target via `LIGHTNET_TARGET_CONTROLLER`; `setup()`/`loop()` live in `src/controller/main.cpp` or `src/panel/main.cpp`.
- **I²C protocol version**: v6 (`Protocol::VERSION` in `Core/Common/ProtocolMeta.hpp`, included via `Common/Protocol.hpp`). Changing the protocol **requires flashing both controller and all panels together**.
- **`animScheduler->tick(millis())`** must be called in the main loop `case 1`.
- **LittleFS** is mounted in `case 0` before the WiFi captive portal starts, so `AppearanceStore` can read `/config/appearance.db`.
- **Single-record config stores** (`AppearanceStore`, `ConfigurationStore`, `AppStateStore`) persist as binary `Database` records via `SingleRecordStore<Codec>` (`Common/Database/SingleRecordStore.hpp`) — one fixed-slot record per `.db` file (`/config/appearance.db`, `/config/configuration.db`, `/config/app_state.db`), sharing the same format as palettes/scenes. Their `*Codec`/`*Record` live under each store's `Store/` subdir.

- **`BOOTLOADER_ENTRY_TOKEN = 0xB0`** — both sides of `PACKET_ENTER_BOOTLOADER` must agree on this value. Do not send `CMD_SWITCH_APPLICATION + BOOTTYPE_BOOTLOADER` (bytes `0x01 0x00`) to the twiboot fork — it WDT-resets the panel.
- **`busIsDisabled` in `LightnetPinger` is static (shared)** — set while any ping pulse is being driven so all pingers drop ISR samples during that window, preventing self-detection.
- **Debug macros** (`D_PRINTLN`, `D_PRINT`, `D_PRINTF` in `Utils/Debug.hpp`; typically wrapped in `DEBUG_IF(DEBUG_*, …)`) are no-ops when `DEBUG=0` in `controller.config.hpp` / `panel.config.hpp`. Serial baud is 57600 everywhere.
