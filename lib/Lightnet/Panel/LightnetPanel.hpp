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

        volatile uint8_t pendingWakeEdge;
        uint32_t lastRelayActivityMs;

        // With probe retries (PanelDiscoveryDriver::PROBE_ATTEMPTS), a downstream panel
        // resolving two empty edges can legitimately go quiet for up to
        // 2 * PROBE_ATTEMPTS * PanelDiscoveryDriver::PROBE_TIMEOUT_MS = 300ms mid-walk -- this
        // must stay above that so an upstream relay panel never starts a bit-banged debug flush
        // (20-30ms, blocking) inside a gap that's really still part of the walk.
        static const uint32_t RELAY_QUIET_MS = 400;

        #if DEBUG
            struct PendingRxBusLog {
                uint8_t  type;
                uint16_t panel;
            };

            static const uint8_t PENDING_RX_BUS_LOG_CAP = 8;

            PendingRxBusLog pendingRxBusLogs[PENDING_RX_BUS_LOG_CAP];
            uint8_t pendingRxBusLogCount;
        #endif

        void pollWake(uint32_t nowMs);
        void pollBytes(uint32_t nowMs);

        // While a probe is outstanding (driver.isProbing()), parks the RX claim/mux on the
        // probed edge directly rather than waiting for a PCINT wake -- the protocol guarantees a
        // reply can only ever come back on that edge (see PanelDiscoveryDriver::probingEdge()'s
        // own comment), so this makes the probe-reply path immune to a wake being lost to
        // crosstalk or a stray transient. A no-op once the edge is already claimed
        // (EdgeFrameReceiver::onEdgeWake() ignores a redundant claim), so calling this every
        // tick() is cheap and also re-claims the edge after EdgeFrameReceiver::FRAME_TIMEOUT_MS
        // releases a stalled claim.
        void pollProbeClaim(uint32_t nowMs);
        void flushPendingRxBusLogs();
        void flushIdleDebugLogs(uint32_t nowMs);
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
