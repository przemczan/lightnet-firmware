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
| `params` | array | Runner-specific parameters |

---

### WAVE

A colour envelope (triangular wave) sweeps from one end of the panel list to the other. The colour is scaled by the wave intensity at each panel position.

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

---

### RIPPLE

A colour ring expands outward from an origin panel. Distance is based on index, not physical position.

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

---

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
