#pragma once

// IPacketSink — the outbound-packet seam for the shared scene engine.
//
// AnimationScheduler builds fully-stamped wire packets and hands them to a sink instead of
// touching a transport directly. The controller has two impls, picked at compile time
// (src/controller/main.cpp): ControllerRelayPacketSink on real hardware, ControllerPacketSink
// (ack-retry + inter-packet pacing, wraps LNBus) under SIM_MODE. The mobile/preview impl forwards
// the raw bytes to the per-panel players (ack/pacing are no-ops). This is what lets the scene
// engine run host-side and on mobile with no Arduino dependency.

#include <stdint.h>
#include "../Common/ProtocolTypes.hpp"  // Protocol::PacketMeta, packetType_t

namespace Lightnet {
    class IPacketSink
    {
        public:
            virtual ~IPacketSink()
            {
            }

            // Send one fully-built packet to `address`. Use makePacket()/makeMeta() so the
            // header is stamped; the sink reads header.type from the buffer. address 0 = general
            // call (all panels). `wantAck` requests an acknowledged transfer on a real bus;
            // sinks without a bus ignore it.
            virtual void send(
                uint8_t                     address,
                const Protocol::PacketMeta *packet,
                uint8_t                     size,
                bool                        wantAck
            ) = 0;

            // Bus settle delay between packets (lets panels process before the next send).
            // No-op off-device; the controller impl maps it to delayMicroseconds().
            virtual void pace(uint16_t microseconds)
            {
                (void)microseconds;
            }
    };
}  // namespace Lightnet
