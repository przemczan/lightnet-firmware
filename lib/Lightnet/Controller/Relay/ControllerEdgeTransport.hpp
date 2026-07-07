#pragma once

// ControllerEdgeTransport — the controller's single physical trunk port (hardware redesign
// plan §1: "Controller: single physical trunk port — all branching happens at panels, not the
// controller"). No mux is needed here, unlike Panel/EdgeUartTransport's per-edge CD74HC4052 —
// there is exactly one edge, so this class is a thin IEdgeLink wrapper around a HardwareSerial
// the caller has already configured (baud + pins, via Serial1.begin() in
// PanelsInitializer::start() — see src/controller/config.hpp's CONTROLLER_TRUNK_RX_PIN/
// CONTROLLER_TRUNK_TX_PIN, and docs/hardware/schematics/Controller.png).
//
// Wired into the live boot path (PanelsInitializer's discovery service and
// ControllerRelayPacketSink both use the shared LNTrunkTransport instance below), but not yet
// bench-validated — no boards exist yet to confirm baud/timing/mux-settling on real silicon (see
// the hardware redesign plan). Self-echo masking is not implemented: every packet type in this
// protocol is strictly single-direction (the controller only ever *sends*
// PACKET_INITIALIZATION_PULL/PACKET_DISCOVERY_ADVANCE; it only ever *reacts to*
// PACKET_REGISTER_EDGE/PACKET_DISCOVERY_DONE — see DiscoveryCoordinator::onFrameArrived), so a
// self-echoed outbound packet can never be misread as a reply regardless of masking — it would
// only waste a few CPU cycles, not cause a correctness bug, so it's deferred rather than
// pre-optimized.

#include <stdint.h>
#include <HardwareSerial.h>
#include "../../Core/Relay/IEdgeLink.hpp"

class ControllerEdgeTransport : public Lightnet::IEdgeLink
{
    public:
        static const uint8_t TRUNK_EDGE = 0;

        explicit ControllerEdgeTransport(HardwareSerial &serial);

        void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override;

        // True if at least one byte is waiting to be read.
        bool available();

        // Reads exactly one byte — caller must have checked available() first.
        uint8_t readByte();

    private:
        HardwareSerial &serial;
};

// The controller's one physical trunk port, shared by PanelsInitializer's discovery service and
// main.cpp's application-traffic sink (ControllerRelayPacketSink) — both send-only and
// receive-only use is safe over the same HardwareSerial, but there is exactly one, matching the
// one physical trunk edge (see the hardware redesign plan §1).
extern ControllerEdgeTransport LNTrunkTransport;
