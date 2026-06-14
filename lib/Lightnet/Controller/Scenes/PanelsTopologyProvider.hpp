#pragma once

// PanelsTopologyProvider — the device-side ITopologyProvider. Reads the live discovered
// panel tree from PanelsInitializer and flattens it into the indices/edgeCounts/links
// arrays SceneTopology rebuilds from. This is the controller half of the topology seam;
// the shared scene engine only sees the abstract ITopologyProvider. (Body is the former
// SceneTopology::rebuild() graph walk, verbatim.)

#include <stdint.h>
#include "../../Core/Controller/Scene/SceneTopology.hpp"               // ITopologyProvider, TopoLink
#include "../Panels/PanelsInitializer.hpp" // PanelsInitializer, Panel, Edge, List
#include "../../Core/Common/LightnetConfig.hpp"

namespace Lightnet {
    class PanelsTopologyProvider : public ITopologyProvider {
        public:
            explicit PanelsTopologyProvider(PanelsInitializer& _initializer)
                : initializer(_initializer)
            {
            }

            uint8_t fillTopology(
                uint8_t *  indices,
                uint8_t *  edgeCounts,
                TopoLink * links,
                uint8_t    maxLinks,
                uint8_t &  linkCount
            ) const override
            {
                linkCount = 0;

                List<Panel *> *panels = initializer.getPanels();
                uint16_t total = panels ? panels->getSize() : 0;

                if (total == 0 || total > LIGHTNET_MAX_PANELS) return 0;

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

                        if (seen || linkCount >= maxLinks) continue;

                        links[linkCount].panelA = a;
                        links[linkCount].edgeA  = edge->index;
                        links[linkCount].panelB = b;
                        links[linkCount].edgeB  = edge->connectedEdge->index;
                        linkCount++;
                    }
                }

                return (uint8_t)total;
            }

        private:
            PanelsInitializer& initializer;
    };
}  // namespace Lightnet
