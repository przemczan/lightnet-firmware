---
icon: material/flask-outline
---

# Testing

How the firmware is tested, what runs where, and how to add new coverage.

Today the only automated layer is **native host-side unit tests** — pure C++ logic compiled and run on your PC. Fast (under 2 s), no device, no flashing. This is what `pio test -e native` runs.

There is no in-device Unity runner today; hardware-only code paths (filesystem I/O, the relay transport, animation timing) are exercised by running the firmware on a real board and observing serial / WebSocket output.

---

## Running native tests

```bash
pio test -e native                       # all suites
pio test -e native -f test_simplejson    # one suite
pio test -e native -vvv                  # verbose (compiler output)
```

!!! warning "Windows: MinGW GCC must be on PATH"
    PlatformIO's `native` platform uses your host's `gcc`/`g++`. On Windows we install MinGW-w64 via MSYS2:

    ```powershell
    winget install MSYS2.MSYS2
    & "C:\msys64\usr\bin\bash.exe" -lc "pacman -Sy --noconfirm --needed mingw-w64-x86_64-gcc"
    ```

    Then either prefix every invocation with `$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH;` or add `C:\msys64\mingw64\bin` to your user `PATH` permanently via System Properties → Environment Variables.

    macOS and Linux: the system `gcc`/`clang` is usually fine — no extra setup.

---

## What's covered

