#pragma once

// DiscoveryCoordinator — the controller's half of the relay discovery protocol.
//
// Today's I2C model gets away with a single fixed-address poll because electrical bus-segment
// switching always makes exactly the right panel answer at that address (see
// PanelDiscoveryDriver.hpp for the panel side of the story). The relay transport has no
// physical segment to gate — every downstream packet is either flooded to every connected edge
// or routed to a parent (PanelRouter), never addressed to one specific panel — so there is no
// way to "poll whoever's currently registering" once discovery is more than one hop deep.
//
// This class is the fix: it keeps exactly one panel "active" at a time (the depth-first walk's
// current frontier) and drives it forward one edge at a time via PACKET_DISCOVERY_ADVANCE,
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

            // `treeBuilder` is optional (nullptr = don't accumulate a topology, e.g. tests that
            // only care about the DFS sequencing) — see DiscoveryTreeBuilder.hpp.
            explicit DiscoveryCoordinator(IEdgeLink &trunkLink, DiscoveryTreeBuilder *treeBuilder = nullptr);

            // Sends the very first probe (assigning index 1) directly on the trunk edge.
            void begin();

            // Feed every frame that arrives on the trunk edge here.
            void onFrameArrived(const Protocol::PacketMeta *frame, uint8_t size);

            // True once the whole tree has been walked (the root's own subtree reports done and
            // the resume stack is empty).
            bool isComplete() const;

        private:
            IEdgeLink &trunkLink;
            DiscoveryTreeBuilder *treeBuilder;
            uint16_t nextPanelIndex;
            uint16_t frontierPanelIndex;    // 0 = the controller itself (sentinel, before the root registers)
            uint16_t stack[Lightnet::LIGHTNET_MAX_PANELS];
            uint8_t stackDepth;
            bool complete;

            void handleRegisterEdgeReply(const Protocol::PacketRegisterEdge *reply);
            void handleDiscoveryDone();
            void sendAdvance(uint16_t target);
    };
}  // namespace Lightnet
