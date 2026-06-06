# Design: Portable / device-agnostic scenes

**Status:** design draft (no firmware written yet)
**Scope:** controller firmware (scene resolution + animation runners), scene JSON schema,
optional config files, later mobile authoring UI.

This document proposes how to make **scenes reusable across different Lightnet devices** —
devices with different panel counts and different physical wiring, whether your own or
someone else's — *without* giving up the precise, per-setup targeting that exists today.

---

## 1. Problem

A scene today targets panels by **1-based discovery index**:

```jsonc
"panels": "all"            // every discovered panel
"panels": [1, 3, 5]        // explicit indices
"panels": { "exclude": [2] }
```

These are resolved at play time against the panels discovered on *this* device, in
`ScenePlayer::resolvePanels` (`lib/Lightnet/Controller/Scenes/ScenePlayer.cpp`). This is
**excellent for precise, setup-specific authoring** — "make panel 3 the accent" — and we are
keeping it unchanged.

Two things block reuse across devices:

1. **Targeting doesn't transfer.** An explicit index means a *different physical panel* on a
   device with different wiring. `"all"` adapts to any count, but it can't express *which*
   panels you mean — "the outer ring", "the centre", "one branch".

2. **Direction isn't real.** The "spatial" runners (`WAVE`, `CHASE`, `RIPPLE` in
   `lib/Lightnet/Controller/Animations/AnimationRunner.hpp`) walk the panel **list in
   discovery order**, not the actual physical topology. `RIPPLE` even takes a hard origin
   *index*. So a left-to-right wave can come out reversed or scrambled on another device — or
   even on the same device after a rewire.

**Goal:** keep precise index targeting, and add a portable, directionality-aware layer built
on the **edge graph the controller already discovers** (zero setup), plus *optional*
per-device tags and auto-derived coordinates for the last mile of cross-owner intent.

### Non-goals
- Changing how panels are discovered or addressed on the I²C bus.
- Any panel-firmware or protocol change (the whole proposal is controller-side — see §8).
- Replacing index targeting (it stays, as a first-class option).

---

## 2. Foundations already in place

The controller already builds a fully traversable **rooted tree** during discovery:

- `Panel { uint8_t index; List<Edge*> edges; }` — `lib/Lightnet/Controller/Panels/Panel.hpp`
- `Edge  { uint8_t index; Edge* connectedEdge; Panel* panel; }` — `…/Panels/Edge.hpp`
- The neighbour across a connector is `edge->connectedEdge->panel`.
- `connectedEdge == nullptr` means **that one connector is unconnected** — by itself it does
  *not* make the panel a leaf.

Some structural definitions we rely on throughout:

- **Root.** The controller has a single edge (`pingEdge`, one `edgePinNo`) and
  `currentPanelIndex` starts at `1`, so the first panel booted off that edge is **always
  panel index 1**. The root is deterministic; it anchors the `root` selector and the default
  directionality source. This *physical* root is also the default **logical root**, which the
  user may reassign to any panel to re-centre the whole rooted view (§4.1).
- **Parent / child.** Every non-root panel has exactly one **parent** connector (toward the
  root) and 0–2 **child** connectors. Parent vs. child is decided by BFS depth from the root:
  the neighbour with smaller depth is the parent; larger-depth neighbours are children.
- **Leaf.** A panel is a leaf when it has **no children** — i.e. *all* of its non-parent
  connectors are unconnected. (Detection is "has no children", **not** "degree 1": the root's
  controller-facing connector is also `nullptr`, so a degree test would misclassify it.)

From this graph, with **zero per-device setup**, we can derive: BFS depth, hop-distance,
adjacency, leaf/branch classification, subtrees, and a canonical traversal order. That is the
engine the whole proposal stands on.

`resolvePanels()` is the **single resolution chokepoint** — every targeting mode flows
through it. All new vocabulary plugs in there, so the rest of the scene pipeline is untouched.

> **Scope note.** Directionality math runs **controller-side** (in the `AnimationRunner`
> subclasses), *not* on the ATmega panels. So it does **not** touch the
> `refgen` / `PanelAnimationPlayer.kt` bit-exact contract, which only governs panel-local
> `AnimationPlayer.cpp` math. Controller runners may use float freely (they already do).

