# Animation Engine — HTTP API Implementation Plan

> NOTE: this file is the working plan. Rename to `animation-system-plan.md` after exiting plan mode.

## Context

The controller has a solid animation foundation (`AnimationScheduler`, `AnimationRunner` subtypes,
9 panel-local types) but no HTTP API to drive it. Goal: add a **Scene/Layer HTTP API** with
SPIFFS-backed JSON storage, full sequence automation, mid-flight brightness/color/palette
control, and WebSocket reactive triggers — wiring together everything that already exists.

Also fixes an existing bug: `animScheduler->tick()` is never called in the current main loop.

---

## Design Decisions Summary

| Decision | Choice | Why |
|---|---|---|
| API format | Nested JSON (HTTP) | Ergonomic for clients, standard tooling |
| Parser | `json-streaming-parser2` (SAX) | O(depth) memory, no DOM, no ArduinoJson |
| Storage format | **JSON files in SPIFFS** (same format as HTTP body) | One format end-to-end; debuggable; trivial schema evolution; `GET` is a file passthrough |
| Schema versioning | Top-level `"schemaVersion": 1` field in JSON | Additive changes free; breaking changes → bump + HTTP 409 |
| Sequencing | Time-based (duration elapsed → advance step) | Same mechanism for panel-local and runner steps; no callback wiring |
| Step parameters | Generic `params[4]` array, dispatched by `animType` | WLED-style; new animation types don't churn struct |
| Runtime representation | Fixed-size `SceneLayer`/`SceneStep` structs (parsed in once at scene load) | Fast 60 fps `tick()` — pointer access, not token walks |
| Notifications | Direct one-shot endpoint bypassing ScenePlayer, on a free group ID | Coexists with running scene via existing group separation |
| **Color model** | **WLED-unified: every color goes through a palette** | One code path. Base colors feed a special `userColors` palette. No special-casing. |
| **Mid-flight changes** | **Option Y — panel-side indirection** | Panels store palette + base colors. Frame-time color resolution. Instant updates with no re-prepare. Fits ATmega88PA-AU (~82 B SRAM). |
| **Palette scope** | **Scene-level default + per-layer override** | One palette per panel at any time (spatial separation). Most scenes use scene-level; layer override for split installations. |
| Filesystem | SPIFFS (matches existing codebase) | Consistency; LittleFS migration is a separate codebase-wide concern |

WLED was the closest reference. We borrow: generic-parameters pattern, unified palette color model,
3 base colors per scene, gradient palettes. We don't borrow segments-as-first-class (our `layer + targeting` is equivalent).

---

## Constants

### `lib/Lightnet/Common/LightnetConfig.hpp` (new — minimal)

Only the cross-cutting limit lives here. Scene-internal caps stay near `ScenePlayer`.

```cpp
#pragma once
static const uint8_t LIGHTNET_MAX_PANELS = 100;  // system-wide panel count cap
static const uint8_t PALETTE_STOPS       = 16;   // gradient stops per palette (WLED-compatible)
static const uint8_t BASE_COLORS_COUNT   = 3;    // primary / secondary / tertiary
```

### `lib/Lightnet/Controller/ScenePlayer.hpp` (scene-internal caps)

```cpp
static const uint8_t SCENE_MAX_LAYERS           = 8;
static const uint8_t SCENE_MAX_STEPS            = 12;
static const uint8_t SCENE_MAX_PANELS_PER_LAYER = 32;   // not LIGHTNET_MAX_PANELS — most layers target few panels
static const uint8_t SCENE_SCHEMA_VERSION       = 1;    // bumped only on breaking JSON schema changes
static const uint8_t PALETTE_SCHEMA_VERSION     = 1;
```

`SCENE_MAX_PANELS_PER_LAYER = 32` is a deliberate choice: "all" is the common case (uses `targetMode`,
not the list), and explicit panel lists are usually short. Capping at 32 saves ~544 bytes vs 100.

---

## Global Brightness, Base Colors, Palettes

### Unified color model

There is **exactly one color resolution path**: every animation color is sampled from a palette at
a specific position. There are no "explicit RGB" or "use base color" paths — those are degenerate
cases of palette sampling.

- A **palette** is a 16-stop gradient: `[(pos, R, G, B), ...]` where pos is 0–255.
- Each scene has a **current palette** (scene-level default) and optionally a **per-layer override**.
- Each scene has **3 base colors** (`primary`, `secondary`, `tertiary`).
- A built-in synthetic palette named `userColors` is defined as
  `[(0, primary), (128, secondary), (255, tertiary)]`. Selecting this palette makes the scene's
  animation track its base colors.
- For one-off colors that don't belong in a palette, the step uses `ColorRef{kind=0}` with inline
  RGB — no palette involvement. This is the "inline" path through the same `ColorRef` mechanism.

### Mid-flight control — Option Y: panel-side indirection

