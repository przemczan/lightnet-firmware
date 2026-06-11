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
"params": [64, 64]
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
  "params": [50]
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
  "params": [10]
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
  "params": [20]
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

## Modifier Layers

Modifier steps transform the colour composited **below** them rather than producing their own (see
[Concepts → Layer compositing](concepts.md#layer-compositing)). They animate a scalar `from` → `to`
(0–255) over `duration`, and a finished modifier **holds** its final value. Place a modifier layer
*after* (above) the layers it should affect.

### MOD_DIM / MOD_DESATURATE / MOD_HUE_SHIFT / MOD_INVERT / MOD_BRIGHTEN / MOD_SATURATE

```json
{
  "type": "MOD_DIM",
  "from": 255,
  "to": 64,
  "duration": 2000
}
```

| Type | `from`/`to` | Identity (no-op) |
|---|---|---|
| `MOD_DIM` | brightness scale down toward black, 255 = full | 255 |
| `MOD_DESATURATE` | saturation scale down toward grey, 255 = unchanged | 255 |
| `MOD_HUE_SHIFT` | hue rotation, 0…255 = a full turn | 0 |
| `MOD_INVERT` | cross-fade toward RGB-inverted (`255-r,255-g,255-b`); 255 = fully inverted | 0 |
| `MOD_BRIGHTEN` | push brightness up toward white, 255 = white | 0 |
| `MOD_SATURATE` | push saturation up toward fully saturated, 255 = max | 0 |

`MOD_DIM`/`MOD_BRIGHTEN` and `MOD_INVERT` are exact (RGB multiply / lerp); desaturate/saturate/hue
variants use an integer HSV approximation on the panel (small, deterministic colour drift, bypassed
at the identity value). Use `MOD_BRIGHTEN`/`MOD_SATURATE` when you want a sweep to *increase*
brightness/saturation rather than suppress it — e.g. a wave that flares a dim background brighter,
or pushes a muted colour toward full saturation.

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
| `runner` | string | Runner name (`WAVE`, `RIPPLE`, `CHASE`, `WHEEL`, `BOUNCE`, `RAIN`, `SPARKLE`) |
| `color` | color ref | Colour for the runner effect (only used when `animates:color`) |
| `duration` | ms | Total duration of the runner effect (one lap, for repeating/looping runners) |
| `directionality` | string | Field mode: `topology` (graph hop-distance, default) or `geometric` (planar layout). Orthogonal to `source`. Ignored by SPARKLE. |
| `source` | string | Where the motion emanates from: `root` (default), `leaves`, `panel:N`, or `all`. Ignored by SPARKLE. |
| `angle` | 0–359 | Geometric **axis** direction in degrees (`directionality:geometric` WAVE/CHASE/BOUNCE/RAIN, and always-geometric **MATRIX** — the direction drops fall; default 0). 2° resolution. Ignored by RIPPLE/SPARKLE. |
| `reverse` | bool | Reverse the travel direction (RAIN: drops rise instead of fall). Ignored by SPARKLE. |
| `waveWidth` / `rippleWidth` / `width` | 0–255 | Band / ring / tail / fade width (also settable as `params[0]`) — see each runner's table for what `width` means for it |
| `repeat` | bool | WAVE/RIPPLE/CHASE: a continuous train of evenly-spaced sweeps instead of a single pass, see below. Ignored by BOUNCE/RAIN/SPARKLE. |
| `repeatCount` / `waves` | 1–255 | With `repeat:true` (WAVE/RIPPLE/CHASE/WHEEL): number of sweeps in flight at once. For **RAIN/SPARKLE/MATRIX** (particle spawners) `waves` is instead the **spawn rate** — drops/flashes per **second**. `params[5]`. Default 1. |
| `speed` | ms | **RAIN/MATRIX only.** The constant drop **fall-time** (one drop's trip down the field); `duration` is the play *window*, not the rate. SPARKLE ignores it (its flashes don't move). `0`/absent ⇒ a 1000 ms default. |
| `animates` | string | What the sweep modulates: `color` (default), `dim`, `desaturate`, `hue`, `invert`, `brighten`, or `saturate` |
| `amount` | 0–255 | Peak intensity for non-`color` targets (also settable as `params[4]`); ignored when `animates:color` |
| `shape` | string | Envelope shape for non-`color` sweeps: `fall` (peak→identity, default), `rise` (identity→peak), or `bell` (identity→peak→identity). Ignored when `animates:color`, and when `repeat:true` (repeating modifier sweeps always use `bell`, see below). |

**What the sweep animates.** By default a runner sweeps `color` — each panel snaps a `PULSE`
between `color` and the layer's background. Setting `animates` to `dim`, `desaturate`, `hue`,
`invert`, `brighten`, or `saturate` instead drives the matching `MOD_*` modifier on each panel as
the sweep passes through it: the panel snaps to `amount` at onset and **decays back to that
property's identity** over the lit window, so the effect modulates — rather than replaces —
whatever colour is composited below it (e.g. a dimming wave over an ambient background, a hue
sweep rotating it, or a `brighten`/`saturate` wave flaring it brighter or more vivid, as it
passes). The identity value is 255 for `dim`/`desaturate` (suppress toward black/grey), 0 for
`hue`/`invert`, and 0 for `brighten`/`saturate` (boost toward white/full saturation).

```json
{
  "runner": "WAVE",
  "source": "root",
  "animates": "dim",
  "amount": 80,
  "waveWidth": 3,
  "duration": 5000
}
```

| `animates` | Drives | `amount` meaning |
|---|---|---|
| `color` (default) | per-panel colour `PULSE` between `color` and background | n/a — `color` is used instead |
| `dim` | `MOD_DIM` sweep | peak dimming: `0` = blackout, `255` = no change |
| `desaturate` | `MOD_DESATURATE` sweep | peak desaturation: `0` = full grey, `255` = no change |
| `hue` | `MOD_HUE_SHIFT` sweep | peak hue rotation: `0…255` = a full turn |
| `invert` | `MOD_INVERT` sweep | peak invert blend: `0` = no change, `255` = fully inverted |
| `brighten` | `MOD_BRIGHTEN` sweep | peak brightening: `0` = no change, `255` = white |
| `saturate` | `MOD_SATURATE` sweep | peak saturation boost: `0` = no change, `255` = fully saturated |

**Envelope shape (`shape`).** For non-`color` sweeps the shape of the modifier envelope within
each panel's lit window is configurable:

| `shape` | Envelope | Effect |
|---|---|---|
| `fall` *(default)* | peak → identity | burst that decays (e.g. brightness flare that fades) |
| `rise` | identity → peak | swell that builds (e.g. brightness that brightens as the wave passes) |
| `bell` | identity → peak → identity | symmetric pulse — bright in the middle, soft on both edges |

No protocol change: this reuses the existing modifier `PREPARE` with `param1 = amount` (peak) and
`param2` = the property's identity value, and the panel's existing linear modifier ramp.

**Repeating sweeps (`repeat`).** WAVE, RIPPLE, and CHASE normally play one pass over `duration`
and stop. Setting `"repeat": true` instead turns `duration` into the time for **one lap** and
loops the sweep forever — a continuous train of evenly-spaced rings/bands/blips, several in
flight at once.

- `animates:color` (default): each pass is a true dark gap between passes (the swapped-colour
  trick — departing → dark → approaching).
- Any other `animates` (`dim`/`desaturate`/`hue`/`invert`/`brighten`/`saturate`): each pass is a
  `bell` envelope (identity → `amount` → identity), regardless of the step's `shape`. A
  rise/fall envelope can't loop without a discontinuity (it would jump from `amount` straight
  back to identity at the seam), so repeating modifier sweeps always use `bell`, which already
  starts and ends at identity and therefore loops cleanly — the same trick used for the WHEEL
  modifier blade.

Needs `schemaVersion: 5`.

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

**Multiple simultaneous sweeps (`repeatCount`).** By default `repeat` runs a single train — one
band/ring/blip in flight at a time, sweeping the whole field every `duration`. Setting
`"repeatCount": N` (N > 1) instead places **N evenly-spaced sweeps in flight at once**, each
travelling at the same speed and width as `repeatCount: 1` (i.e. `duration` stays the time for
one sweep to cross the field; the train just gets denser). Works for any `animates` target.
Requires `schemaVersion: 5`.

```json
{
  "runner": "WAVE",
  "source": "root",
  "color": {
    "palette": 200
  },
  "waveWidth": 2,
  "repeat": true,
  "repeatCount": 3,
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
| `source` | `root`, `panel:N`, `leaves`, or `all` (all panels pulse as one, no directionality) | `root` |

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
is the time for one full rotation.

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
| `animates` | What each blade modulates (same options as other runners; default `color`) | `color` |
| `amount` | Peak intensity for non-`color` targets (0–255) | — |

`angle` and `waveWidth`/`rippleWidth` are N/A. When `animates` is not `color`, the blade automatically
uses a `bell` envelope (soft on both edges) since WHEEL always loops — `shape` has no effect on WHEEL.
Needs `schemaVersion: 5`.

---

### BOUNCE

Like WAVE — a triangular brightness band sweeps along the distance axis from `source` — but the
band is a single "wave always 1" that **bounces back and forth forever**. Unlike WAVE (whose band
slides fully on and off the canvas), BOUNCE's **peak travels only the real panel span** and
**reflects the instant it reaches either end** — it never slides off-canvas — so the motion reads
as a continuous pendulum that turns around right at the edge panels. `duration` is the time for
**one pass** (one direction). `repeat`/`repeatCount` are ignored — there is always exactly one band
in flight.

```json
{
  "runner": "BOUNCE",
  "source": "root",
  "color": {
    "palette": 128
  },
  "width": 3,
  "duration": 2000
}
```

| Field | Meaning | Default |
|---|---|---|
| `width` (`params[0]`) | Band width in rings (hops illuminated at peak) | 3 |

Direction starts as `source` → far end (or the reverse, if `reverse:true`) and flips on every
subsequent pass. Needs `schemaVersion: 7`.

---

### RAIN

RAIN is a **particle spawner**, not a compiled sweep. While the step plays, the controller
launches **drops** at a steady rate (`waves`, drops per second), each down a random root→leaf path
of the panel tree. Panels light in sequence as the drop's head passes (the cascade) and fade
behind it over the `width`-ring tail. Each drop is an independent one-shot that finishes on its
own, so the pattern is **genuinely random and never repeats** — and when the play window ends the
in-flight drops simply finish (a soft, seamless boundary).

- `duration` is the **play window** — how long RAIN runs before the scene advances/loops; it does
  *not* set drop speed. Use `0` on the last step to rain indefinitely.
- `speed` is the constant **drop fall-time** — how long one drop takes to cross its path.
- `waves` is the **spawn rate** in drops per second.
- `width` is the **tail length** in rings (hops behind the head that fade out).
- `reverse` makes drops rise (leaf→root). `animates` (+ `amount`/`shape`) work as for any runner
  (e.g. `animates:dim` → dimming raindrops); `colorFrom` is the colour the tail fades to (default
  black), `color` the head colour.
- **Directionality.** By default drops fall **down the tree** (root→leaf). With
  `"directionality": "geometric"` + `"angle"` they instead fall along the **planar layout axis**
  (the *visual* down): each drop is a **1-wide streak** that starts at a top panel and follows
  actual panel connections downhill along `angle`, independent of the wiring. Use this when the
  tree topology doesn't match the physical arrangement.

```json
{
  "runner": "RAIN",
  "color": { "palette": 200 },
  "width": 3,
  "waves": 4,
  "speed": 800,
  "duration": 0
}
```

| Field | Meaning | Default |
|---|---|---|
| `waves` (`params[5]`) | Spawn rate — **drops per second** | 1 |
| `speed` (ms) | Drop fall-time — one drop's trip across its path | 1000 |
| `width` (`params[0]`) | Tail length in rings behind the head | 0 |
| `reverse` | Drops rise (leaf→root) instead of fall | false |

> Drops follow tree **paths** (one random source→leaf per drop). Source/junction panels sit on
> many drops at once; if a panel exceeds its 8 composite slots the busiest drop loses that segment
> (a small gap). Needs `schemaVersion: 7`.

---

### SPARKLE

SPARKLE is a **particle spawner**: while the step plays, the controller flashes **random panels**
at a steady rate (`waves`, flashes per second) — each an almost-instant onset followed by a fade
(`width`). It has no directionality (`source`/`directionality`/`reverse`/`angle` are ignored).
Flashes are independent one-shots, so the twinkle is **genuinely random and never repeats**; when
the play window ends, in-flight fades simply finish.

- `duration` is the **play window** (use `0` on the last step to twinkle indefinitely).
- `waves` is the **spawn rate** in flashes per second.
- `width` is the **fade-out duration** (`0`–`255`, longer = slower fade).
- `animates` (+ `amount`/`shape`) and `colorFrom`→`color` work as for any runner. SPARKLE ignores
  `speed`.

```json
{
  "runner": "SPARKLE",
  "color": { "palette": 32 },
  "width": 80,
  "waves": 6,
  "duration": 0
}
```

| Field | Meaning | Default |
|---|---|---|
| `waves` (`params[5]`) | Spawn rate — **flashes per second** | 1 |
| `width` (`params[0]`) | Fade-out duration (`0`–`255`, longer = slower) | 0 (instant off) |

Needs `schemaVersion: 7`.

---

### MATRIX

MATRIX is the **constant-speed** cousin of RAIN — the classic *digital-rain* look. Same particle
spawner (drops with a soft head + fading `width` tail, spawned at `waves`/sec, jittered), but where
RAIN's speed varies per drop, **every MATRIX drop falls at the same uniform speed** (velocity =
field span ÷ `speed`), with a **softened leading edge**.

- **Geometric** (`directionality:geometric`, the default): each drop is a **straight line** down the
  `angle` axis at a random position — it lights the panels the line passes through, **antialiased**:
  brightness falls off (smoothstep) with a panel's perpendicular distance to the line over a soft
  *virtual width*, so the column reads as a smooth line rather than a few hard-lit panels. Many lines
  at different positions = falling columns. `angle` sets the fall direction; `reverse` (or
  `angle ± 180°`) flips it.
- **Topology**: a random root→leaf tree path, but each hop takes `speed` ÷ tree-depth — so every
  drop falls at the same rate (this is what distinguishes it from RAIN's per-drop pacing).

```json
{
  "runner": "MATRIX",
  "color": { "palette": 96 },
  "angle": 270,
  "width": 3,
  "waves": 4,
  "speed": 1200,
  "duration": 0
}
```

| Field | Meaning | Default |
|---|---|---|
| `waves` (`params[5]`) | Spawn rate — **drops per second** | 1 |
| `speed` (ms) | Drop fall-time — one drop's trip down the field (constant for all drops) | 1000 |
| `width` (`params[0]`) | Tail length in rings behind the head | 0 |
| `angle` | Geometric fall direction (degrees); `reverse` flips it | 0 |

Needs `schemaVersion: 7`. Contrast with [RAIN](#rain): RAIN's variable speed and organic,
connection-following path vs. MATRIX's uniform speed and straight lines.
