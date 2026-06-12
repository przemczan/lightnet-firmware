#include "PacketMirror.hpp"
#include "WebsocketServer.hpp"
#include "../../../Common/Protocol.hpp"
#include "../../../Core/Anim/AnimationTypes.hpp"
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

// PACKET_ANIMATION_CONTROL is mirrored live but not snapshotted (see isSnapshotted), so a
// STOP that clears a slot on the real panel would otherwise leave that slot's PREPARE/START
// entries in the snapshot forever — replayed as phantom "residue" layers to any client that
// joins/re-enables mirroring afterwards. Drop them here so the snapshot tracks STOPped slots.
void PacketMirror::invalidateSnapshot(uint8_t address, uint8_t group_id)
{
    for (uint16_t i = 0; i < snapshotEntryCount; ) {
        SnapshotEntry &e = snapshotIndex[i];

        // address/group_id == 0 are General Call / "all slots" — match every snapshot
        // entry's address/key respectively, the same way AnimationPlayer::control() does.
        bool matches = ((address == 0) || (e.address == address)) &&
                       ((group_id == 0) || (e.key == group_id) ) &&
                       ((e.type == Protocol::PACKET_ANIMATION_PREPARE) || (e.type == Protocol::PACKET_ANIMATION_START) );

        if (!matches) {
            i++;
            continue;
        }

        uint16_t recSize = RECORD_HEADER + e.size;
        uint8_t *base = snapshotRecords();

        // Shift remaining record bytes left over the removed entry.
        memmove(base + e.offset, base + e.offset + recSize, snapshotRecordsLen - e.offset - recSize);
        snapshotRecordsLen -= recSize;

        // Fix up offsets of entries that pointed past the removed region.
        for (uint16_t j = 0; j < snapshotEntryCount; j++) {
            if (snapshotIndex[j].offset > e.offset) {
                snapshotIndex[j].offset -= recSize;
            }
        }

        // Remove this entry from the index — shift the tail left, re-check position i.
        for (uint16_t j = i; j < snapshotEntryCount - 1; j++) {
            snapshotIndex[j] = snapshotIndex[j + 1];
        }

        snapshotEntryCount--;
    }
}

void PacketMirror::capture(uint8_t address, const void *packet, uint8_t size, uint8_t type)
{
    if (!isMirrored(type)) {
        return;
    }

    // A STOP clears the matching slot(s) on the real panel; mirror the same invalidation
    // onto the snapshot so a late-joining/re-mirroring client doesn't replay stale entries.
    if (type == Protocol::PACKET_ANIMATION_CONTROL && size >= 7) {
        const uint8_t *p = (const uint8_t *)packet;

        if (p[5] == Lightnet::ANIM_CTRL_STOP) {
            invalidateSnapshot(address, p[6]);
        }
    }

    // Snapshot first: it has its own buffer, so a full live-stream ring must never
    // block recording the latest state a late-joining client needs to replay.
    if (isSnapshotted(type)) {
        updateSnapshot(address, packet, size, type);
    }

    // Coalesce SET_COLOR: the preview flushes at ~30fps, so only the latest colour per
    // panel in this window matters. Overwrite an existing record for the same panel rather
    // than appending — bounds the ring even if a runner streams per-panel colours.
    if (type == Protocol::PACKET_SET_COLOR) {
        uint16_t off = 0;

        while (off < recordsLen) {
            uint8_t *r   = records() + off;
            uint8_t rsz = r[2];

            if (r[0] == address && r[1] == type && rsz == size) {
                memcpy(&r[RECORD_HEADER], packet, size);

                return;
            }

            off = (uint16_t)(off + RECORD_HEADER + rsz);
        }
    }

    if ((uint16_t)(recordsLen + RECORD_HEADER + size) > RECORDS_CAP) {
        droppedCount++;

        return;
    }

    // Record the capture time. Use the earliest capture time for the whole batch
    // so the MIRROR_BATCH 'controllerMillis' reflects when the first packet was
    // sent, reducing skew between PREPARE and START when the mirror coalesces
    // multiple packets into one flush.
    uint32_t now = millis();

    if (firstRecordMs == 0) firstRecordMs = now;
    else if (now < firstRecordMs) firstRecordMs = now;

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

    // Use the earliest captured record timestamp if available; otherwise use
    // current millis(). This keeps the preview timing closer to the controller's
    // actual send times when multiple packets are coalesced.
    uint32_t now = (firstRecordMs != 0) ? firstRecordMs : millis();

    DEBUG_IF(DEBUG_API, {
        D_PRINTLN("[MIRROR] records sent", recordCount);

        if (droppedCount) {
            D_PRINTLN("[MIRROR] dropped records (buffer full)", droppedCount);
        }
    });

    // Send in chunks of at most FLUSH_CHUNK_CAP record bytes, so a large burst
    // (e.g. all PREPARE/START packets at scene start) is sent as several smaller
    // MIRROR_BATCH frames instead of one large one.
    while (recordCount > 0) {
        uint8_t *r = records();
        uint16_t chunkBytes = 0;
        uint16_t chunkCount = 0;

        while (chunkCount < recordCount) {
            uint8_t recSize = RECORD_HEADER + r[chunkBytes + 2];

            if (chunkBytes > 0 && (uint16_t)(chunkBytes + recSize) > FLUSH_CHUNK_CAP) {
                break;
            }

            chunkBytes += recSize;
            chunkCount++;
        }

        uint8_t *p = payload();

        // memcpy: payload lands at an odd offset (sizeof(PacketMeta)==13), so direct
        // multi-byte stores would be unaligned and fault on ESP8266.
        memcpy(&p[0], &now, sizeof(now));
        memcpy(&p[4], &chunkCount, sizeof(chunkCount));

        uint16_t payloadSize = PAYLOAD_HEADER + chunkBytes;

        WebsocketApi::updatePacketMeta(
            (WebsocketApi::PacketMeta *)frame,
            WebsocketApi::MIRROR_BATCH,
            payloadSize
        );

        server->sendToMirroringClients(frame, sizeof(WebsocketApi::PacketMeta) + payloadSize);

        uint16_t remaining = recordsLen - chunkBytes;

        if (remaining > 0) {
            memmove(records(), records() + chunkBytes, remaining);
        }

        recordsLen  -= chunkBytes;
        recordCount -= chunkCount;
    }

    droppedCount = 0;
    firstRecordMs = 0;

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
