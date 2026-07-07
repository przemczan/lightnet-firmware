#include "PanelDiscovery.hpp"

namespace Lightnet {
    PanelDiscovery::PanelDiscovery(uint8_t edgeCount)
        : edgeCount_(edgeCount), hasParentFlag(false), parent(0)
    {
        for (uint8_t edge = 0; edge < edgeCount && edge < MAX_EDGES; edge++) {
            this->edges[edge] = EdgeLinkState::Unexplored;
        }
    }

    uint8_t PanelDiscovery::edgeCount() const
    {
        return this->edgeCount_;
    }

    bool PanelDiscovery::hasParent() const
    {
        return this->hasParentFlag;
    }

    uint8_t PanelDiscovery::parentEdge() const
    {
        return this->parent;
    }

    EdgeLinkState PanelDiscovery::edgeState(uint8_t edgeIndex) const
    {
        return this->edges[edgeIndex];
    }

    bool PanelDiscovery::isConnected(uint8_t edgeIndex) const
    {
        return this->edges[edgeIndex] == EdgeLinkState::Connected;
    }

    bool PanelDiscovery::onParentOffer(uint8_t fromEdge)
    {
        if (!this->hasParentFlag) {
            this->hasParentFlag = true;
            this->parent = fromEdge;
            this->edges[fromEdge] = EdgeLinkState::Connected;

            return true;
        }

        if (fromEdge == this->parent) {
            return true;  // idempotent re-offer on the already-established parent edge
        }

        this->edges[fromEdge] = EdgeLinkState::NotConnected;  // loop: reject

        return false;
    }

    void PanelDiscovery::onChildProbeAccepted(uint8_t edgeIndex)
    {
        this->edges[edgeIndex] = EdgeLinkState::Connected;
    }

    void PanelDiscovery::onChildProbeFailed(uint8_t edgeIndex)
    {
        this->edges[edgeIndex] = EdgeLinkState::NotConnected;
    }
}  // namespace Lightnet
