#pragma once

#include <Arduino.h>
#include "../../Core/Controller/IPacketSink.hpp"
#include "../../Common/LightnetBus.hpp"

namespace Lightnet {
    class ControllerPacketSink : public IPacketSink
    {
        public:
            explicit ControllerPacketSink(LightnetBus& _bus) : bus(_bus)
            {
            }

            void send(
            uint8_t                     address,
            const Protocol::PacketMeta *packet,
            uint8_t                     size,
            bool                        wantAck
            ) override
            {
                if (wantAck) {
                    uint8_t err = 1;

                    for (uint8_t attempt = 0; attempt < 3 && err != 0; attempt++) {
                        if (attempt > 0) delayMicroseconds(100);

                        err = bus.sendPacketAck(address, packet, size);
                    }
                } else {
                    bus.sendPacketNack(address, packet, size);
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
