#include "PanelDiscoveryDriver.hpp"
#include "../Common/ProtocolMeta.hpp"
#include "../../Utils/Debug.hpp"

namespace Lightnet {
    PanelDiscoveryDriver::PanelDiscoveryDriver(PanelDiscovery &discovery, IEdgeLink &link)
        : discovery(discovery), link(link), assignedIndex(0), pendingAssignIndex(0),
        probingEdge(NO_EDGE), probeStartedMs(0)
    {
    }

    void PanelDiscoveryDriver::onFrameArrived(
        uint8_t                     fromEdge,
        const Protocol::PacketMeta *frame,
        uint8_t                     size,
        uint32_t                    nowMs
    )
    {
        (void)size;

        switch (frame->header.type) {
            case Protocol::PACKET_INITIALIZATION_PULL:
                this->handleInitializationPull(fromEdge, (const Protocol::PacketInitializationPull *)frame);
                break;

            case Protocol::PACKET_REGISTER_EDGE:

                if (fromEdge == this->probingEdge) {
                    this->handleRegisterEdgeReply(fromEdge, (const Protocol::PacketRegisterEdge *)frame, nowMs);
                }

                break;

            case Protocol::PACKET_DISCOVERY_ADVANCE:
                this->handleAdvance((const Protocol::PacketDiscoveryAdvance *)frame, nowMs);
                break;

            default:
                break;  // not discovery traffic
        }
    }

    uint16_t PanelDiscoveryDriver::assignedPanelIndex() const
    {
        return this->assignedIndex;
    }

    void PanelDiscoveryDriver::tick(uint32_t nowMs)
    {
        if (this->probingEdge == NO_EDGE) {
            return;
        }

        if (nowMs - this->probeStartedMs < PROBE_TIMEOUT_MS) {
            return;
        }

        uint8_t timedOutEdge = this->probingEdge;

        this->probingEdge = NO_EDGE;
        this->discovery.onChildProbeFailed(timedOutEdge);
        this->tryNextEdge(nowMs);

        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] probe timeout edge"), timedOutEdge));
    }

    void PanelDiscoveryDriver::handleInitializationPull(
        uint8_t                                   fromEdge,
        const Protocol::PacketInitializationPull *pull
    )
    {
        bool accepted = this->discovery.onParentOffer(fromEdge);

        Protocol::PacketRegisterEdge reply =
            Protocol::makePacket<Protocol::PacketRegisterEdge>(Protocol::PACKET_REGISTER_EDGE);

        reply.edgeIndex       = fromEdge;
        reply.parentEdgeIndex = pull->parentEdgeIndex;

        if (accepted) {
            if (this->assignedIndex == 0) {
                this->assignedIndex = pull->panelIndex;  // first-ever offer -- adopt the assigned index
            }

            reply.panelIndex = this->assignedIndex;
        } else {
            reply.panelIndex = Protocol::DISCOVERY_REJECTED_INDEX;  // loop-closing edge
        }

        this->link.sendOnEdge(fromEdge, Protocol::packetMeta(reply), sizeof(reply));

        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] pull edge"), fromEdge, DPF("-> idx"), reply.panelIndex));
    }

    void PanelDiscoveryDriver::handleRegisterEdgeReply(
        uint8_t                             fromEdge,
        const Protocol::PacketRegisterEdge *reply,
        uint32_t                            nowMs
    )
    {
        this->probingEdge = NO_EDGE;

        if (reply->panelIndex != Protocol::DISCOVERY_REJECTED_INDEX) {
            this->discovery.onChildProbeAccepted(fromEdge);

            DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] child accepted edge"), fromEdge));

            // Stop here -- do NOT relay the reply upstream ourselves. PanelRouter's upstream
            // rule ("arrived on any non-parent edge -> route to parent") doesn't check whether
            // that edge was Connected before this frame arrived, so as long as the caller also
            // runs this same frame through this panel's PanelRouter (see the class comment),
            // it carries the reply the rest of the way to the controller with no special-casing
            // needed here. The controller descends into the new child before telling us to try
            // our next edge, which is what makes the walk depth-first.
            return;
        }

        this->discovery.onChildProbeFailed(fromEdge);
        this->tryNextEdge(nowMs);

        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] child rejected edge"), fromEdge));
    }

    void PanelDiscoveryDriver::handleAdvance(const Protocol::PacketDiscoveryAdvance *advance, uint32_t nowMs)
    {
        if (advance->meta.header.targetPanelIndex != this->assignedIndex) {
            return;  // not addressed to us -- PanelRouter has already relayed it downstream
        }

        this->pendingAssignIndex = advance->assignIndex;
        this->tryNextEdge(nowMs);

        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] advance assign idx"), advance->assignIndex));
    }

    void PanelDiscoveryDriver::tryNextEdge(uint32_t nowMs)
    {
        uint8_t edgeCount = this->discovery.edgeCount();

        for (uint8_t edge = 0; edge < edgeCount; edge++) {
            bool isParentEdge = edge == this->discovery.parentEdge();

            if (isParentEdge || this->discovery.edgeState(edge) != EdgeLinkState::Unexplored) {
                continue;
            }

            Protocol::PacketInitializationPull pull =
                Protocol::makePacket<Protocol::PacketInitializationPull>(Protocol::PACKET_INITIALIZATION_PULL);

            pull.panelIndex       = this->pendingAssignIndex;
            pull.parentEdgeIndex  = edge;

            this->probingEdge   = edge;
            this->probeStartedMs = nowMs;
            this->link.sendOnEdge(edge, Protocol::packetMeta(pull), sizeof(pull));

            DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] probing edge"), edge));

            return;
        }

        // No Unexplored edges remain -- this panel's whole subtree is resolved.
        Protocol::PacketDiscoveryDone done =
            Protocol::makePacket<Protocol::PacketDiscoveryDone>(Protocol::PACKET_DISCOVERY_DONE);

        done.panelIndex = this->assignedIndex;

        this->link.sendOnEdge(this->discovery.parentEdge(), Protocol::packetMeta(done), sizeof(done));

        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] subtree done idx"), this->assignedIndex));
    }
}  // namespace Lightnet
