---
icon: material/flask-outline
---

# Testing

How the firmware is tested, what runs where, and how to add new coverage.

Today the only automated layer is **native host-side unit tests** — pure C++ logic compiled and run on your PC. Fast (under 2 s), no device, no flashing. This is what `pio test -e native` runs.

There is no in-device Unity runner today; hardware-only code paths (filesystem I/O, I²C, animation timing) are exercised by running the firmware on a real board and observing serial / WebSocket output.

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
| `test_http_url` | [`test/test_http_url/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_http_url/test_main.cpp) | `Http::isSafeName` (path-traversal, special chars, length cap), `Http::nameFromUrl` (prefix match, overflow, null inputs) |
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
| `test_scene_capi` | [`test/test_scene_capi/test_main.cpp`](https://github.com/przemczan/lightnet-firmware/blob/master/test/test_scene_capi/test_main.cpp) | Scene C ABI (`Core/CApi/controller_core_c.h`) — load+play emits packets, bad-JSON rejection, stop emits control, mirror-batch drain |

255 tests total, ~11 s wall time.

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