**Panel state additions** (`PanelRuntimeState` in panel firmware):
- Current palette: `GradientStop palette[PALETTE_STOPS]` — 16 × 4 = **64 bytes**
- Base colors: `ColorRGB baseColors[3]` — **9 bytes**
- Global brightness: `uint8_t globalBrightness` — **1 byte**
- **Total: ~74 bytes** new SRAM on panel (fits ATmega88PA-AU comfortably)

When the panel renders a frame, it resolves each animation's `ColorRef` against its current palette
+ base colors + global brightness multiplier. Any update to those state values takes effect on the
next frame — instant, with no re-prepare.

### Panel constraint: one palette per panel at a time

Since the panel stores a single palette, panels touched by multiple layers all see the same palette.
**Layer-level palette overrides require spatial separation** — the controller enforces at scene
load time that layers with different palettes don't share any panel. Validation rule:

> For every pair of layers (A, B), compute their *effective palette*: the layer's override if set,
> else the scene-level palette. If effectivePalette(A) ≠ effectivePalette(B), then
> targets(A) ∩ targets(B) = ∅.

Violations return HTTP 422 with a clear error.

### Mid-flight palette updates with active scene overrides

When `PUT /api/palette` fires while a scene with per-layer overrides is playing, the new palette
is the *scene-level default*. It should only reach panels whose effective palette comes from that
default (i.e., panels in layers without an override). Panels in layers with their own override
keep their override palette.

Implementation: `AppearanceStore::updatePalette()` delegates to `ScenePlayer::onGlobalPaletteChanged()`
when a scene is playing. ScenePlayer iterates layers; for each layer without an override, it
broadcasts the new palette to that layer's targets. With no scene playing, AppearanceStore
broadcasts to all panels directly.

### Protocol additions (`Common/Protocol.hpp`)

| Packet | Dir | Size | Use |
|---|---|---|---|
| `PACKET_SET_PALETTE` | C→P unicast or General Call | 64 B + header | Replace panel's current palette |
| `PACKET_SET_BASE_COLORS` | C→P unicast or General Call | 9 B + header | Replace panel's 3 base colors |
| `PACKET_SET_GLOBAL_BRIGHTNESS` | General Call | 1 B + header | Replace global brightness multiplier |
| `PACKET_ANIMATION_PREPARE` (modified) | C→P unicast | enlarged | `colorFrom`/`colorTo` are now `ColorRef` (4 B each) instead of `ColorRGB` (3 B each) |

`PACKET_SET_PALETTE` is sent via **General Call** for scene-level palette (all panels), and
**unicast** for per-layer overrides (each affected panel). Broadcast palette update completes in
~13 ms; unicast to 30 panels is ~540 ms (acceptable for one-time scene loads, rare).

### Persistence — `/config/appearance.json`

All visual state survives reboots in a single file:

```json
{
  "schemaVersion": 1,
  "brightness": 192,
  "baseColors": ["#FF4400", "#FF8800", "#000000"],
  "palette": "lava"
}
```

Written whenever any of these change (any of the PUT endpoints below, or when a scene loads and
its own colors/palette become the active appearance). Read once at controller boot:

1. Open `/config/appearance.json` (create with defaults if missing: brightness 255, base colors
   white/black/black, palette `userColors`).
2. Validate against schema version; on mismatch use defaults and log a warning.
3. Apply: broadcast `PACKET_SET_GLOBAL_BRIGHTNESS`, `PACKET_SET_BASE_COLORS`, and the resolved
   `PACKET_SET_PALETTE` to all panels right after discovery completes.

A scene's own `colors` and `palette` fields are its **initial state**: when a scene loads, those
values overwrite the active appearance and get persisted. A scene stopping does not reset
appearance — the last values stay live.

---

## Palette JSON Schema

```json
{
  "schemaVersion": 1,
  "name": "lava",
  "stops": [
    [0,   "#000000"],
    [46,  "#240000"],
    [96,  "#711100"],
    [148, "#8E0301"],
    [204, "#FF4702"],
    [255, "#FFFFFF"]
  ]
}
```

- 1–16 stops; controller linearly interpolates to fill 16-slot output sent to panels.
- `pos` strictly increasing 0–255. First stop must have pos=0, last must have pos=255.
- Stored at `/palettes/<name>.json`.

**Built-in palettes** shipped as defaults (created in SPIFFS on first boot if missing):
- `rainbow`, `lava`, `ocean`, `forest`, `party`, `sunset`, `aurora`, `embers`
- `userColors` — synthetic, derived from scene base colors (not a stored file; resolved on the controller at palette-push time)

---

## ColorRef (panel-side color reference, 4 bytes)

Sent in `PACKET_ANIMATION_PREPARE` instead of raw RGB. Panel resolves at frame time.

```cpp
struct ColorRef {
    uint8_t kind;                                // 0=inline RGB, 1=palette pos, 2=base color slot
    union {
        struct { uint8_t r, g, b; }      rgb;        // kind=0
        struct { uint8_t pos, _0, _1; }  palette;    // kind=1
        struct { uint8_t slot, _0, _1; } useColor;   // kind=2
        uint8_t raw[3];                              // for memcpy / wire serialization
    };
};
// Total: 4 bytes packed. Access:
//   cr.rgb.r / cr.rgb.g / cr.rgb.b      when kind == 0
//   cr.palette.pos                       when kind == 1
//   cr.useColor.slot                     when kind == 2
// (`inline` is a C++ keyword, so the kind=0 member is named `rgb`.)
```

