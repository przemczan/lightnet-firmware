#pragma once

// ============================================================================
// TopologyIndex — a rooted, derived view of the discovered panel tree.
//
// Pure C++ (no Arduino), so it is unit-testable natively. It is built on top
// of a PanelGraph (the root-independent raw adjacency, shared with other
// rooted views such as PanelGeometry — see PanelGraph.hpp); a thin
// controller-side adapter feeds that graph from PanelsInitializer::getPanels().
//
// "Rooted view" means: given a chosen root panel, it computes BFS depth, parent
// pointers, child counts (→ leaf/branch), a deterministic canonical traversal
// order, plus subtree / multi-source distance helpers. Re-rooting the tree (see
// docs/animations/scene-authoring.md §10) is simply build() with a different
// root — everything downstream reads this index, never the raw graph.
// ============================================================================

#include <stdint.h>
#include <string.h>
#include "../../Core/Anim/LightnetConfig.hpp"
#include "PanelGraph.hpp" // PanelGraph, TopoLink

namespace Lightnet {
    // Fixed-capacity bitset over panel *slots* (0..count-1), not panel indices.
    struct PanelSet {
        static const uint8_t WORDS = (LIGHTNET_MAX_PANELS + 7) / 8;
        uint8_t              bits[WORDS];

        void clearAll()
        {
            memset(bits, 0, sizeof(bits));
        }

        void set(uint8_t i)
        {
            bits[i >> 3] |=  (uint8_t)(1u << (i & 7));
        }

        void unset(uint8_t i)
        {
            bits[i >> 3] &= (uint8_t) ~(1u << (i & 7));
        }

        bool test(uint8_t i) const
        {
            return (bits[i >> 3] >> (i & 7)) & 1u;
        }

        void orWith(const PanelSet& o)
        {
            for (uint8_t w = 0; w < WORDS; w++) bits[w] |= o.bits[w];
        }

        void andWith(const PanelSet& o)
        {
            for (uint8_t w = 0; w < WORDS; w++) bits[w] &= o.bits[w];
        }

        // Complement within the valid slot range [0, count).
        void invert(uint8_t count)
        {
            for (uint8_t i = 0; i < count; i++) test(i) ? unset(i) : set(i);
        }

        uint8_t popcount(uint8_t count) const
        {
            uint8_t n = 0;

            for (uint8_t i = 0; i < count; i++) if (test(i)) n++;

            return n;
        }
    };

    class TopologyIndex
    {
        public:
            TopologyIndex() : graph(nullptr), n(0), rootSlot(0), maxDepthVal(0)
            {
            }

            // Build the rooted view on top of a pre-built raw graph.
            //   graph        : the root-independent adjacency, built once and shared
            //                  with other rooted views (e.g. PanelGeometry). Borrowed —
            //                  must outlive this object and stay unchanged while it's
            //                  in use (rebuild both together via SceneTopology::rebuild).
            //   rootPanelIdx : 1-based panel to root at; if absent, falls back to the
            //                  smallest panel index (the physical root, index 1).
            // Returns false on malformed input (empty graph).
            bool build(const PanelGraph &g, uint8_t rootPanelIdx)
            {
                graph = &g;
                n     = g.count();

                if (n == 0) return false;

                rootSlot = pickRoot(rootPanelIdx);
                bfsFromRoot();
                computeCanonicalOrder();

                return true;
            }

            uint8_t count()                  const
            {
                return n;
            }

            uint8_t panelAt(uint8_t slot)    const
            {
                return graph->panelAt(slot);
            }

            uint8_t root()                   const
            {
                return rootSlot;
            }

            uint8_t depthOf(uint8_t slot)    const
            {
                return depth[slot];
            }

            uint8_t maxDepth()               const
            {
                return maxDepthVal;
            }

            uint8_t canonicalPos(uint8_t s)  const
            {
                return canonPos[s];
            }

            uint8_t parentOf(uint8_t slot)   const
            {
                return parent[slot];
            }                                                               // 0xFF for the root

            bool    isLeaf(uint8_t slot)     const
            {
                return childCount[slot] == 0;
            }

            bool    isBranch(uint8_t slot)   const
            {
                return childCount[slot] >= 2;
            }

            uint8_t degree(uint8_t slot)     const
            {
                return graph->degree(slot);
            }

            uint8_t neighborSlot(uint8_t slot, uint8_t i) const
            {
                return graph->neighborSlot(slot, i);
            }

            bool slotOf(uint8_t panelIndex, uint8_t& slot) const
            {
                return graph->slotOf(panelIndex, slot);
            }

