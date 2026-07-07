#pragma once

// PanelRouter — the flood/route forwarding decision for one relay panel.
//
// Store-and-forward, not cut-through: this is only ever called with a frame the caller has
// already fully received and CRC-validated. The decision itself is a single rule: never
// repeat a frame back out the edge it arrived on; a frame arriving on the parent edge
// floods to every other connected edge (downstream), a frame arriving on any other edge
// routes upstream to the parent only. Reads topology from PanelDiscovery — it owns no
// connection state of its own, so there is exactly one source of truth for "what edges
// exist and are connected."
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
            PanelRouter(const PanelDiscovery &topology, IEdgeLink &link);

            // A validated frame arrived on `fromEdge` — repeat it to whichever edges the
            // flood/route rule selects.
            void onFrameArrived(uint8_t fromEdge, const Protocol::PacketMeta *packet, uint8_t size);

        private:
            const PanelDiscovery &topology;
            IEdgeLink &link;
    };
}  // namespace Lightnet