### A worked topology

Used throughout the rest of this doc. Triangular panels (`N = 3`), wired as:

```
            [1]  root            depth 0
           /   \
        [2]     [3]              depth 1
        /         \
      [4]         [5]            depth 2
                    \
                    [6]          depth 3
```

| Property | Value |
|---|---|
| depths | 1→0, 2→1, 3→1, 4→2, 5→2, 6→3 |
| `root` | `{1}` |
| `leaves` (no children) | `{4, 6}` |
| `branches` (≥2 children) | `{1}` |
| `depth:1` | `{2, 3}` · `depth:2` → `{4, 5}` · `depth:1-2` → `{2,3,4,5}` |
| `subtree:3` | `{3, 5, 6}` · `neighbors:3` → `{1, 5}` |
| canonical order (DFS, edge-index order) | `1, 2, 4, 3, 5, 6` (positions 0…5) |
| `even` / `odd` (by position) | even `{1,4,5}` · odd `{2,3,6}` |
| `fraction:0-0.5` | positions 0–2 → `{1, 2, 4}` |
| `first:2` / `last:2` | `{1, 2}` / `{5, 6}` |

---

## 3. Resolution pipeline — one chokepoint, three kinds of selector

A layer's `panels` value resolves at play time into a concrete index list. Three kinds, all
freely composable:

1. **Explicit / set ops (existing, kept):** `[1,3,5]`, `"all"`, `{ "exclude": [2] }`.
2. **Graph selectors (new, portable, zero setup):** derived from the live edge graph.
3. **Tag selectors (new, optional):** resolved against a per-device tag map.

### 3.1 Grammar

A `panels` value (and any nested operand) is one of:

| Form | Example | Meaning |
|---|---|---|
| int array | `[1, 3, 5]` | explicit indices |
| bare token | `"all"`, `"root"`, `"leaves"`, `"branches"`, `"even"`, `"odd"` | graph selector |
| parametric token | `"depth:1-2"`, `"subtree:3"`, `"neighbors:3"`, `"fraction:0-0.33"`, `"first:2"`, `"last:2"`, `"tag:accent"` | graph/tag selector with an argument |
| composition object | `{ "any": [..] }`, `{ "all": [..] }`, `{ "not": sel }`, `{ "exclude": [ints] }` | set algebra over nested selectors |

`{ "any": […] }` = union, `{ "all": […] }` = intersection, `{ "not": sel }` = complement vs.
all panels, `{ "exclude": [ints] }` = back-compat sugar.

**Backward compatibility:** the v2 forms (`"all"`, `[..]`, `{ "exclude": [..] }`) are a
strict subset of this grammar, so v2 *targeting* resolves to the same panel **set**. Two
order/direction details change for existing scenes (both intentional, both detailed in
§6.4): the resolved list is emitted in discovery order, and runner directionality moves to
the φ-field.

### 3.2 Empty-resolution policy

Any selector that resolves to **zero panels** on the target device → the layer **contributes
nothing** (graceful skip). Play time **never errors** on an empty resolution. A layer may
carry an optional sibling `"fallback"` selector, used *only* when `panels` resolves empty:

```jsonc
{ "panels": "tag:accent", "fallback": "all", "sequence": [ /* … */ ] }
```

Parse errors are reserved for malformed grammar, not for "this selector matched nothing here".

### 3.3 Examples on the worked topology

```jsonc
"panels": "leaves"                          // {4, 6}
"panels": "depth:1-2"                        // {2, 3, 4, 5}
"panels": { "any": ["root", "leaves"] }      // {1, 4, 6}
"panels": { "all": ["subtree:3", "leaves"] } // {6}
"panels": { "not": "subtree:3" }             // {1, 2, 4}
"panels": [1, 3, 5]                           // precise, device-specific (kept)
```

---

## 4. Graph selector vocabulary

Computed from a cached **`TopologyIndex`**, built once per discovery and invalidated on
re-discovery (depth, parent pointers, adjacency, canonical order, leaf/branch flags).

### 4.1 Logical root (re-rooting the tree)

