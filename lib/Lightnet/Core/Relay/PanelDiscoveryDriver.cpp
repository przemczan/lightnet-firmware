#include "PanelDiscoveryDriver.hpp"
#include "../Common/ProtocolMeta.hpp"
#include "../../Utils/Debug.hpp"

namespace Lightnet {
    PanelDiscoveryDriver::PanelDiscoveryDriver(PanelDiscovery &discovery, IEdgeLink &link)
        : discovery(discovery), link(link), assignedIndex(0), pendingAssignIndex(0),
        probingEdgeIndex(NO_EDGE), probeAttempts(0), probeStartedMs(0), deferredLogCount(0)
    {
    }

    void PanelDiscoveryDriver::deferLog(DeferredDiscLog code, uint16_t a, uint16_t b)
    {
        if (this->deferredLogCount >= DEFERRED_LOG_CAP) {
            // Queue full -- evict the oldest entry instead of dropping the new one. A busy edge
            // (e.g. exhausting a dead-end edge, which alone can cost PROBE_ATTEMPTS worth of
            // Probe+ProbeTimeout pairs) can fill this before the flush loop ever gets a quiet
            // window to drain any of it; dropping new entries meant every capture went silent
            // right at the interesting part instead of showing what happened most recently.
            for (uint8_t i = 1; i < DEFERRED_LOG_CAP; i++) {
                this->deferredLogs[i - 1] = this->deferredLogs[i];
            }

            this->deferredLogCount--;
        }

        this->deferredLogs[this->deferredLogCount].code = code;
        this->deferredLogs[this->deferredLogCount].a    = a;
        this->deferredLogs[this->deferredLogCount].b    = b;
        this->deferredLogCount++;
    }

    void PanelDiscoveryDriver::printDeferredLog(const DeferredLogEntry &entry)
    {
        switch (entry.code) {
            case DeferredDiscLog::Pull:
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(
                             DPF("[DISC] pull edge"),
                             (uint8_t)entry.a,
                             DPF("-> idx"),
                             entry.b
                ));
                break;

            case DeferredDiscLog::AdvAccept:
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] advance assign idx"), entry.a));
                break;

