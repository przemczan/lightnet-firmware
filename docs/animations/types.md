---
icon: material/play-box-multiple-outline
---

# Animation Types

Reference for every panel-local animation type and every controller runner. For field semantics (color references, duration, loop flags) see [Concepts](concepts.md).

---

## Panel-Local Animations

All panel-local animations run entirely on the ATmega with **zero per-frame I²C traffic**. The controller sends a single `PACKET_ANIMATION_PREPARE` + `GENERAL CALL START` to set them in motion.

### Common step fields

| Field | Type | Notes |
|---|---|---|
| `type` | string | Animation type name (see below) |
| `colorFrom` / `colorTo` | color ref | Start and end colours — see [Color References](concepts.md#color-references) |
| `color` | color ref | Alias for `colorTo` when only one colour is needed |
| `duration` | 0–65535 ms | Animation duration. `0` = infinite, valid only on the last step of a looping scene. |
| `loop` | bool | Loop this individual step indefinitely (FLAG_LOOP). |
| `pingpong` | bool | Reverse direction at end instead of looping (FLAG_PINGPONG). |
| `params` | array, ≤2 × 0–255 | Type-specific parameters (see each type). The parser accepts up to 4 entries for forward-compatibility, but the panel firmware currently uses only `params[0]` and `params[1]`. |

---

### SOLID

Holds a static colour. Use as the last step to freeze the panel.

```json
{"type": "SOLID", "color": "#FF4400"}
```

No parameters.

---

### FADE

Linearly interpolates colour from `colorFrom` to `colorTo`.

```json
{"type": "FADE", "colorFrom": "#0044FF", "colorTo": "#000000", "duration": 1500}
```

No parameters.

---

### TRANSITION

Interpolates colour from `colorFrom` to `colorTo` (equivalent to FADE).

```json
{
  "type": "TRANSITION",
  "colorFrom": "#000000",
  "colorTo":   "#FF4400",
  "duration": 2000
}
```

No parameters.

---

### BREATHE

Sinusoidal (parabolic-approximation) colour envelope: oscillates between `colorFrom` and `colorTo` and repeats.

```json
{"type": "BREATHE", "colorFrom": "#000000", "colorTo": "#0088FF", "duration": 3000, "loop": true}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Speed multiplier (reserved, currently unused) | 0 |

---

### PULSE

3-phase (rise → hold → fall) colour flash. Interpolates between `colorFrom` and `colorTo`.

```json
{"type": "PULSE", "colorFrom": "#000000", "colorTo": "#FFFFFF", "duration": 600, "params": [64, 64]}
```

| params index | Meaning | Range |
|---|---|---|
| `params[0]` | Rise phase proportion (0–255 of total duration) | 0–255 |
| `params[1]` | Fall phase proportion (0–255 of total duration) | 0–255 |

The **hold** phase is whatever remains: `hold_pct = 255 − rise − fall`. If rise + fall exceeds 255, the firmware proportionally scales them so they sum to 255 and the hold is zero.

---

### BLINK

Binary toggle between `colorTo` (on) and `colorFrom` (off) at a fixed period.

```json
{"type": "BLINK", "colorFrom": "#000000", "colorTo": "#FF0000", "duration": 0, "loop": true, "params": [50]}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Half-period in ms (on time = off time = this value) | 1 |

---

### HUE_CYCLE

6-step integer HSV→RGB rainbow rotation. Ignores `colorFrom`/`colorTo`.

```json
{"type": "HUE_CYCLE", "duration": 0, "loop": true, "params": [10]}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Rotation speed (higher = faster) | 1 |

---

### STROBE

Binary flash at a frequency in Hz. On = `colorTo`, off = black.

```json
{"type": "STROBE", "color": "#FFFFFF", "duration": 2000, "params": [20]}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Frequency in Hz | 1 |

---

### REACTIVE

Decay-model animation triggered by WebSocket beats. Rests at `colorFrom`; on trigger, instantly jumps to `colorTo` then decays back over time.

```json
{
  "type": "REACTIVE",
  "colorFrom": "#110000",
  "colorTo":   "#FF8800",
  "duration": 0,
  "params": [180]
}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Decay rate (units per second, 0–255). Higher = faster decay. | 0 |

!!! tip "Tuning decay for music sync"
    At 120 BPM (500 ms between beats), `params[0] = 210` decays just before the next beat, creating a punchy flash. Lower values produce a slower trailing glow.

Triggers are sent via WebSocket — see [API → WebSocket Triggers](api.md#websocket-triggers).

---

## Controller Runners

Runners are computed on the controller ESP each frame and send scaled `SET_COLOR` packets over I²C. They appear as steps with a `"runner"` field instead of `"type"`.

### Common runner step fields

| Field | Type | Notes |
|---|---|---|
| `runner` | string | Runner name (`WAVE`, `RIPPLE`, `CHASE`) |
| `color` | color ref | Colour for the runner effect |
| `duration` | ms | Total duration of the runner effect |
| `directionality` | string | Field mode: `topology` (graph hop-distance, default) or `geometric` (planar layout). Orthogonal to `source`. |
| `source` | string | Where the motion emanates from: `root` (default), `leaves`, `panel:N`, or `all` |
| `angle` | 0–359 | Geometric **axis sweep** direction in degrees (only `directionality:geometric` WAVE/CHASE; default 0). 2° resolution. Ignored by RIPPLE. |
| `reverse` | bool | Reverse the travel direction |
| `waveWidth` / `rippleWidth` | 0–255 | Band / ring width in **rings** (also settable as `params[0]`) |

**Directionality.** Two orthogonal choices — the field **mode** (`directionality`) and the
**`source`** it emanates from — both controller-side:

- **Topology (graph hop-distance from the `source`)** — the default. Runners move along each
  panel's hop-distance (not discovery-list order), so the effect keeps its shape on any device.
  `source:root` emanates outward from the (logical) root, `source:leaves` converges inward,
  `source:panel:N` radiates from a specific panel, and `reverse` flips it.
- **Geometric (`directionality:geometric`)** — uses the *physical* flat layout instead of the
  wiring. The controller derives each panel's (x,y) from the regular-polygon geometry of the tree
  (no setup, no protocol change; same layout as the mobile visualizer). Two sub-behaviours:
  - **WAVE / CHASE** sweep along a straight **axis** at `angle` degrees — straight-line motion
    across the piece that graph distance can't express. `source` is N/A (an axis has no origin);
    `angle` is a tune-by-eye dial (deterministic per device, not a literal compass bearing).
  - **RIPPLE** expands as true **Euclidean rings** from the `source` centre(s): `source:root`
    from the root, `source:panel:N` from panel N, and `source:leaves` runs **one ripple per leaf**
    (concurrent fronts converging inward). `angle` is ignored.

  `reverse` flips travel in both. Falls back to topology (same `source`) if the layout can't be
  embedded. Requires `schemaVersion: 3`. The legacy `source:"geometric"` still parses (→
  `directionality:geometric`, axis sweep from the default root).

Full explanation with diagrams:
[Scene Authoring → Directionality](scene-authoring.md#8-directionality-the-source-field).

---

### WAVE

A triangular brightness band sweeps along the distance axis from the `source` — panels at the
same distance (a "ring") light together. `color` is scaled by the band intensity at each panel.

```json
{ "runner": "WAVE", "source": "root", "color": {"palette": 128}, "waveWidth": 3, "duration": 5000 }
```

| Field | Meaning | Default |
|---|---|---|
| `waveWidth` (`params[0]`) | Band width in rings (hops illuminated at peak) | 3 |

---

### RIPPLE

A brightness ring expands outward from the `source` panel(s). Distance is **graph hops**, so it
follows the wiring on any device.

```json
{ "runner": "RIPPLE", "source": "panel:3", "color": "#FF4400", "rippleWidth": 2, "duration": 2000 }
```

| Field | Meaning | Default |
|---|---|---|
| `rippleWidth` (`params[0]`) | Ring width in rings | 2 |

!!! note "Legacy `originPanel`"
    Older scenes set `"originPanel": N`; it is still accepted and maps to `source: "panel:N"`.

---

### CHASE

A single lit ring steps outward along the distance axis from the `source` over the duration.

```json
{ "runner": "CHASE", "source": "root", "color": {"useColor": 0}, "duration": 3000 }
```

No width parameter.
