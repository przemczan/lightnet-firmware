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
{
  "type": "SOLID",
  "color": "#FF4400"
}
```

No parameters.

---

### FADE

Linearly interpolates colour from `colorFrom` to `colorTo`.

```json
{
  "type": "FADE",
  "colorFrom": "#0044FF",
  "colorTo": "#000000",
  "duration": 1500
}
```

No parameters.

---

### TRANSITION

Interpolates colour from `colorFrom` to `colorTo` (equivalent to FADE).

```json
{
  "type": "TRANSITION",
  "colorFrom": "#000000",
  "colorTo": "#FF4400",
  "duration": 2000
}
```

No parameters.

---

### BREATHE

Sinusoidal (parabolic-approximation) colour envelope: oscillates between `colorFrom` and `colorTo` and repeats.

```json
{
  "type": "BREATHE",
  "colorFrom": "#000000",
  "colorTo": "#0088FF",
  "duration": 3000,
  "loop": true
}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Speed multiplier (reserved, currently unused) | 0 |

---

### PULSE

3-phase (rise → hold → fall) colour flash. Interpolates between `colorFrom` and `colorTo`.

```json
{
  "type": "PULSE",
  "colorFrom": "#000000",
  "colorTo": "#FFFFFF",
  "duration": 600,
  "params": [
    64,
    64
  ]
}
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
{
  "type": "BLINK",
  "colorFrom": "#000000",
  "colorTo": "#FF0000",
  "duration": 0,
  "loop": true,
  "params": [
    50
  ]
}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Half-period in ms (on time = off time = this value) | 1 |

---

### HUE_CYCLE

6-step integer HSV→RGB rainbow rotation. Ignores `colorFrom`/`colorTo`.

```json
{
  "type": "HUE_CYCLE",
  "duration": 0,
  "loop": true,
  "params": [
    10
  ]
}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Rotation speed (higher = faster) | 1 |

---

### STROBE

Binary flash at a frequency in Hz. On = `colorTo`, off = black.

```json
{
  "type": "STROBE",
  "color": "#FFFFFF",
  "duration": 2000,
  "params": [
    20
  ]
}
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
  "colorTo": "#FF8800",
  "duration": 0,
  "params": [
    180
  ]
}
```

| params index | Meaning | Default |
|---|---|---|
| `params[0]` | Decay rate (units per second, 0–255). Higher = faster decay. | 0 |

!!! tip "Tuning decay for music sync"
    At 120 BPM (500 ms between beats), `params[0] = 210` decays just before the next beat, creating a punchy flash. Lower values produce a slower trailing glow.

