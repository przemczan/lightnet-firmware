#include "DiscoveryCoordinator.hpp"
#include "../Common/ProtocolMeta.hpp"

namespace Lightnet {
    DiscoveryCoordinator::DiscoveryCoordinator(IEdgeLink &trunkLink, DiscoveryTreeBuilder *treeBuilder)
        : trunkLink(trunkLink), treeBuilder(treeBuilder), nextPanelIndex(1), frontierPanelIndex(0),
        stackDepth(0), complete(false)
    {
    }

    void DiscoveryCoordinator::begin()
    {
        Protocol::PacketInitializationPull pull =
            Protocol::makePacket<Protocol::PacketInitializationPull>(Protocol::PACKET_INITIALIZATION_PULL);

        pull.panelIndex      = this->nextPanelIndex;
        pull.parentEdgeIndex = TRUNK_EDGE;

        this->trunkLink.sendOnEdge(TRUNK_EDGE, Protocol::packetMeta(pull), sizeof(pull));
    }

    void DiscoveryCoordinator::onFrameArrived(const Protocol::PacketMeta *frame, uint8_t size)
    {
        (void)size;

        switch (frame->header.type) {
            case Protocol::PACKET_REGISTER_EDGE:
                this->handleRegisterEdgeReply((const Protocol::PacketRegisterEdge *)frame);
                break;

            case Protocol::PACKET_DISCOVERY_DONE:
                this->handleDiscoveryDone();
                break;

            default:
                break;  // not discovery traffic
        }
    }

    bool DiscoveryCoordinator::isComplete() const
    {
        return this->complete;
    }

    void DiscoveryCoordinator::handleRegisterEdgeReply(const Protocol::PacketRegisterEdge *reply)
    {
        if (reply->panelIndex == Protocol::DISCOVERY_REJECTED_INDEX) {
            return;  // rejections are resolved locally by the probing panel, never forwarded here
        }

        if (this->treeBuilder) {
            if (this->frontierPanelIndex == 0) {
                this->treeBuilder->addRoot(reply->panelIndex);
            } else {
                this->treeBuilder->addLink(
                    this->frontierPanelIndex,
                    reply->parentEdgeIndex,
                    reply->panelIndex,
                    reply->edgeIndex
                );
            }
        }

        this->stack[this->stackDepth++] = this->frontierPanelIndex;
        this->frontierPanelIndex         = reply->panelIndex;
        this->nextPanelIndex++;

        this->sendAdvance(this->frontierPanelIndex);
    }

    void DiscoveryCoordinator::handleDiscoveryDone()
    {
        if (this->stackDepth == 0) {
            this->complete = true;  // shouldn't happen (the root always pushes the 0 sentinel first)

            return;
        }

        uint16_t resumeTarget = this->stack[--this->stackDepth];

        if (resumeTarget == 0) {
            this->complete = true;  // popped back to the controller sentinel -- whole tree resolved

            return;
        }

        this->frontierPanelIndex = resumeTarget;
        this->sendAdvance(resumeTarget);
    }

    void DiscoveryCoordinator::sendAdvance(uint16_t target)
    {
        Protocol::PacketDiscoveryAdvance advance =
            Protocol::makePacket<Protocol::PacketDiscoveryAdvance>(Protocol::PACKET_DISCOVERY_ADVANCE, target);

        advance.assignIndex = this->nextPanelIndex;

        this->trunkLink.sendOnEdge(TRUNK_EDGE, Protocol::packetMeta(advance), sizeof(advance));
    }
}  // namespace Lightnet