By default the **logical root** = the physical root (panel index 1, the controller-connected
panel). A device may **designate any panel as the logical root**. This re-centres the entire
rooted view — `depth`, parent/child, `subtree`, canonical order, leaf/branch classification,
and the default `source:root` for directionality. It makes **center-oriented animations easy
to fit a particular setup**: point the logical root at the panel that is physically "the
middle", and `source:root` ripples radiate from it while `depth:N` becomes rings around it.

- **Pure recomputation.** Re-rooting an *undirected* tree only flips parent/child orientation
  — no edge moves. So it changes **only the derived rooted view** in `TopologyIndex` (depth,
  parents, canonical order, leaf/branch, default source). Implementation is simply "build the
  index from a different start node"; because all root-dependent code reads `TopologyIndex`
  (never the raw graph), the rest of the system transparently uses the re-rooted view — the
  requested "treat the transformed topology as if it were the discovered one".
- **What it does NOT touch:** panel I²C indices/addresses (physical bus identity), the raw
  discovered adjacency, `neighbors:N` (adjacency is root-independent), or the tag/layout keys
  (still keyed by physical index). Re-rooting never goes near discovery or the bus.
- **Per-device, persisted, device-local.** Stored keyed by panel index (e.g.
  `/config/topology.json` → `{ "logicalRoot": 5 }`), applied once after discovery and
  re-applied on change. Like tags and layout (§5), it is a per-device *intent* knob, **not**
  part of the shared scene — so a center-out scene lands correctly on different setups because
  each owner picks their own centre.
- **Leaves under re-rooting.** "Leaf = no children relative to the logical root", so the
  chosen root is never a leaf and the old physical root becomes an ordinary node (a leaf if it
  has a single connector). The set of degree-1 panels is fixed; only the root's own
  classification shifts.
- **Edge case.** If the designated root panel disappears on a rewire, fall back to the
  physical root (index 1) and clear the setting.

| Selector | Meaning |
|---|---|
| `root` | the root panel (index 1, depth 0) |
| `leaves` | panels with no children |
| `branches` | fork panels with ≥2 children |
| `depth:N` / `depth:a-b` | panels exactly `N` hops from the root, or the inclusive band `a..b` |
| `subtree:N` | panel `N` and everything below it (away from the root) |
| `neighbors:N` | panels directly connected to panel `N` |
| `fraction:a-b` | panels whose normalized position along the canonical order is in `[a,b]` |
| `first:K` / `last:K` | the first / last `K` panels in canonical order |
| `even` / `odd` | parity of canonical-order position |

**What the arguments mean** (chain `root → A → B → C`, root = panel 1):

- **`depth:N`** — `N` is the hop count from the root; root is depth 0. `depth:0` = root,
  `depth:1` = A, `depth:2` = B. `depth:1-2` = `{A, B}` — an inclusive band, i.e. the "rings"
  at those distances. On a tree, a depth band selects the whole ring at that distance.
- **`fraction:a-b`** — `a, b ∈ [0,1]` are a *normalized* position along the canonical order
  (0 = first panel, 1 = last). `fraction:0-0.33` ≈ first third; `fraction:0.5-1` = second
  half. **Scales with panel count → portable**, so this is the preferred selector for
  "intent" ("the front portion") regardless of how many panels a device has.
- **`first:K` / `last:K`** — `K` is an *absolute integer count*. `first:1` = just the first
  panel; `last:2` = the final two. Precise but not count-portable (4 panels vs. 40 panels
  cover very differently) — use `fraction` when portability matters, `first/last:K` when you
  want an exact number.
- **`subtree:N` / `neighbors:N`** — `N` is a panel index on *this* device. `subtree:3` =
  panel 3 plus all descendants; `neighbors:3` = panels directly wired to panel 3.

### Canonical order

A deterministic **DFS pre-order from the root**, visiting each panel's connectors in
edge-index order. The ordering key is `(depth, parentEdgeIndex, edgeIndex)`, which makes it
**rotation-tolerant** and stable across reboots for a fixed wiring. It backs `fraction`,
`first/last`, `even/odd`, and any positional fallback.

---

## 5. Tag selectors (optional, per-device)

For meanings the graph cannot infer ("the panel *I* think of as the accent"), a device may
carry a small tag map.

- **Storage:** `/config/panel-tags.json`, keyed by panel index:
  ```json
  { "1": ["accent", "left"], "5": ["accent"] }
  ```
