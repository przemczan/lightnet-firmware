#pragma once

// ============================================================================
// SceneTopology — the topology/targeting half of scene playback.
//
// Owns the discovered panel tree's views (root-independent PanelGraph + the
// rooted TopologyIndex and the planar PanelGeometry) and resolves a layer's
// PanelSelector against them. Split out of ScenePlayer purely for separation
// of concerns — the playback state machine stays focused on timing/state while
// this owns topology/targeting. See docs/animations/scene-authoring.md §2, §6, §8.
//
// Pure: it pulls the current panel tree through an injected ITopologyProvider
// (controller wraps PanelsInitializer; mobile supplies a cached/virtual tree) and is
// otherwise a thin composition over the natively-tested PanelGraph / TopologyIndex /
// PanelGeometry / PanelSelector. Single-threaded use only.
// ============================================================================

#include <stdint.h>
#include "../Common/LightnetConfig.hpp"
#include "PanelGraph.hpp"
#include "TopologyIndex.hpp"
#include "PanelGeometry.hpp"
#include "PanelSelector.hpp"

namespace Lightnet {
    // Source of the current panel tree, decoupled from discovery. The controller impl
    // reads the live PanelsInitializer; the mobile impl yields a cached or user-authored
    // (virtual) tree. Co-located with SceneTopology (both move to Core/Scene later) since
    // it references TopoLink. Buffers are caller-owned, sized LIGHTNET_MAX_PANELS.
    class ITopologyProvider
    {
        public:
            virtual ~ITopologyProvider()
            {
            }

            // Fill indices[]/edgeCounts[] (per panel) and links[] (up to maxLinks), set
            // linkCount, and return the panel count (0 → no/invalid tree).
            virtual uint8_t fillTopology(
                uint8_t * indices,
                uint8_t * edgeCounts,
                TopoLink *links,
                uint8_t   maxLinks,
                uint8_t & linkCount
            ) const = 0;
    };

    class SceneTopology
    {
        public:
            explicit SceneTopology(const ITopologyProvider& _provider)
                : provider(_provider), logicalRoot_(1)
            {
            }

            // Logical root panel the rooted view is built from (§4.1). 0 → physical root (1).
            // Stores only; call rebuild() to apply.
            void setLogicalRoot(uint8_t panelIndex)
            {
                logicalRoot_ = panelIndex ? panelIndex : 1;
            }

            uint8_t logicalRoot() const
            {
                return logicalRoot_;
            }

            // Rebuild all views from the current panel tree (via the provider), rooted at
            // logicalRoot_. Reflects the live tree on every play/resume/re-root.
            void rebuild()
            {
                uint8_t indices[LIGHTNET_MAX_PANELS];
                uint8_t edgeCounts[LIGHTNET_MAX_PANELS];  // polygon side count per panel (for geometry)
                TopoLink links[LIGHTNET_MAX_PANELS];
                uint8_t linkCount = 0;

                uint8_t total = provider.fillTopology(indices, edgeCounts, links, LIGHTNET_MAX_PANELS, linkCount);

                if (total == 0 || total > LIGHTNET_MAX_PANELS) {
                    graph.build(nullptr, 0, nullptr, 0); // empty / unusable graph
                    topo.build(graph, logicalRoot_);
                    geometry.build(graph, nullptr, 0);

                    return;
                }

                graph.build(indices, total, links, linkCount);
                topo.build(graph, logicalRoot_);

                // Geometric directionality borrows the same graph but is anchored at the lowest
                // panel index (0 → lowest), matching the mobile visualizer frame regardless of
                // logicalRoot.
                geometry.build(graph, edgeCounts, 0);
            }

            // Resolve a layer selector into up to maxLen 1-based panel indices (slot/discovery
            // order). A malformed selector resolves to nothing → count == 0.
            void resolvePanels(const PanelSelector& target, uint8_t *out, uint8_t maxLen, uint8_t& count) const
            {
                count = 0;

                PanelSet set;

                if (!resolveSelector(target, topo, set)) return;

                emitPanelIndices(set, topo, out, maxLen, count);
            }

            // Read-only views for the runner directionality math (computeDistanceField /
            // computeGeometricField / computeGeometricCenterField in fireStep).
            const TopologyIndex& index() const
            {
                return topo;
            }

            const PanelGeometry& geom() const
            {
                return geometry;
            }

            uint8_t maxDepth() const
            {
                return topo.maxDepth();
            }

        private:
            const ITopologyProvider& provider;

            // Root-independent raw adjacency, built once per rebuild() and borrowed by both
            // rooted views below (see PanelGraph.hpp).
            PanelGraph graph;
            // Rooted view of the tree; layer selectors and the hop-distance field resolve
            // against this. See docs/animations/scene-authoring.md §2, §6, §8.
            TopologyIndex topo;
            // Planar layout of the same tree, anchored at the lowest panel index (visualizer
            // frame), used by geometric runner directionality. Independent of logicalRoot_.
            PanelGeometry geometry;

            uint8_t logicalRoot_;             // panel index the rooted view is built from (§4.1; default 1)
    };
}  // namespace Lightnet
