# Animations, Scenes & Palettes

This document covers everything needed to drive the Lightnet animation system via HTTP and WebSocket. It assumes the controller firmware from the `scenes` branch is running.

---

## Table of Contents

1. [Core Concepts](#1-core-concepts)
2. [Global Appearance Control](#2-global-appearance-control)
3. [Palettes](#3-palettes)
4. [Scene Structure](#4-scene-structure)
5. [Animation Types](#5-animation-types)
6. [Controller Runners](#6-controller-runners)
7. [Color References](#7-color-references)
8. [HTTP API Reference](#8-http-api-reference)
9. [WebSocket Triggers](#9-websocket-triggers)
10. [Sequencing & Timing](#10-sequencing--timing)
11. [Notifications & One-Shot Animations](#11-notifications--one-shot-animations)
12. [Examples](#12-examples)

---

## 1. Core Concepts

### Scenes

A **scene** is the top-level playback unit. It groups one or more layers that run simultaneously. A scene can loop indefinitely or play once and stop.

```
Scene
├── Layer 1  (group 1, all panels, sequence of steps)
├── Layer 2  (group 2, panels 0-5, single runner step)
└── Layer 3  (group 3, panels 6-11, single looping animation)
```

Scenes are stored as JSON files on the controller's SPIFFS filesystem at `/scenes/<name>.json`. The HTTP body and the stored file share the same JSON format.

### Layers

A **layer** is an independent animation track inside a scene. Each layer:
- Targets a set of panels (`"all"`, a list, or an exclude list)
- Belongs to a **group ID** (1–254) — panels run all groups concurrently without interference
- Runs a sequence of steps back-to-back, advancing automatically when the current step ends

Because groups are independent, panels can run several overlapping layers simultaneously. A panel playing group 1 (ambient breathe) and group 2 (notification pulse) at the same time works without any interaction.

### Steps

A **step** is a single animation segment within a layer's sequence. Steps are executed in order, advancing automatically when `durationMs` elapses. A step can be:
- A **panel-local animation** (`"type": "BREATHE"`, etc.) — runs entirely on the ATmega with zero per-frame I²C traffic
- A **controller runner** (`"runner": "WAVE"`, etc.) — computed on the ESP each frame, sends per-panel brightness over I²C

### Groups

Groups are the synchronisation unit. When the controller fires a `GENERAL CALL START` on group 3, every panel that has an animation queued for group 3 starts simultaneously (±2.5 µs jitter). Groups 1–254 are valid; 0 is reserved for system use.

The **group ID is unique per layer within a scene** — the controller validates this on save.

### Palettes

A **palette** is a 16-stop gradient of `(position, R, G, B)` entries. The controller linearly interpolates between stops to produce smooth colour transitions. Every animation step references colour through a palette position, a base-colour slot, or an explicit inline RGB value — all three are unified under the same `ColorRef` mechanism.

See [Section 3](#3-palettes) and [Section 7](#7-color-references) for details.

---

## 2. Global Appearance Control

Three values define the current look of all panels, independent of any playing scene:

| Setting | Range | Description |
|---|---|---|
| **Brightness** | 0–255 | Global multiplier applied on top of every animation's per-frame brightness |
| **Base Colors** | 3 × #RRGGBB | Primary, secondary, tertiary colours used by the `userColors` palette |
| **Palette** | name string | Currently active gradient palette (e.g. `"lava"`) |

These are persisted in `/config/appearance.json` and restored on every boot. They take effect immediately — the panel resolves all animation colours at frame time using the current palette/base-colours, so a mid-flight change appears on the very next rendered frame without restarting anything.

### Persistence file

```json
{
  "schemaVersion": 1,
  "brightness": 192,
  "baseColors": ["#FF4400", "#FF8800", "#000000"],
  "palette": "lava"
}
```

When a scene loads with its own `"colors"` and `"palette"` fields, those values overwrite the active appearance and are persisted. Stopping a scene does **not** revert the appearance — the last-applied values remain.

### HTTP endpoints

```http
GET  /api/appearance
PUT  /api/appearance        {"brightness":192, "baseColors":["#FF4400","#FF8800","#000000"], "palette":"lava"}

GET  /api/brightness
PUT  /api/brightness        {"value": 128}

GET  /api/colors
PUT  /api/colors            {"primary":"#FF0000"}          ← any subset; unspecified slots unchanged

GET  /api/palette
PUT  /api/palette           {"palette": "lava"}            ← name must exist in PaletteStore
```

All `PUT` endpoints:
- Return `200 {}` on success
- Return `422 {"error":"..."}` on invalid input (bad hex, unknown palette, out-of-range value)
- Persist atomically to SPIFFS (write `.tmp` → rename) and broadcast to panels

---

## 3. Palettes

### What a palette is

A palette is an array of 1–16 gradient stops, each with a position (0–255) and an RGB colour. The controller linearly interpolates between adjacent stops to produce a continuous 256-entry gradient.

Palette JSON schema:
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

Rules:
- Positions must be strictly increasing, 0–255.
- First stop must have position 0; last must have position 255.
- 1–16 stops. Fewer stops = coarser gradient.

### Built-in palettes

These are always available; they cannot be deleted:

| Name | Description |
|---|---|
| `rainbow` | Full hue spectrum |
| `lava` | Black → dark red → orange → white |
| `ocean` | Dark navy → teal → bright cyan/white |
| `forest` | Dark green → bright lime |
| `party` | Purple → magenta → orange → yellow → cyan |
| `sunset` | Deep purple → warm red → golden orange |
| `aurora` | Dark teal → bright green → purple → pink |
| `embers` | Black → dark red → bright orange-gold |

### Special palette: `userColors`

`userColors` is a synthetic palette built from the current base colours at the moment it is pushed to the panels. It is not stored as a file. When selected:

```
stop[0] = (position=0,   color=primary)
stop[1] = (position=128, color=secondary)
stop[2] = (position=255, color=tertiary)
```

Animations that reference palette positions 0, 128, and 255 will use the primary, secondary, and tertiary base colours respectively. Positions between stops are linearly interpolated.

This is the default palette for new scenes — if no `"palette"` field is specified, animations track the three base colours.

### Palette HTTP API

```http
GET    /api/palettes               → ["rainbow","lava","ocean","forest","party","sunset","aurora","embers","userColors",...]
GET    /api/palettes/:name         → palette JSON
POST   /api/palettes               body: palette JSON  → saves to /palettes/<name>.json
DELETE /api/palettes/:name         → 403 if built-in, 200 otherwise
```

### Per-layer palette override

A layer can specify its own palette, overriding the scene-level default for the panels it targets:

```json
{
  "group": 2,
  "panels": [0, 1, 2],
  "palette": "ocean",
  "sequence": [...]
}
```

**Constraint**: panels with a per-layer palette override must not overlap with any other layer that uses a different palette. The controller validates this at scene-save time and returns `422` if violated, because each panel stores only one palette at a time.

---

## 4. Scene Structure

### Full scene example

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
  "layers": [
    {
      "group": 1,
      "panels": "all",
      "sequence": [
        {
          "type": "TRANSITION",
          "colorFrom": {"useColor": 2},
          "colorTo":   {"useColor": 0},
          "brightnessFrom": 0,
          "brightnessTo": 220,
          "duration": 3000
        },
        {
          "type": "BREATHE",
          "color": {"useColor": 0},
          "brightnessFrom": 60,
          "brightnessTo": 220,
          "duration": 4000,
          "loop": true
        }
      ]
    },
    {
      "group": 2,
      "panels": "all",
      "sequence": [
        {
          "runner": "WAVE",
          "color": {"palette": 200},
          "duration": 8000,
          "params": [3]
        }
      ]
    }
  ]
}
```

### Field reference

| Field | Required | Default | Description |
|---|---|---|---|
| `schemaVersion` | No | 1 | Schema version check. `409` if greater than firmware's version. |
| `name` | No | — | 1–19 chars, `[a-zA-Z0-9_-]`. Required when saving via `POST /api/scenes`. |
| `loop` | No | `false` | When `true`, all layers restart from step 0 after their last step completes. |
| `colors` | No | white/black/black | Scene's base colours for `userColors` palette and `{"useColor":N}` references. |
| `palette` | No | `"userColors"` | Active palette for all layers that don't have their own override. |
| `layers` | Yes | — | Array of 1–8 layer objects. |

### Panel targeting

```json
"panels": "all"              // all discovered panels
"panels": [0, 2, 5]          // specific panel indices (0-based)
"panels": {"exclude": [3]}   // all panels except listed indices
```

Panel indices are assigned during discovery in tree-traversal order. Up to 32 panels per layer targeting list.

---

## 5. Animation Types

All panel-local animations run entirely on the ATmega with **zero per-frame I²C traffic**. The controller sends a single `PACKET_ANIMATION_PREPARE` + `GENERAL CALL START` to set them in motion.

### Common step fields

| Field | Type | Notes |
|---|---|---|
| `type` | string | Animation type name (see below) |
| `colorFrom` / `colorTo` | color ref | Start and end colours — see [Section 7](#7-color-references) |
| `color` | color ref | Alias for `colorTo` when only one colour is needed |
| `brightnessFrom` | 0–255 | Starting brightness (default 255) |
| `brightnessTo` | 0–255 | Ending brightness (default 255) |
| `duration` | 0–65535 ms | Animation duration. `0` = infinite, valid only on the last step of a looping scene. |
| `loop` | bool | Loop this individual step indefinitely (FLAG_LOOP). |
| `pingpong` | bool | Reverse direction at end instead of looping (FLAG_PINGPONG). |
| `params` | array, ≤4 × 0–255 | Type-specific parameters (see each type). |

### SOLID

Holds a static colour and brightness. Use as the last step to freeze the panel.

```json
{"type": "SOLID", "color": "#FF4400", "brightnessTo": 200}
```

No parameters.

### FADE

Linearly interpolates brightness from `brightnessFrom` to `brightnessTo`. The colour stays fixed at `colorTo`.

```json
{"type": "FADE", "color": "#0044FF", "brightnessFrom": 255, "brightnessTo": 0, "duration": 1500}
```

No parameters.

### TRANSITION

Simultaneously interpolates both colour (from `colorFrom` to `colorTo`) and brightness.

```json
{
  "type": "TRANSITION",
  "colorFrom": "#000000",
  "colorTo":   "#FF4400",
  "brightnessFrom": 0,
  "brightnessTo": 200,
  "duration": 2000
}
```

No parameters.

### BREATHE

Sinusoidal (parabolic-approximation) brightness envelope: rises to `brightnessTo`, falls back to `brightnessFrom`, and repeats. Colour is fixed at `colorTo`.

```json
{"type": "BREATHE", "color": "#0088FF", "brightnessFrom": 10, "brightnessTo": 220, "duration": 3000, "loop": true}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Speed multiplier (reserved, currently unused) | 0 |

### PULSE

3-phase (rise → hold → fall) brightness flash. Colour is fixed at `colorTo`.

```json
{"type": "PULSE", "color": "#FFFFFF", "brightnessFrom": 0, "brightnessTo": 255, "duration": 600, "params": [64, 128, 64]}
```

| params index | Meaning | Range |
|---|---|---|
| `params[0]` | Rise phase proportion (0–255 of total duration) | 0–255 |
| `params[1]` | Hold phase proportion | 0–255 |
| `params[2]` | Fall phase proportion | 0–255 |

Rise + hold + fall should sum to ≤ 255. The remainder is split proportionally if they exceed 255.

### BLINK

Binary on/off at a fixed period. On = `brightnessTo`, off = `brightnessFrom`. Colour from `colorTo`.

```json
{"type": "BLINK", "color": "#FF0000", "brightnessFrom": 0, "brightnessTo": 255, "duration": 0, "loop": true, "params": [50]}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Half-period in ms (on time = off time = this value) | 1 |

### HUE_CYCLE

6-step integer HSV→RGB rainbow rotation. Ignores `colorFrom`/`colorTo`.

```json
{"type": "HUE_CYCLE", "brightnessTo": 200, "duration": 0, "loop": true, "params": [10]}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Rotation speed (higher = faster) | 1 |

### STROBE

Binary flash at a frequency in Hz. On = `brightnessTo`, off = 0. Colour from `colorTo`.

```json
{"type": "STROBE", "color": "#FFFFFF", "brightnessTo": 255, "duration": 2000, "params": [20]}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Frequency in Hz | 1 |

### REACTIVE

Decay-model animation triggered by WebSocket beats. Rests at `colorFrom`/`brightnessFrom`; on trigger, instantly jumps to `colorTo`/`brightnessTo` then decays back over time.

```json
{
  "type": "REACTIVE",
  "colorFrom":      "#110000",
  "colorTo":        "#FF8800",
  "brightnessFrom": 20,
  "brightnessTo":   255,
  "duration": 0,
  "params": [180]
}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Decay rate (units per second, 0–255). Higher = faster decay. | 0 |

Triggers are sent via WebSocket (see [Section 9](#9-websocket-triggers)).

---

## 6. Controller Runners

Runners are computed on the controller ESP each frame and send per-panel brightness over I²C. They appear as steps with a `"runner"` field instead of `"type"`.

Common runner step fields:

| Field | Type | Notes |
|---|---|---|
| `runner` | string | Runner name (`WAVE`, `RIPPLE`, `CHASE`) |
| `color` | color ref | Colour sent to all panels before the runner begins |
| `duration` | ms | Total duration of the runner effect |
| `params` | array | Runner-specific parameters |

### WAVE

A brightness envelope (triangular wave) sweeps from one end of the panel list to the other.

```json
{
  "runner": "WAVE",
  "color": {"palette": 128},
  "duration": 5000,
  "params": [3]
}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Wave width in panels (how many panels are illuminated at peak) | 3 |

### RIPPLE

A brightness ring expands outward from an origin panel. Distance is based on index, not physical position.

```json
{
  "runner": "RIPPLE",
  "color": "#FF4400",
  "duration": 2000,
  "params": [2, 0]
}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Ring width in panels | 2 |
| `params[1]` | Origin panel index | 0 |

### CHASE

A single lit panel travels through the panel list over the duration.

```json
{
  "runner": "CHASE",
  "color": {"useColor": 0},
  "duration": 3000
}
```

No parameters.

---

## 7. Color References

Every colour field in a step (`colorFrom`, `colorTo`, `color`) accepts any of three forms:

### 1. Inline RGB

```json
"color": "#FF4400"
"color": {"r": 255, "g": 68, "b": 0}
```

The RGB value is stored directly in the step. The panel uses it as-is. This is the `ColorRef{kind=0}` path.

### 2. Palette position

```json
"colorTo": {"palette": 200}
```

Samples the active palette (the one pushed to the panel for this layer) at position 0–255. The panel resolves this at frame time — if the active palette changes mid-flight, the colour updates on the next frame. This is `ColorRef{kind=1}`.

### 3. Base colour slot

```json
"color": {"useColor": 0}   // primary
"color": {"useColor": 1}   // secondary
"color": {"useColor": 2}   // tertiary
```

References one of the three scene (or global) base colours. The panel resolves against its current `baseColors` state. Updating base colours via `PUT /api/colors` while an animation using `useColor` is running will change the displayed colour on the next frame. This is `ColorRef{kind=2}`.

---

## 8. HTTP API Reference

All endpoints are on port 80 (`http://lightnet-<chipid>.local`).

### Appearance

| Method | Path | Body / Response |
|---|---|---|
| `GET` | `/api/appearance` | `{"brightness":N,"baseColors":["#..","#..","#.."],"palette":"..."}` |
| `PUT` | `/api/appearance` | Same shape, any subset of fields |
| `GET` | `/api/brightness` | `{"value":N}` |
| `PUT` | `/api/brightness` | `{"value":N}` — N: 0–255 |
| `GET` | `/api/colors` | `{"primary":"#..","secondary":"#..","tertiary":"#.."}` |
| `PUT` | `/api/colors` | Any subset of `primary`/`secondary`/`tertiary` |
| `GET` | `/api/palette` | `{"palette":"lava"}` |
| `PUT` | `/api/palette` | `{"palette":"lava"}` — must be a known palette name |

### Palettes (library management)

| Method | Path | Body / Response |
|---|---|---|
| `GET` | `/api/palettes` | `["rainbow","lava",...]` |
| `GET` | `/api/palettes/:name` | Palette JSON |
| `POST` | `/api/palettes` | Palette JSON body |
| `DELETE` | `/api/palettes/:name` | 403 for built-ins |

### Scenes (scene library)

| Method | Path | Body / Response |
|---|---|---|
| `POST` | `/api/scenes` | Scene JSON body — saves to `/scenes/<name>.json` |
| `GET` | `/api/scenes` | `[{"name":"sunset"},...]` |
| `GET` | `/api/scenes/:name` | Scene JSON (raw file passthrough) |
| `DELETE` | `/api/scenes/:name` | — |

### Scene playback

| Method | Path | Body / Response |
|---|---|---|
| `POST` | `/api/scenes/play` | Scene JSON body (inline play, not saved) or `{"name":"sunset"}` (play stored) |
| `POST` | `/api/scenes/:name/play` | — (plays stored scene by name) |
| `POST` | `/api/scenes/stop` | — |
| `GET` | `/api/scenes/status` | `{"playing":true,"name":"sunset","loop":true,"layers":[{"group":1,"step":0,"total":2},...]}` |

### One-shot / triggers

| Method | Path | Body / Response |
|---|---|---|
| `POST` | `/api/animations/play` | Single-layer scene JSON (no SPIFFS, no scene state, free group ID) |
| `POST` | `/api/animations/trigger` | `{"group":1,"value":200}` — fires a REACTIVE beat |

### Error responses

| Code | Meaning |
|---|---|
| `200 {}` | Success |
| `404 {"error":"not_found"}` | Scene / palette doesn't exist |
| `409 {"error":"schema_too_new","scene":N,"firmware":M}` | Scene file has a newer schema version than the firmware supports |
| `422 {"error":"<message>"}` | Validation failure — no changes applied |

---

## 9. WebSocket Triggers

Connect to `ws://lightnet-<chipid>.local/ws` using the binary MessageApi protocol.

To fire a beat trigger for a REACTIVE animation on group 1 with peak level 200:

```
MSG_ANIMATION_TRIGGER (type=8) payload:
  uint8_t groupId = 1
  uint8_t value   = 200   // 0-255 peak level
```

The controller broadcasts `PACKET_ANIMATION_UPDATE_PARAMS` with `PARAM_TRIGGER` to all panels in that group. Each panel running a REACTIVE animation on that group instantly jumps to `brightnessTo`/`colorTo` and begins decaying at its configured `decayRate`.

For music sync, fire triggers on beat events. At 120 BPM the inter-beat window is 500 ms. The controller spends only ~140 µs of I²C time per trigger; between triggers there is zero I²C traffic.

---

## 10. Sequencing & Timing

### Step advancement

Steps within a layer advance automatically when `durationMs` elapses. The controller checks elapsed time each pass through the main loop. There is no callback mechanism — advancement is purely time-based.

```
Layer starts at t=0
  Step 0 (TRANSITION, 2000ms)  → plays 0–2000ms
  Step 1 (BREATHE, 3000ms)     → plays 2000–5000ms
  Step 2 (FADE, 1000ms)        → plays 5000–6000ms
  Scene loop? → restart all layers at t=6000ms (if loop=true)
```

Multiple layers within a scene run in parallel from the same start moment. Each layer advances independently.

### Infinite steps

`"duration": 0` means the step runs indefinitely. This is only valid as the **last step** of a layer in a **looping scene**. Using duration 0 on a non-last step is a validation error.

### Loop flag on individual steps

Setting `"loop": true` on a step (FLAG_LOOP) causes the panel to loop that animation type continuously on its own, independent of the scene-level loop. The scene player still advances to the next step after `durationMs` — the FLAG_LOOP tells the panel-side AnimationPlayer what to do inside that window.

### Scene loop vs step loop

| `scene.loop: true` | Layer restarts from step 0 after all steps complete |
| `step.loop: true`  | The animation type cycles within this step's `durationMs` window |

Both can be combined: a BREATHE with `loop: true` and a finite `durationMs` breathes continuously for that duration, then the scene advances to the next step.

### Runners in sequences

Runners can be mixed with panel-local steps in the same sequence:

```json
"sequence": [
  {"runner": "RIPPLE", "color": "#FF4400", "duration": 1500, "params": [2, 0]},
  {"type": "FADE", "color": "#FF4400", "brightnessTo": 0, "duration": 800}
]
```

The controller finishes the runner (waits for `durationMs`) before firing the next step to panels.

---

## 11. Notifications & One-Shot Animations

The scene player is single-instance — only one scene plays at a time. Notifications that should appear *over* an ambient scene use a **free group ID** that doesn't conflict with the scene.

Use `POST /api/animations/play` to send a single-layer animation directly, bypassing the scene system:

```json
{
  "group": 250,
  "panels": "all",
  "sequence": [
    {"type": "PULSE", "color": "#FF0000", "brightnessFrom": 0, "brightnessTo": 255, "duration": 500, "params": [64, 128, 64]},
    {"type": "FADE", "color": "#FF0000", "brightnessTo": 0, "duration": 400}
  ]
}
```

The notification runs on group 250 while the ambient scene continues on groups 1–N. The panel's AnimationPlayer handles both groups independently. No SPIFFS involved.

---

## 12. Examples

### Example 1 — Ambient breathe (single colour)

```json
{
  "name": "warm-breathe",
  "loop": true,
  "colors": {"primary": "#FF8800"},
  "palette": "userColors",
  "layers": [
    {
      "group": 1,
      "panels": "all",
      "sequence": [
        {"type": "BREATHE", "color": {"useColor": 0},
         "brightnessFrom": 20, "brightnessTo": 200,
         "duration": 4000, "loop": true}
      ]
    }
  ]
}
```

Change colour mid-flight: `PUT /api/colors {"primary":"#0044FF"}` — the breathe immediately shifts to blue on the next frame.

---

### Example 2 — Colour wash with wave overlay

```json
{
  "name": "lava-wave",
  "loop": true,
  "palette": "lava",
  "layers": [
    {
      "group": 1,
      "panels": "all",
      "sequence": [
        {"type": "SOLID", "color": {"palette": 128}, "brightnessTo": 80}
      ]
    },
    {
      "group": 2,
      "panels": "all",
      "sequence": [
        {"runner": "WAVE", "color": {"palette": 220}, "duration": 4000, "params": [4]},
        {"type": "SOLID", "color": {"palette": 128}, "brightnessTo": 80, "duration": 1000}
      ]
    }
  ]
}
```

Group 1 holds a static dim background. Group 2 cycles: bright wave sweeps across, then holds the background level for a beat, then repeats.

---

### Example 3 — Music-reactive fire

```json
{
  "name": "fire-reactive",
  "loop": true,
  "palette": "embers",
  "layers": [
    {
      "group": 1,
      "panels": "all",
      "sequence": [
        {
          "type": "REACTIVE",
          "colorFrom":      {"palette": 50},
          "colorTo":        {"palette": 220},
          "brightnessFrom": 20,
          "brightnessTo":   255,
          "duration": 0,
          "params": [210]
        }
      ]
    }
  ]
}
```

Send WebSocket trigger on every beat: `MSG_ANIMATION_TRIGGER group=1 value=255`. Panels jump to the bright orange end of the embers palette and decay over ~1.2 s.

Adjust `params[0]` (decay rate) to match the tempo: `210` at 120 BPM decays just before the next beat, creating a punchy flash. Lower values produce a slower glow.

---

### Example 4 — Scene with two spatial zones, different palettes

```json
{
  "name": "split-zones",
  "loop": true,
  "layers": [
    {
      "group": 1,
      "panels": [0, 1, 2, 3, 4],
      "palette": "ocean",
      "sequence": [
        {"type": "HUE_CYCLE", "brightnessTo": 180, "duration": 0, "loop": true, "params": [8]}
      ]
    },
    {
      "group": 2,
      "panels": [5, 6, 7, 8, 9],
      "palette": "lava",
      "sequence": [
        {"type": "BREATHE", "color": {"palette": 200}, "brightnessFrom": 30, "brightnessTo": 220, "duration": 3000, "loop": true}
      ]
    }
  ]
}
```

Panels 0–4 cycle through ocean hues. Panels 5–9 breathe lava orange. Spatial palette override is valid because the two panel sets don't overlap.

---

### Example 5 — Boot-up sequence that settles into ambient

```json
{
  "name": "startup",
  "loop": false,
  "palette": "rainbow",
  "layers": [
    {
      "group": 1,
      "panels": "all",
      "sequence": [
        {"runner": "WAVE",  "color": {"palette": 0},   "duration": 1500, "params": [5]},
        {"runner": "RIPPLE","color": {"palette": 128},  "duration": 1200, "params": [3, 0]},
        {"type": "TRANSITION",
         "colorFrom": {"palette": 200},
         "colorTo":   {"useColor": 0},
         "brightnessFrom": 255, "brightnessTo": 150,
         "duration": 2000},
        {"type": "BREATHE",
         "color": {"useColor": 0},
         "brightnessFrom": 80, "brightnessTo": 180,
         "duration": 0}
      ]
    }
  ]
}
```

Play once (`loop: false`). The sequence runs wave → ripple → colour transition → infinite breathe in the base colour.

---

## Appendix: Validation Rules

These apply to `POST /api/scenes` and `POST /api/scenes/play`. All violations return `HTTP 422`.

| Field | Rule |
|---|---|
| Scene name | `[a-zA-Z0-9_-]`, 1–19 chars |
| Layer count | 1–8 |
| Steps per layer | 1–12 |
| `group` | 1–254; unique within the scene |
| `type` + `runner` | Mutually exclusive — cannot both be set |
| `type` value | Must be a known animation type string |
| `runner` value | `WAVE`, `RIPPLE`, or `CHASE` |
| `duration` | 0–65535 ms; 0 only on the last step of a looping scene |
| Color values | Valid `#RRGGBB` hex, `{r,g,b}` each 0–255, `{"palette":0-255}`, or `{"useColor":0-2}` |
| `brightness*` | 0–255 |
| `params[i]` | 0–255 |
| `params` length | 0–4 |
| Panel indices | 0–(discovered panel count − 1) |
| Panel list length | 1–32 per layer |
| Layer palette override | Layers with different effective palettes must not share any target panel |
| Infinite last step | Only valid when `scene.loop: true` |