- **Usage:** `"panels": "tag:accent"` → all panels carrying that tag *on this device*
  (`{1, 5}` above).
- A tag no panel carries here is just an empty resolution → handled by the uniform
  empty-resolution policy in §3.2 (skip, or use the layer's `"fallback"`).
- **Authoring (later phase):** tap-to-tag in the app; endpoints `GET/PUT /api/panel-tags`.
  Adding endpoints triggers the API-doc update checklist in `CLAUDE.md`.

> **Why tags are the rare escape hatch, not mandatory setup.** The graph selectors already
> express most "intent": outer ring = `leaves`, centre = `root`, one arm = `subtree:N`, a
> ripple origin = a `source`. Tags exist only for the residue that is genuinely a personal
> label with no structural meaning. A shared scene that uses only graph selectors needs **no
> per-device setup at all**.

### Identity / key stability

Tag and layout maps are keyed by panel **index**, which is safe because index assignment is
deterministic for a fixed wiring (see §8). Indices shift only on physical rewire — the one
case where re-tagging is expected anyway.

**Contingency** (documented, not built): if discovery is ever made non-deterministic, switch
both maps to a *structural-path key* — the root→panel sequence of edge indices. That is the
same machinery as the canonical order, used as a full identity rather than a sort key.

---

## 6. Directionality as a scalar field `φ(panel) ∈ [0, 1]`

Every moving runner is redefined as: compute a 1-D coordinate `φ` per targeted panel, then
map `(φ, t)` → brightness, where `t ∈ [0,1]` is normalized elapsed time. Only the **source**
of `φ` differs. This replaces "walk the list in discovery order" with a real, portable
spatial field.

### 6.1 Graph-distance field (default, portable)

`source` is a **set** `S` of panels. For each targeted panel `p`:

```
φ(p) = dist(p, S) / maxDist
```

where `dist(p, S)` is the minimum hop-distance from `p` to any panel in `S`, and `maxDist` is
the largest such distance over the targeted panels. So **φ = 0 at the source and 1 at the
farthest panel; the effect emanates from the source outward** as `t` runs 0 → 1.

This makes every source well-defined, single- or multi-source:

| `source` | Field |
|---|---|
| `root` (default) | emanate outward from the root |
| `panel:N` | emanate from panel `N` |
| `leaves` | multi-source: φ=0 at *every* leaf, rising inward → effect converges from the extremities toward the interior (the distance-to-nearest-leaf field) |
| `tag:x` | multi-source from all panels tagged `x` |
| `all` (degenerate) | `maxDist = 0` → φ=0 everywhere → uniform/synchronous, no gradient |

`"reverse": true` flips `φ → 1 − φ`, so the effect travels back *toward* the source (e.g. a
ripple collapsing inward to the root). Note `root` here means the **logical root** (§4.1): on
a re-rooted device every `source:root` field — and the `depth`/`subtree` selectors —
automatically re-centre on the chosen panel.

**Worked field values** (topology from §2):

```
source:root    φ:  1→0.00  2→0.33  3→0.33  4→0.67  5→0.67  6→1.00   (radiates out)
source:leaves  φ:  4→0.00  6→0.00  5→0.50  2→0.50  1→1.00  3→1.00   (converges in)
```

### 6.2 Geometric field (optional tier, auto-derived)

For scenes that truly want literal screen-space direction (a real left → right sweep), we
project panel **coordinates** onto a requested axis. Crucially, the coordinates are
**auto-derived, not hand-authored**:

Panels are **regular polygons** (`NUMBER_OF_EDGES`, range 3–5, fixed edge angles — see
`src/panel.config.hpp.example`). So the planar layout is *computable from the topology*: walk
the tree from the root and place each child by its shared edge (parent-edge angle ↔
child-edge angle), assigning every panel an `(x, y)` and rotation. **Zero per-device setup**
for flat installations.

A geometric step (`"axis"` / `"angle"`) then projects those coordinates onto the requested
axis to produce `φ`.

> **Tiling caveat.** A clean non-overlapping planar embedding is only guaranteed for
> plane-tiling polygons — `N = 3` (triangles) and `N = 4` (squares). Regular **pentagons
> (`N = 5`) and mixed-`N` networks have no general planar tiling**, so their auto-embedding
> may overlap; those installs use the graph-distance fallback or the manual `layout.json`
> override. This is a Phase-4 concern and blocks nothing earlier.

- **No protocol change, no extra panel reporting.** Discovery already registers *every* edge
  of every panel — the panel cycles all of its edge indices and sends a `PacketRegisterEdge`
  for each *before* it tries to boot a child (`lib/Lightnet/Panel/LightnetPanel.cpp`,
  `registerEdges`/`setNextEdgeToRegister`). So the controller already holds each panel's full
  edge set: `N = panel->edges->getSize()`, plus every edge's index (→ angle) and connection
  status. Mixed-shape networks work for free. The whole geometric tier is **controller-side
  firmware only**.
- **Manual input shrinks to at most a single global "up" orientation** (how the whole piece
  is physically mounted) — never per-panel placement. `/config/layout.json` becomes a
  *computed cache + optional overrides*, needed only for non-planar / 3-D arrangements.
- **Fallback when geometry can't be embedded** (non-planar/overlapping arrangement, or an
  `axis`/`angle` step before the embedding is computed) → revert to graph-distance
  `source:root`, so such scenes still run.

