---
icon: material/shimmer
---

# Animations & Scenes

Lightnet's animation system lets you compose multi-layer light shows in JSON and play them across any number of panels. Panel-local animations run entirely on the ATmega after a single setup packet — zero per-frame I²C traffic. Controller runners are computed on the ESP each frame and stream brightness values over I²C.

---

## How it fits together

```
Scene
└── Layer (group ID, panel target, optional palette)
    └── Step  →  Step  →  Step …
        ├── Panel-local  (BREATHE, PULSE, WAVE …)  runs on ATmega
        └── Controller runner  (WAVE, RIPPLE, CHASE)  runs on ESP
```

Multiple layers within a scene run in parallel. Each layer targets its own set of panels and belongs to a **group ID** so the controller can start them simultaneously with a single I²C broadcast.

---

## Panel-local animations

Run entirely on the ATmega. The controller sends one setup packet; the panel handles every frame itself.

| Type | What it does |
|---|---|
| `SOLID` | Holds a static colour and brightness |
| `FADE` | Linearly ramps brightness between two values |
| `TRANSITION` | Simultaneously interpolates colour and brightness |
| `BREATHE` | Sinusoidal (parabolic) brightness envelope — loops smoothly |
| `PULSE` | 3-phase rise → hold → fall flash |
| `BLINK` | Binary on/off at a fixed half-period |
| `HUE_CYCLE` | 6-step HSV rainbow rotation |
| `STROBE` | Binary flash at a frequency in Hz |
| `REACTIVE` | Decay model triggered by WebSocket beat events |

## Controller runners

Computed on the ESP each frame. Per-panel brightness is streamed over I²C.

| Runner | What it does |
|---|---|
| `WAVE` | Triangular brightness envelope sweeps across the panel list |
| `RIPPLE` | Brightness ring expands outward from an origin panel |
| `CHASE` | A single lit panel travels through the panel list |

---

## In this section

- [**Concepts**](concepts.md) — scenes, layers, steps, groups, palettes, colour references, and timing
- [**Animation Types**](types.md) — field reference and parameters for every type and runner
- [**API & Examples**](api.md) — HTTP endpoints, WebSocket triggers, and five worked examples
