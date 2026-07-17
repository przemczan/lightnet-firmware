#ifndef LIGHTNET_TARGET_CONTROLLER
#include "ArqEdgeLink.hpp"

#include <string.h>
#include "../Core/Common/ProtocolMeta.hpp"
#include "../Runtime/PanelClock.hpp"
#include "../Utils/Crc.hpp"

ArqEdgeLink::ArqEdgeLink(EdgeUartTransport &transport, ILinkWindowHooks &hooks)
    : transport(transport), hooks(hooks)
{
}

void ArqEdgeLink::sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size)
{
    this->transport.sendOnEdge(edgeIndex, packet, size);

    if (!Protocol::isLinkAckedType(packet->header.type) || size > sizeof(this->retransmitBuffer)) {
        return;
    }

    // Copied AFTER the first transmission: `packet` may point into EdgeFrameReceiver's own
    // frame buffer (a relay), which nothing overwrites until the next inbound frame -- and no
    // frame can start arriving before this window ends.
    memcpy(this->retransmitBuffer, packet, size);

    uint16_t frameCrc = crc16(this->retransmitBuffer, size);

    for (uint8_t attempt = 0; ; attempt++) {
        if (this->awaitLinkAck(edgeIndex, frameCrc)) {
            return;
        }

        if (attempt >= Lightnet::LINK_RETRANSMITS) {
            this->linkAckTimeouts++;

            return;  // residual loss -- end-to-end retries recover
        }

        this->linkRetransmits++;
        this->transport.sendOnEdge(edgeIndex, (const Protocol::PacketMeta *)this->retransmitBuffer, size);
    }
}

bool ArqEdgeLink::awaitLinkAck(uint8_t edgeIndex, uint16_t frameCrc)
{
    this->hooks.onLinkWindowBegin(edgeIndex);
    this->transport.selectRxEdge(edgeIndex);
    this->ackMatcher.reset();

    uint32_t start = Lightnet::millis();
    bool acked     = false;

    while ((uint32_t)(Lightnet::millis() - start) < Lightnet::LINK_ACK_TIMEOUT_MS) {
        if (!this->transport.available()) {
            continue;
        }

        uint16_t ackedCrc;

        if (this->ackMatcher.pushByte(this->transport.readByte(), &ackedCrc)
            && ackedCrc == frameCrc) {
            acked = true;
            break;
        }
    }

    this->hooks.onLinkWindowEnd();

    return acked;
}

uint16_t ArqEdgeLink::retransmitCount() const
{
    return this->linkRetransmits;
}

uint16_t ArqEdgeLink::ackTimeoutCount() const
{
    return this->linkAckTimeouts;
}

#endif  // LIGHTNET_TARGET_CONTROLLER
