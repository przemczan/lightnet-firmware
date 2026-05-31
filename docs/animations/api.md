---
icon: material/api
---

# API & Examples

Global appearance control, the full HTTP endpoint surface, WebSocket triggers, one-shot notifications, five worked scene examples, and the validation rule appendix.

---

## Global Appearance Control

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

!!! info "Scene play does not persist appearance"
    Playing a scene does **not** change or persist the global appearance state. The scene's `"colors"` and `"palette"` fields define the colour context for that scene's animations only. To change the persistent appearance alongside a scene play, call the appearance endpoints explicitly before or after.

### HTTP endpoints

```http
GET    /api/appearance
PATCH  /api/appearance        {"brightness":192, "baseColors":["#FF4400","#FF8800","#000000"], "palette":"lava"}
```

All fields are optional — omit any you don't want to change. Returns `200 {}` on success, `422 {"error":"..."}` on invalid input, persists atomically to SPIFFS, and broadcasts to panels.

---

## HTTP API Reference

All endpoints are on port 80 (`http://lightnet-<chipid>.local`).

### Appearance

| Method | Path | Body / Response |
|---|---|---|
| `GET` | `/api/appearance` | `{"brightness":N,"baseColors":["#..","#..","#.."],"palette":"..."}` |
| `PUT` | `/api/appearance` | Same shape, any subset of fields |
| `GET` | `/api/appearance/brightness` | `{"value":N}` |
| `PUT` | `/api/appearance/brightness` | `{"value":N}` — N: 0–255 |
| `GET` | `/api/appearance/colors` | `{"primary":"#..","secondary":"#..","tertiary":"#.."}` |
| `PUT` | `/api/appearance/colors` | Any subset of `primary`/`secondary`/`tertiary` |
| `GET` | `/api/appearance/palette` | `{"palette":"lava"}` |
| `PUT` | `/api/appearance/palette` | `{"palette":"lava"}` — must be a known palette name |

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
| `POST` | `/api/scenes/play` | Full scene JSON body — plays inline, not saved to SPIFFS |
| `POST` | `/api/scenes/:name/play` | — (plays stored scene by name) |
| `POST` | `/api/scenes/stop` | — |
| `POST` | `/api/scenes/speed` | `{"speed":<float>}` — change playback speed [0.1, 10.0] while playing |
| `GET` | `/api/scenes/status` | `{"playing":false}` or `{"playing":true,"scene":"sunset","loop":true,"layers":2,"speed":1.0}` |

### One-shot / triggers

| Method | Path | Body / Response |
|---|---|---|
| `POST` | `/api/animations/play` | Flat layer object: `{"group":N,"panels":..., "type":"BREATHE","color":"#FF0000","duration":2000}` |
| `POST` | `/api/animations/trigger` | `{"group":1,"value":200}` — fires a REACTIVE beat |

### Error responses

| Code | Meaning |
|---|---|
| `200 {}` | Success |
| `404 {"error":"not_found"}` | Scene / palette doesn't exist |
| `409 {"error":"schema_too_new","scene":N,"firmware":M}` | Scene file has a newer schema version than the firmware supports |
| `422 {"error":"<message>"}` | Validation failure — no changes applied |

---

## WebSocket Triggers

Connect to `ws://lightnet-<chipid>.local/ws` using the binary WebsocketApi protocol.

To fire a beat trigger for a REACTIVE animation on group 1 with peak level 200:

```
MSG_ANIMATION_TRIGGER (type=8) payload:
  uint8_t groupId = 1
  uint8_t value   = 200   // 0-255 peak level
```

The controller broadcasts `PACKET_ANIMATION_UPDATE_PARAMS` with `PARAM_TRIGGER` to all panels in that group. Each panel running a REACTIVE animation on that group instantly jumps to `brightnessTo`/`colorTo` and begins decaying at its configured `decayRate`.

For music sync, fire triggers on beat events. At 120 BPM the inter-beat window is 500 ms. The controller spends only ~140 µs of I²C time per trigger; between triggers there is zero I²C traffic.

---

## Notifications & One-Shot Animations

The scene player is single-instance — only one scene plays at a time. Notifications that should appear *over* an ambient scene use a **free group ID** that doesn't conflict with the scene.

Use `POST /api/animations/play` to send a single animation step directly, bypassing the scene system. The body is a **flat object** — all step fields live at the root alongside `group` and `panels`:

```json
{
  "group": 250,
  "panels": "all",
  "type": "PULSE",
  "color": "#FF0000",
  "brightnessFrom": 0,
  "brightnessTo": 255,
  "duration": 500,
  "params": [64, 64]
}
```

All the same step fields documented in [Animation Types](types.md) and [Controller Runners](types.md#controller-runners) are supported. The notification runs on group 250 while the ambient scene continues on groups 1–N. The panel's AnimationPlayer handles both groups independently. No SPIFFS involved.

For chained steps on a notification (e.g. pulse → fade), use a short scene via `POST /api/scenes/play` with a free group ID instead.

---

## Examples

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

Change colour mid-flight: `PUT /api/appearance/colors {"primary":"#0044FF"}` — the breathe immediately shifts to blue on the next frame.

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
| Scene name | `[a-zA-Z0-9_-]`, 1–18 chars |
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
| Layer palette override | Layers with different effective palettes should not share any target panel (not validated at save time) |
| Infinite last step | Only valid when `scene.loop: true` |
