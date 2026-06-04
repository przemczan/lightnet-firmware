#include "PacketMirror.hpp"
#include "WebsocketServer.hpp"
#include "../../../Common/Protocol.hpp"
#include <string.h>

PacketMirror::PacketMirror()
{
}

bool PacketMirror::isMirrored(uint8_t type)
{
    switch (type) {
        case Protocol::PACKET_TURN_ON_OFF:
        case Protocol::PACKET_SET_COLOR:
        case Protocol::PACKET_ANIMATION_PREPARE:
        case Protocol::PACKET_ANIMATION_START:
        case Protocol::PACKET_ANIMATION_CONTROL:
        case Protocol::PACKET_ANIMATION_UPDATE_PARAMS:
        case Protocol::PACKET_SET_PALETTE:
        case Protocol::PACKET_SET_BASE_COLORS:
        case Protocol::PACKET_SET_GLOBAL_BRIGHTNESS:
            return true;
        default:
            return false;
    }
}

void PacketMirror::capture(uint8_t address, const void *packet, uint8_t size, uint8_t type)
{
    if (!isMirrored(type)) {
        return;
    }

    if ((uint16_t)(recordsLen + RECORD_HEADER + size) > RECORDS_CAP) {
        droppedCount++;

        return;
    }

    uint8_t *rec = records() + recordsLen;

    rec[0] = address;
    rec[1] = type;
    rec[2] = size;
    memcpy(&rec[RECORD_HEADER], packet, size);

    recordsLen += RECORD_HEADER + size;
    recordCount++;
}

bool PacketMirror::flushTo(WebsocketServer *server)
{
    if (recordCount == 0) {
        return false;
    }

    uint32_t now = millis();
    uint8_t *p = payload();

    // memcpy: payload lands at an odd offset (sizeof(PacketMeta)==13), so direct
    // multi-byte stores would be unaligned and fault on ESP8266.
    memcpy(&p[0], &now, sizeof(now));
    memcpy(&p[4], &recordCount, sizeof(recordCount));

    uint16_t payloadSize = PAYLOAD_HEADER + recordsLen;

    WebsocketApi::updatePacketMeta(
        (WebsocketApi::PacketMeta *)frame,
        WebsocketApi::MIRROR_BATCH,
        payloadSize
    );

    server->broadcastFrame(frame, sizeof(WebsocketApi::PacketMeta) + payloadSize);

    DEBUG_IF(DEBUG_API, {
        if (droppedCount) {
            D_PRINTLN("[MIRROR] dropped records (buffer full)", droppedCount);
        }
    });

    recordsLen   = 0;
    recordCount  = 0;
    droppedCount = 0;

    return true;
}
