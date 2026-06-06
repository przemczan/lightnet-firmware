#pragma once

// ============================================================================
// TopologyIndex — a rooted, derived view of the discovered panel tree.
//
// Pure C++ (no Arduino), so it is unit-testable natively. It is built from a
// generic list of physical links (panel-connector to panel-connector); a thin
// controller-side adapter feeds it from PanelsInitializer::getPanels().
//
// "Rooted view" means: given a chosen root panel, it computes BFS depth, parent
// pointers, child counts (→ leaf/branch), a deterministic canonical traversal
// order, plus adjacency / subtree / multi-source distance helpers. Re-rooting
// the tree (see docs/design/scene-portability.md §4.1) is simply build() with a
// different root — everything downstream reads this index, never the raw graph.
// ============================================================================

#include <stdint.h>
#include <string.h>
#include "../../Common/LightnetConfig.hpp"

namespace Lightnet {
    // One physical connector-to-connector link between two panels (undirected).
    // Provide each tree link exactly once; endpoint order is irrelevant.
    struct TopoLink {
        uint8_t panelA; // 1-based panel index
        uint8_t edgeA;  // connector (edge) index on panel A
        uint8_t panelB; // 1-based panel index
        uint8_t edgeB;  // connector (edge) index on panel B
    };

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
            TopologyIndex() : n(0), rootSlot(0), maxDepthVal(0)
            {
            }

            // Build the rooted view.
            //   panelIndices : the n distinct 1-based panel indices (slot s ↔ panelIndices[s]).
            //   links        : the n-1 tree links, each provided once.
            //   rootPanelIdx : 1-based panel to root at; if absent, falls back to the
            //                  smallest panel index (the physical root, index 1).
            // Returns false on malformed input (empty / over capacity).
            bool build(
            const uint8_t * panelIndices,
            uint8_t         panelCount,
            const TopoLink *links,
            uint8_t         linkCount,
            uint8_t         rootPanelIdx
            )
            {
                if (panelCount == 0 || panelCount > MAXN) {
                    n = 0;

                    return false;
                }

                n = panelCount;
                memset(slotByIndex, 0xFF, sizeof(slotByIndex));

                for (uint8_t s = 0; s < n; s++) {
                    idx[s]                      = panelIndices[s];
                    slotByIndex[panelIndices[s]] = s;
                }

                buildAdjacency(links, linkCount);
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
                return idx[slot];
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
                return deg[slot];
            }

            uint8_t neighborSlot(uint8_t slot, uint8_t i) const
            {
                return adjSlot[adjStart[slot] + i];
            }

            bool slotOf(uint8_t panelIndex, uint8_t& slot) const
            {
                uint8_t s = slotByIndex[panelIndex];

                if (s == 0xFF) return false;

                slot = s;

                return true;
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

                    for (uint8_t e = adjStart[u]; e < adjStart[u] + deg[u]; e++) {
                        uint8_t v = adjSlot[e];

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

                    for (uint8_t e = adjStart[u]; e < adjStart[u] + deg[u]; e++) {
                        uint8_t v = adjSlot[e];

                        if (dist[v] == 0xFF) {
                            dist[v]     = dist[u] + 1;
                            queue[qt++] = v;
                        }
                    }
                }
            }

        private:
            static const uint8_t MAXN = LIGHTNET_MAX_PANELS;

            uint8_t n;
            uint8_t rootSlot;
            uint8_t maxDepthVal;

            uint8_t idx[MAXN];          // slot → 1-based panel index
            uint8_t slotByIndex[256];   // 1-based panel index → slot (0xFF = none)
            uint8_t depth[MAXN];
            uint8_t parent[MAXN];       // slot → parent slot (0xFF = root)
            uint8_t childCount[MAXN];
            uint8_t canonPos[MAXN];

            // Adjacency in CSR form: neighbours of slot s are adjSlot[adjStart[s] .. +deg[s]).
            uint8_t deg[MAXN];
            uint8_t adjStart[MAXN];
            uint8_t adjSlot[2 * MAXN];
            uint8_t adjMyEdge[2 * MAXN]; // local connector index (for canonical ordering)

            void buildAdjacency(const TopoLink *links, uint8_t linkCount)
            {
                memset(deg, 0, n);

                for (uint8_t l = 0; l < linkCount; l++) {
                    uint8_t a = slotByIndex[links[l].panelA];
                    uint8_t b = slotByIndex[links[l].panelB];

                    if (a == 0xFF || b == 0xFF) continue;

                    deg[a]++;
                    deg[b]++;
                }

                uint16_t acc = 0;

                for (uint8_t s = 0; s < n; s++) {
                    adjStart[s] = (uint8_t)acc;
                    acc        += deg[s];
                }

                uint8_t cursor[MAXN];

                for (uint8_t s = 0; s < n; s++) cursor[s] = adjStart[s];

                for (uint8_t l = 0; l < linkCount; l++) {
                    uint8_t a = slotByIndex[links[l].panelA];
                    uint8_t b = slotByIndex[links[l].panelB];

                    if (a == 0xFF || b == 0xFF) continue;

                    adjSlot[cursor[a]]   = b;
                    adjMyEdge[cursor[a]] = links[l].edgeA;
                    cursor[a]++;

                    adjSlot[cursor[b]]   = a;
                    adjMyEdge[cursor[b]] = links[l].edgeB;
                    cursor[b]++;
                }
            }

            uint8_t pickRoot(uint8_t rootPanelIdx) const
            {
                if (slotByIndex[rootPanelIdx] != 0xFF) return slotByIndex[rootPanelIdx];

                uint8_t best = 0;

                for (uint8_t s = 1; s < n; s++) if (idx[s] < idx[best]) best = s;

                return best;
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

                    for (uint8_t e = adjStart[u]; e < adjStart[u] + deg[u]; e++) {
                        uint8_t v = adjSlot[e];

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

                    for (uint8_t e = adjStart[u]; e < adjStart[u] + deg[u]; e++) {
                        uint8_t v = adjSlot[e];

                        if (parent[v] == u && cc < 8) {
                            cs[cc] = v;
                            ce[cc] = adjMyEdge[e];
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
