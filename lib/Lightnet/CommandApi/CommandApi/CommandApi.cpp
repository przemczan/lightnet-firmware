#include "CommandApi.hpp"

void CommandApi::updatePacketMeta(PacketMeta *meta, packet_t type, uint16_t payloadSize)
{
    meta->header.type = type;
    meta->header.protocolVersion = CommandApi::VERSION;
    meta->header.nonce = micros();
    meta->headerCrc = crc16(&meta->header, sizeof(PacketHeader));
    meta->payloadCrc = crc16(meta->payload, payloadSize);
    meta->payloadSize = payloadSize;
}
