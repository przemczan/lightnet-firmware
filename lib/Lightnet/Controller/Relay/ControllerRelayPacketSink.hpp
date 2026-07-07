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
// descriptor lever was rejected in §6) — so a logical "address" no longer opens a bus transaction
// the way I2C's did. Instead it has to travel inside the packet itself, in
// PacketHeader.targetPanelIndex (0 = broadcast). AnimationScheduler/ScenePlayer/PanelsController
// all build packets via Protocol::makePacket<T>(type) with no target, since they only learn the
// logical `address` at send() time (an existing, unchanged call pattern — see
// Core/Controller/IPacketSink.hpp's class comment) — so this class re-stamps the target and
// recomputes the header CRC on a local mutable copy before writing it to the wire. The caller's
// own buffer, and the IPacketSink interface itself, are untouched.
//
// Acks are NOT implemented here yet: `wantAck` is accepted but ignored, so every send is
// currently fire-and-forget. Hardware redesign plan §3 says acks should be kept for rare,
// low-frequency operations (e.g. turning panels on/off) — but *how* an ack travels back over the
// relay (a reply packet routed upstream through PanelRouter, with the controller blocking on it —
// similar in shape to DiscoveryCoordinator's synchronous request/reply, but for application
// traffic) hasn't been designed yet. Treat this as a known, flagged gap, not a silent omission —
// see the hardware redesign plan. pace() is likewise still the IPacketSink base-class no-op:
// controller pacing between packets (§3/§5) is a real firmware responsibility on this transport,
// not yet tuned — needs bench validation once boards exist, same as every other timing constant
// in this design.
//
// setOnPacketSent() mirrors LightnetBus's own callback (Common/LightnetBus.hpp) so
// mirrorOutboundPacket() (src/controller/main.cpp) can capture scene/animation traffic for the
// live WebSocket preview (PacketMirror) regardless of which sink is active — without this, the
// mirror would only ever see the rare fetchState/OTA traffic still left on LNBus.
//
// Wired into the live boot path via src/controller/main.cpp's activeSink — not yet bench-tested,
// see ControllerEdgeTransport.hpp.

#include <stdint.h>
#include <string.h>
#include "../../Core/Controller/IPacketSink.hpp"
#include "../../Core/Common/ProtocolMeta.hpp"
#include "ControllerEdgeTransport.hpp"

namespace Lightnet {
    class ControllerRelayPacketSink : public IPacketSink
    {
        public:
            typedef void (*onPacketSent_t)(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size);

            explicit ControllerRelayPacketSink(ControllerEdgeTransport &transport)
                : transport(transport), onPacketSentCallback(nullptr)
            {
            }

            void setOnPacketSent(onPacketSent_t callback)
            {
                this->onPacketSentCallback = callback;
            }

            void send(
            uint8_t                     address,
            const Protocol::PacketMeta *packet,
            uint8_t                     size,
            bool                        wantAck
            ) override
            {
                (void)wantAck;  // see class comment — acking over the relay is a known, unbuilt gap

                uint8_t buffer[Protocol::MAX_PACKET_SIZE];

                memcpy(buffer, packet, size);

                Protocol::PacketMeta *restamped = (Protocol::PacketMeta *)buffer;

                Protocol::setPacketMeta(restamped, restamped->header.type, address);

                if (this->onPacketSentCallback) {
                    this->onPacketSentCallback(address, restamped, size);
                }

                this->transport.sendOnEdge(ControllerEdgeTransport::TRUNK_EDGE, restamped, size);
            }

        private:
            ControllerEdgeTransport &transport;
            onPacketSent_t onPacketSentCallback;
    };
}  // namespace Lightnet
