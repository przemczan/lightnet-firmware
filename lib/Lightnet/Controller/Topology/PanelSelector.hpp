#pragma once

// ============================================================================
// PanelSelector — a compact RPN byte-program describing which panels a scene
// layer targets, resolved at play time against a TopologyIndex.
//
// Pure C++ (no Arduino) → natively unit-testable. The scene parser emits one of
// these per layer; ScenePlayer evaluates it against the device's live topology.
//
// The legacy v2 targeting forms map straight in, so this is a strict superset:
//   "all"            → [ALL]
//   [1,3,5]          → [INDICES 3 1 3 5]
//   {"exclude":[2]}  → [INDICES 1 2  NOT]      (all panels minus {2})
//
// See docs/design/scene-portability.md §3–§4.
// ============================================================================

#include <stdint.h>
#include "TopologyIndex.hpp"

namespace Lightnet {
    // Opcodes. Leaf ops push a PanelSet; AND/OR/NOT combine the stack top(s).
    enum SelOp : uint8_t {
        SEL_ALL = 1,    // every panel
        SEL_ROOT,       // the (logical) root
        SEL_LEAVES,     // panels with no children
        SEL_BRANCHES,   // panels with >= 2 children
        SEL_EVEN,       // even canonical-order position
        SEL_ODD,        // odd canonical-order position
        SEL_DEPTH,      // + a, b      : depth band [a,b] inclusive
        SEL_SUBTREE,    // + panelIdx  : panel + descendants
        SEL_NEIGHBORS,  // + panelIdx  : panels directly wired to panelIdx
        SEL_FRACTION,   // + a, b      : normalized canonical position in [a/255, b/255]
        SEL_FIRST,      // + k         : first k in canonical order
        SEL_LAST,       // + k         : last  k in canonical order
        SEL_INDICES,    // + len, idx… : explicit 1-based panel indices
        SEL_TAG,        // + tagId     : per-device tag (Phase 3; resolves empty for now)
        SEL_AND,        // pop b,a → a ∩ b
        SEL_OR,         // pop b,a → a ∪ b
        SEL_NOT,        // pop a    → complement(a)
    };

    // Cap on an authored explicit index list ([1,3,5] / {"exclude":[…]}). Mirrors the
    // legacy SCENE_MAX_PANELS_PER_LAYER; kept here so the pure parser/resolver don't
    // need the Arduino-bound ScenePlayer header.
    static const uint8_t SEL_MAX_INDEX_LIST = 32;

    struct PanelSelector {
        static const uint8_t CAP = 64;
        uint8_t              ops[CAP];
        uint8_t              len;

        void clear()
        {
            len = 0;
        }

        // Append one byte; returns false if the program would overflow CAP.
        bool emit(uint8_t b)
        {
            if (len >= CAP) return false;

            ops[len++] = b;

            return true;
        }
    };

    // Max nesting depth of the evaluation stack (PanelSets). Composition is left-folded
    // so realistic programs need very little; 6 is comfortable headroom.
    static const uint8_t SEL_STACK_MAX = 6;

