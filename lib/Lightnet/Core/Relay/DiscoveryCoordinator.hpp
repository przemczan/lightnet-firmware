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

            // How often to resend the root probe while nothing has replied yet. The very first
            // pull can easily lose the race against a panel's own boot time (power-on reset +
            // USART init), and it's a broadcast onto a currently-idle trunk, so resending costs
            // nothing but another 0xFF-prefixed frame -- see DiscoveryCoordinator.cpp's tick().
            static const uint32_t ROOT_RETRY_INTERVAL_MS = 500;

            // `treeBuilder` is optional (nullptr = don't accumulate a topology, e.g. tests that
            // only care about the DFS sequencing) — see DiscoveryTreeBuilder.hpp.
            explicit DiscoveryCoordinator(IEdgeLink &trunkLink, DiscoveryTreeBuilder *treeBuilder = nullptr);

            // Sends the very first probe (assigning index 1) directly on the trunk edge. `nowMs`
            // seeds the root-registration timeout; callers that don't care about the timeout
            // (most existing tests) can omit it.
            void begin(uint32_t nowMs = 0);

            // Feed every frame that arrives on the trunk edge here.
            void onFrameArrived(const Protocol::PacketMeta *frame, uint8_t size);

            // Resends the root probe every ROOT_RETRY_INTERVAL_MS until it registers, and gives
            // up with an empty tree after ROOT_TIMEOUT_MS. Call regularly regardless of whether a
            // frame arrived; a no-op once the root has registered or the tree is complete.
            void tick(uint32_t nowMs);

            // True once the whole tree has been walked (the root's own subtree reports done and
            // the resume stack is empty) — or the root-registration timeout elapsed with no
            // panel ever replying, in which case the tree is complete but empty.
            bool isComplete() const;

        private:
            IEdgeLink &trunkLink;
            DiscoveryTreeBuilder *treeBuilder;
            uint16_t nextPanelIndex;
            uint16_t frontierPanelIndex;    // 0 = the controller itself (sentinel, before the root registers)
            uint16_t stack[Lightnet::LIGHTNET_MAX_PANELS];
            uint8_t stackDepth;
            bool complete;
            uint32_t beginMs;
            uint32_t lastRootPullMs;

            void sendRootPull(uint32_t nowMs);
            void handleRegisterEdgeReply(const Protocol::PacketRegisterEdge *reply);
            void handleDiscoveryDone();
            void sendAdvance(uint16_t target);
    };
}  // namespace Lightnet