            // Fill `out` with `slot` and all of its descendants (the subtree rooted at slot).
            void fillSubtree(uint8_t slot, PanelSet& out) const
            {
                uint8_t stack[MAXN];
                uint8_t sp = 0;

                stack[sp++] = slot;

                while (sp) {
                    uint8_t u = stack[--sp];

                    out.set(u);

                    for (uint8_t i = 0; i < graph->degree(u); i++) {
                        uint8_t v = graph->neighborSlot(u, i);

                        if (parent[v] == u) stack[sp++] = v;
                    }
                }
            }

            // Min hop-distance from any slot in `sources` to every slot → dist[0..n).
            // Unreachable slots get 0xFF (cannot happen in a connected tree). Underpins
            // the φ directionality field in Phase 2.
            void distancesFrom(const PanelSet& sources, uint8_t *dist) const
            {
                uint8_t queue[MAXN];
                uint8_t qh = 0, qt = 0;

                for (uint8_t s = 0; s < n; s++) dist[s] = 0xFF;

                for (uint8_t s = 0; s < n; s++) {
                    if (sources.test(s)) {
                        dist[s]     = 0;
                        queue[qt++] = s;
                    }
                }

                while (qh < qt) {
                    uint8_t u = queue[qh++];

                    for (uint8_t i = 0; i < graph->degree(u); i++) {
                        uint8_t v = graph->neighborSlot(u, i);

                        if (dist[v] == 0xFF) {
                            dist[v]     = dist[u] + 1;
                            queue[qt++] = v;
                        }
                    }
                }
            }

        private:
            static const uint8_t MAXN = LIGHTNET_MAX_PANELS;

            const PanelGraph *graph; // borrowed root-independent adjacency (see build())

            uint8_t n;
            uint8_t rootSlot;
            uint8_t maxDepthVal;

            uint8_t depth[MAXN];
            uint8_t parent[MAXN];       // slot → parent slot (0xFF = root)
            uint8_t childCount[MAXN];
            uint8_t canonPos[MAXN];

            uint8_t pickRoot(uint8_t rootPanelIdx) const
            {
                uint8_t slot;

                if (graph->slotOf(rootPanelIdx, slot)) return slot;

                return graph->lowestSlot();
            }

            void bfsFromRoot()
            {
                uint8_t queue[MAXN];
                bool visited[MAXN];
                uint8_t qh = 0, qt = 0;

                for (uint8_t s = 0; s < n; s++) {
                    depth[s]      = 0;
                    parent[s]     = 0xFF;
                    childCount[s] = 0;
                    visited[s]    = false;
                }

                maxDepthVal       = 0;
                depth[rootSlot]   = 0;
                parent[rootSlot]  = 0xFF;
                visited[rootSlot] = true;
                queue[qt++]       = rootSlot;

                while (qh < qt) {
                    uint8_t u = queue[qh++];

                    for (uint8_t i = 0; i < graph->degree(u); i++) {
                        uint8_t v = graph->neighborSlot(u, i);

                        if (!visited[v]) {
                            visited[v] = true;
                            parent[v]  = u;
                            depth[v]   = depth[u] + 1;

                            if (depth[v] > maxDepthVal) maxDepthVal = depth[v];

                            childCount[u]++;
                            queue[qt++] = v;
                        }
                    }
                }
            }

            // DFS pre-order from the root, visiting each node's children in ascending
            // order of *this node's* connector index → deterministic, rotation-tolerant.
            void computeCanonicalOrder()
            {
                uint8_t stack[MAXN];
                uint8_t sp    = 0;
                uint8_t order = 0;

                stack[sp++] = rootSlot;

                while (sp) {
                    uint8_t u = stack[--sp];

                    canonPos[u] = order++;

                    // Gather children of u together with u's connector index toward each.
                    uint8_t cs[8];
                    uint8_t ce[8];
                    uint8_t cc = 0;

                    for (uint8_t i = 0; i < graph->degree(u); i++) {
                        uint8_t v = graph->neighborSlot(u, i);

                        if (parent[v] == u && cc < 8) {
                            cs[cc] = v;
                            ce[cc] = graph->neighborMyEdge(u, i);
                            cc++;
                        }
                    }

                    // Insertion sort children by connector index, ascending.
                    for (uint8_t i = 1; i < cc; i++) {
                        uint8_t kv = cs[i], keo = ce[i];
                        int8_t j  = (int8_t)i - 1;

                        while (j >= 0 && ce[j] > keo) {
                            cs[j + 1] = cs[j];
                            ce[j + 1] = ce[j];
                            j--;
                        }

                        cs[j + 1] = kv;
                        ce[j + 1] = keo;
                    }

                    // Push in reverse so the smallest-connector child is popped (visited) first.
                    for (uint8_t i = cc; i > 0; i--) stack[sp++] = cs[i - 1];
                }
            }
    };
}  // namespace Lightnet
