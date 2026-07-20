#pragma once

// DiscoveryTreeBuilder — accumulates the topology discovered by DiscoveryCoordinator into the
// same TopoLink[]/indices[]/edgeCounts[] shape PanelGraph::build() consumes.
// DiscoveryCoordinator only tracks protocol-sequencing state (the DFS frontier/resume
// stack) — it doesn't know the parent's own edge index for a link, only the parent panel's
// *index* (frontierPanelIndex). This class is fed one (parentIndex, parentEdge, childIndex,
// childEdge) tuple per discovered panel and turns it into a flat link list — the same
// representation Sim/PanelsInitializerSim.cpp already builds directly, so the real device-side
// adapter can reuse its Panel/Edge conversion unchanged (see Controller/Panels/PanelsInitializer.cpp).
//
// The root panel (the one wired directly to the controller's own trunk edge) has no panel-side
// peer to link against — PanelGraph models panel-to-panel adjacency only, so the controller-to-root
// link is intentionally omitted, exactly like Sim's spanning tree has N-1 links for N panels.
//
// Pure logic, no Arduino — host-testable.

#include <stdint.h>
#include "../Common/LightnetConfig.hpp"
#include "../Controller/PanelGraph.hpp"  // TopoLink

namespace Lightnet {
    class DiscoveryTreeBuilder
    {
        public:
            static const uint8_t MAX_PANELS = LIGHTNET_MAX_PANELS;

            explicit DiscoveryTreeBuilder(uint8_t edgeCountPerPanel);

            void reset();

            // The very first panel discovered — wired directly to the controller's trunk, so
            // there is no PanelGraph link to record for it.
            void addRoot(uint16_t panelIndex);

            // A non-root panel was discovered as a child of `parentIndex`, linked via
            // `parentEdge` on the parent's side and `childEdge` on its own side.
            void addLink(uint16_t parentIndex, uint16_t parentEdge, uint16_t childIndex, uint16_t childEdge);

            uint8_t         panelCount() const;
            const uint8_t  *indices() const;      // panelCount() entries, 1-based panel indices
            const uint8_t  *edgeCounts() const;    // panelCount() entries
            const TopoLink *links() const;
            uint8_t         linkCount() const;

        private:
            uint8_t edgeCountPerPanel;
            uint8_t indices_[MAX_PANELS];
            uint8_t edgeCounts_[MAX_PANELS];
            uint8_t panelCount_;
            TopoLink links_[MAX_PANELS];  // at most panelCount() - 1 links
            uint8_t linkCount_;

            void recordPanel(uint16_t panelIndex);
    };
}  // namespace Lightnet
