#pragma once

// ControllerPacketSink — the device-side IPacketSink. Drives the physical I2C bus,
// keeping the ack-retry and inter-packet pacing that used to live in AnimationScheduler.
// This is the controller half of the bus seam; the shared scene engine only ever sees
// the abstract IPacketSink.

#include <Arduino.h>
#include "../../Core/Controller/Scene/IPacketSink.hpp"
#include "../../Common/LightnetBus.hpp"

namespace Lightnet {
    class ControllerPacketSink : public IPacketSink {
        public:
            explicit ControllerPacketSink(LightnetBus& _bus) : bus(_bus) {}

            void send(
                uint8_t                address,
                Protocol::packetType_t type,
                const void *           packet,
                uint8_t                size,
                bool                   wantAck
            ) override
            {
                // LNBus takes a non-const pointer but does not mutate the packet.
                void *p = const_cast<void *>(packet);

                if (wantAck) {
                    // Retry on failure so a single bus glitch doesn't leave a panel with no
                    // animation queued (matches the previous AnimationScheduler behaviour).
                    uint8_t err = 1;

                    for (uint8_t attempt = 0; attempt < 3 && err != 0; attempt++) {
                        if (attempt > 0) delayMicroseconds(100);

                        err = bus.sendPacketAck(address, p, size, type);
                    }
                } else {
                    bus.sendPacketNack(address, p, size, type);
                }
            }

            void pace(uint16_t microseconds) override
            {
                delayMicroseconds(microseconds);
            }

        private:
            LightnetBus& bus;
    };
}  // namespace Lightnet
