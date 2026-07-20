#pragma once

// PanelDiscovery — what this panel knows about its own edges, and the loop-rejection rule.
//
// A panel adopts exactly one edge as its parent (the edge a discovery offer is first
// accepted on) and never again — a second offer arriving on a different edge means the
// wiring closes a loop back to an already-discovered panel, and is rejected rather than
// accepted as a new parent. Rejected and genuinely-empty edges collapse to the same
// NotConnected state, since both mean "never use this edge for anything." This is the
// only defense against a flood looping forever: packets carry no hop-count/TTL/visited
// list, so termination depends entirely on the discovered topology being cycle-free.
//
// Pure state machine — no I/O, no transport.

#include <stdint.h>

namespace Lightnet {
    enum class EdgeLinkState : uint8_t {
        Unexplored = 0,
        Connected,
        NotConnected,
    };

    class PanelDiscovery
    {
        public:
            static constexpr uint8_t MAX_EDGES = 6;  // headroom beyond today's 3-5 edge panels

            explicit PanelDiscovery(uint8_t edgeCount);

            uint8_t edgeCount() const;
            bool hasParent() const;
            uint8_t parentEdge() const;  // valid only when hasParent()
            EdgeLinkState edgeState(uint8_t edgeIndex) const;
            bool isConnected(uint8_t edgeIndex) const;

            // The panel index of the child discovered behind `edgeIndex`, or 0 if none is
            // recorded (indices are assigned starting at 1; the parent edge always reads 0).
            // Discovery assigns indices in pre-order DFS (see DiscoveryCoordinator's stack
            // walk), so each child's own index is also the lower bound of its whole subtree's
            // contiguous index range — which is all PanelRouter needs to route an addressed
            // frame to the right branch.
            uint16_t childIndex(uint8_t edgeIndex) const;

            // A neighbour on `fromEdge` is offering to become this panel's parent.
            // Returns true if accepted (first-ever offer, or an idempotent re-offer on the
            // already-established parent edge); false if rejected as a loop (this panel
            // already has a *different* parent edge) — `fromEdge` is marked NotConnected.
            bool onParentOffer(uint8_t fromEdge);

            // This panel probed `edgeIndex` looking for a child, and a genuinely new,
            // unregistered device accepted — record the edge as a confirmed child link and
            // remember the child's assigned panel index (from its own PacketRegisterEdge
            // reply) for PanelRouter's branch routing.
            void onChildProbeAccepted(uint8_t edgeIndex, uint16_t childPanelIndex);

            // This panel probed `edgeIndex` and either got a "already registered elsewhere"
            // rejection (loop) or no response at all (nothing wired) — both leave the edge
            // unusable, so they're treated identically.
            void onChildProbeFailed(uint8_t edgeIndex);

        private:
            uint8_t edgeCount_;
            bool hasParentFlag;
            uint8_t parent;
            EdgeLinkState edges[MAX_EDGES];
            uint16_t childIndexes[MAX_EDGES];
    };
}  // namespace Lightnet