`SceneStep` in RAM uses the same 4-byte `ColorRef` — no separate encoding for storage vs wire.

---

## HTTP API additions

### Appearance (live visual state)

```
GET  /api/appearance                  → full state
                                        { "brightness": 192,
                                          "baseColors": ["#FF4400","#FF8800","#000000"],
                                          "palette": "lava" }

PUT  /api/appearance                  bulk update (any subset of fields, atomic write)

GET  /api/brightness                  → { "value": 0-255 }
PUT  /api/brightness                  body: { "value": 0-255 }

GET  /api/colors                      → { "primary":"...", "secondary":"...", "tertiary":"..." }
PUT  /api/colors                      body: same (any subset of slots can be updated)

GET  /api/palette                     → { "palette": "lava" }       (current selection)
PUT  /api/palette                     body: { "palette": "lava" }   (must reference a stored palette)
```

All of these:
- Take effect on next panel frame (Option Y — no re-prepare).
- Update `/config/appearance.json` atomically (`.tmp` + rename).
- Work whether or not a scene is playing.

### Palette CRUD (stored palette definitions)

```
GET    /api/palettes                  → list
POST   /api/palettes                  body: palette JSON (saves to /palettes/<name>.json)
GET    /api/palettes/:name            → palette JSON (file passthrough)
DELETE /api/palettes/:name            (built-in palettes return 403)
```

---

## JSON Schema (HTTP API)

### Scene
```json
{
  "schemaVersion": 1,
  "name": "sunset",
  "loop": true,
  "colors": {
    "primary":   "#FF4400",
    "secondary": "#FF8800",
    "tertiary":  "#000000"
  },
  "palette": "lava",
  "layers": [ <Layer>, ... ]
}
```

`colors` and `palette` are optional. Defaults: `colors` defaults to `{#FFFFFF, #000000, #000000}`,
`palette` defaults to `"userColors"` (which means the animation tracks `colors`).

`schemaVersion` is the only schema-control mechanism. Adding new optional fields to steps/layers
in the future is fine — old files just don't have them, defaults apply. Removing or changing
the meaning of an existing required field requires bumping `SCENE_SCHEMA_VERSION` and rejecting
older files with HTTP 409.

### Layer — group + target + sequence
```json
{
  "group": 1,
  "panels": "all",
  "palette": "ocean",
  "sequence": [
    { "type": "TRANSITION",
      "colorFrom": {"palette": 0},
      "colorTo":   {"palette": 200},
      "duration": 2000 },
    { "type": "BREATHE",
      "color": {"useColor": 0},
      "duration": 3000 },
    { "runner": "RIPPLE",
      "color":  "#FF8800",
      "duration": 1500, "params": [2, 0] }
  ]
}
```

`palette` on a layer is optional. When present, it overrides the scene-level palette for panels
in this layer's target — subject to the spatial-separation validation (see Mid-flight control above).

Color references in steps:
- `"#RRGGBB"` or `{r,g,b}` — inline RGB (`ColorRef{kind=0}`)
- `{"palette": N}` where N is 0–255 — sample the active palette at position N (`ColorRef{kind=1}`)
- `{"useColor": 0|1|2}` — reference scene base color slot (`ColorRef{kind=2}`)

### AnimStep — generic-parameter shape
| Field | Type | Notes |
|---|---|---|
| `type` | string | `SOLID` `FADE` `TRANSITION` `BREATHE` `PULSE` `BLINK` `HUE_CYCLE` `STROBE` `REACTIVE` |
| `runner` | string | `WAVE` `RIPPLE` `CHASE` — mutually exclusive with `type` |
| `colorFrom` / `colorTo` | inline / palette ref / useColor ref | see "Color references" above |
| `color` | alias for `colorTo` when only one color is needed | optional |
| `brightnessFrom` / `brightnessTo` | 0–255 | optional |
| `duration` | 0–65535 ms | 0 = infinite, only valid on last step of a looping scene |
| `loop` / `pingpong` | bool | maps to FLAG_LOOP / FLAG_PINGPONG (panel-local only) |
| `params` | array of 0–255, length ≤ 4 | type-specific (see below) |

**Per-type parameter mapping** (documented, not enforced by structure):
| animType | params[0] | params[1] | params[2] | params[3] |
|---|---|---|---|---|
| BREATHE | speed | — | — | — |
| PULSE | rise ms | hold ms | fall ms | — |
| BLINK / STROBE | half-period ms / frequency Hz | — | — | — |
| HUE_CYCLE | period ms | — | — | — |
| REACTIVE | decay rate | — | — | — |
| RUN_WAVE | waveWidth | — | — | — |
| RUN_RIPPLE | rippleWidth | originPanel | — | — |
| RUN_CHASE | — | — | — | — |

