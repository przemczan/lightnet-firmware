#pragma once

// LightnetPanel — the panel-side entry point for the relay network. Wires together discovery,
// routing, and packet dispatch over a point-to-point relay trunk.
//
// Wires together every pure piece built for this design: PanelDiscovery (per-edge topology
// state) + PanelDiscoveryDriver (the discovery protocol) + PanelRouter (flood/route forwarding)
// + PanelFrameDispatcher (the one "should I act on this locally" decision) + EdgeFrameReceiver
// (RX edge-tagging) sit behind EdgeUartTransport (the real hardware: shared USART + mux).
//
// Threading model (the one real design decision made at this hardware boundary, not previously
// covered by any pure-logic piece): EdgeFrameReceiver was built and tested as synchronous,
// single-context logic, but three different execution contexts want to touch this panel's state
// (the PCINT wake ISR, the USART RX ISR, and the main loop). Rather than make EdgeFrameReceiver
// itself thread-safe, every EdgeFrameReceiver/PanelDiscoveryDriver/PanelRouter/PanelFrameDispatcher
// call happens from ONE place — the main loop (tick(), via pollWake()/pollBytes()) — and both
// ISRs only ever do the minimal, race-free handoff a real ISR is safe to do:
//   - The USART RX ISR pushes each raw byte into EdgeUartTransport's existing ByteRing
//     (onRxByte()) — already real-time-safe, built for exactly this.
//   - The PCINT wake ISR discards a wake that lands while this panel is transmitting (our own
//     drive can couple onto a neighbouring edge's separate wake-sense line — see
//     onEdgeWakeIsr()'s own comment) and otherwise just latches which edge woke into a single
//     volatile byte; the actual claim decision (EdgeFrameReceiver::onEdgeWake()) and the mux
//     switch (EdgeUartTransport::selectRxEdge()) both happen from pollWake() in the main loop
//     instead of from the ISR, so they're never racing tick()'s other calls into the same
//     objects. A second wake arriving before pollWake() drains the first overwrites it — an
//     accepted simplification given the single-active-flow invariant (hardware redesign plan
//     §3): only one edge should legitimately be waking at a time in correctly-functioning
//     hardware.
// Whether main-loop-driven mux switching reacts fast enough relative to the sender's preamble
// byte is genuinely a bench-spike question (see the plan's step 1), not something resolved here.
//
// UNVALIDATED HARDWARE — no bench spike has run. This class builds and links against real
// register-level code, but nothing in this design (here or on the now-also-cut-over controller
// side) has been exercised on real silicon yet — no boards exist.
//
// PACKET_ENTER_BOOTLOADER/PACKET_RESET_DEVICE, and the discovery control plane
// (PACKET_INITIALIZATION_PULL/PACKET_REGISTER_EDGE/PACKET_DISCOVERY_ADVANCE/PACKET_DISCOVERY_DONE
// — see PanelDiscoveryDriver.hpp), reach a panel regardless of protocolVersion —
// Protocol::isVersionExemptType() (ProtocolMeta.hpp) is consulted by validatePacket() itself, so
// EdgeFrameReceiver's framer surfaces these types as complete frames even when
// header.protocolVersion doesn't match this build's. This is what lets a panel still be
// discovered (and so still addressable for ENTER_BOOTLOADER) after a controller reboot re-runs
// discovery at a newer version than this panel is currently running — without it, a
// version-mismatched panel would look identical to an empty, unwired port and drop out of the
// tree with no way back in. Every other packet type still needs a matching version to be
// trusted.

#include <stdint.h>
#include "../Core/Relay/PanelDiscovery.hpp"
#include "../Core/Relay/PanelDiscoveryDriver.hpp"
#include "../Core/Relay/PanelRouter.hpp"
#include "../Core/Relay/PanelFrameDispatcher.hpp"
#include "../Core/Relay/EdgeFrameReceiver.hpp"
#include "../Core/Panel/AnimationPlayer.hpp"
#include "EdgeUartTransport.hpp"
#include "RGBController.hpp"

class LightnetPanel
{
    public:
        static const uint8_t NO_EDGE = 0xFF;

        LightnetPanel();

        // Configures the transport (baud) and constructs the RGB driver. Call once at startup,
        // after Lightnet::clockInit()/sei().
        void begin();