Triggers are sent via WebSocket — see [API → WebSocket Triggers](api.md#websocket-triggers).

---

## Modifier Layers

Modifier steps transform the colour composited **below** them rather than producing their own (see
[Concepts → Layer compositing](concepts.md#layer-compositing)). They animate a scalar `from` → `to`
(0–255) over `duration`, and a finished modifier **holds** its final value. Place a modifier layer
*after* (above) the layers it should affect.

### MOD_BRIGHTNESS / MOD_SATURATION / MOD_HUE_SHIFT / MOD_INVERT

```json
{
  "type": "MOD_BRIGHTNESS",
  "from": 255,
  "to": 64,
  "duration": 2000
}
```

| Type | `from`/`to` | Identity (no-op) |
|---|---|---|
| `MOD_BRIGHTNESS` | brightness scale, 255 = full | 255 |
| `MOD_SATURATION` | saturation scale, 255 = unchanged | 255 |
| `MOD_HUE_SHIFT` | hue rotation, 0…255 = a full turn | 0 |
| `MOD_INVERT` | cross-fade toward RGB-inverted (`255-r,255-g,255-b`); 255 = fully inverted | 0 |

Brightness and invert are exact (RGB multiply / lerp); saturation/hue use an integer HSV
approximation on the panel (small, deterministic colour drift, bypassed at the identity value).

---

## Controller Runners

Runners appear as steps with a `"runner"` field instead of `"type"`. As of protocol v6 a runner is
**compiled at step start into one local PULSE per panel** — each panel gets a per-panel `startDelayMs`
(its onset in the sweep) and a pulse shaped to the envelope, then a single general-call START. The
sweep then runs **autonomously on the panels** (no per-frame `SET_COLOR` streaming), and a runner
participates in [layer compositing](concepts.md#layer-compositing) like any other layer — it defaults
to the `max` blend so its dark phase is transparent over the background/layers below.

### Common runner step fields

| Field | Type | Notes |
|---|---|---|
| `runner` | string | Runner name (`WAVE`, `RIPPLE`, `CHASE`, `WHEEL`) |
| `color` | color ref | Colour for the runner effect (only used when `animates:color`) |
| `duration` | ms | Total duration of the runner effect |
| `directionality` | string | Field mode: `topology` (graph hop-distance, default) or `geometric` (planar layout). Orthogonal to `source`. |
| `source` | string | Where the motion emanates from: `root` (default), `leaves`, `panel:N`, or `all` |
| `angle` | 0–359 | Geometric **axis sweep** direction in degrees (only `directionality:geometric` WAVE/CHASE; default 0). 2° resolution. Ignored by RIPPLE. |
| `reverse` | bool | Reverse the travel direction |
| `waveWidth` / `rippleWidth` | 0–255 | Band / ring width in **rings** (also settable as `params[0]`) |
| `repeat` | bool | WAVE/RIPPLE/CHASE: a continuous train of evenly-spaced sweeps instead of a single pass — colour-only, see below |
| `animates` | string | What the sweep modulates: `color` (default), `brightness`, `saturation`, `hue`, or `invert` |
| `amount` | 0–255 | Peak intensity for non-`color` targets (also settable as `params[4]`); ignored when `animates:color` |

**What the sweep animates.** By default a runner sweeps `color` — each panel snaps a `PULSE`
between `color` and the layer's background. Setting `animates` to `brightness`, `saturation`,
`hue`, or `invert` instead drives the matching `MOD_*` modifier on each panel as the sweep passes
through it: the panel snaps to `amount` at onset and **decays back to that property's identity**
(255 for brightness/saturation, 0 for hue/invert) over the lit window, so the effect modulates —
rather than replaces — whatever colour is composited below it (e.g. a brightness wave dimming an
ambient background, or a hue sweep rotating it, as it passes).

```json
{
  "runner": "WAVE",
  "source": "root",
  "animates": "brightness",
  "amount": 80,
  "waveWidth": 3,
  "duration": 5000
}
```

| `animates` | Drives | `amount` meaning |
|---|---|---|
| `color` (default) | per-panel colour `PULSE` between `color` and background | n/a — `color` is used instead |
| `brightness` | `MOD_BRIGHTNESS` sweep | peak dimming: `0` = blackout, `255` = no change |
| `saturation` | `MOD_SATURATION` sweep | peak desaturation: `0` = full grey, `255` = no change |
| `hue` | `MOD_HUE_SHIFT` sweep | peak hue rotation: `0…255` = a full turn |
| `invert` | `MOD_INVERT` sweep | peak invert blend: `0` = no change, `255` = fully inverted |

No protocol change: this reuses the existing modifier `PREPARE` with `param1 = amount` (peak) and
`param2` = the property's identity value, and the panel's existing linear modifier ramp.

**Repeating sweeps (`repeat`).** WAVE, RIPPLE, and CHASE normally play one pass over `duration`
and stop. Setting `"repeat": true` instead turns `duration` into the time for **one lap** and
loops the sweep forever — a continuous train of evenly-spaced rings/bands/blips, several in
flight at once, each with a true dark gap between passes. Colour-only (`animates:color`): the
linear modifier ramp that drives `brightness`/`saturation`/`hue`/`invert` can't loop without a
sawtooth jump, so `repeat` is ignored for those targets. Needs `schemaVersion: 5`.

```json
{
  "runner": "RIPPLE",
  "source": "root",
  "color": {
    "palette": 96
  },
  "rippleWidth": 1,
  "repeat": true,
  "duration": 1500
}
```

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
  - **RIPPLE** grows a **Euclidean ring** from the `source` centre(s) that lights whatever panel
    surface it intersects (each panel is its circumscribed disc, so overlapping panels light
    together): `source:root` from the root, `source:panel:N` from panel N, and `source:leaves`
    runs **one ripple per leaf** (concurrent fronts converging inward). `angle` is ignored.

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
{
  "runner": "WAVE",
  "source": "root",
  "color": {
    "palette": 128
  },
  "waveWidth": 3,
  "duration": 5000
}
```

| Field | Meaning | Default |
|---|---|---|
| `waveWidth` (`params[0]`) | Band width in rings (hops illuminated at peak) | 3 |

---

### RIPPLE

A brightness ring expands outward from the `source` panel(s). Distance is **graph hops**, so it
follows the wiring on any device.

```json
{
  "runner": "RIPPLE",
  "source": "panel:3",
  "color": "#FF4400",
  "rippleWidth": 2,
  "duration": 2000
}
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
{
  "runner": "CHASE",
  "source": "root",
  "color": {
    "useColor": 0
  },
  "duration": 3000
}
```

No width parameter.

---

### WHEEL

`lines` evenly-spaced blades rotate continuously about a centre — a spinning pinwheel/radar
sweep. Always geometric (it needs each panel's planar bearing from the centre; **no topology
fallback** — produces no output if the layout can't be embedded) and always loops; `duration`
is the time for one full rotation. Colour-only (`animates:color`).

```json
{
  "runner": "WHEEL",
  "source": "root",
  "color": {
    "palette": 64
  },
  "lines": 3,
  "thickness": 24,
  "duration": 4000
}
```

| Field | Meaning | Default |
|---|---|---|
| `lines` (`params[5]`) | Number of rotating blades, 1–6 | 1 |
| `thickness` (`params[0]`) | Blade angular width in **degrees** (shares the slot with `waveWidth`/`rippleWidth`, but in degrees, not rings) | 0 |
| `source` | Pivot: `root`, `panel:N`, or `leaves`/`all` (averaged to a single centre point) | `root` |
| `reverse` | Spin the other way | `false` |

`angle` and `waveWidth`/`rippleWidth` are N/A. Needs `schemaVersion: 5`.
