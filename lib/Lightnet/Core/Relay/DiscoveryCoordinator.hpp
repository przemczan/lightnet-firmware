#pragma once

// DiscoveryCoordinator — the controller's half of the relay discovery protocol.
//
// Keeps exactly one panel "active" at a time (the depth-first walk's current frontier) and
// drives it forward one edge at a time via PACKET_DISCOVERY_ADVANCE,
// which is flooded downstream like any ordinary packet (no PanelRouter changes needed — only
// the addressed panel acts on it) and address-filtered by the target's own panel index. A
// successful registration (PacketRegisterEdge reaching the controller) pushes the current
// frontier onto a resume stack and descends into the new child; PACKET_DISCOVERY_DONE pops the
// stack to resume an ancestor that may still have unexplored edges of its own, and an empty
// stack after a DONE means the whole tree is resolved. All this bookkeeping lives here, on the
// controller, which has RAM to spare — panels only ever track their own local edge states (see
// PanelDiscovery), never anything about the wider tree.
//
// The ADVANCE just sent to the current frontier is resent every ADVANCE_RETRY_INTERVAL_MS until
// something moves the walk forward (a REGISTER_EDGE or DISCOVERY_DONE bumps lastProgressMs) --
// covers the ADVANCE itself, or the frontier's own reply, getting lost on the wire. The
// frontier's PanelDiscoveryDriver ignores a duplicate ADVANCE that arrives while one of its own
// probes is still outstanding (see PanelDiscoveryDriver::handleAdvance()), so a resend can never
// derail an in-progress probe, only recover from a genuine loss.
//
// Pure logic, no Arduino — built and tested against a mock/sim IEdgeLink, since the real
// controller-side UART trunk transport doesn't exist yet.

#include <stdint.h>
#include "IEdgeLink.hpp"
#include "DiscoveryTreeBuilder.hpp"
#include "../Common/LightnetConfig.hpp"

namespace Lightnet {
    class DiscoveryCoordinator
    {
        public:
            // The controller has exactly one physical trunk edge.
            static const uint8_t TRUNK_EDGE = 0;

            // How long to wait for a reply to the very first probe before giving up on there
            // being any panel at all and completing with an empty tree.
            static const uint32_t ROOT_TIMEOUT_MS = 3000;

            // After the root has registered, how long to wait without any further
            // REGISTER_EDGE or DISCOVERY_DONE before giving up and booting with the partial
            // tree discovered so far. Override via DISCOVERY_WALK_STALL_TIMEOUT_MS in
            // controller.config.hpp (see controller.config.hpp.example).
            #ifndef DISCOVERY_WALK_STALL_TIMEOUT_MS
                static const uint32_t WALK_STALL_TIMEOUT_MS = 5000;

            #else
                static const uint32_t WALK_STALL_TIMEOUT_MS = DISCOVERY_WALK_STALL_TIMEOUT_MS;

            #endif

            // How often to resend the root probe while nothing has replied yet. The very first
            // pull can easily lose the race against a panel's own boot time (power-on reset +
            // USART init), and it's a broadcast onto a currently-idle trunk, so resending costs
            // nothing but another 0xFF-prefixed frame -- see DiscoveryCoordinator.cpp's tick().
            static const uint32_t ROOT_RETRY_INTERVAL_MS = 500;

            // How often to resend PACKET_DISCOVERY_ADVANCE to the current frontier while no
            // REGISTER_EDGE/DISCOVERY_DONE has moved the walk forward. Covers the ADVANCE itself
            // getting lost, and also the frontier's reply/DONE getting lost -- either way, the
            // frontier's own PanelDiscoveryDriver treats a re-ADVANCE while nothing is
            // outstanding as a fresh instruction (a duplicate arriving mid-probe is ignored
            // instead, see PanelDiscoveryDriver::handleAdvance()), so a resend is always safe to
            // send and only ever a no-op past what already happened.
            static const uint32_t ADVANCE_RETRY_INTERVAL_MS = 500;

            // `treeBuilder` is optional (nullptr = don't accumulate a topology, e.g. tests that
            // only care about the DFS sequencing) — see DiscoveryTreeBuilder.hpp.
            explicit DiscoveryCoordinator(IEdgeLink &trunkLink, DiscoveryTreeBuilder *treeBuilder = nullptr);

            // Sends the very first probe (assigning index 1) directly on the trunk edge. `nowMs`
            // seeds the root-registration timeout; callers that don't care about the timeout
            // (most existing tests) can omit it.
            void begin(uint32_t nowMs = 0);

            // Feed every frame that arrives on the trunk edge here. `nowMs` is used to track
            // walk-progress for WALK_STALL_TIMEOUT_MS; callers may pass 0 in tests that do not
            // exercise the stall timeout.
            void onFrameArrived(const Protocol::PacketMeta *frame, uint8_t size, uint32_t nowMs = 0);

            // Resends the root probe every ROOT_RETRY_INTERVAL_MS until it registers, gives
            // up with an empty tree after ROOT_TIMEOUT_MS, and abandons an in-progress walk
            // after WALK_STALL_TIMEOUT_MS with no REGISTER_EDGE/DISCOVERY_DONE progress. Call
            // regularly regardless of whether a frame arrived.
            void tick(uint32_t nowMs);

            // True once the whole tree has been walked (the root's own subtree reports done and
            // the resume stack is empty), the root-registration timeout elapsed with no panel
            // ever replying (empty tree), or the walk stall timeout fired (partial tree).
            bool isComplete() const;

            // True when isComplete() because WALK_STALL_TIMEOUT_MS elapsed mid-walk — the
            // accumulated tree is partial but still usable.
            bool walkStalled() const;

        private:
            IEdgeLink &trunkLink;
            DiscoveryTreeBuilder *treeBuilder;
            uint16_t nextPanelIndex;
            uint16_t frontierPanelIndex;    // 0 = the controller itself (sentinel, before the root registers)
            uint16_t stack[Lightnet::LIGHTNET_MAX_PANELS];
            uint8_t stackDepth;
            bool complete;
            bool walkStalledFlag;
            uint32_t beginMs;
            uint32_t lastRootPullMs;
            uint32_t lastProgressMs;
            uint32_t lastAdvanceMs;

            void sendRootPull(uint32_t nowMs);
            void handleRegisterEdgeReply(const Protocol::PacketRegisterEdge *reply, uint32_t nowMs);
            void handleDiscoveryDone(uint32_t nowMs);
            void sendAdvance(uint16_t target, uint32_t nowMs);
    };
}  // namespace Lightnet
