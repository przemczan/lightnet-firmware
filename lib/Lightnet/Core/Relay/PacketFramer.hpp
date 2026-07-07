#pragma once

// PacketFramer — reconstructs framed Protocol packets from a raw byte stream.
//
// I2C handed receivers packet boundaries for free: each bus transaction carries its own byte
// count from the Wire/TWI layer, independent of packet content. The relay's shared UART has
// no such out-of-band length — PacketMeta carries a header CRC but no length field — so a
// byte-stream receiver has to recover framing itself. This class does that: feed it bytes one
// at a time (or in any grouping) as they arrive off the wire; pushByte() returns true exactly
// when a complete, CRC-valid frame has accumulated, and frame()/frameSize() expose it.
//
// Resync strategy: the first byte of an unstarted frame is always interpreted as a packet
// type and used to look up that type's fixed wire size (Protocol::packetSizeForType()) — an
// unrecognized type value is treated as noise and skipped rather than accumulated. Once a
// frame's full expected length has arrived, its header CRC is checked; a mismatch discards the
// whole accumulated buffer and resumes scanning for a type byte from the very next byte fed
// in. This is deliberately simple — it relies on the single-active-flow invariant (hardware
// redesign plan §3) keeping real desyncs rare, and does not attempt to search for a valid
// frame buried inside discarded bytes.
//
// A completed frame is consumed implicitly: the next pushByte() call starts accumulating a new
// frame from a clean slate, so a caller only needs to read frame()/frameSize() before that
// call (no separate consume()/reset() call is required, though reset() is exposed for a
// caller that wants to abandon an in-progress frame early, e.g. on an inter-byte timeout).
//
// Pure logic — no Arduino, no timing. A caller driving real hardware still needs its own
// inter-byte timeout to force a reset() if a frame stalls mid-flight; this class has no notion
// of time.

#include <stdint.h>
#include "../Common/ProtocolMeta.hpp"

namespace Lightnet {
    class PacketFramer
    {
        public:
            static const uint8_t MAX_FRAME_SIZE = Protocol::MAX_PACKET_SIZE;

            // validateProtocolVersion=false is for the relay OTA bootloader only (see
            // lib/Lightnet/Panel/bootloader/) — flashing is how a protocolVersion mismatch gets
            // resolved, so the resident bootloader must accept a frame regardless of which
            // protocolVersion the sender stamped into it. Every other caller keeps the default.
            explicit PacketFramer(bool validateProtocolVersion = true);

            // Feed one incoming byte. Returns true exactly when a complete, CRC-valid frame
            // is now buffered and ready to read via frame()/frameSize().
            bool pushByte(uint8_t value);

            const Protocol::PacketMeta *frame() const;
            uint8_t frameSize() const;

            // Discards any accumulated bytes and resumes scanning for a type byte.
            void reset();

        private:
            uint8_t buffer[MAX_FRAME_SIZE] __attribute__((aligned(2)));
            uint8_t filled;
            uint8_t expectedSize;
            bool validateProtocolVersion;
    };
}  // namespace Lightnet