### 6.3 Runner math (as built)

Controller-side, float. The runners sweep each panel's **raw hop-distance** `coord` (the
*un-normalised* φ — see the note below) over time `t ∈ [0,1]`, differing only in the envelope.
`maxCoord` is the largest coord among the targeted panels; `width` is in **rings (hops)**.
Implemented in the pure, natively-tested `RunnerMath.hpp`:

| Runner | Sweep | Brightness |
|---|---|---|
| `WAVE`   | `center = −1.5 + (maxCoord+3)·t` | triangular band: `255·(1 − \|coord − center\|/(width/2))`, else 0 |
| `RIPPLE` | `radius = (maxCoord+1)·t` | expanding ring: `255·(1 − \|coord − radius\|/(width/2))`, else 0 |
| `CHASE`  | `lit = min(maxCoord, ⌊(maxCoord+1)·t⌋)` | single lit ring: `coord == lit ? 255 : 0` |

This **unifies the three runner classes** onto one field abstraction; each stores a `coord[]`
parallel to `panelAddresses[]` (alongside the existing `lastBrightness[]` delta cache).

> **Why raw hops, not normalised φ (deviation from §6.1).** §6.1 defines φ ∈ [0,1] for
> exposition, but the runners keep the coordinate as the integer **hop-distance** and never
> divide by `maxCoord`. This (a) keeps `waveWidth`/`rippleWidth` in intuitive **ring units**,
> (b) makes a chain rooted at one end reproduce the legacy list-order visual exactly, and
> (c) structurally avoids a divide-by-zero when `maxCoord == 0` (e.g. `source:all`, single
> panel). `coord` contract: integer hop units, `0 … maxCoord` (Phase 4's geometric field will
> feed this same axis).

### 6.4 Migrating existing (v2) runner scenes — behaviour change

The `panels` *targeting* grammar is a strict superset (§3.1), so targeting behaves
identically. **The runner directionality is not** — the `φ`-field replaces the old
discovery-list traversal, so existing runner scenes can render differently. This is
intentional: discovery-list order was never a real spatial axis. The migration is:

| v2 runner | v2 behaviour | v3 mapping |
|---|---|---|
| `RIPPLE` | origin = `originPanel`, but the old `tick` compared it to the **0-based list position** `i` (not the panel index, despite the docs) | `source:"panel:<originPanel>"` — now a graph origin keyed by **panel index** |
| `WAVE` / `CHASE` | swept the panel **list in discovery order** | `source:"root"` (default) — visually different, now topology-based |

The parser performs this mapping itself: a runner step accepts `"source"` (`root` / `leaves` /
`all` / `panel:N`) and `"reverse": true`, and the legacy `originPanel` key is rewritten to
`source:panel:N`. A step with no `source` defaults to `root` (zeroed `params`).

