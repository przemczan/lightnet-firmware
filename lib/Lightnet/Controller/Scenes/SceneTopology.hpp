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
// Not pure (rebuild() reads the live PanelsInitializer), but its core is a thin
// composition over the natively-tested PanelGraph / TopologyIndex /
// PanelGeometry / PanelSelector. Single-threaded use only.
// ============================================================================

#include <stdint.h>
#include "../../Core/Anim/LightnetConfig.hpp"
#include "../Panels/PanelsInitializer.hpp"
#include "../Topology/PanelGraph.hpp"
#include "../Topology/TopologyIndex.hpp"
#include "../Topology/PanelGeometry.hpp"
#include "../Topology/PanelSelector.hpp"
#include "../Topology/TagResolver.hpp"

namespace Lightnet {
    class SceneTopology
    {
        public:
            explicit SceneTopology(PanelsInitializer& _initializer)
                : initializer(_initializer), logicalRoot_(1), tagResolver(nullptr)
            {
            }

            // Device tag map used to resolve `tag:` selectors. Null = tags resolve empty.
            void setTagResolver(const ITagResolver *r)
            {
                tagResolver = r;
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

            // Rebuild all views from the live discovered graph, rooted at logicalRoot_.
            // Reflects the current panel tree on every play/resume/re-root.
            void rebuild()
            {
                List<Panel *> *panels = initializer.getPanels();
                uint16_t total = panels ? panels->getSize() : 0;

                if (total == 0 || total > LIGHTNET_MAX_PANELS) {
                    graph.build(nullptr, 0, nullptr, 0); // empty / unusable graph
                    topo.build(graph, logicalRoot_);
                    geometry.build(graph, nullptr, 0);

                    return;
                }

                uint8_t indices[LIGHTNET_MAX_PANELS];
                uint8_t edgeCounts[LIGHTNET_MAX_PANELS];  // polygon side count per panel (for geometry)
                TopoLink links[LIGHTNET_MAX_PANELS];
                uint8_t linkCount = 0;

                for (uint16_t i = 0; i < total; i++) {
                    Panel *panel = panels->get(i);

                    indices[i] = (uint8_t)panel->index;

                    List<Edge *> *edges = panel->edges;
                    uint16_t ec    = edges ? edges->getSize() : 0;

                    edgeCounts[i] = (uint8_t)ec;

                    for (uint16_t e = 0; e < ec; e++) {
                        Edge *edge = edges->get(e);

                        if (!edge || !edge->connectedEdge || !edge->connectedEdge->panel) continue;

                        uint8_t a = (uint8_t)panel->index;
                        uint8_t b = (uint8_t)edge->connectedEdge->panel->index;

                        // connectedEdge is normally set on the parent side only, so each link is
                        // seen once — but dedupe defensively in case both sides are ever linked.
                        bool seen = false;

                        for (uint8_t k = 0; k < linkCount; k++) {
                            if ((links[k].panelA == a && links[k].panelB == b) ||
                                (links[k].panelA == b && links[k].panelB == a)) {
                                seen = true;
                                break;
                            }
                        }

                        if (seen || linkCount >= LIGHTNET_MAX_PANELS) continue;

                        links[linkCount].panelA = a;
                        links[linkCount].edgeA  = edge->index;
                        links[linkCount].panelB = b;
                        links[linkCount].edgeB  = edge->connectedEdge->index;
                        linkCount++;
                    }
                }

                graph.build(indices, (uint8_t)total, links, linkCount);
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

                if (!resolveSelector(target, topo, set, tagResolver)) return;

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
            PanelsInitializer& initializer;

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
            const ITagResolver *tagResolver;  // device tag map for `tag:` selectors (null until wired)
    };
}  // namespace Lightnet
