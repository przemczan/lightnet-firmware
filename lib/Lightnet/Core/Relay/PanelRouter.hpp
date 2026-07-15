#pragma once

// PanelRouter — the forwarding decision for one relay panel.
//
// Store-and-forward, not cut-through: this is only ever called with a frame the caller has
// already fully received and CRC-validated. Never repeat a frame back out the edge it
// arrived on; a frame arriving on any non-parent edge routes upstream to the parent only.
// A frame arriving on the parent edge goes downstream: broadcasts flood every other
// connected edge, while an addressed frame routes to the single branch that contains the
// target (see routeDownstream() — pre-order DFS index assignment makes each child's index
// the lower bound of its subtree's contiguous range). Branch routing matters beyond wire
// economy: a parent mid-flood is deaf (EdgeUartTransport masks RX and the wake interrupts
// for the whole of each send), so a sibling flood would swallow the fast reply an addressed
// frame solicits — deterministically, when the target sits on the first-flooded edge.
//
// Reads topology from PanelDiscovery — it owns no connection state of its own, so there is
// exactly one source of truth for "what edges exist and are connected."
//
// Pure decision logic sitting behind IEdgeLink — no transport, no I/O beyond that seam.
// See docs: hardware redesign plan §1/§7.

#include <stdint.h>
#include "IEdgeLink.hpp"
#include "PanelDiscovery.hpp"

namespace Lightnet {
    class PanelRouter
    {
        public:
            static const uint8_t NO_EDGE = 0xFF;

            PanelRouter(const PanelDiscovery &topology, IEdgeLink &link);

            // A validated frame arrived on `fromEdge` — repeat it to whichever edges the
            // routing rule selects.
            void onFrameArrived(uint8_t fromEdge, const Protocol::PacketMeta *packet, uint8_t size);

        private:
            const PanelDiscovery &topology;
            IEdgeLink &link;

            void routeDownstream(uint8_t fromEdge, const Protocol::PacketMeta *packet, uint8_t size);
    };
}  // namespace Lightnet
