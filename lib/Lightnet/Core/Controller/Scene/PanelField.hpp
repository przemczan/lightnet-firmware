#pragma once

// ============================================================================
// PanelField — the directionality field for controller-side runners.
//
// A moving runner (WAVE/RIPPLE/CHASE) sweeps a 1-D spatial coordinate over time.
// That coordinate is each panel's graph **hop-distance from a source set** — the
// (un-normalised) φ-field of docs/animations/scene-authoring.md §8. Raw hops are
// used deliberately: width parameters stay in intuitive ring units, a chain
// rooted at one end reproduces the legacy list-order behaviour, and there is no
// division by maxCoord (so a zero-span field can't divide by zero).
//
// Pure C++ (TopologyIndex only) → natively unit-testable.
// ============================================================================

#include <stdint.h>
#include "TopologyIndex.hpp"
#include "../../Common/AnimationTypes.hpp"

namespace Lightnet {
    // How a runner step encodes its directionality in SceneStep.params[].
    // (Runner steps never go through PREPARE, so params[] is controller-side only.)
    //
    // Directionality is two orthogonal choices:
    //   • mode    — graph hop-distance (default) vs planar geometry (RUNNER_FLAG_GEOMETRIC)
    //   • source  — where the field emanates from (root/leaves/all/panel:N)
    // plus an `angle` that only steers a *geometric* axis sweep (wave/chase). See fireStep.
    //
    // A third, orthogonal choice — `animates` (SceneStep.animates, AnimateTarget) — picks
    // *what* the sweep modulates and, for non-colour targets, `amount` (RUNNER_PARAM_AMOUNT)
    // sets the peak intensity.
    //
    // A fourth, independent toggle — `repeat` (RUNNER_FLAG_REPEAT) — turns WAVE/RIPPLE/
    // CHASE into a continuous train instead of a single sweep (compile*Repeating in
    // RunnerCompile.hpp); WHEEL (always a continuous rotation) ignores it.
    // RUNNER_PARAM_LINES (WHEEL) and RUNNER_PARAM_REPEAT_COUNT (WAVE/RIPPLE/CHASE) share slot 5 — mutually exclusive.
    static const uint8_t RUNNER_PARAM_WIDTH        = 0; // wave/ripple band width (rings); wheel: blade thickness (degrees)
    static const uint8_t RUNNER_PARAM_SRC_KIND     = 1; // RunnerSource
    static const uint8_t RUNNER_PARAM_SRC_ARG      = 2; // SRC_PANEL: panel index; geometric wave/chase: angle/2°
    static const uint8_t RUNNER_PARAM_FLAGS        = 3;
    static const uint8_t RUNNER_PARAM_AMOUNT       = 4; // peak scalar (0-255) for non-colour `animates` targets
    static const uint8_t RUNNER_PARAM_LINES        = 5; // WHEEL: number of rotating blades (1-6)
    static const uint8_t RUNNER_PARAM_REPEAT_COUNT = 5; // WAVE/RIPPLE/CHASE with `repeat`: waves per duration (0 or 1 = one wave)
    static const uint8_t RUNNER_FLAG_REVERSE   = 0x01;
    static const uint8_t RUNNER_FLAG_GEOMETRIC = 0x02; // planar geometry instead of graph hop-distance
    static const uint8_t RUNNER_FLAG_REPEAT    = 0x20; // WAVE/RIPPLE/CHASE: continuous train instead of one sweep

    // Source the field emanates from. Default (0) = root, so a zeroed step radiates
    // outward from the root — matching the v2 wave/chase migration. Orthogonal to the
    // geometric flag: a geometric ripple still uses these to choose its centre(s).
    enum RunnerSource : uint8_t {
        SRC_ROOT      = 0, // the (logical) root
        SRC_LEAVES    = 1, // every leaf (field converges inward / one ripple per leaf)
        SRC_ALL       = 2, // all panels — hop-distance: degenerate (maxCoord 0, uniform);
                           // geometric ripple: single ripple from the geometric centre (see PanelGeometry.hpp)
        SRC_PANEL     = 3, // a specific panel index (SRC_ARG)
    };

    // Build the source slot-set for `kind`/`arg`. An empty result (e.g. SRC_PANEL
    // naming a panel that isn't here) falls back to the root, per §6.1.
    inline void buildSourceSet(const TopologyIndex& topo, uint8_t kind, uint8_t arg, PanelSet& out)
    {
        out.clearAll();

        const uint8_t n = topo.count();

        switch (kind) {
            case SRC_LEAVES:

                for (uint8_t s = 0; s < n; s++) if (topo.isLeaf(s)) out.set(s);

                break;

            case SRC_ALL:

                for (uint8_t s = 0; s < n; s++) out.set(s);

                break;

            case SRC_PANEL:
            {
                uint8_t sl;

                if (topo.slotOf(arg, sl)) out.set(sl);

                break;
            }

            case SRC_ROOT:
            default:

                if (n) out.set(topo.root());

                break;
        }

        if (n && out.popcount(n) == 0) out.set(topo.root()); // empty → root fallback
    }

    // Compute each targeted panel's spatial coordinate (hop-distance from the source),
    // writing coordOut[0..panelCount) parallel to panels[]. With `reverse`, the field is
    // flipped so the effect travels back toward the source. Returns maxCoord (the largest
    // coordinate among the targeted panels) — the sweep span for the runner envelopes.
    inline uint8_t computeDistanceField(
        const TopologyIndex& topo,
        const uint8_t *      panels,
        uint8_t              panelCount,
        uint8_t              srcKind,
        uint8_t              srcArg,
        bool                 reverse,
        uint8_t *            coordOut
    )
    {
        PanelSet src;

        buildSourceSet(topo, srcKind, srcArg, src);

        uint8_t dist[LIGHTNET_MAX_PANELS];

        topo.distancesFrom(src, dist);

        uint8_t maxD = 0;

        for (uint8_t i = 0; i < panelCount; i++) {
            uint8_t sl, d = 0;

            if (topo.slotOf(panels[i], sl) && dist[sl] != 0xFF) d = dist[sl];

            coordOut[i] = d;

            if (d > maxD) maxD = d;
        }

        if (reverse) {
            for (uint8_t i = 0; i < panelCount; i++) coordOut[i] = (uint8_t)(maxD - coordOut[i]);
        }

        return maxD;
    }
}  // namespace Lightnet