Friendly aliases like `waveWidth`, `rippleWidth`, `originPanel` are accepted at JSON parse time
and mapped to `params[]` by the parser — clients can use whichever style they prefer.

### Panel targeting
- `"panels": "all"` — all discovered panels (no list stored)
- `"panels": [0, 2, 5]` — specific panel indices (LIST mode)
- `"panels": {"exclude": [3]}` — all except listed (EXCLUDE mode)

---

## Animation Type Enum (unified — no `isRunner` flag)

```cpp
// Panel-local: 0-31
enum AnimType : uint8_t {
    ANIM_SOLID = 0, ANIM_FADE, ANIM_TRANSITION, ANIM_BREATHE, ANIM_PULSE,
    ANIM_BLINK, ANIM_HUE_CYCLE, ANIM_STROBE, ANIM_REACTIVE,
    // ... reserved up to 31

    // Controller runners: 64+
    RUN_WAVE   = 64,
    RUN_RIPPLE = 65,
    RUN_CHASE  = 66,
};

inline bool isRunnerType(uint8_t t) { return t >= 64; }
```

One field, one validation path. Adding a new animation type doesn't change struct layout.

---

## Storage Format — JSON on SPIFFS

SPIFFS path: `/scenes/<name>.json` — name limited to 19 chars (SPIFFS max path 31, `/scenes/` = 8, `.json` = 5).

Files are the exact same JSON shape the HTTP API accepts. No binary representation anywhere on
disk. Schema evolution is naturally additive: new optional fields appear in new scenes, old
scenes simply don't have them and defaults apply.

### Flows
```
POST /api/scenes        →  AsyncWebServer chunks  →  streaming parser  →  validate
                        →  if valid: write incoming bytes to /scenes/<name>.json
GET  /api/scenes/:name  →  request->send(SPIFFS, "/scenes/<name>.json", "application/json")
                           (raw file passthrough — no parse, no re-encode)
POST /api/scenes/:n/play →  open file → feed bytes through streaming parser → populate
                           ScenePlayer structs → start
```

### Save strategy — atomic file write

To avoid leaving a half-written file on power loss during save:

1. Stream incoming bytes to `/scenes/<name>.json.tmp` as they arrive.
2. On parse completion + validation success: `SPIFFS.remove("/scenes/<name>.json")` then
   `SPIFFS.rename(".tmp", "<name>.json")`.
3. On parse failure or validation failure: `SPIFFS.remove("/scenes/<name>.json.tmp")` — original
   file (if any) is untouched.

### Schema versioning

`schemaVersion` is read first during the streaming parse. If it's missing or 0, treat as 1.
If it's greater than `SCENE_SCHEMA_VERSION`, return `HTTP 409 {"error":"schema_too_new","scene":N,"firmware":M}` and refuse to load. Client must upgrade firmware or downgrade the scene externally.

Currently only v1 exists. The check is one if-statement, not a framework.

---

## HTTP API

```
POST   /api/scenes                 ← save/overwrite scene
GET    /api/scenes                 ← list: [{name, size}]
GET    /api/scenes/:name           ← return scene as JSON
DELETE /api/scenes/:name

POST   /api/scenes/play            ← play inline scene JSON
POST   /api/scenes/:name/play      ← play stored scene by name
POST   /api/scenes/stop            ← stop current scene
GET    /api/scenes/status          ← current scene state

POST   /api/animations/play        ← one-shot single animation, bypasses ScenePlayer
                                     body: { group, panels, type/runner, ...params }
POST   /api/animations/trigger     ← reactive beat trigger: { group, value }
```

### One-shot animations & overlay model

`ScenePlayer` is single-instance — it holds at most one active scene. **Notifications and
ambient scenes coexist via group IDs**, not via multiple players:

- Ambient scene uses groups 1-N.
- Notification fires via `POST /api/animations/play` on a free group ID (e.g. 250).
- Both run concurrently because the panel-side `AnimationPlayer` already supports independent
  animations on non-overlapping groups (existing infrastructure, per CLAUDE.md).
- The one-shot endpoint calls `AnimationScheduler::playOnPanels()` directly — no scene state, no SPIFFS.

This avoids the complexity of multiple `ScenePlayer` instances while still covering the
notification-over-ambient use case.

---

## Input Validation

Validation runs inline inside the streaming parser as fields arrive. Any failure returns
`HTTP 422 {"error":"<message>"}` and aborts — no partial writes, no side effects.

| Field | Rule |
|---|---|
| Scene name | `[a-zA-Z0-9_-]`, 1–19 chars |
| `layers.length` | 1–`SCENE_MAX_LAYERS` |
| `sequence.length` | 1–`SCENE_MAX_STEPS` |
| `group` | 1–254 (0 reserved); unique across layers in same scene |
| `type` + `runner` | mutually exclusive |
| `type` value | known `ANIM_*` enum string |
| `runner` value | `WAVE` `RIPPLE` `CHASE` |
| `duration` | 0–65535; 0 only on last step of looping scene |
| color fields | one of: `#RRGGBB` hex, `{r,g,b}` 0–255 each, `{"palette": 0-255}`, `{"useColor": 0-2}` |
| `brightness*` | 0–255 |
| `params[i]` | 0–255 |
| `params.length` | 0–4 |
| `originPanel` (or `params[1]` for RIPPLE) | 0–(discovered panel count − 1) |
| Panel indices | 0–(discovered panel count − 1) |
| Panel list length | 1–`SCENE_MAX_PANELS_PER_LAYER` |
| Infinite step | only allowed as last step of looping scene |