| Suite | File | What it tests |
|---|---|---|
| `test_simplejson` | [`test/test_simplejson/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_simplejson/test_main.cpp) | `jsonFindKey`, cursor-based iterators (`jsonEnterObject` / `jsonNextKey` / `jsonSkipValue` / `jsonReadFloat`), `SimpleJson` accessor class, hex colour parsing |
| `test_http_url` | [`test/test_http_url/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_http_url/test_main.cpp) | `Http::isSafeName` / `Http::isSafeId` (path-traversal, special chars, length cap), `Http::nameFromUrl` / `Http::idFromUrl` (prefix match, overflow, null inputs) |
| `test_entry_id` | [`test/test_entry_id/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_entry_id/test_main.cpp) | Scene/palette id generation — deterministic ids, validation, random id helper |
| `test_json_inject` | [`test/test_json_inject/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_json_inject/test_main.cpp) | `jsonUpsertId` — inject/replace top-level `"id"` in scene JSON blobs |
| `test_palette_parser` | [`test/test_palette_parser/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_palette_parser/test_main.cpp) | `parsePaletteJson` — happy paths (with/without name, pretty-printed JSON, reverse key order), every documented failure mode, `PALETTE_STOPS` cap |
| `test_palette_codec` | [`test/test_palette_codec/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_palette_codec/test_main.cpp) | `PaletteCodec` — `PaletteRecord` round-trip serialize/deserialize, empty-name and invalid-stops rejection |
| `test_database` | [`test/test_database/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_database/test_main.cpp) | `Database<Codec>` + `DatabaseFormat` — create/open, insert/replace/remove, tombstone reuse, version mismatch, truncated file rejection (in-memory `IRandomAccessStorage`) |
| `test_config_codecs` | [`test/test_config_codecs/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_config_codecs/test_main.cpp) | Single-record config codecs (`AppearanceCodec`/`ConfigurationCodec`/`AppStateCodec`) — fixed-slot `Database` round-trip, out-of-range rejection, string-field termination |
| `test_panel_graph` | [`test/test_panel_graph/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_graph/test_main.cpp) | `PanelGraph` — the shared root-independent adjacency: slot↔panel-index round-trip, `lowestSlot`, degree/neighbour CSR walk, per-side connector indices, symmetry, single-panel/empty/over-capacity builds |
| `test_topology` | [`test/test_topology/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_topology/test_main.cpp) | `TopologyIndex` — depth, leaf/branch, canonical order, neighbours, subtree, multi-source distances, re-rooting, fallback root (against the worked topology in [Scene Authoring §2](animations/scene-authoring.md#2-how-the-panels-are-connected--topology)) |
| `test_panel_selector` | [`test/test_panel_selector/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_selector/test_main.cpp) | `resolveSelector` — every graph selector, `any`/`all`/`not` composition, v2-form equivalence, malformed-program rejection |
| `test_panel_selector_parser` | [`test/test_panel_selector_parser/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_selector_parser/test_main.cpp) | `parsePanelSelector` — JSON `panels` grammar → RPN → resolved panels, including nested composition and error cases |
| `test_panel_field` | [`test/test_panel_field/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_field/test_main.cpp) | `computeDistanceField` — hop-distance field from each `source` (root/leaves/panel/all), `reverse`, missing-source fallback, max-coord over the targeted subset |
| `test_panel_geometry` | [`test/test_panel_geometry/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_geometry/test_main.cpp) | `PanelGeometry` planar layout (centroids match the mobile visualizer frame) + `computeGeometricField` axis projection (horizontal/vertical/2-D), `reverse`, single-panel uniform, empty-build invalid |
| `test_runner_math` | [`test/test_runner_math/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_runner_math/test_main.cpp) | `RunnerMath` wave/ripple/chase envelopes + sweep positions, including zero-width (no divide) |
| `test_runner_compile` | [`test/test_runner_compile/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_runner_compile/test_main.cpp) | `RunnerCompile` — WAVE/CHASE/RIPPLE/REPEATING/WHEEL/BOUNCE onset/peak/end timing and lit-coord, zero-width/zero-period edge cases |
| `test_runner_spawn` | [`test/test_runner_spawn/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_runner_spawn/test_main.cpp) | `RunnerSpawn` — deterministic RNG, due-count rate/burst-cap, sweep spawn schedule (`count`), pool round-robin, path building, SPARKLE/RAIN spawn timing |
| `test_compositor` | [`test/test_compositor/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_compositor/test_main.cpp) | `Core/Panel/ColorCompose` — blend modes (opaque/add/multiply/screen/darken/overlay/difference/subtract/max), HSV roundtrip, dim/desaturate/brighten/saturate/hue-shift/invert modifiers, layer fold |
| `test_panel_anim` | [`test/test_panel_anim/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_anim/test_main.cpp) | Portable `Core/Panel/AnimationPlayer` — time-as-parameter (deterministic FADE), `setColorDirect` ungated (delta-gate regression), `FLAG_CURRENT_COLOR_*` reads current output, SOLID hold |
| `test_spsc_queue` | [`test/test_spsc_queue/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_spsc_queue/test_main.cpp) | Lock-free `Core/Common/SpscByteQueue` — FIFO order, full/empty edges, wrap-boundary straddle integrity, panel-sized (70 B) max record, 200k-iteration fuzz vs. a reference model |
| `test_main_loop_queue` | [`test/test_main_loop_queue/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_main_loop_queue/test_main.cpp) | `Utils/MainLoopQueue` — FIFO post/drain, POD arg round-trip, zero-length args, null-fn and oversized-args rejection, full-queue recovery |
| `test_scene_player` | [`test/test_scene_player/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_scene_player/test_main.cpp) | `ScenePlayer` end-to-end via a mock `IPacketSink` — SOLID scene emits PREPARE+START, `stop()` emits control packet and clears playing state |
| `test_scene_codec` | [`test/test_scene_codec/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_scene_codec/test_main.cpp) | `SceneCodec` — `SceneRecord` round-trip serialize/deserialize, name validation, record-id matching |
| `test_scene_writer` | [`test/test_scene_writer/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_scene_writer/test_main.cpp) | Scene JSON parse → serialize → parse round-trip |
| `test_scene_duration` | [`test/test_scene_duration/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_scene_duration/test_main.cpp) | Scene duration calculation — single-layer sum, multi-layer max, zero-duration steps |
| `test_scene_capi` | [`test/test_scene_capi/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_scene_capi/test_main.cpp) | Scene C ABI (`Core/CApi/controller_core_c.h`) — load+play emits packets, bad-JSON rejection, stop emits control, mirror-batch drain |
| `test_panel_discovery` | [`test/test_panel_discovery/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_discovery/test_main.cpp) | `Core/Relay/PanelDiscovery` — parent-offer acceptance, idempotent re-offer, loop rejection on a second edge, child-probe accept/fail |
| `test_panel_router` | [`test/test_panel_router/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_router/test_main.cpp) | `Core/Relay/PanelRouter` — flood-from-parent / route-to-parent decision table, plus a 3-node in-memory fabric with a deliberate wiring loop proving discovery rejects it and a flood terminates rather than circulating |
| `test_packet_framer` | [`test/test_packet_framer/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_packet_framer/test_main.cpp) | `Protocol::packetSizeForType` + `Core/Relay/PacketFramer` — recovering packet boundaries from a raw byte stream (no out-of-band length like I2C provided): byte-at-a-time and bulk feeds, corrupted-header-CRC resync, unrecognized-type-byte skipping, back-to-back frames with no explicit reset, sizing the relay OTA bootloader control plane, and the `validateProtocolVersion` bypass that plane depends on (rejected by default, accepted when constructed for the bootloader) |
| `test_discovery_coordinator` | [`test/test_discovery_coordinator/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_discovery_coordinator/test_main.cpp) | `Core/Relay/DiscoveryCoordinator` — the controller's depth-first resume-stack sequencing against a mock `IEdgeLink`: push/advance on a new registration, pop/backtrack on `DISCOVERY_DONE`, completion only once the stack unwinds to the controller sentinel, rejected replies and unrelated packet types ignored, and (when an optional `DiscoveryTreeBuilder` is supplied) that accepted registrations feed it a root/link while rejections never do |
| `test_discovery_tree_builder` | [`test/test_discovery_tree_builder/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_discovery_tree_builder/test_main.cpp) | `Core/Relay/DiscoveryTreeBuilder` — accumulates `(parent, parentEdge, child, childEdge)` tuples into the `indices[]`/`edgeCounts[]`/`TopoLink[]` shape `PanelGraph::build()` consumes: a root alone has no links, both edge indices are recorded per link, chained/branching trees build links in order, `reset()` clears accumulated state |
| `test_panel_discovery_driver` | [`test/test_panel_discovery_driver/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_discovery_driver/test_main.cpp) | `Core/Relay/PanelDiscoveryDriver` — the panel's local probe/offer/reject/timeout sequence against a real `PanelDiscovery` + mock `IEdgeLink`: fresh acceptance, idempotent re-offer, loop rejection, `DISCOVERY_ADVANCE` address filtering, the full probe→accept→advance→reject→done sequence, and probe-timeout expiry via `tick()` |
| `test_discovery_end_to_end` | [`test/test_discovery_end_to_end/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_discovery_end_to_end/test_main.cpp) | The whole relay discovery protocol (`DiscoveryCoordinator` + `PanelDiscoveryDriver` + unmodified `PanelRouter`) over real exchanged packets on a 3-node fabric with a deliberate wiring loop and two genuinely empty ports — proves correct index assignment, tree connectivity, loop rejection on both sides, probe-timeout handling, and (via a `DiscoveryTreeBuilder` wired into the coordinator) that the accumulated `indices[]`/`TopoLink[]` matches the real spanning tree with the loop-closing edge never recorded — not just the decision table in isolation |
| `test_byte_ring` | [`test/test_byte_ring/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_byte_ring/test_main.cpp) | `Core/Common/ByteRing` — lock-free single-producer/single-consumer byte ring for a UART RX path (one push/pop per byte, unlike `SpscByteQueue`'s length-prefixed records): FIFO order, full/empty edges, wrap-boundary integrity, reset |
| `test_edge_frame_receiver` | [`test/test_edge_frame_receiver/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_edge_frame_receiver/test_main.cpp) | `Core/Relay/EdgeFrameReceiver` — tags a completed frame with the edge it arrived on given one shared USART + mux: wake claims/ignores an edge, bytes dropped without a claim, a full frame tagged with the claiming edge, claim released on completion, a corrupted header CRC does not release the claim (still the same in-flight transmission), stalled-claim recovery via `tick()`, byte activity resetting the timeout clock |
| `test_panel_frame_dispatcher` | [`test/test_panel_frame_dispatcher/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_panel_frame_dispatcher/test_main.cpp) | `Core/Relay/PanelFrameDispatcher` — the one decision the panel's dispatch loop needs per arrived frame, against a real `PanelDiscovery`/`PanelDiscoveryDriver`/`PanelRouter` behind a mock `IEdgeLink`: no local dispatch before this panel is assigned an index, `PACKET_INITIALIZATION_PULL` never reaching `PanelRouter` (asserted via a detectable extra send if it did), broadcast/own-index targets dispatched locally once assigned, another panel's unicast still relayed but not dispatched locally |

359 tests total.

---

## What is testable natively, what isn't

| Module | Native? | Notes |
|---|---|---|
| [`Utils/SimpleJson.hpp`](https://github.com/przemczan/lightnet-firmware/blob/master/lib/Lightnet/Utils/SimpleJson.hpp) | ✅ | Pure C++, header-only |
| [`Controller/Palettes/PaletteJson.hpp`](https://github.com/przemczan/lightnet-firmware/blob/master/lib/Lightnet/Controller/Palettes/PaletteJson.hpp) | ✅ | Pure parser, split out of `PaletteStore` specifically for testability |
| [`Controller/API/http/HttpUrl.hpp`](https://github.com/przemczan/lightnet-firmware/blob/master/lib/Lightnet/Controller/API/http/HttpUrl.hpp) | ✅ | Pure C string helpers, split out of `HttpHelpers.hpp` |
| `Core/Common/Palette.hpp` (`samplePalette`) | ✅ | Pure interpolation math — not yet tested but eligible |
| `PaletteStore::resolve`/`save`/`exists` | ❌ | Need LittleFS / hardware |
| HTTP handlers (`PaletteServer`, `SceneServer`, …) | ❌ | Need `AsyncWebServerRequest` mocks; not worth the effort |
| `Core/Controller/ScenePlayer.hpp` + `AnimationScheduler` | ✅ | Decoupled via `IPacketSink`/`IPaletteResolver`/`ITopologyProvider`; driven with synthetic `millis()` against a mock sink (`test_scene_player`, `test_scene_capi`) |
| Panel firmware (ATmega side) | ❌ | Cross-compiled, no native runtime |

Rule of thumb: **if the file only includes `<stdint.h>`, `<string.h>`, `<stddef.h>` and other headers in this column, it's testable natively.** As soon as it pulls in `<Arduino.h>`, `<FS.h>`, or `<ESPAsyncWebServer.h>`, it's not.

---

## Adding a new test suite

1. Create `test/test_<name>/test_main.cpp`. The directory name must start with `test_`.
2. Include the header you want to exercise via its path under `lib/Lightnet/` (the `[env:native]` `build_flags` already pass `-I lib/Lightnet`):

    ```cpp
    #include <unity.h>
    #include "Controller/Palettes/PaletteJson.hpp"
    ```

3. Write `void test_<something>()` functions using Unity assertions (`TEST_ASSERT_TRUE`, `TEST_ASSERT_EQUAL_STRING`, `TEST_ASSERT_FLOAT_WITHIN`, …).
4. Provide `setUp` / `tearDown` (can be empty) and `main`:

    ```cpp
    void setUp(void) {}
    void tearDown(void) {}

    int main() {
        UNITY_BEGIN();
        RUN_TEST(test_something);
        return UNITY_END();
    }
    ```

5. Run `pio test -e native -f test_<name>`.

If the header you want to test currently has an Arduino or filesystem dependency, **extract the pure logic into its own header first** — that's how `PaletteJson.hpp` and `HttpUrl.hpp` came to exist. A header that needs Arduino can't be tested natively; one that doesn't, can.

---

## When to add a test

- **After fixing a bug in pure logic**, write the regression test before closing. Both bugs that motivated this test infrastructure (the `jsonFindKey` depth-zero scan and the missing body-buffer null terminator) are now covered. Future variants will be caught immediately.
- **Before refactoring** a pure module, add tests for the current behaviour so the refactor has a safety net.
- **For new pure utilities**, write the test alongside the code — these tests are cheap and the round-trip is sub-second.

Don't bother adding native tests for code that integrates with hardware. Exercise that on a live controller instead.

---

- [Architecture](architecture.md) — where each library lives
- [Build & Flash](getting-started.md) — `pio` reference
- [Troubleshooting](troubleshooting.md) — serial debugging on a live device
