#pragma once

// ControllerRelayPacketSink — the controller's IPacketSink implementation for the relay
// transport (protocol v10 addressing, hardware redesign plan §11.1). This is the real-hardware
// sink: AnimationScheduler/ScenePlayer/PanelsController all take IPacketSink& and stay
// transport-agnostic, so main.cpp is the only place that picks a concrete sink — this one on real
// builds, ControllerPacketSink (wrapping LNBus/LightnetBusSim) under SIM_MODE, where sim panels
// only ever respond to LightnetBus-routed commands.
//
// PanelRouter floods every downstream packet to every connected edge unconditionally — there is
// no per-edge routing table (§11.1 rejected that for the same SRAM reasons the broadcast-
// descriptor lever was rejected in §6). The target address travels inside the packet itself, in
// PacketHeader.targetPanelIndex (0 = broadcast). AnimationScheduler/ScenePlayer/PanelsController
// all build packets via Protocol::makePacket<T>(type) with no target, since they only learn the
// logical `address` at send() time (an existing, unchanged call pattern — see
// Core/Controller/IPacketSink.hpp's class comment) — so this class re-stamps the target and
// recomputes the header CRC on a local mutable copy before writing it to the wire. The caller's
// own buffer, and the IPacketSink interface itself, are untouched.
//
// wantAck: a matching reply is a real, addressed unicast — arrives on this panel's ancestors'
// non-parent edges and rides their own unmodified PanelRouter upstream rule (exactly like
// PACKET_DISCOVERY_DONE), so no PanelRouter changes were needed to make replies reach the
// controller. `send(wantAck=true)` blocks (bounded by ACK_TIMEOUT_MS) for a PACKET_ACK; on
// timeout it simply returns, same as ControllerPacketSink's own best-effort retry loop —
// IPacketSink::send() has no return value, so neither sink can surface a failure to the caller
// either way. requestReply() is the same receive machinery exposed for callers that need the
// reply's payload (PanelsController::fetchState(), the relay OTA client) rather than a bare ack.
//
// No correlation id exists on any reply type (PACKET_ACK is meta-only; the relay OTA replies
// identify their sender via PacketHeader.targetPanelIndex, which requestReply() checks against
// the caller's expected source). Safety instead comes from flushing any stray buffered bytes
// immediately before every send — since the controller only ever waits for one reply at a time
// (this call is synchronous/blocking), a stale reply from an already-abandoned previous wait is
// the only realistic cross-talk, and flush-before-send discards it.
//
// requestReply()'s own outbound send deliberately does NOT go through onPacketSentCallback (the
// live-preview mirror hook) — FETCH_STATE queries and OTA control traffic aren't scene/animation
// state changes, so there's nothing for the mirror to usefully preview.
//
// setOnPacketSent() mirrors LightnetBus's own callback (Common/LightnetBus.hpp) so
// mirrorOutboundPacket() (src/controller/main.cpp) can capture scene/animation traffic for the
// live WebSocket preview (PacketMirror) regardless of which sink is active — without this, the
// mirror would see nothing at all on real hardware, since LNBus never carries any traffic there.
//
// Wired into the live boot path via src/controller/main.cpp's activeSink — not yet bench-tested,
// see ControllerEdgeTransport.hpp.

#include <stdint.h>
#include "../../Core/Controller/IPacketSink.hpp"
#include "../../Core/Common/ProtocolMeta.hpp"
#include "../../Core/Relay/PacketFramer.hpp"
#include "ControllerEdgeTransport.hpp"

namespace Lightnet {
    class ControllerRelayPacketSink : public IPacketSink
    {
        public:
            typedef void (*onPacketSent_t)(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size);

            // Not yet bench-validated — a placeholder generous enough for the plan's own
            // worst-case depth-50 latency estimate (a few ms) plus real-world margin.
            static const uint32_t ACK_TIMEOUT_MS = 300;

            explicit ControllerRelayPacketSink(ControllerEdgeTransport &transport);

            void setOnPacketSent(onPacketSent_t callback)
            {
                this->onPacketSentCallback = callback;
            }

            void send(
                uint8_t                     address,
                const Protocol::PacketMeta *packet,
                uint8_t                     size,
                bool                        wantAck
            ) override;

            // Sends `request` to `targetPanelIndex`, then blocks (bounded by timeoutMs) for a
            // frame of type `expectedReplyType`. On success, copies up to replyBufferSize bytes
            // of the reply into replyBuffer and returns true. The caller is responsible for
            // checking any identifying field in the reply payload (e.g.
            // PacketPanelState::panelState::panelIndex) — this method only matches on type.
            bool requestReply(
                uint16_t                    targetPanelIndex,
                const Protocol::PacketMeta *request,
                uint8_t                     requestSize,
                Protocol::packetType_t      expectedReplyType,
                Protocol::PacketMeta *      replyBuffer,
                uint8_t                     replyBufferSize,
                uint32_t                    timeoutMs = ACK_TIMEOUT_MS
            );

        private:
            ControllerEdgeTransport &transport;
            onPacketSent_t onPacketSentCallback;
            Lightnet::PacketFramer replyFramer;

            void flushStrayBytes();
            bool awaitFrame(
                Protocol::packetType_t expectedType,
                Protocol::PacketMeta * outBuffer,
                uint8_t                outBufferSize,
                uint32_t               timeoutMs
            );
    };
}  // namespace Lightnet
