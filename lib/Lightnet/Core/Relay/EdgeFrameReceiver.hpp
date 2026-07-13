#pragma once

// EdgeFrameReceiver — tags a completed frame with the edge it arrived on, given one shared
// USART + mux rather than one receiver per edge (see docs/hardware/schematics/Panel.png and the
// hardware redesign plan §4/§11.2).
//
// PanelRouter and PanelDiscoveryDriver both need to know which edge a frame arrived on
// (`fromEdge`), but there is only one physical RX pin, muxed across a panel's edges one at a
// time. Something has to decide, for each incoming byte, which edge it belongs to — that's this
// class. It owns one PacketFramer and a single "which edge is currently claimed" latch:
//
//   1. A wake transition on edge X (the PCINT lines, PB1/PB2/PB3) calls onEdgeWake(X, now). If no
//      edge is currently claimed, X is latched and the caller must switch the mux to it
//      (selectRxEdge(X)) before real bytes start arriving. A wake on a *different* edge while one
//      is already claimed is ignored — the single-active-flow invariant (plan §3) guarantees
//      nothing legitimate is happening on two edges at once, so this is either noise or a
//      neighbour's own settling transient, not a real second transmission.
//   2. Every byte belonging to the claimed edge feeds the one shared PacketFramer via onByte().
//   3. A complete, CRC-valid frame releases the claim (so the next wake can claim the mux again)
//      and is exposed via frame()/frameSize()/fromEdge() until the next onByte() call.
//   4. tick() recovers from a claim that never produces a complete frame (a stray wake with no
//      real transmission behind it, or a frame that stalls mid-flight) by releasing the claim
//      after FRAME_TIMEOUT_MS of inactivity — mirrors PacketFramer's own note that a real driver
//      needs its own timeout, since PacketFramer has no notion of time.
//
// The sender-side preamble byte the schematic's mux-settling note calls for (one throwaway byte
// before the real frame, absorbing the mux's ~70ns settling time plus this class's wake-to-claim
// reaction latency) needs no special handling here: PacketFramer's own resync already treats an
// unrecognized/mismatched leading byte as noise and skips it, so a garbage preamble byte simply
// never starts a frame.
//
// Pure logic — no Arduino, no hardware register access. EdgeUartTransport (which does own the
// real registers) is the caller: its wake-detection ISR calls onEdgeWake()/selectRxEdge(), its RX
// byte ISR calls onByte(), and its main-loop poll calls tick().

#include <stdint.h>
#include "PacketFramer.hpp"

namespace Lightnet {
    class EdgeFrameReceiver
    {
        public:
            static const uint8_t NO_EDGE = 0xFF;
            // Placeholder -- needs bench validation once boards exist, same as
            // PanelDiscoveryDriver::PROBE_TIMEOUT_MS.
            static const uint32_t FRAME_TIMEOUT_MS = 20;

            // Absolute cap on how long a single claim may be held, regardless of ongoing byte
            // activity. FRAME_TIMEOUT_MS alone only catches a claim that goes quiet -- a claim on
            // a floating/noisy edge (e.g. a PCINT wake mis-latched by wake-line crosstalk, see
            // EdgeUartTransport's own crosstalk note) can keep resetting that gap forever if
            // noise arrives faster than every FRAME_TIMEOUT_MS, permanently starving every other
            // edge's real wake (onEdgeWake() ignores a wake while any claim is held). A real frame
            // (4-byte preamble + up to Protocol::MAX_PACKET_SIZE) finishes in low single-digit ms
            // even at the slowest supported baud, so this is generous headroom, not a tight bound.
            static const uint32_t MAX_CLAIM_MS = 100;

            EdgeFrameReceiver();

            // A wake transition was noticed on `edgeIndex`. Returns true if this edge was newly
            // claimed (the caller must switch the mux to it via selectRxEdge()); false if a
            // different edge already holds the claim (ignored) or this edge already holds it
            // (redundant wake, no-op).
            bool onEdgeWake(uint8_t edgeIndex, uint32_t nowMs);

            // Feed one byte belonging to the currently-claimed edge. Bytes arriving with no
            // active claim are dropped (defensive — shouldn't happen if wake gating is correct).
            // Returns true exactly when a complete, CRC-valid frame is now ready.
            bool onByte(uint8_t value, uint32_t nowMs);

            // Releases a stalled claim (see class comment, point 4) — call every loop iteration.
            void tick(uint32_t nowMs);

            // Valid from the onByte() call that returned true until the next onByte() call.
            const Protocol::PacketMeta *frame() const;
            uint8_t frameSize() const;
            uint8_t fromEdge() const;

            // Diagnostics only -- the edge currently holding the claim, or NO_EDGE if idle.
            // Unlike fromEdge() (which reports the *last completed* frame's edge), this reflects
            // live, possibly-stuck state.
            uint8_t claimedEdge() const;

        private:
            PacketFramer framer;
            uint8_t activeEdge;
            uint8_t completedEdge;
            uint32_t lastActivityMs;
            uint32_t claimStartMs;

            void release();
    };
}  // namespace Lightnet
