#pragma once

// ControllerEdgeTransport — the controller's single physical trunk port (hardware redesign
// plan §1: "Controller: single physical trunk port — all branching happens at panels, not the
// controller"). No mux is needed here, unlike Panel/EdgeUartTransport's per-edge CD74HC4052 —
// there is exactly one edge, so this class is a thin IEdgeLink wrapper around a HardwareSerial
// the caller has already configured (baud + pins, via Serial1.begin() in
// PanelsInitializer::start() — see src/controller/config.hpp's CONTROLLER_TRUNK_RX_PIN/
// CONTROLLER_TRUNK_TX_PIN, and docs/hardware/schematics/Controller.png).
//
// The trunk's line driver (U4, EM74LVC1G125GW in Controller.png) has an active-low output-enable
// gating it onto the shared half-duplex wire — bench-confirmed necessary, not optional: tying it
// permanently low (always enabled) makes U4 fight an incoming panel reply whenever this side is
// idle-high, since a UART TX pin stays actively driven between frames rather than tri-stating
// itself. begin()/sendOnEdge() drive that GPIO exactly like Panel/EdgeUartTransport::sendOnEdge()
// gates its own per-edge tri-state buffers via setEdgeEnable() — enabled only for the duration of
// an actual send, tri-stated (idle HIGH) the rest of the time so the wire is free for a reply.
//
// Wired into the live boot path (PanelsInitializer's discovery service and
// ControllerRelayPacketSink both use the shared LNTrunkTransport instance below). Self-echo
// masking of received bytes is not implemented: every packet type in this protocol is strictly
// single-direction (the controller only ever *sends* PACKET_INITIALIZATION_PULL/
// PACKET_DISCOVERY_ADVANCE; it only ever *reacts to* PACKET_REGISTER_EDGE/PACKET_DISCOVERY_DONE —
// see DiscoveryCoordinator::onFrameArrived), so a self-echoed outbound packet can never be
// misread as a reply regardless of masking — it would only waste a few CPU cycles, not cause a
// correctness bug, so it's deferred rather than pre-optimized. The output-enable gating above is
// what actually prevents the echo/contention in the first place.

#include <stdint.h>
#include <HardwareSerial.h>
#include "../../Core/Relay/IEdgeLink.hpp"

class ControllerEdgeTransport : public Lightnet::IEdgeLink
{
    public:
        static const uint8_t TRUNK_EDGE = 0;

        explicit ControllerEdgeTransport(HardwareSerial &serial);

        // Configures the output-enable GPIO gating U4 onto the shared trunk wire. Must be called
        // once, after the serial port itself is configured (Serial1.begin() in
        // PanelsInitializer::start()) and before the first sendOnEdge().
        void begin(uint8_t outputEnablePin);

        void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override;

        // True if at least one byte is waiting to be read.
        bool available();

        // Reads exactly one byte — caller must have checked available() first.
        uint8_t readByte();

    private:
        HardwareSerial &serial;
        uint8_t outputEnablePin;
};

// The controller's one physical trunk port, shared by PanelsInitializer's discovery service and
// main.cpp's application-traffic sink (ControllerRelayPacketSink) — both send-only and
// receive-only use is safe over the same HardwareSerial, but there is exactly one, matching the
// one physical trunk edge (see the hardware redesign plan §1).
extern ControllerEdgeTransport LNTrunkTransport;
