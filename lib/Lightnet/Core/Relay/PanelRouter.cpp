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
            this->routeDownstream(fromEdge, packet, size);

            return;
        }

        // Upstream: route toward the parent only, never flood back down to other children.
        if (this->topology.isConnected(this->topology.parentEdge())) {
            this->link.sendOnEdge(this->topology.parentEdge(), packet, size);
        }
    }

    // Broadcasts (target 0) repeat out every connected child edge. An addressed frame goes to
    // exactly one branch: discovery assigns indices in pre-order DFS (see PanelDiscovery::
    // childIndex()), so each child's subtree is the contiguous index range starting at the
    // child's own index — the target therefore lives behind the child with the largest recorded
    // index <= target. Routing only engages when every connected child edge has a recorded
    // index; any child without one (discovery incomplete or a pre-walk transient) falls back to
    // the flood so no reachable panel is ever silently cut off.
    void PanelRouter::routeDownstream(uint8_t fromEdge, const Protocol::PacketMeta *packet, uint8_t size)
    {
        uint16_t target = packet->header.targetPanelIndex;

        if (target != 0) {
            uint8_t bestEdge         = NO_EDGE;
            uint16_t bestChildIndex  = 0;
            bool allChildrenIndexed  = true;

            for (uint8_t edge = 0; edge < this->topology.edgeCount(); edge++) {
                if (edge == fromEdge || !this->topology.isConnected(edge)) {
                    continue;
                }

                uint16_t childIndex = this->topology.childIndex(edge);

                if (childIndex == 0) {
                    allChildrenIndexed = false;
                } else if (childIndex <= target && childIndex > bestChildIndex) {
                    bestChildIndex = childIndex;
                    bestEdge       = edge;
                }
            }

            if (allChildrenIndexed) {
                if (bestEdge != NO_EDGE) {
                    this->link.sendOnEdge(bestEdge, packet, size);
                }

                return;  // no matching branch: the target is not in this subtree -- drop
            }
        }

        for (uint8_t edge = 0; edge < this->topology.edgeCount(); edge++) {
            if (edge != fromEdge && this->topology.isConnected(edge)) {
                this->link.sendOnEdge(edge, packet, size);
            }
        }
    }
}  // namespace Lightnet
