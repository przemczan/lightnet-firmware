# Core/CApi — C ABIs for the portable core

Two flat C surfaces the mobile app binds to, both producing/consuming the **same wire packets** the
controller mirrors:

- **`panel_core_c.h`** — one panel's player (`Lightnet::AnimationPlayer`, `lib/Lightnet/Core/Panel`).
- **`controller_core_c.h`** — the whole controller scene engine (`Lightnet::ScenePlayer` +
  `AnimationScheduler` + runners, `lib/Lightnet/Core/Controller`), so a scene can be previewed
  **without a controller**. See "Scene C ABI" below.

The two compose: `controller_core` produces a scene's packets (the same ones a controller would
send), the app feeds them into the per-panel `panel_core` players it already drives for the live
preview, so offline preview and live preview share one render path.

## panel_core_c.h

A flat C surface over `Lightnet::AnimationPlayer` (`lib/Lightnet/Core/Panel`), so
the **same C++ animation math** the firmware runs can be consumed by the mobile app:

- **Android** — built via NDK `externalNativeBuild` pointing at this `CMakeLists.txt`; bound through JNI.
- **iOS** — `panel_core` linked into the `ComposeApp` framework; `panel_core_c.h` consumed via Kotlin/Native cinterop.

Clock-domain translation, resync, and mirror-packet plumbing stay in the mobile Kotlin wrapper —
this layer is pure animation math. Time is a caller-supplied 16-bit ms counter (wraps ~65.5 s),
matching the firmware contract.

## Build & test (host)

```bash
cmake -S lib/Lightnet/Core/CApi -B .build/panel-core-c
cmake --build .build/panel-core-c
ctest --test-dir .build/panel-core-c --output-on-failure
```

`smoke` drives the same FADE-midpoint case as `test/test_panel_anim` and asserts `(127,127,127)`.

## Consuming from another repo (mobile)

`LIGHTNET_CORE_DIR` defaults to the in-repo `lib/Lightnet/Core` (this directory's parent). When the
mobile project builds this (firmware vendored as a git submodule, or a sibling checkout), point it
at that path:

```bash
cmake -S <firmware>/lib/Lightnet/Core/CApi -B build \
      -DLIGHTNET_CORE_DIR=<firmware>/lib/Lightnet/Core
```

## API shape

`anim_create`/`anim_destroy`, then per frame:

```c
anim_tick(h, now);
if (anim_take_dirty(h)) anim_get_color(h, &r, &g, &b);
```

Packet handlers take the **raw wire bytes** (PacketMeta header included) and decode via the firmware
struct layout — no re-serialization on the mobile side. `anim_prepare` / `anim_set_palette` /
`anim_set_base_colors` take byte buffers; `anim_set_background` / `anim_set_color_direct` take RGB
scalars; `anim_start` / `anim_control` / `anim_update_params` take the small scalar fields + `now`.

## Scene C ABI (`controller_core_c.h`)

Runs `SceneParser -> ScenePlayer -> AnimationScheduler -> runners` with no hardware. The engine
resolves against three caller-supplied seams instead of the bus/filesystem/discovery:

```c
h = scene_create();
scene_set_sink(h, cb, user);                          // receive emitted wire packets
scene_set_topology(h, idx, n, links, lc, ec, root);   // panel tree (cached or user-authored)
scene_set_palette(h, "fire", stops, count);           // named palettes the scene uses
scene_set_tag(h, "left", panels, count);              // device tags the scene targets
scene_load_and_play(h, json, len, now);               // parse + start -> emits packets
... each frame: scene_tick(h, now);                   // advance steps -> emits packets
scene_destroy(h);
```

The sink callback gets `(user, address, type, bytes, len)` — raw wire packets (PacketMeta header
included), identical to a `MIRROR_BATCH` record. `address` 0 = general call. `links` is
`link_count * 4` bytes `{panelA, edgeA, panelB, edgeB}`; palette `stops` are `count * 4` bytes
`{pos, r, g, b}`. `controller_core` builds as a second static library from this same
`CMakeLists.txt` (`LIGHTNET_SCENE_DIR` defaults to `Core/Controller`); link both `panel_core`
and `controller_core`. `controller_core_smoke` proves a one-layer SOLID scene emits one PREPARE per
panel + a general-call START.
