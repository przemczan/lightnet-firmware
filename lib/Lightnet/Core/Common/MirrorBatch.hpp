#pragma once

#include "MirrorBatch.h"
#include <string.h>

namespace Lightnet {
    inline uint8_t *mirrorRecordPayload(MirrorRecordHeader *rec)
    {
        return (uint8_t *)rec + MIRROR_RECORD_HEADER_SIZE;
    }

    inline const uint8_t *mirrorRecordPayloadConst(const MirrorRecordHeader *rec)
    {
        return (const uint8_t *)rec + MIRROR_RECORD_HEADER_SIZE;
    }

    inline uint16_t mirrorRecordTotalSize(const MirrorRecordHeader *rec)
    {
        return (uint16_t)(MIRROR_RECORD_HEADER_SIZE + rec->size);
    }

    inline void mirrorRecordWrite(
        MirrorRecordHeader *rec,
        uint8_t             address,
        uint8_t             type,
        uint8_t             size,
        const void *        packet
    )
    {
        rec->address = address;
        rec->type    = type;
        rec->size    = size;
        memcpy(mirrorRecordPayload(rec), packet, size);
    }

    // memcpy: batch header may sit at an odd offset inside the WS frame (ESP8266).
    inline void mirrorBatchWriteHeader(uint8_t *dst, uint32_t controllerMillis, uint16_t count)
    {
        MirrorBatchHeader hdr;

        hdr.controllerMillis = controllerMillis;
        hdr.count            = count;
        memcpy(dst, &hdr, sizeof hdr);
    }

    inline void mirrorBatchReadHeader(const uint8_t *src, MirrorBatchHeader *out)
    {
        memcpy(out, src, sizeof(MirrorBatchHeader));
    }
}  // namespace Lightnet