---

## Streaming JSON Parser

**Library:** `json-streaming-parser2` by `squix78` (~2 KB code, SAX-style).
Memory: O(nesting depth) ≈ a few hundred bytes; no DOM at any point.

Fed directly from `AsyncWebServer` body chunks — no buffering. Each chunk callback feeds the
parser; parser fires events into our `SceneParseContext` state machine:

```cpp
enum class ParseState : uint8_t {
    ROOT, SCENE_FIELDS, LAYERS_ARRAY,
    LAYER_FIELDS, SEQUENCE_ARRAY, STEP_FIELDS,
    PANELS_FIELD, PARAMS_ARRAY
};

struct SceneParseContext {
    ParseState  state;
    uint8_t     layerIdx, stepIdx;
    bool        loop;
    char        name[20];
    char        errMsg[48];
    bool        valid;
    SceneLayer  layers[SCENE_MAX_LAYERS];   // filled in-place as parsed
    uint8_t     layerCount;
};
```

Stack-allocated context (~2 KB) lives only during the POST request. Field setters validate
ranges inline; on first violation `valid = false` and `errMsg` is set — parser keeps consuming
bytes to avoid stalling the request stream, but no struct mutations occur.

---

## Core Types

### `SceneStep` (18 bytes — generic params + ColorRef)

```cpp
struct SceneStep {
    uint8_t  animType;                 // ANIM_* (0-31) or RUN_* (64+)
    uint8_t  flags;                    // FLAG_LOOP | FLAG_PINGPONG | ...
    uint16_t durationMs;
    ColorRef colorFrom;                // 4 bytes — inline RGB, palette pos, or base color slot
    ColorRef colorTo;                  // 4 bytes
    uint8_t  brightnessFrom;
    uint8_t  brightnessTo;
    uint8_t  params[4];                // type-specific, mapped at execution
};
```

No `isRunner` field. No named runner-specific fields. The 4-byte `ColorRef` is the same wire
format sent to panels in `PACKET_ANIMATION_PREPARE` — no separate encoding. Adding a new
animation type costs zero struct changes.

### `SceneLayer` (~252 bytes)

```cpp
enum class PanelTargetMode : uint8_t { ALL, LIST, EXCLUDE };

struct SceneLayer {
    uint8_t         groupId;
    uint8_t         stepCount;
    PanelTargetMode targetMode;
    uint8_t         targetCount;
    char            palette[16];                              // empty = inherit scene-level
    uint8_t         targetList[SCENE_MAX_PANELS_PER_LAYER];   // 32 bytes
    SceneStep       steps[SCENE_MAX_STEPS];                   // 12 × 18 = 216 bytes
};
// = 4 + 16 + 32 + 216 = 268 bytes
```

8 layers × 268 = ~2.1 KB static in `ScenePlayer`. Still well within budget.

### `ScenePlayer`

```cpp
class ScenePlayer {
    AnimationScheduler& scheduler;
    PanelsInitializer&  initializer;
    PaletteStore&       paletteStore;            // for resolving palette names to GradientStop[16]

    SceneLayer  layers[SCENE_MAX_LAYERS];
    uint8_t     currentStep[SCENE_MAX_LAYERS];
    uint32_t    stepStartMs[SCENE_MAX_LAYERS];
    uint8_t     layerCount;
    bool        sceneLoop;
    bool        playing;
    char        sceneName[20];

    // Scene-level color state
    char                  scenePalette[16];   // scene default palette name
    Protocol::ColorRGB    baseColors[3];      // primary, secondary, tertiary
    GradientStop          resolvedPalettes[SCENE_MAX_LAYERS][PALETTE_STOPS];  // pre-resolved per-layer palettes (scene default if no override)

    void fireStep(uint8_t layerIdx, uint32_t nowMs);
    void resolvePanels(const SceneLayer&, uint8_t* out, uint8_t& count);
    bool validatePaletteSpatialSeparation();   // ensures layers with different palettes don't share panels
    void sendPaletteToPanels(uint8_t layerIdx);   // sends PACKET_SET_PALETTE to layer's targets
    void sendBaseColorsToAllPanels();

public:
    void loadFromContext(const SceneParseContext&);
    void play(uint32_t nowMs);                 // also pushes palettes + base colors to panels
    void stop();
    void tick(uint32_t nowMs);
    bool isPlaying() const;
    void writeStatusJson(AsyncResponseStream&) const;

    // Mid-flight control (also called by AppearanceStore on user-driven updates)
    void updateBaseColors(const Protocol::ColorRGB[3]);   // broadcasts PACKET_SET_BASE_COLORS
    void updateScenePalette(const char* paletteName);     // re-resolves + broadcasts
};
```

