#pragma once

// TrunkFrameReceiver — feeds a byte stream through a PacketFramer with an inter-byte idle-gap
// reset, for single-edge (non-muxed) RX paths: the controller's one physical trunk port
// (ControllerDiscoveryService, ControllerRelayPacketSink), which has no per-edge claim/mux to
// reason about (contrast Core/Relay/EdgeFrameReceiver.hpp, the panel's muxed equivalent).
//
// Frames travel as contiguous byte bursts — a store-and-forward relay hop always writes a whole
// frame back-to-back (see EdgeUartTransport::sendOnEdge() / ControllerEdgeTransport::sendOnEdge())
// — so any gap of IDLE_GAP_RESET_MS or more between bytes while a frame is only partially
// accumulated means the leading bytes were noise or a truncated frame, never the front of a real
// one still in flight. Without this reset, a stray leading byte (e.g. line glitches from a
// panel's floating tri-state-buffer OE# pins during its own power-up reset) can make PacketFramer
// latch onto an unrelated byte as a phony type/size and silently swallow every real frame that
// follows until enough bytes happen to realign — self-inflicted packet loss with no signal that
// it's happening. EdgeFrameReceiver already self-heals this way via its own FRAME_TIMEOUT_MS;
// this is the same idea for the controller's un-muxed trunk RX.
//
// Pure logic — no Arduino, no transport.

#include <stdint.h>
#include "PacketFramer.hpp"

namespace Lightnet {
    class TrunkFrameReceiver
    {
        public:
            // How long a gap between bytes is allowed before a partially-accumulated frame is
            // discarded rather than kept waiting for its remaining bytes. Generous relative to
            // any real inter-byte gap (back-to-back UART bytes at the slowest supported trunk
            // baud, 100kbps, are ~80us apart) but tight relative to the shortest real silence
            // between frames in this protocol.
            static const uint32_t IDLE_GAP_RESET_MS = 10;

            explicit TrunkFrameReceiver(bool validateProtocolVersion = true);

            // Feed one incoming byte. Returns true exactly when a complete, CRC-valid frame is
            // now buffered and ready to read via frame()/frameSize().
            bool onByte(uint8_t value, uint32_t nowMs);

            const Protocol::PacketMeta *frame() const;
            uint8_t frameSize() const;

            // Discards any accumulated bytes and resumes scanning for a type byte.
            void reset();

        private:
            PacketFramer framer;
            uint32_t lastByteMs;
    };
}  // namespace Lightnet