**Fidelity of the migration** (deliberate, not bugs): for a chain rooted at one end
(`coord == index`), **WAVE is preserved exactly**. **CHASE** matches within **~1 ring** (the
field sweeps `maxCoord+1` vs. the old `panelCount`, and clamps at the end instead of the old
`% panelCount` wrap). **RIPPLE** shares that sweep-extent drift *and* its origin is
reinterpreted — old `originPanel` indexed the resolved-list position, v3 keys it by panel
index — so a ripple's origin can move by more than a ring. If exact origin matters, set
`source:"panel:N"` explicitly.

> **Reserved params.** On a runner step, `params[1..3]` encode the source (kind / arg /
> reverse) and `params[0]` the width — set them via `source`/`reverse`/`waveWidth`/
> `rippleWidth`, not a raw `params:[…]` array, which would collide.

**Resolved-list emission order.** Targeting now resolves through a set (bitset), so panels are
emitted in **discovery (slot) order**, not the authored order of an explicit `[5,3,1]` list.
This is invisible to global anims (every panel gets the same packet) and to `all`. It only
affects a *runner over an explicit ordered list*, which in Phase 1 still sweeps list order —
and Phase 2 replaces that with the φ-field regardless. Authored list order is therefore not
preserved; use a `source` (Phase 2) to express direction.

---

## 7. Scene format / schema changes

- Bump `SCENE_SCHEMA_VERSION` 2 → 3.
- `panels` grammar extended per §3 (string/parametric selectors, composition objects,
  optional sibling `"fallback"`). **v2 scenes parse unchanged.**
- Runner steps gain `"source"` (`root | leaves | panel:N | tag:x | all`) + optional
  `"reverse"`, and optional `"axis"` / `"angle"` for the geometric field.
- Config files: `/config/panel-tags.json` (optional) and `/config/layout.json` (optional —
  computed cache + overrides; absence means coordinates are derived live, or the geometric
  field falls back to graph-distance).
- **No protocol change anywhere.** Polygon edge-count `N`, edge angles, and adjacency are all
  recoverable from existing discovery data, so nothing here forces a coordinated
  controller + panel reflash.

### Example scenes

**Portable, graph-only** — a ripple radiating from the centre, then a wave converging on the
extremities. Runs on *any* device, no setup:

```jsonc
{
  "schemaVersion": 3,
  "name": "pulse_and_gather",
  "loop": true,
  "palette": "ocean",
  "layers": [
    {
      "group": "ripple",
      "panels": "all",
      "sequence": [
        { "runner": "RIPPLE", "source": "root", "color": "#30C0FF",
          "rippleWidth": 2, "duration": 1800 }
      ]
    },
    {
      "group": "gather",
      "startAfter": "ripple",
      "panels": "all",
      "sequence": [
        { "runner": "WAVE", "source": "leaves", "color": "#FFFFFF",
          "duration": 2200 }
      ]
    }
  ]
}
```

**Mixed precise + portable** — exact accent panel for *this* device, plus a portable backdrop:

```jsonc
{
  "schemaVersion": 3,
  "name": "accent_plus_backdrop",
  "layers": [
    { "group": "accent",   "panels": [3],
      "sequence": [ { "type": "BREATHE", "colorFrom": "#000000", "colorTo": "#FF2070", "duration": 2000 } ] },
    { "group": "backdrop", "panels": { "not": [3] },
      "sequence": [ { "type": "SOLID", "color": { "palette": 40 }, "duration": 2000 } ] }
  ]
}
```

**Tag-based with fallback** — uses each owner's `accent` tag, but still does something on a
device that has none:

```jsonc
{ "group": "hi", "panels": "tag:accent", "fallback": "leaves",
  "sequence": [ { "runner": "CHASE", "source": "root", "color": "#FFD040", "duration": 1500 } ] }
```

**Geometric** — a literal left-to-right sweep (auto-derived coordinates):

```jsonc
{ "group": "sweep", "panels": "all",
  "sequence": [ { "runner": "WAVE", "axis": "x", "color": "#80FF80", "duration": 2500 } ] }
```

---

## 8. Identity & determinism (why index keys are safe)

The tag map, the layout map, and explicit-index targeting all assume panel indices are stable
for a fixed wiring. They are:

- **Controller side** — discovery is serialized: pull → `PacketRegisterEdge` → ack, one panel
  at a time, `currentPanelIndex++` per panel (`PanelsInitializer.cpp`). No race.
