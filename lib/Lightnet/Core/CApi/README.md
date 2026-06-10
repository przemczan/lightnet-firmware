# Core/CApi — C ABI for the portable animation core

A flat C surface (`anim_core_c.h`) over `Lightnet::AnimationPlayer` (`lib/Lightnet/Core/Anim`), so
the **same C++ animation math** the firmware runs can be consumed by the mobile app:

- **Android** — built via NDK `externalNativeBuild` pointing at this `CMakeLists.txt`; bound through JNI.
- **iOS** — `anim_core` linked into the `ComposeApp` framework; `anim_core_c.h` consumed via Kotlin/Native cinterop.

Clock-domain translation, resync, and mirror-packet plumbing stay in the mobile Kotlin wrapper —
this layer is pure animation math. Time is a caller-supplied 16-bit ms counter (wraps ~65.5 s),
matching the firmware contract.

## Build & test (host)

```bash
cmake -S lib/Lightnet/Core/CApi -B .build/anim-core-c
cmake --build .build/anim-core-c
ctest --test-dir .build/anim-core-c --output-on-failure
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