    // Evaluate one leaf opcode into `s`, advancing `i` past its inline operands.
    // Returns false only on a truncated program (operands run past len).
    inline bool selEvalLeaf(
        uint8_t              op,
        const PanelSelector& sel,
        uint8_t&             i,
        const TopologyIndex& topo,
        PanelSet&            s
    )
    {
        const uint8_t COUNT = topo.count();
        const uint8_t avail = (uint8_t)(sel.len - i);

        switch (op) {
            case SEL_ALL:

                for (uint8_t x = 0; x < COUNT; x++) s.set(x);

                return true;

            case SEL_ROOT:
                s.set(topo.root());

                return true;

            case SEL_LEAVES:

                for (uint8_t x = 0; x < COUNT; x++) if (topo.isLeaf(x)) s.set(x);

                return true;

            case SEL_BRANCHES:

                for (uint8_t x = 0; x < COUNT; x++) if (topo.isBranch(x)) s.set(x);

                return true;

            case SEL_EVEN:

                for (uint8_t x = 0; x < COUNT; x++) if ((topo.canonicalPos(x) & 1) == 0) s.set(x);

                return true;

            case SEL_ODD:

                for (uint8_t x = 0; x < COUNT; x++) if (topo.canonicalPos(x) & 1) s.set(x);

                return true;

            case SEL_DEPTH:
            {
                if (avail < 2) return false;

                uint8_t a = sel.ops[i++], b = sel.ops[i++];

                for (uint8_t x = 0; x < COUNT; x++) {
                    uint8_t d = topo.depthOf(x);

                    if (d >= a && d <= b) s.set(x);
                }

                return true;
            }

            case SEL_SUBTREE:
            {
                if (avail < 1) return false;

                uint8_t p = sel.ops[i++], sl;

                if (topo.slotOf(p, sl)) topo.fillSubtree(sl, s);

                return true;
            }

            case SEL_NEIGHBORS:
            {
                if (avail < 1) return false;

                uint8_t p = sel.ops[i++], sl;

                if (topo.slotOf(p, sl)) {
                    for (uint8_t k = 0; k < topo.degree(sl); k++) s.set(topo.neighborSlot(sl, k));
                }

                return true;
            }

            case SEL_FRACTION:
            {
                if (avail < 2) return false;

                uint8_t a = sel.ops[i++], b = sel.ops[i++];

                if (COUNT <= 1) {
                    if (a == 0) s.set(0);

                    return true;
                }

                uint16_t span = (uint16_t)(COUNT - 1);

                for (uint8_t x = 0; x < COUNT; x++) {
                    uint16_t np255 = (uint16_t)topo.canonicalPos(x) * 255;

                    if (np255 >= (uint16_t)a * span && np255 <= (uint16_t)b * span) s.set(x);
                }

                return true;
            }

            case SEL_FIRST:
            {
                if (avail < 1) return false;

                uint8_t k = sel.ops[i++];

                for (uint8_t x = 0; x < COUNT; x++) if (topo.canonicalPos(x) < k) s.set(x);

                return true;
            }

            case SEL_LAST:
            {
                if (avail < 1) return false;

                uint8_t k    = sel.ops[i++];
                uint8_t from = (k >= COUNT) ? 0 : (uint8_t)(COUNT - k);

                for (uint8_t x = 0; x < COUNT; x++) if (topo.canonicalPos(x) >= from) s.set(x);

                return true;
            }

            case SEL_INDICES:
            {
                if (avail < 1) return false;

                uint8_t listLen = sel.ops[i++];

                if ((uint8_t)(sel.len - i) < listLen) return false;

                for (uint8_t k = 0; k < listLen; k++) {
                    uint8_t p = sel.ops[i++], sl;

                    if (topo.slotOf(p, sl)) s.set(sl); // missing indices are skipped
                }

                return true;
            }

            case SEL_TAG:

                if (avail < 1) return false;

                i++;          // tagId — Phase 3; resolves to empty for now

                return true;

            default:
                return false; // unknown opcode in a leaf position
        }
    }

    // Resolve a selector program into `out` (a slot bitset). Returns false on a
    // malformed program (stack under/overflow, bad opcode, truncated operands).
    inline bool resolveSelector(const PanelSelector& sel, const TopologyIndex& topo, PanelSet& out)
    {
        PanelSet stack[SEL_STACK_MAX];
        uint8_t sp = 0;
        uint8_t i  = 0;

        while (i < sel.len) {
            uint8_t op = sel.ops[i++];

            if (op == SEL_AND || op == SEL_OR) {
                if (sp < 2) return false;

                if (op == SEL_AND) stack[sp - 2].andWith(stack[sp - 1]);
                else stack[sp - 2].orWith(stack[sp - 1]);

                sp--;
            } else if (op == SEL_NOT) {
                if (sp < 1) return false;

                stack[sp - 1].invert(topo.count());
            } else {
                if (sp >= SEL_STACK_MAX) return false;

                stack[sp].clearAll();

                if (!selEvalLeaf(op, sel, i, topo, stack[sp])) return false;

                sp++;
            }
        }

        if (sp != 1) return false;

        out = stack[0];

        return true;
    }

    // Flatten a resolved slot set into 1-based panel indices, in slot (discovery)
    // order, bounded by maxLen. Preserves the existing emission order so runner
    // visuals are unchanged until the Phase-2 φ refactor.
    inline void emitPanelIndices(
        const PanelSet&      set,
        const TopologyIndex& topo,
        uint8_t *            out,
        uint8_t              maxLen,
        uint8_t&             count
    )
    {
        count = 0;

        for (uint8_t x = 0; x < topo.count(); x++) {
            if (set.test(x) && count < maxLen) out[count++] = topo.panelAt(x);
        }
    }
}  // namespace Lightnet
