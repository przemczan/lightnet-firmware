#include "PacketMirror.hpp"
#include "WebsocketServer.hpp"
#include "../../../Common/Protocol.hpp"
#include "../../../Core/Common/AnimationTypes.hpp"
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
        case Protocol::PACKET_SET_BACKGROUND:
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
        case Protocol::PACKET_SET_BACKGROUND:
        case Protocol::PACKET_TURN_ON_OFF:
        case Protocol::PACKET_ANIMATION_PREPARE:
        case Protocol::PACKET_ANIMATION_START:
            return true;
        default:
            return false;
    }
}

void PacketMirror::updateSnapshot(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size)
{
    if (size < sizeof(Protocol::PacketMeta)) {
        return;
    }

    const uint8_t type = (uint8_t)packet->header.type;
    uint8_t key = 0;

    switch (packet->header.type) {
        case Protocol::PACKET_ANIMATION_PREPARE:

            if (size >= sizeof(Protocol::PacketAnimationPrepare)) {
                key = ((const Protocol::PacketAnimationPrepare *)packet)->group_id;
            }

            break;
        case Protocol::PACKET_ANIMATION_START:

            if (size >= sizeof(Protocol::PacketAnimationStart)) {
                key = ((const Protocol::PacketAnimationStart *)packet)->group_id;
            }

            break;
        default:
            break;
    }

    for (uint16_t i = 0; i < snapshotEntryCount; i++) {
        SnapshotEntry &e = snapshotIndex[i];

        if (e.address == address && e.type == type && e.key == key) {
            Lightnet::mirrorRecordWrite((MirrorRecordHeader *)(snapshotRecords() + e.offset), address, type, size, packet);

            return;
        }
    }

    if (snapshotEntryCount >= SNAPSHOT_MAX_ENTRIES ||
        (uint16_t)(snapshotRecordsLen + MIRROR_RECORD_HEADER_SIZE + size) > SNAPSHOT_RECORDS_CAP) {
        snapshotDroppedCount++;

        return;
    }

    MirrorRecordHeader *rec = (MirrorRecordHeader *)(snapshotRecords() + snapshotRecordsLen);

    Lightnet::mirrorRecordWrite(rec, address, type, size, packet);

    snapshotIndex[snapshotEntryCount] = { address, type, key, snapshotRecordsLen, size };
    snapshotEntryCount++;
    snapshotRecordsLen += MIRROR_RECORD_HEADER_SIZE + size;
}

void PacketMirror::invalidateSnapshot(uint8_t address, uint8_t group_id)
{
    for (uint16_t i = 0; i < snapshotEntryCount; ) {
        SnapshotEntry &e = snapshotIndex[i];

        bool matches = ((address == 0) || (e.address == address)) &&
                       ((group_id == 0) || (e.key == group_id)) &&
                       ((e.type == Protocol::PACKET_ANIMATION_PREPARE) || (e.type == Protocol::PACKET_ANIMATION_START));

        if (!matches) {
            i++;
            continue;
        }

        uint16_t recSize = MIRROR_RECORD_HEADER_SIZE + e.size;
        uint8_t *base = snapshotRecords();

        memmove(base + e.offset, base + e.offset + recSize, snapshotRecordsLen - e.offset - recSize);
        snapshotRecordsLen -= recSize;

        for (uint16_t j = 0; j < snapshotEntryCount; j++) {
            if (snapshotIndex[j].offset > e.offset) {
                snapshotIndex[j].offset -= recSize;
            }
        }

        for (uint16_t j = i; j < snapshotEntryCount - 1; j++) {
            snapshotIndex[j] = snapshotIndex[j + 1];
        }

        snapshotEntryCount--;
    }
}

void PacketMirror::capture(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size)
{
    if (size < sizeof(Protocol::PacketMeta)) {
        return;
    }

    const uint8_t type = (uint8_t)packet->header.type;

    if (!isMirrored(type)) {
        return;
    }

    if (packet->header.type == Protocol::PACKET_ANIMATION_CONTROL &&
        size >= sizeof(Protocol::PacketAnimationControl)) {
        const Protocol::PacketAnimationControl *ctrl = (const Protocol::PacketAnimationControl *)packet;

        if (ctrl->cmd == Lightnet::ANIM_CTRL_STOP) {
            invalidateSnapshot(address, ctrl->group_id);
        }
    }

    if (isSnapshotted(type)) {
        updateSnapshot(address, packet, size);
    }

    if (type == Protocol::PACKET_SET_COLOR) {
        uint16_t off = 0;

        while (off < recordsLen) {
            const MirrorRecordHeader *rec = (const MirrorRecordHeader *)(records() + off);

            if (rec->address == address && rec->type == type && rec->size == size) {
                memcpy(Lightnet::mirrorRecordPayload((MirrorRecordHeader *)rec), packet, size);

                return;
            }

            off = (uint16_t)(off + Lightnet::mirrorRecordTotalSize(rec));
        }
    }

    if ((uint16_t)(recordsLen + MIRROR_RECORD_HEADER_SIZE + size) > RECORDS_CAP) {
        if (server) {
            flushTo(server);
        } else {
            droppedCount++;

            return;
        }
    }

    uint32_t now = millis();

    if (firstRecordMs == 0) firstRecordMs = now;
    else if (now < firstRecordMs) firstRecordMs = now;

    MirrorRecordHeader *rec = (MirrorRecordHeader *)(records() + recordsLen);

    Lightnet::mirrorRecordWrite(rec, address, type, size, packet);

    recordsLen += MIRROR_RECORD_HEADER_SIZE + size;
    recordCount++;
}

bool PacketMirror::flushTo(WebsocketServer *server)
{
    if (recordCount == 0) {
        return false;
    }

    uint32_t now = (firstRecordMs != 0) ? firstRecordMs : millis();

    Lightnet::mirrorBatchWriteHeader(payload(), now, recordCount);

    uint16_t payloadSize = MIRROR_BATCH_HEADER_SIZE + recordsLen;

    WebsocketApi::updatePacketMeta(
        (WebsocketApi::PacketMeta *)frame,
        WebsocketApi::MIRROR_BATCH,
        payloadSize
    );

    server->sendToMirroringClients(frame, sizeof(WebsocketApi::PacketMeta) + payloadSize);

    DEBUG_IF(DEBUG_API, {
        D_PRINTLN("[MIRROR] records sent", recordCount);

        if (droppedCount) {
            D_PRINTLN("[MIRROR] dropped records (buffer full)", droppedCount);
        }
    });

    recordsLen    = 0;
    recordCount   = 0;
    droppedCount  = 0;
    firstRecordMs = 0;

    return true;
}

void PacketMirror::flushSnapshotTo(WebsocketServer *server, uint32_t clientId)
{
    if (snapshotEntryCount == 0) {
        return;
    }

    uint32_t now = millis();

    Lightnet::mirrorBatchWriteHeader(snapshotPayload(), now, snapshotEntryCount);

    uint16_t payloadSize = MIRROR_BATCH_HEADER_SIZE + snapshotRecordsLen;

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