`resolvedPalettes[layer]` is the 16-stop palette to push to each layer's panels. Each entry is
populated at scene load by looking up the layer's palette (or scene default) on the controller's
palette filesystem. This pre-resolution means `play()` only needs to send the already-prepared
gradient — no JSON parse, no interpolation on the panel.

`tick()` for each active layer: if `nowMs - stepStartMs[i] >= steps[current].durationMs`,
advance step. If past last step and `sceneLoop`, reset all layers to step 0; else mark
layer inactive. On step advance, call `fireStep()` → `scheduler.playOnPanels(...)` for
panel-local, or `scheduler.addRunner(new <Runner>(...))` for runners. Runner choice
dispatched on `animType >= 64`.

### `AnimationServer`

```cpp
class AnimationServer {
    AsyncWebServer&               server;
    ScenePlayer&                  player;
    PanelsController&             panels;
    PanelsInitializer&            initializer;
    Lightnet::AnimationScheduler& scheduler;

    // SPIFFS I/O
    bool writeFileFromStream(const char* name, AsyncWebServerRequest*, SceneParseContext&);
    bool loadFromFile(const char* name);                  // streaming-parse file → ScenePlayer

    // Endpoint handlers
    void handleListScenes(AsyncWebServerRequest*);
    void handleGetScene(AsyncWebServerRequest*);          // raw SPIFFS passthrough
    void handleDeleteScene(AsyncWebServerRequest*);
    void handleOneShotPlay(AsyncWebServerRequest*);       // one-shot via SceneParseContext (single layer/step)
    void handleTrigger(AsyncWebServerRequest*, uint8_t group, uint8_t value);
    void handleStatus(AsyncWebServerRequest*);

public:
    AnimationServer(AsyncWebServer&, ScenePlayer&, PanelsController&,
                    PanelsInitializer&, Lightnet::AnimationScheduler&);
    void begin();
};
```

---

## WebSocket Extension

**`lib/Lightnet/MessageApi/MessageApi.hpp`**
```cpp
MSG_ANIMATION_TRIGGER = 8  // payload: uint8_t groupId, uint8_t value
```

**`MessageHandler`** accepts `AnimationScheduler*`, handles type 8 → `scheduler->triggerGroup(groupId, value)`.

---

## Memory Budget

### Controller (ESP8266, ~40 KB heap)

| Operation | Heap |
|---|---|
| Idle / playing | `ScenePlayer` ~2.7 KB static (layers 2.1 KB + resolvedPalettes 512 B + misc); 0 transient |
| `POST /api/scenes` | `SceneParseContext` ~2.2 KB stack + streaming parser ~300 B |
| `GET /api/scenes/:name` | SPIFFS file passthrough — 0 parse, 0 extra heap |
| `POST /api/scenes/:n/play` | Streaming parse from SPIFFS into existing `SceneLayer[]` — ~300 B transient |
| `POST /api/animations/play` (one-shot) | ~50 B stack |
| `GET /api/scenes/status` | ~256 B stack for JSON output |
| `PUT /api/colors` | ~64 B stack |
| `PUT /api/palette` | ~300 B (streaming-parse palette file from SPIFFS) |
| `PUT /api/brightness` | ~32 B stack |
| `PUT /api/appearance` (bulk) | ~400 B (parses any subset of fields, may parse palette) |
| Boot read of `/config/appearance.json` | ~256 B stack |

### Panel SRAM additions (per panel)

| Item | Bytes |
|---|---|
| `palette[16]` (GradientStop = 4 B each) | 64 |
| `baseColors[3]` (RGB) | 9 |
| `globalBrightness` | 1 |
| `AnimationState` growth: ColorRef × 2 per slot × 4 queue | 8 |
| **Total panel SRAM increase** | **~82 B** |

ATmega328: trivial. ATmega88PA-AU (1 KB SRAM): comfortably fits with current ~370 B baseline usage.
Flash code for palette interpolation: ~300-500 B.

ArduinoJson is not used. `json-streaming-parser2` is the only new dependency.

---

## Modified Files Summary