            case DeferredDiscLog::Probe:
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] probing edge"), (uint8_t)entry.a));
                break;

            case DeferredDiscLog::DoneSend:
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] subtree done idx"), entry.a));
                break;

            case DeferredDiscLog::ChildAccepted:
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] child accepted edge"), (uint8_t)entry.a));
                break;

            case DeferredDiscLog::ChildRejected:
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] child rejected edge"), (uint8_t)entry.a));
                break;

            case DeferredDiscLog::ProbeTimeout:
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] probe timeout edge"), (uint8_t)entry.a));
                break;

            default:
                break;
        }
    }

    bool PanelDiscoveryDriver::flushOneDeferredLog()
    {
        if (this->deferredLogCount == 0) {
            return false;
        }

        this->printDeferredLog(this->deferredLogs[0]);

        for (uint8_t i = 1; i < this->deferredLogCount; i++) {
            this->deferredLogs[i - 1] = this->deferredLogs[i];
        }

        this->deferredLogCount--;

        return this->deferredLogCount > 0;
    }

    void PanelDiscoveryDriver::flushDeferredLogs()
    {
        while (this->deferredLogCount > 0) {
            this->flushOneDeferredLog();
        }
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

                if (fromEdge == this->probingEdgeIndex) {
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

    bool PanelDiscoveryDriver::isProbing() const
    {
        return this->probingEdgeIndex != NO_EDGE;
    }

    uint8_t PanelDiscoveryDriver::probingEdge() const
    {
        return this->probingEdgeIndex;
    }

    void PanelDiscoveryDriver::tick(uint32_t nowMs)
    {
        if (this->probingEdgeIndex == NO_EDGE) {
            return;
        }

        if (nowMs - this->probeStartedMs < PROBE_TIMEOUT_MS) {
            return;
        }

        uint8_t timedOutEdge = this->probingEdgeIndex;

        this->deferLog(DeferredDiscLog::ProbeTimeout, timedOutEdge);
        this->probeAttempts++;

        if (this->probeAttempts < PROBE_ATTEMPTS) {
            this->sendProbe(timedOutEdge, nowMs);  // retry the same edge -- see class comment

            return;
        }

        this->probingEdgeIndex = NO_EDGE;
        this->discovery.onChildProbeFailed(timedOutEdge);
        this->tryNextEdge(nowMs);
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
        this->deferLog(DeferredDiscLog::Pull, fromEdge, reply.panelIndex);
    }

    void PanelDiscoveryDriver::handleRegisterEdgeReply(
        uint8_t                             fromEdge,
        const Protocol::PacketRegisterEdge *reply,
        uint32_t                            nowMs
    )
    {
        this->probingEdgeIndex = NO_EDGE;

        if (reply->panelIndex != Protocol::DISCOVERY_REJECTED_INDEX) {
            this->discovery.onChildProbeAccepted(fromEdge);
            this->deferLog(DeferredDiscLog::ChildAccepted, fromEdge);

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
        this->deferLog(DeferredDiscLog::ChildRejected, fromEdge);
    }

    void PanelDiscoveryDriver::handleAdvance(const Protocol::PacketDiscoveryAdvance *advance, uint32_t nowMs)
    {
        if (advance->meta.header.targetPanelIndex != this->assignedIndex) {
            return;  // not addressed to us -- PanelRouter has already relayed it downstream
        }

        if (this->isProbing()) {
            // A probe from an earlier ADVANCE is still outstanding -- this is the controller's
            // own retransmit of that same ADVANCE (its own reply/DONE never reached it, see
            // DiscoveryCoordinator), not a new instruction. Restarting the walk here would
            // overwrite probingEdgeIndex mid-probe and orphan the outstanding reply.
            return;
        }

        this->pendingAssignIndex = advance->assignIndex;
        this->deferLog(DeferredDiscLog::AdvAccept, advance->assignIndex);
        this->tryNextEdge(nowMs);
    }

    void PanelDiscoveryDriver::tryNextEdge(uint32_t nowMs)
    {
        uint8_t edgeCount = this->discovery.edgeCount();

        for (uint8_t edge = 0; edge < edgeCount; edge++) {
            bool isParentEdge = edge == this->discovery.parentEdge();

            if (isParentEdge || this->discovery.edgeState(edge) != EdgeLinkState::Unexplored) {
                continue;
            }

            this->probeAttempts = 0;
            this->sendProbe(edge, nowMs);

            return;
        }

        // No Unexplored edges remain -- this panel's whole subtree is resolved.
        Protocol::PacketDiscoveryDone done =
            Protocol::makePacket<Protocol::PacketDiscoveryDone>(Protocol::PACKET_DISCOVERY_DONE);

        done.panelIndex = this->assignedIndex;

        this->link.sendOnEdge(this->discovery.parentEdge(), Protocol::packetMeta(done), sizeof(done));
        this->deferLog(DeferredDiscLog::DoneSend, this->assignedIndex, this->discovery.parentEdge());
    }

    void PanelDiscoveryDriver::sendProbe(uint8_t edge, uint32_t nowMs)
    {
        Protocol::PacketInitializationPull pull =
            Protocol::makePacket<Protocol::PacketInitializationPull>(Protocol::PACKET_INITIALIZATION_PULL);

        pull.panelIndex      = this->pendingAssignIndex;
        pull.parentEdgeIndex = edge;

        this->probingEdgeIndex = edge;
        this->probeStartedMs   = nowMs;
        this->link.sendOnEdge(edge, Protocol::packetMeta(pull), sizeof(pull));
        this->deferLog(DeferredDiscLog::Probe, edge);
    }
}  // namespace Lightnet
