#include "CommandApi.hpp"

void CommandApi::Cmd::updateMeta(CommandMeta *meta, command_t type, uint16_t payloadSize)
{

    meta->header.type = type;
    meta->header.protocolVersion = CommandApi::VERSION;
    meta->header.nonce = micros();
    meta->headerCrc = crc16(&meta->header, sizeof(CommandHeader));
    meta->payloadCrc = crc16(meta->payload, payloadSize);
    meta->payloadSize = payloadSize;
}