        // Drives everything: wake/byte polling, discovery timeouts, animation playback, and
        // mirroring the current colour to the LED. Call every main loop iteration.
        void tick(uint32_t nowMs);

        // ISR entry point (PCINT wake line transition) — minimal, see class comment.
        void onEdgeWakeIsr(uint8_t edgeIndex);

    private:
        Lightnet::PanelDiscovery discovery;
        Lightnet::PanelDiscoveryDriver driver;
        Lightnet::PanelRouter router;
        Lightnet::PanelFrameDispatcher dispatcher;
        Lightnet::EdgeFrameReceiver receiver;
        Lightnet::AnimationPlayer animPlayer;
        RGBController rgbController;

        // Bitmask, one bit per edge (bit N = edge N woke since the last pollWake() drain) --
        // NOT a single "last edge wins" latch. A single-value latch loses a genuine wake on one
        // edge whenever a later ISR firing for a *different* edge overwrites it before pollWake()
        // reads it, which real hardware has shown happens: a panel's own relay transmission on
        // one edge leaks crosstalk onto another edge's wake-sense line right as its
        // isTransmitting()-masked window ends (see onEdgeWakeIsr()'s own note), producing a burst
        // of unmasked wakes for that other edge that can repeatedly clobber a real, pending wake
        // for the edge actually carrying traffic. A mask survives that: every edge that woke
        // stays set until explicitly drained, so a real wake gets a chance on every pollWake()
        // call for as long as its own transmission keeps re-triggering it, not just the one
        // instant a single-value latch would have captured.
        volatile uint8_t pendingWakeMask;
        uint32_t lastRelayActivityMs;
        uint32_t probingSinceMs;

        // Mirror of whether the PCINT wake interrupts are currently gated off (see
        // syncWakeInterruptSuppression()) -- avoids a register read-modify-write per call when
        // the state hasn't changed. Main-loop only, so not volatile.
        bool wakeInterruptsSuppressed;

        // With probe retries (PanelDiscoveryDriver::PROBE_ATTEMPTS), a downstream panel
        // resolving two empty edges can legitimately go quiet for up to
        // 2 * PROBE_ATTEMPTS * PanelDiscoveryDriver::PROBE_TIMEOUT_MS = 120ms mid-walk -- this
        // must stay above that so an upstream relay panel never starts a bit-banged debug flush
        // (tens of ms, blocking) inside a gap that's really still part of the walk.
        static const uint32_t RELAY_QUIET_MS = 200;

        // driver.isProbing() should self-resolve within PROBE_ATTEMPTS * PROBE_TIMEOUT_MS
        // (~150ms). If it's still set this long after it first became true, something is stuck
        // (e.g. tick() itself was starved by continuous RX) -- stop gating the debug flush on it
        // so the stall is visible in the log instead of silent.
        static const uint32_t PROBE_STUCK_MS = 2000;

        // Caps how many bytes pollBytes() drains per call. Continuous/flooded RX (e.g. noise on
        // a mis-selected edge) must not starve tick()'s other duties -- especially
        // driver.tick()'s own probe-timeout recovery and pollProbeClaim()'s mux re-parking -- so
        // this yields back once the cap is hit even if more bytes are available; a normal frame
        // is far smaller than this and still drains within the same tick() call.
        static const uint8_t MAX_BYTES_PER_POLL = 32;

        #if DEBUG
            struct PendingRxBusLog {
                uint8_t  type;
                uint16_t panel;
            };

            static const uint8_t PENDING_RX_BUS_LOG_CAP = 8;

            PendingRxBusLog pendingRxBusLogs[PENDING_RX_BUS_LOG_CAP];
            uint8_t pendingRxBusLogCount;

            // Bypasses every other gate (isProbing/RELAY_QUIET_MS/queue draining) -- prints
            // regardless, throttled only by its own interval, so a receiver stuck deaf (no bytes,
            // no driver events, nothing to otherwise flush) is still visible instead of producing
            // total silence. Only fires while !driver.isProbing() (discovery's own timing-critical
            // windows are already covered by pollProbeClaim() and stay undisturbed).
            static const uint32_t HEARTBEAT_INTERVAL_MS = 1000;
            uint32_t lastHeartbeatMs;