| File | Change |
|---|---|
| `lib/Lightnet/Common/LightnetConfig.hpp` | **new** — `LIGHTNET_MAX_PANELS`, `PALETTE_STOPS`, `BASE_COLORS_COUNT` |
| `lib/Lightnet/Common/Palette.hpp` | **new** — `GradientStop`, `Palette` types, interpolation helper |
| `lib/Lightnet/Common/ColorRef.hpp` | **new** — 4-byte `ColorRef` used in protocol + scene structs |
| `lib/Lightnet/Common/Protocol.hpp` | Add `PACKET_SET_PALETTE`, `PACKET_SET_BASE_COLORS`, `PACKET_SET_GLOBAL_BRIGHTNESS`. Modify `PACKET_ANIMATION_PREPARE` to carry `ColorRef` instead of `ColorRGB`. |
| `lib/Lightnet/Controller/ScenePlayer.hpp/.cpp` | **new** — scene-internal caps + sequence engine + palette/baseColors mgmt |
| `lib/Lightnet/Controller/AnimationServer.hpp/.cpp` | **new** — HTTP routes, streaming JSON parse, atomic file I/O, palette + brightness endpoints |
| `lib/Lightnet/Controller/PaletteStore.hpp/.cpp` | **new** — SPIFFS palette CRUD + built-in defaults seeded on first boot |
| `lib/Lightnet/Controller/AppearanceStore.hpp/.cpp` | **new** — owns `/config/appearance.json`; load on boot, atomic save on update, broadcasts state to panels |
| `lib/Lightnet/Controller/AnimationRunner.hpp/.cpp` | Replace 3× `MAX_PANELS` with `LIGHTNET_MAX_PANELS`. Runners now accept `ColorRef`. |
| `lib/Lightnet/Controller/AnimationScheduler.hpp/.cpp` | Default `maxPanels = LIGHTNET_MAX_PANELS`. PREPARE signature carries `ColorRef`. |
| `lib/Lightnet/Panel/LightnetPanel.hpp/.cpp` | Store `palette[16]`, `baseColors[3]`, `globalBrightness`. Handle new packets. |
| `lib/Lightnet/Panel/AnimationPlayer.hpp/.cpp` | Resolve `ColorRef` → RGB at frame time using panel's palette + base colors. Apply `globalBrightness` to output. |
| `lib/Lightnet/Panel/RGBController.hpp/.cpp` | Final-stage multiply by `globalBrightness/255` before LED write. |
| `lib/Lightnet/MessageApi/MessageApi.hpp` | Add `MSG_ANIMATION_TRIGGER = 8` |
| `lib/Lightnet/MessageApi/MessageHandler.hpp/.cpp` | Accept `AnimationScheduler*`, handle type 8 |
| `src/controller/main.h` | Include `AnimationServer.hpp` |
| `src/controller/main.cpp` | Hoist `SPIFFS.begin()` to case 0; instantiate `PaletteStore`, `AppearanceStore`, `ScenePlayer`, `AnimationServer`; `AppearanceStore::loadAndApply()` before `setupWiFi()`; add `animScheduler->tick()` + `scenePlayer->tick()` to loop |
| `lib/Lightnet/AppServer/AppServer.cpp` | Remove `SPIFFS.begin()` calls — SPIFFS is now mounted earlier in main |
| `platformio.ini` | Add `squix78/json-streaming-parser2` to controller envs |

---

## Wiring in `main.cpp`

`case 0` (post-discovery, before `setupWiFi()` — WiFi config portal can block up to 30s, so
appearance must be applied to panels before that):

```cpp
// SPIFFS used to be mounted inside AppServer (during setupWiFi). Hoist here so PaletteStore
// and AppearanceStore can read /palettes/ and /config/ before WiFi setup begins.
#ifdef ARDUINO_ARCH_ESP32
SPIFFS.begin(true);
#else
SPIFFS.begin();
#endif

animScheduler  = new Lightnet::AnimationScheduler();
animScheduler->initialize();

paletteStore   = new PaletteStore();
paletteStore->seedBuiltInsIfMissing();          // creates rainbow/lava/ocean/... on first boot

scenePlayer    = new ScenePlayer(*animScheduler, LNPanelsInitializer, *paletteStore);

appearance     = new AppearanceStore(*panelsController, *paletteStore);
appearance->loadAndApply();                      // reads /config/appearance.json, broadcasts to panels
```

`AppServer` (constructed later in `setupWiFi`) should stop calling `SPIFFS.begin()` since it's
already mounted — change it to just register the static route.

After `setupWiFi()` (HTTP server is now up):
```cpp
animServer = new AnimationServer(*webServer, *scenePlayer, *panelsController,
                                  LNPanelsInitializer, *animScheduler,
                                  *paletteStore, *appearance);
animServer->begin();
```

`case 1` main loop (two new ticks):
```cpp
animScheduler->tick(millis());   // fix: currently missing
scenePlayer->tick(millis());
```

---

## File Creation Order

1. `Common/LightnetConfig.hpp` (minimal)
2. `Common/Palette.hpp` (GradientStop, interpolation)
3. `Common/ColorRef.hpp`
4. Modify `Common/Protocol.hpp` — new packets, `PREPARE` carries `ColorRef`
5. Panel firmware updates (`LightnetPanel`, `AnimationPlayer`, `RGBController`) — palette/baseColors/globalBrightness state, packet handlers, frame-time color resolution
6. Modify `Controller/AnimationRunner.hpp/.cpp` (ColorRef wiring)
7. Modify `Controller/AnimationScheduler.hpp/.cpp`
8. `Controller/PaletteStore.hpp/.cpp` (with built-in palettes seeded on first boot)
9. `Controller/AppearanceStore.hpp/.cpp` (owns `/config/appearance.json`)
10. `Controller/ScenePlayer.hpp/.cpp`
11. `Controller/AnimationServer.hpp/.cpp` (all HTTP endpoints, delegating persistence to `AppearanceStore`)
12. Modify `MessageApi/MessageApi.hpp`, `MessageHandler.hpp/.cpp`
13. Modify `src/controller/main.h`, `main.cpp`
14. Modify `platformio.ini`

