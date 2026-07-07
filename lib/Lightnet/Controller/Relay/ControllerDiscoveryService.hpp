#pragma once

// ControllerDiscoveryService — glues ControllerEdgeTransport's raw bytes to
// DiscoveryCoordinator via PacketFramer: transport -> framer -> protocol logic, the same
// three-layer shape the panel side will eventually use for its own RX path. Polling-based
// (tick() drains whatever has arrived since the last call) rather than interrupt-driven —
// unlike the panel's mux-switching, there is no hard real-time constraint here, and
// HardwareSerial already buffers incoming bytes in hardware, so nothing is lost between tick()
// calls as long as they happen often enough to stay ahead of the RX FIFO filling up.
//
// Wired into the live boot path via Controller/Panels/PanelsInitializer.cpp — not yet
// bench-validated, see ControllerEdgeTransport.hpp.

#include <stdint.h>
#include "ControllerEdgeTransport.hpp"
#include "../../Core/Relay/PacketFramer.hpp"
#include "../../Core/Relay/DiscoveryCoordinator.hpp"
#include "../../Core/Relay/DiscoveryTreeBuilder.hpp"

class ControllerDiscoveryService
{
    public:
        // edgeCountPerPanel: the uniform per-panel edge count (see DiscoveryTreeBuilder.hpp) —
        // needed to fill in the discovered tree's edgeCounts[] for PanelGraph::build().
        ControllerDiscoveryService(ControllerEdgeTransport &transport, uint8_t edgeCountPerPanel);

        // Sends the first probe down the trunk.
        void begin();

        // Drains whatever bytes have arrived since the last call through the framer, feeding
        // any completed frame to the coordinator.
        void tick();

        bool isComplete() const;

        // The topology accumulated so far — valid to read once isComplete() is true.
        const Lightnet::DiscoveryTreeBuilder &tree() const;

    private:
        ControllerEdgeTransport &transport;
        Lightnet::PacketFramer framer;
        Lightnet::DiscoveryTreeBuilder treeBuilder;
        Lightnet::DiscoveryCoordinator coordinator;
};