            // Wake-path liveness counters (wrap silently, "is this changing" only). The first two
            // are touched from onEdgeWakeIsr() (real ISR context, hence volatile); the claim ones
            // are only ever touched from pollWake() in the main loop.
            volatile uint8_t wakeIsrFiredCount;
            volatile uint8_t wakeIsrMaskedCount;
            uint8_t wakeClaimGrantedCount;
            uint8_t wakeClaimIgnoredCount;
            uint8_t lastWakeEdgeSeen;
        #endif

        void pollWake(uint32_t nowMs);
        void pollBytes(uint32_t nowMs);

        // While a probe is outstanding (driver.isProbing()), parks the RX claim/mux on the
        // probed edge directly rather than waiting for a PCINT wake -- the protocol guarantees a
        // reply can only ever come back on that edge (see PanelDiscoveryDriver::probingEdge()'s
        // own comment), so this makes the probe-reply path immune to a wake being lost to
        // crosstalk or a stray transient. Uses EdgeFrameReceiver::preemptClaim(): a claim held
        // by any *other* edge during a probe is by definition noise (a crosstalk wake, or the
        // tail of a frame this panel's own transmission talked over) and is evicted rather than
        // allowed to hold the mux off the probed edge until it times out -- with
        // EdgeFrameReceiver::FRAME_TIMEOUT_MS as long as the probe retry interval
        // (PanelDiscoveryDriver::PROBE_TIMEOUT_MS), one such stale claim straddles the next
        // attempt and eats its reply too. A no-op while the
        // probed edge already holds the claim, so calling this every tick() is cheap and also
        // re-parks the edge after EdgeFrameReceiver::FRAME_TIMEOUT_MS releases a stalled claim.
        void pollProbeClaim(uint32_t nowMs);

        // Keeps the PCINT wake interrupts enabled exactly while no claim is held. The wake-sense
        // lines are the edges' data lines themselves (see EdgeUartTransport::
        // setWakeInterruptsEnabled()), so leaving them enabled during a claimed frame lets the
        // wake ISR fire on every bit transition and starve the (lower-priority) USART RX ISR
        // into overruns -- observed on real hardware as the same mid-frame byte lost on every
        // retransmit of a frame whose byte pattern is transition-dense enough, which silently
        // killed the discovery walk one hop down. A claim marks exactly the window where wakes
        // carry no information (a wake for the claimed edge is redundant, one for any other edge
        // is ignored), so gating on the claim loses nothing. Call after every point where the
        // claim can change hands: a wake/probe/self claim grant, a completed frame, or a
        // receiver.tick() timeout.
        void syncWakeInterruptSuppression();

        void flushPendingRxBusLogs();
        void flushIdleDebugLogs(uint32_t nowMs);
        #if DEBUG
            void flushHeartbeatLog(uint32_t nowMs);
        #endif
        void handlePacket(const Protocol::PacketMeta *packet, uint8_t size);

        void handleTurnOnOff(const Protocol::PacketTurnOnOff *packet);
        void handleSetColor(const Protocol::PacketSetColor *packet);
        void handlePanelConfiguration(const Protocol::PacketPanelConfiguration *packet);
        void handleAnimationPrepare(const Protocol::PacketAnimationPrepare *packet);
        void handleAnimationStart(const Protocol::PacketAnimationStart *packet);
        void handleAnimationControl(const Protocol::PacketAnimationControl *packet);
        void handleAnimationUpdateParams(const Protocol::PacketAnimationUpdateParams *packet);
        void handleSetPalette(const Protocol::PacketSetPalette *packet);
        void handleSetBaseColors(const Protocol::PacketSetBaseColors *packet);
        void handleSetGlobalBrightness(const Protocol::PacketSetGlobalBrightness *packet);
        void handleEnterBootloader(const Protocol::PacketEnterBootloader *packet);
        void handleFetchState();

        // Routes a reply one hop upstream (this panel's own parent edge) — ancestors' unmodified
        // PanelRouter carries it the rest of the way to the controller, same as
        // PACKET_DISCOVERY_DONE. Used for the rare, low-frequency operations the hardware
        // redesign plan §3 says keep a real acknowledgment (turn on/off, panel configuration).
        void sendAck();
};

extern LightnetPanel LNPanel;