- **Panel side** — each panel cycles its edges in fixed index order
  (`LightnetPanel::setNextEdgeToRegister`), so the whole-tree boot sequence — and therefore
  index assignment — is reproducible for a fixed wiring.

Indices change only on **physical rewire** (the case the user accepts as a re-map trigger).
Panel **rotation** is absorbed in panel firmware (edges reindex by parent edge); the
canonical order's rotation-tolerant key absorbs the rest. The structural-path key (§5) remains
a documented contingency only.

---

## 9. Edge cases & fallbacks

| Situation | Behaviour |
|---|---|
| Selector resolves to empty on this device | layer contributes nothing, unless a `"fallback"` selector is given (§3.2) |
| Explicit index that doesn't exist here | skipped (current behaviour, kept) |
| Geometric runner can't embed (non-planar) | graph-distance `source:root` |
| `source` panel/tag absent | empty source set → treated as `source:root` |
| Panel rotated in place | handled in panel firmware; canonical order unaffected |

---

## 10. Recommended phasing

1. **Firmware:** `TopologyIndex` (bidirectional adjacency, depth, parents, canonical order),
   **parameterised by a start node** so re-rooting is just a rebuild (§4.1), + graph
   selectors in `resolvePanels`, with native tests. *(See note below.)*
2. **Firmware:** refactor the runners onto the `φ`-field model with the graph-distance source.
3. **Firmware + config:** `panel-tags.json` + tag selectors + endpoints; persist the logical
   root (`/config/topology.json`) + a `GET/PUT /api/topology/root` endpoint.
4. **Firmware:** auto-derive planar coordinates from topology; geometric `φ` + `axis`/`angle`;
   optional global "up" orientation. No protocol change.
5. **Mobile:** authoring UI for selectors, tags, and the (rare) layout overrides.

> **Phase-1 nicety.** The controller currently links `connectedEdge` only parent→child in
> `PanelsInitializer::registerPanel`. The `TopologyIndex` should build **full bidirectional
> adjacency** (trivial — pair up by panel index + edge index) so `neighbors:N`, ripple
> flood-fill, and the geometric walk all have it.

---

## 11. Verification

- **Native unit tests** (`pio test -e native`) for selector resolution against a mock
  `TopologyIndex` (assert each selector on the §2 worked topology) and for the `φ`-field math
  (assert the worked field values).
- **Sim / live** — `tools/api-shell` to play sample portable scenes; `mirror-dump.js` to
  confirm the per-panel `SET_COLOR` pattern matches expectation across different mock
  topologies.
- **Sample scenes** added under `tools/` (the §7 examples).

---

## 12. Open items

- Exact `"reverse": true` vs. `"direction": "inward" | "outward"` spelling (cosmetic).
- Non-planar / 3-D installs: the override format inside `layout.json` (Phase 4 detail).
- Whether `TopologyIndex` rebuilds eagerly on every discovery or lazily on first scene play
  (perf vs. simplicity).
- Logical root (§4.1) is device-local by design; decide whether a scene may *optionally* pin
  its own root for authoring (probably not — keep it a per-device intent knob like tags).

---

## 13. Source-file map (for implementation)

| File | Role in this design |
|---|---|
| `lib/Lightnet/Controller/Scenes/ScenePlayer.cpp` (`resolvePanels`, `fireStep`) | resolution chokepoint to extend; runner construction |
| `lib/Lightnet/Controller/Scenes/ScenePlayer.hpp` (`SceneLayer`, `PanelSelector`) | targeting data model |
| `lib/Lightnet/Controller/Scenes/SceneParser.cpp` | `panels` grammar + runner-step parsing |
| `lib/Lightnet/Controller/Animations/AnimationRunner.hpp/.cpp` | runner classes to refactor onto `φ` |
| `lib/Lightnet/Controller/Panels/{Panel,Edge,PanelsInitializer}.hpp` | graph source for `TopologyIndex` |
| `lib/Lightnet/Panel/LightnetPanel.cpp` | confirms every edge is registered (N derivable) |
| `docs/animations.md`, `docs/animations/api.md`, `docs/architecture.md`, `openapi.json` | doc/update targets once implemented |
