#pragma once

// ControllerRelayPacketSink — the controller's IPacketSink implementation for the relay
// transport (protocol v10 addressing, hardware redesign plan §11.1). This is the real-hardware
// sink: AnimationScheduler/ScenePlayer/PanelsController all take IPacketSink& and stay
// transport-agnostic, so main.cpp is the only place that picks a concrete sink — this one on real
// builds, ControllerPacketSink (wrapping LNBus/LightnetBusSim) under SIM_MODE, where sim panels
// only ever respond to LightnetBus-routed commands.
//
// PanelRouter floods broadcasts to every connected edge, and routes an addressed downstream
// packet to the single branch containing the target (pre-order DFS index ranges — see
// PanelRouter.hpp; the "routing table" is just one u16 child index per edge). The target address
// travels inside the packet itself, in
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
// timeout it simply returns. requestReply() is the same receive machinery exposed for callers
// that need the reply's payload (PanelsController::fetchState(), the relay OTA client) rather
// than a bare ack. Scene/animation traffic passes wantAck=false — panels don't ack those yet,
// and the main loop must not block on per-panel timeouts during playback.
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
#include "../../Core/Relay/TrunkFrameReceiver.hpp"
#include "../../Core/Relay/LinkArq.hpp"
#include "ControllerEdgeTransport.hpp"

namespace Lightnet {
    class ControllerRelayPacketSink : public IPacketSink
    {
        public:
            typedef void (*onPacketSent_t)(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size);

            // Bounds the end-to-end reply wait. Sized for deep chains WITH the link-ARQ layer's
            // per-hop overhead: a depth-30 chunk round trip is ~155 ms of pure transit (frame +
            // hop ack per hop, both directions) before any hop retransmissions.
            static const uint32_t ACK_TIMEOUT_MS = 500;

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
            // Sender-side link-ARQ context for one in-flight trunk frame (see
            // Core/Relay/LinkArq.hpp): awaitFrame() retransmits `frame` while the hop ack
            // (PACKET_LINK_ACK echoing frameCrc) hasn't arrived and attempts remain. Integrated
            // into the one receive loop rather than a separate window so a fast end-to-end
            // reply arriving before/instead of the hop ack is still caught, never consumed as
            // window noise.
            struct LinkArqTx {
                const uint8_t *frame;
                uint8_t        size;
                uint16_t       frameCrc;
                uint8_t        attemptsLeft;
                uint32_t       lastSentAtMs;
                bool           acked;
            };

            ControllerEdgeTransport &transport;
            onPacketSent_t onPacketSentCallback;
            Lightnet::TrunkFrameReceiver replyFramer;

            void flushStrayBytes();
            void emitLinkAck(uint16_t frameCrc);
            bool awaitFrame(
                Protocol::packetType_t expectedType,
                Protocol::PacketMeta * outBuffer,
                uint8_t                outBufferSize,
                uint32_t               timeoutMs,
                LinkArqTx *            arq = nullptr
            );
    };
}  // namespace Lightnet