---

## Verification

**Build**
1. `pio run -e initializer_wemos` compiles cleanly

**Basic flow**
2. `POST /api/scenes` valid JSON → `200 {"saved":"test"}`
3. `GET /api/scenes` → `[{"name":"test"}]`
4. `GET /api/scenes/test` → JSON round-trips
5. `POST /api/scenes/test/play` → panels animate
6. `GET /api/scenes/status` → reflects current scene
7. `POST /api/scenes/stop`

**Sequencing & layers**
8. 2-step sequence auto-advances
9. 2-layer scene with different groups runs simultaneously
10. Runner step followed by panel-local step in same layer
11. `"loop":true` scene restarts from step 0

**One-shot + reactive**
12. `POST /api/animations/play` while a scene is playing → notification fires on free group without interrupting scene
13. WebSocket `MSG_ANIMATION_TRIGGER` → `triggerGroup()` called

**Validation — all expect HTTP 422**
14. `"group": 0` reserved
15. Duplicate group across layers
16. `"duration": 0` on non-last step
17. `type` + `runner` both present
18. `"brightnessTo": 300`
19. `"color": "#ZZZZZZ"`
20. Name with `/` or `..`
21. More than `SCENE_MAX_LAYERS` layers
22. `params.length: 5`

**Schema versioning**
23. POST scene with `"schemaVersion": 99` → `409 {"error":"schema_too_new","scene":99,"firmware":1}`
24. POST scene without `schemaVersion` field → treated as 1, accepted
25. POST scene with `"schemaVersion": 1` and an unrecognized optional field (e.g. `"future_field": 42`) → field silently ignored, scene saves successfully

**Atomic save**
26. Save a valid scene, then save an invalid one with same name → original file remains intact, `.tmp` file is cleaned up
27. Verify `/scenes/<name>.json.tmp` does not exist after either success or failure

**Appearance — persistence**
28. Fresh boot, no `/config/appearance.json` → file created with defaults (brightness 255, white base, `userColors`)
29. `PUT /api/brightness {"value":128}` → file updates to `brightness:128`
30. `PUT /api/colors {"primary":"#FF0000"}` (partial) → only `primary` updated in file, secondary/tertiary preserved
31. `PUT /api/palette {"palette":"lava"}` → file updates; `palette` field is "lava"
32. `PUT /api/appearance` with all three keys → atomic update of all three in one write
33. Reboot controller → `appearance.json` parsed, panels receive `PACKET_SET_GLOBAL_BRIGHTNESS`, `PACKET_SET_BASE_COLORS`, `PACKET_SET_PALETTE` after discovery completes
34. `GET /api/appearance` returns full current state
35. Hex-edit `appearance.json` schemaVersion to 99 → boot logs warning, falls back to defaults

**Global brightness mid-flight**
36. `PUT /api/brightness {"value":128}` while a scene is playing → panels visibly dim immediately, animation continues uninterrupted
37. `PUT /api/brightness {"value":300}` → 422, no change applied or persisted

**Base colors mid-flight**
38. Play scene with `palette: "userColors"` → colors come from current base colors (whatever they were last set to)
39. `PUT /api/colors {"primary":"#FF0000"}` mid-animation → panels visibly update next frame, no restart, elapsed time preserved
40. `PUT` with bad hex `"#ZZZZZZ"` → 422, no change applied or persisted

**Palette CRUD + swap**
41. `POST /api/palettes` with valid shape → 200, file at `/palettes/<name>.json`
42. `GET /api/palettes` lists user palette + built-ins
43. `PUT /api/palette {"palette":"lava"}` mid-animation → panels visibly retint, animation continues, `appearance.json` updated
44. `PUT /api/palette {"palette":"doesnotexist"}` → 404, no change applied or persisted
45. `DELETE /api/palettes/lava` (built-in) → 403, file untouched

**Scene-overrides-appearance behavior**
46. Set appearance brightness=200, palette=lava; load scene with own `colors`+`palette: "ocean"` → after scene start, appearance shows `palette: ocean` and scene's base colors; brightness stays 200 (scenes don't override brightness)
47. Stop scene → palette/colors stay at scene's values (do not revert)

**Per-layer palette spatial validation**
48. POST scene with two layers, different palettes (after applying scene-default), overlapping panel targets → 422 `"layers_share_panel_with_different_palette"`
49. POST same scene with non-overlapping panel targets → 200; verify each panel receives the correct palette via `PACKET_SET_PALETTE`

**Mid-flight palette + layer override interaction**
50. Scene has layer A (no override → scene-default "lava") and layer B (override "ocean") on non-overlapping panels. `PUT /api/palette {"palette":"rainbow"}` → layer A panels switch to rainbow; layer B panels stay on ocean.
51. With no scene playing, `PUT /api/palette {"palette":"forest"}` → all panels receive `PACKET_SET_PALETTE` with forest.
