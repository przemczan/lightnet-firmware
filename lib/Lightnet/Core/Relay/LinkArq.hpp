#pragma once

// LinkArq — the pure pieces of the relay's link-ARQ layer (hop-level acknowledgment).
//
// Why this layer exists: per-hop frame loss on the relay compounds geometrically with tree
// depth — at measured per-channel loss rates (see docs/architecture.md's relay section) a
// 30-panel chain delivers almost nothing end-to-end, and no end-to-end retry count survives
// that scaling. A hop acknowledgment (PACKET_LINK_ACK) with a couple of link-local
// retransmissions converts the multiplicative loss into a small per-link residual.
//
// Protocol (both sides of every hop, controller trunk included):
//   - After transmitting a frame whose type Protocol::isLinkAckedType() returns true for, the
//     sender listens on the same edge for a PACKET_LINK_ACK echoing the frame's full-frame
//     CRC-16, for LINK_ACK_TIMEOUT_MS; on timeout it retransmits, up to LINK_RETRANSMITS times,
//     then gives up (end-to-end retries recover the residual).
//   - A receiver completing a valid acked-type frame ALWAYS sends the LINK_ACK back on the
//     ingress edge first, then checks LinkDedup: a frame whose (edge, frameCrc) was already
//     seen within LINK_DEDUP_WINDOW_MS is a hop retransmission whose previous ack was lost —
//     it is re-acked but NOT dispatched again (no double relay, no double local action).
//   - The single-active-flow invariant guarantees the ack window is quiet: the only frame that
//     can legitimately arrive while a sender waits is the ack itself (the earliest end-to-end
//     reply is one full dispatch behind it). If the ack is lost AND the peer's own reply beats
//     the retransmission, the retransmission can collide with it half-duplex-style — both die,
//     the dedup window absorbs the retransmission on the peer, and the end-to-end retry
//     recovers. Rare (requires an ack loss first) and accepted.
//
// The device glue (mux parking, wake masking, the actual blocking wait) lives in
// Panel/ArqEdgeLink and Controller/Relay/ControllerRelayPacketSink; everything here is pure
// and natively tested (test/test_link_arq).

#include <stdint.h>
#include "../Common/ProtocolTypes.hpp"

namespace Lightnet {
    // How long a sender listens for the hop ack before retransmitting. Budget: the receiver's
    // main-loop turnaround (completed frame -> ack transmission starts, worst case one full
    // tick() with an animation frame, ~0.5 ms) plus the ack frame itself (2 preamble + 9 bytes,
    // 0.44 ms at 250 kbaud). Millisecond clock granularity makes the effective wait
    // (LINK_ACK_TIMEOUT_MS - 1)..LINK_ACK_TIMEOUT_MS.
    const uint32_t LINK_ACK_TIMEOUT_MS = 4;

    // Link-local retransmissions after the initial send. Two bring a q-lossy hop's residual to
    // q^3 (8% -> 0.05%) — enough that a 30-hop chain's compounded loss stays low single digits.
    const uint8_t LINK_RETRANSMITS = 2;

    // How long a receiver remembers a frame's (edge, frameCrc) for duplicate suppression.
    // Wire frames carry no nonce or sequence field, so two frames with identical bytes are
    // indistinguishable from a hop retransmission by content alone -- and legitimate
    // identical-by-construction repeats exist (a re-polled FETCH_STATE, a repeated command, an
    // end-to-end retry of the same chunk). The window therefore hugs the sender's actual
    // retransmission span as tightly as possible: the last retransmission leaves at
    // LINK_RETRANSMITS x LINK_ACK_TIMEOUT_MS = 8 ms after the original, plus one max frame
    // time in flight. Anything byte-identical arriving later than this is treated as a new,
    // deliberate frame and dispatched -- the cost of a rare real duplicate slipping through
    // (a stray extra reply upstream) is far lower than eating a legitimate frame.
    const uint32_t LINK_DEDUP_WINDOW_MS = 12;

    // LinkAckMatcher — a micro-framer that recognizes exactly one packet type (PACKET_LINK_ACK)
    // in a raw byte stream. Used inside a sender's ack window instead of the full PacketFramer:
    // it needs only a sizeof(PacketLinkAck) buffer, and — unlike a general framer — it must
    // treat every non-ack byte as noise to skip, never accumulate a foreign frame (per the
    // single-active-flow argument above, anything else in the window is a mangled ack, our own
    // echo tail, or a reply that is already lost to this window either way).
    class LinkAckMatcher
    {
        public:
            LinkAckMatcher();

            // Feed one received byte. Returns true exactly when a complete PACKET_LINK_ACK with
            // a valid header CRC just finished; *outFrameCrc then holds its echoed frameCrc.
            bool pushByte(uint8_t value, uint16_t *outFrameCrc);

            void reset();

        private:
            uint8_t buffer[sizeof(Protocol::PacketLinkAck)];
            uint8_t filled;
    };

    // LinkDedup — per-edge (frameCrc, timestamp) memory implementing the receiver's duplicate
    // window. One slot per edge suffices: hops are half-duplex and single-flow, so at most one
    // frame per edge is ever inside its retransmission span at a time.
    template <uint8_t EdgeCount>
    class LinkDedup
    {
        public:
            LinkDedup()
            {
                for (uint8_t i = 0; i < EdgeCount; i++) {
                    this->entries[i].armed = false;
                }
            }

            // True if (edge, frameCrc) repeats within LINK_DEDUP_WINDOW_MS — the caller must
            // re-ack but not re-dispatch. Always (re)notes the sighting, so an ongoing
            // retransmission burst keeps extending its own window.
            bool checkAndNote(uint8_t edge, uint16_t frameCrc, uint32_t nowMs)
            {
                if (edge >= EdgeCount) {
                    return false;
                }

                Entry &entry = this->entries[edge];

                bool duplicate = entry.armed
                                 && (entry.frameCrc == frameCrc)
                                 && ((uint32_t)(nowMs - entry.seenAtMs) < LINK_DEDUP_WINDOW_MS);

                entry.armed    = true;
                entry.frameCrc = frameCrc;
                entry.seenAtMs = nowMs;

                return duplicate;
            }

        private:
            struct Entry {
                uint16_t frameCrc;
                uint32_t seenAtMs;
                bool     armed;
            };

            Entry entries[EdgeCount];
    };
}  // namespace Lightnet
