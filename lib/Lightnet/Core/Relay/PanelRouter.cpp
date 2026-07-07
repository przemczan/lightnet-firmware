#include "PanelRouter.hpp"

namespace Lightnet {
    PanelRouter::PanelRouter(const PanelDiscovery &topology, IEdgeLink &link)
        : topology(topology), link(link)
    {
    }

    void PanelRouter::onFrameArrived(uint8_t fromEdge, const Protocol::PacketMeta *packet, uint8_t size)
    {
        if (!this->topology.hasParent()) {
            return;  // not yet discovered; nothing to route through
        }

        if (fromEdge == this->topology.parentEdge()) {
            // Downstream flood: repeat out every other connected edge.
            for (uint8_t edge = 0; edge < this->topology.edgeCount(); edge++) {
                if (edge != fromEdge && this->topology.isConnected(edge)) {
                    this->link.sendOnEdge(edge, packet, size);
                }
            }

            return;
        }

        // Upstream: route toward the parent only, never flood back down to other children.
        if (this->topology.isConnected(this->topology.parentEdge())) {
            this->link.sendOnEdge(this->topology.parentEdge(), packet, size);
        }
    }
}  // namespace Lightnet
