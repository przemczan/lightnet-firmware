#pragma once

// LightnetPanel — the panel-side entry point for the relay network (hardware redesign plan
// §10/§11). Replaces the earlier I2C-based LightnetPanel/LightnetPanelEdge/LightnetPinger
// entirely — panel and controller no longer share a physical bus, so there is nothing left to
// preserve from the old ping-pulse + Wire flow. Only one panel firmware exists; this is it.
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
//   - The PCINT wake ISR just latches which edge woke (onEdgeWakeIsr()) into a single volatile
//     byte; the actual claim decision (EdgeFrameReceiver::onEdgeWake()) and the mux switch
//     (EdgeUartTransport::selectRxEdge()) both happen from pollWake() in the main loop instead of
//     from the ISR, so they're never racing tick()'s other calls into the same objects. A second
//     wake arriving before pollWake() drains the first overwrites it — an accepted simplification
//     given the single-active-flow invariant (hardware redesign plan §3): only one edge should
//     legitimately be waking at a time in correctly-functioning hardware.
// Whether main-loop-driven mux switching reacts fast enough relative to the sender's preamble
// byte is genuinely a bench-spike question (see the plan's step 1), not something resolved here.
//
// UNVALIDATED HARDWARE — no bench spike has run. This class builds and links against real
// register-level code, but nothing in this design (here or on the now-also-cut-over controller
// side) has been exercised on real silicon yet — no boards exist.
//
// Known gap, inherited from PacketFramer rather than introduced here: the old LightnetPanel's
// handleIncomingPackets() deliberately skipped protocol-version validation for
// PACKET_ENTER_BOOTLOADER/PACKET_RESET_DEVICE, since flashing is how a version mismatch gets
// resolved and must never itself be version-gated. PacketFramer (and so EdgeFrameReceiver) has no
// equivalent per-type bypass — it validates the protocol version unconditionally before a frame
// ever surfaces as complete, so a version-mismatched panel cannot currently be reset/reflashed
// over the relay. Not fixed here: doing so means either teaching PacketFramer a bypass list or
// changing what "complete" means for it — a real, separate change to shared, already-tested logic.

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

        void pollWake(uint32_t nowMs);
        void pollBytes(uint32_t nowMs);
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
};

extern LightnetPanel LNPanel;
