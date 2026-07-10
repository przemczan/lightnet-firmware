#pragma once

// PanelDiscoveryDriver — the panel's half of the relay discovery protocol (see
// DiscoveryCoordinator.hpp for why the old "poll a fixed address" trick doesn't carry over,
// and the overall shape of the walk).
//
// Sits on top of PanelDiscovery (the pure per-edge decision table -- loop rejection lives
// there, unchanged) and IEdgeLink. Tick-based, matching ScenePlayer::tick(millis())'s existing
// pattern elsewhere in this codebase, since a probe needs a real timeout: a genuinely empty
// port never replies at all.
//
// Three things this class does. PanelRouter itself needs no changes for any of them, but the
// caller (the real firmware's frame-arrival path, or a test fabric) MUST run every frame that
// isn't a PACKET_INITIALIZATION_PULL through this panel's PanelRouter as well as through this
// driver -- see the note on point 3.
//   1. Answers a PACKET_INITIALIZATION_PULL arriving on any edge (fresh acceptance, idempotent
//      re-offer on the existing parent edge, or loop rejection -- all three fall out of
//      PanelDiscovery::onParentOffer()'s existing return value). This reply is always a direct,
//      single-hop send on the same edge -- PULL must never be run through PanelRouter (it has
//      no "is this meant for me" guard, unlike REGISTER_EDGE/ADVANCE below, so relaying it
//      anywhere else would corrupt an unrelated node's already-resolved edge state).
//   2. When addressed by a PACKET_DISCOVERY_ADVANCE, works through its own Unexplored edges one
//      at a time (skipping its parent edge), probing each directly -- this has to bypass the
//      router, since an edge not yet in the topology isn't something any router rule knows how
//      to forward to. Reports PACKET_DISCOVERY_DONE once none remain.
//   3. When a direct probe reply arrives: on acceptance, marks the edge Connected and *stops* --
//      it does NOT relay the reply upstream itself. PanelRouter's upstream rule ("arrived on any
//      non-parent edge -> route to parent") doesn't check whether that edge was Connected before
//      this frame arrived, so simply also running the same frame through this panel's
//      PanelRouter carries it the rest of the way to the controller with no special-casing
//      needed here (a rejected reply harmlessly rides the same path and is dropped once it
//      reaches the controller, which is fine -- rejections are rare). Stopping on acceptance
//      (rather than immediately trying the next edge) is what makes the walk depth-first: the
//      controller descends into the new child first, and only tells this panel to continue
//      once that child's whole subtree is resolved. On rejection or a local timeout, this panel
//      tries its next edge immediately with no controller round-trip.
//
// On the panel build, discovery debug lines are queued via deferLog() and flushed from
// LightnetPanel::flushIdleDebugLogs() -- never from the frame handler itself, since each
// bit-banged print blocks long enough to overflow the relay RX ring (see DebugSerial.hpp).

#include <stdint.h>
#include "IEdgeLink.hpp"
#include "PanelDiscovery.hpp"

namespace Lightnet {
    class PanelDiscoveryDriver
    {
        public:
            static const uint8_t NO_EDGE = 0xFF;
            // Placeholder -- the real value needs bench validation once boards exist (mux
            // settling + PCINT-wake latency + real per-hop time all factor in).
            static const uint32_t PROBE_TIMEOUT_MS = 50;

            PanelDiscoveryDriver(PanelDiscovery &discovery, IEdgeLink &link);

            // Feed every frame that arrives on `fromEdge`.
            void onFrameArrived(uint8_t fromEdge, const Protocol::PacketMeta *frame, uint8_t size, uint32_t nowMs);

            // Drives the local per-edge probe timeout -- call every loop iteration.
            void tick(uint32_t nowMs);

            // This panel's own assigned index -- 0 until a PACKET_INITIALIZATION_PULL has been
            // accepted.
            uint16_t assignedPanelIndex() const;

            bool isProbing() const;
            void flushDeferredLogs();
            bool flushOneDeferredLog();

        private:
            enum class DeferredDiscLog : uint8_t {
                None = 0,
                Pull,
                AdvAccept,
                Probe,
                DoneSend,
                ChildAccepted,
                ChildRejected,
                ProbeTimeout,
            };

            struct DeferredLogEntry {
                DeferredDiscLog code;
                uint16_t        a;
                uint16_t        b;
            };

            static const uint8_t DEFERRED_LOG_CAP = 4;

            PanelDiscovery &discovery;
            IEdgeLink &link;
            uint16_t assignedIndex;              // 0 until this panel itself has been assigned one
            uint16_t pendingAssignIndex;          // index to hand out, from the most recent ADVANCE
            uint8_t probingEdge;                  // NO_EDGE when not currently probing
            uint32_t probeStartedMs;
            DeferredLogEntry deferredLogs[DEFERRED_LOG_CAP];
            uint8_t deferredLogCount;

            void deferLog(DeferredDiscLog code, uint16_t a = 0, uint16_t b = 0);
            void printDeferredLog(const DeferredLogEntry &entry);
            void handleInitializationPull(uint8_t fromEdge, const Protocol::PacketInitializationPull *pull);
            void handleRegisterEdgeReply(uint8_t fromEdge, const Protocol::PacketRegisterEdge *reply, uint32_t nowMs);
            void handleAdvance(const Protocol::PacketDiscoveryAdvance *advance, uint32_t nowMs);
            void tryNextEdge(uint32_t nowMs);
    };
}  // namespace Lightnet
