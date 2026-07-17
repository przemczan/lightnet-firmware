#include "DiscoveryCoordinator.hpp"
#include "../Common/ProtocolMeta.hpp"
#include "../../Utils/Debug.hpp"

namespace Lightnet {
    DiscoveryCoordinator::DiscoveryCoordinator(IEdgeLink &trunkLink, DiscoveryTreeBuilder *treeBuilder)
        : trunkLink(trunkLink), treeBuilder(treeBuilder), nextPanelIndex(1), frontierPanelIndex(0),
        stackDepth(0), complete(false), walkStalledFlag(false), beginMs(0), lastRootPullMs(0),
        lastProgressMs(0), lastAdvanceMs(0)
    {
    }

    void DiscoveryCoordinator::begin(uint32_t nowMs)
    {
        this->beginMs         = nowMs;
        this->lastProgressMs  = nowMs;
        this->walkStalledFlag = false;

        this->sendRootPull(nowMs);
    }

    void DiscoveryCoordinator::sendRootPull(uint32_t nowMs)
    {
        this->lastRootPullMs = nowMs;

        Protocol::PacketInitializationPull pull =
            Protocol::makePacket<Protocol::PacketInitializationPull>(Protocol::PACKET_INITIALIZATION_PULL);

        pull.panelIndex      = this->nextPanelIndex;
        pull.parentEdgeIndex = TRUNK_EDGE;

        this->trunkLink.sendOnEdge(TRUNK_EDGE, Protocol::packetMeta(pull), sizeof(pull));

        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(DPF("[DISC] root pull sent, assigning idx"), pull.panelIndex));
    }

    void DiscoveryCoordinator::onFrameArrived(const Protocol::PacketMeta *frame, uint8_t size, uint32_t nowMs)
    {
        (void)size;

        switch (frame->header.type) {
            case Protocol::PACKET_REGISTER_EDGE:
                this->handleRegisterEdgeReply((const Protocol::PacketRegisterEdge *)frame, nowMs);
                break;

            case Protocol::PACKET_DISCOVERY_DONE:
                this->handleDiscoveryDone((const Protocol::PacketDiscoveryDone *)frame, nowMs);
                break;

            default:
                break;  // not discovery traffic
        }
    }

    void DiscoveryCoordinator::tick(uint32_t nowMs)
    {
        if (this->complete) {
            return;
        }

        if (this->frontierPanelIndex == 0) {
            if ((nowMs - this->beginMs) >= ROOT_TIMEOUT_MS) {
                this->complete = true;  // no panel ever replied to the root probe -- empty tree

                return;
            }

            if ((nowMs - this->lastRootPullMs) >= ROOT_RETRY_INTERVAL_MS) {
                this->sendRootPull(nowMs);  // still no reply -- the first pull may have lost the race
            }

            return;
        }

        if ((nowMs - this->lastProgressMs) >= WALK_STALL_TIMEOUT_MS) {
            this->complete        = true;
            this->walkStalledFlag = true;

            return;
        }

        if ((nowMs - this->lastAdvanceMs) >= ADVANCE_RETRY_INTERVAL_MS) {
            this->sendAdvance(this->frontierPanelIndex, nowMs);  // ADVANCE, its reply, or DONE may have been lost
        }
    }

    bool DiscoveryCoordinator::isComplete() const
    {
        return this->complete;
    }

    bool DiscoveryCoordinator::walkStalled() const
    {
        return this->walkStalledFlag;
    }

    void DiscoveryCoordinator::handleRegisterEdgeReply(const Protocol::PacketRegisterEdge *reply, uint32_t nowMs)
    {
        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(
                     DPF("[DISC] register-edge reply panelIndex"),
                     reply->panelIndex,
                     DPF("edge"),
                     reply->edgeIndex,
                     DPF("parentEdge"),
                     reply->parentEdgeIndex
        ));

        if (reply->panelIndex == Protocol::DISCOVERY_REJECTED_INDEX) {
            return;  // rejections are resolved locally by the probing panel, never forwarded here
        }

        // Deduplicate: a genuinely new registration always carries the index handed out with the
        // newest PULL, which is exactly nextPanelIndex (consumed below on acceptance). A probed
        // panel answers every PULL it hears, so a probe retry crossing a slow first reply (e.g.
        // the child was mid-way through a blocking debug print) produces the same reply twice --
        // and the second copy, arriving after this index was consumed, would push the frontier
        // and descend a second time, corrupting the walk. Filtering on the index also gates
        // lastProgressMs, so a stream of duplicates can't hold off WALK_STALL_TIMEOUT_MS.
        if (reply->panelIndex != this->nextPanelIndex) {
            return;
        }

        // Capacity cap: never accept an index past LIGHTNET_MAX_PANELS -- the resume stack
        // below and DiscoveryTreeBuilder's arrays are sized for exactly that many panels, so
        // descending further would overflow them. Ignoring the registration stalls the walk,
        // which then completes with the capped partial tree via WALK_STALL_TIMEOUT_MS. The
        // stackDepth guard is belt-and-suspenders for the same bound.
        if (this->nextPanelIndex > LIGHTNET_MAX_PANELS
            || this->stackDepth >= LIGHTNET_MAX_PANELS) {
            return;
        }

        this->lastProgressMs = nowMs;

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

        this->sendAdvance(this->frontierPanelIndex, nowMs);
    }

    void DiscoveryCoordinator::handleDiscoveryDone(const Protocol::PacketDiscoveryDone *done, uint32_t nowMs)
    {
        // Deduplicate: only the current frontier can legitimately report done (it is the one
        // panel holding an ADVANCE). A duplicate DONE -- the same report relayed twice when an
        // ADVANCE retry crosses the first copy -- would pop the resume stack a second time and
        // skip an ancestor's remaining edges entirely.
        if (done->panelIndex != this->frontierPanelIndex) {
            return;
        }

        this->lastProgressMs = nowMs;

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
        this->sendAdvance(resumeTarget, nowMs);
    }

    void DiscoveryCoordinator::sendAdvance(uint16_t target, uint32_t nowMs)
    {
        Protocol::PacketDiscoveryAdvance advance =
            Protocol::makePacket<Protocol::PacketDiscoveryAdvance>(Protocol::PACKET_DISCOVERY_ADVANCE, target);

        advance.assignIndex = this->nextPanelIndex;

        this->trunkLink.sendOnEdge(TRUNK_EDGE, Protocol::packetMeta(advance), sizeof(advance));
        this->lastAdvanceMs = nowMs;
    }
}  // namespace Lightnet
