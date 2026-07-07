#pragma once

// IEdgeLink — the outbound seam for the panel relay network.
//
// PanelRouter computes which edges a frame should be repeated out to and hands each one
// to a sink instead of touching a transport directly. The real panel firmware implements
// this over the shared-USART/mux hardware (see lib/Lightnet/Panel); native tests implement
// it as an in-memory fabric. Pure/Arduino-free so relay logic stays host-testable.

#include <stdint.h>
#include "../Common/ProtocolTypes.hpp"  // Protocol::PacketMeta

namespace Lightnet {
    class IEdgeLink
    {
        public:
            virtual ~IEdgeLink()
            {
            }

            // Repeat one already-framed packet out `edgeIndex`.
            virtual void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) = 0;
    };
}  // namespace Lightnet
