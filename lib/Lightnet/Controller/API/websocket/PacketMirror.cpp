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

bool PacketMirror::isSnapshotted(uint8_t type)
{
    switch (type) {
        case Protocol::PACKET_SET_GLOBAL_BRIGHTNESS:
        case Protocol::PACKET_SET_BASE_COLORS:
        case Protocol::PACKET_SET_PALETTE:
        case Protocol::PACKET_TURN_ON_OFF:
        case Protocol::PACKET_ANIMATION_PREPARE:
        case Protocol::PACKET_ANIMATION_START:
            return true;
        default:
            return false;
    }
}

void PacketMirror::updateSnapshot(uint8_t address, const void *packet, uint8_t size, uint8_t type)
{
    uint8_t key = 0;

    // group_id sits at byte 6 in both PREPARE (meta + animType) and START (meta +
    // seq_id). Keying on it keeps per-group entries distinct, so a panel running
    // several concurrent animation groups replays every one of them, not just the last.
    if ((type == Protocol::PACKET_ANIMATION_PREPARE ||
         type == Protocol::PACKET_ANIMATION_START) && size >= 7) {
        key = ((const uint8_t *)packet)[6];
    }

    // Upsert: find existing entry for (address, type, key) and overwrite in place.
    for (uint16_t i = 0; i < snapshotEntryCount; i++) {
        SnapshotEntry &e = snapshotIndex[i];

        if (e.address == address && e.type == type && e.key == key) {
            // All packets of the same type have the same fixed size, so overwrite is safe.
            memcpy(snapshotRecords() + e.offset + RECORD_HEADER, packet, size);

            return;
        }
    }

    // New entry — check capacity guards.
    if (snapshotEntryCount >= SNAPSHOT_MAX_ENTRIES ||
        (uint16_t)(snapshotRecordsLen + RECORD_HEADER + size) > SNAPSHOT_RECORDS_CAP) {
        snapshotDroppedCount++;

        return;
    }

    uint8_t *rec = snapshotRecords() + snapshotRecordsLen;

    rec[0] = address;
    rec[1] = type;
    rec[2] = size;
    memcpy(&rec[RECORD_HEADER], packet, size);

    snapshotIndex[snapshotEntryCount] = { address, type, key, snapshotRecordsLen, size };
    snapshotEntryCount++;
    snapshotRecordsLen += RECORD_HEADER + size;
}

void PacketMirror::capture(uint8_t address, const void *packet, uint8_t size, uint8_t type)
{
    if (!isMirrored(type)) {
        return;
    }

    // Snapshot first: it has its own buffer, so a full live-stream ring must never
    // block recording the latest state a late-joining client needs to replay.
    if (isSnapshotted(type)) {
        updateSnapshot(address, packet, size, type);
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

    server->sendToMirroringClients(frame, sizeof(WebsocketApi::PacketMeta) + payloadSize);

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

void PacketMirror::flushSnapshotTo(WebsocketServer *server, uint32_t clientId)
{
    if (snapshotEntryCount == 0) {
        return;
    }

    uint32_t now = millis();
    uint8_t *p = snapshotPayload();

    // memcpy for unaligned-safe writes (payload is at an odd offset on ESP8266).
    memcpy(&p[0], &now, sizeof(now));
    memcpy(&p[4], &snapshotEntryCount, sizeof(snapshotEntryCount));

    uint16_t payloadSize = PAYLOAD_HEADER + snapshotRecordsLen;

    WebsocketApi::updatePacketMeta(
        (WebsocketApi::PacketMeta *)snapshotFrame,
        WebsocketApi::MIRROR_BATCH,
        payloadSize
    );

    server->sendFrameToClient(clientId, snapshotFrame, sizeof(WebsocketApi::PacketMeta) + payloadSize);

    DEBUG_IF(DEBUG_API, {
        if (snapshotDroppedCount) {
            D_PRINTLN("[MIRROR] snapshot dropped entries (buffer full)", snapshotDroppedCount);
            snapshotDroppedCount = 0;
        }
    });
}
