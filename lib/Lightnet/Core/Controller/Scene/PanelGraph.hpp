#pragma once

// ============================================================================
// PanelGraph — the raw, root-independent adjacency of the discovered panel
// tree (slot↔panel-index mapping + CSR adjacency with per-side connector
// indices).
//
// TopologyIndex and PanelGeometry both derive a *rooted* view from this same
// underlying graph (rooted at logicalRoot and at the lowest panel index,
// respectively) — so the raw graph itself is identical between them and is
// built once here and borrowed by both, instead of being recomputed and
// stored twice. See docs/architecture.md and docs/animations/scene-authoring.md §2.
//
// Pure C++ (no Arduino), so it is unit-testable natively.
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

    class PanelGraph
    {
        public:
            PanelGraph() : n(0)
            {
            }

            // Build the raw graph.
            //   panelIndices : the n distinct 1-based panel indices (slot s ↔ panelIndices[s]).
            //   links        : the n-1 tree links, each provided once.
            // Returns false on malformed input (empty / over capacity); count() is 0 in that case.
            bool build(
            const uint8_t * panelIndices,
            uint8_t         panelCount,
            const TopoLink *links,
            uint8_t         linkCount
            )
            {
                n = 0;

                if (panelCount == 0 || panelCount > MAXN) return false;

                n = panelCount;
                memset(slotByIndex, 0xFF, sizeof(slotByIndex));

                for (uint8_t s = 0; s < n; s++) {
                    idx[s]                       = panelIndices[s];
                    slotByIndex[panelIndices[s]] = s;
                }

                buildAdjacency(links, linkCount);

                return true;
            }

            uint8_t count()               const
            {
                return n;
            }

            uint8_t panelAt(uint8_t slot) const
            {
                return idx[slot];
            }

            bool slotOf(uint8_t panelIndex, uint8_t& slot) const
            {
                uint8_t s = slotByIndex[panelIndex];

                if (s == 0xFF) return false;

                slot = s;

                return true;
            }

            uint8_t degree(uint8_t slot) const
            {
                return deg[slot];
            }

            // i-th neighbour of `slot`, i in [0, degree(slot)).
            uint8_t neighborSlot(uint8_t slot, uint8_t i)     const
            {
                return adjSlot[adjStart[slot] + i];
            }

            // `slot`'s own connector index facing that neighbour.
            uint8_t neighborMyEdge(uint8_t slot, uint8_t i)   const
            {
                return adjMyEdge[adjStart[slot] + i];
            }

            // The neighbour's connector index facing `slot`.
            uint8_t neighborPeerEdge(uint8_t slot, uint8_t i) const
            {
                return adjPeerEdge[adjStart[slot] + i];
            }

            // Slot of the lowest panel index — the visualizer's anchor convention
            // (PanelGeometry roots there regardless of the chosen logical root).
            uint8_t lowestSlot() const
            {
                uint8_t best = 0;

                for (uint8_t s = 1; s < n; s++) if (idx[s] < idx[best]) best = s;

                return best;
            }

        private:
            static const uint8_t MAXN = LIGHTNET_MAX_PANELS;

            uint8_t n;
            uint8_t idx[MAXN];            // slot → 1-based panel index
            uint8_t slotByIndex[256];     // 1-based panel index → slot (0xFF = none)

            // Adjacency in CSR form: neighbours of slot s are adjSlot[adjStart[s] .. +deg[s]),
            // carrying the connector index on *both* sides of each edge.
            uint8_t deg[MAXN];
            uint8_t adjStart[MAXN];
            uint8_t adjSlot[2 * MAXN];
            uint8_t adjMyEdge[2 * MAXN];   // connector index on the owning slot
            uint8_t adjPeerEdge[2 * MAXN]; // connector index on the neighbour

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

                    adjSlot[cursor[a]]     = b;
                    adjMyEdge[cursor[a]]   = links[l].edgeA;
                    adjPeerEdge[cursor[a]] = links[l].edgeB;
                    cursor[a]++;

                    adjSlot[cursor[b]]     = a;
                    adjMyEdge[cursor[b]]   = links[l].edgeB;
                    adjPeerEdge[cursor[b]] = links[l].edgeA;
                    cursor[b]++;
                }
            }
    };
}  // namespace Lightnet
