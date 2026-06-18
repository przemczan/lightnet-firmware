#pragma once
/* MirrorBatch.h — wire layout of a MIRROR_BATCH WebSocket payload (and scene_drain output).
 *
 * After the outer WebsocketApi::PacketMeta header, the payload is:
 *   MirrorBatchHeader
 *   count × MirrorRecordHeader + packet bytes (full Protocol::PacketMeta wire packet)
 *
 * Shared by PacketMirror, controller_core_c, mobile bindings, and tools/api-shell.
 * Field order is part of the on-wire contract — change here only with a version bump.
 */

#include <stdint.h>

#ifdef __cplusplus
    extern "C" {
#endif

#if defined(_MSC_VER)
    #pragma pack(push, 1)
#endif

typedef struct __attribute__((__packed__)) {
    uint8_t address; /* I²C target; 0 = general call */
    uint8_t type;    /* Protocol::packetType_t */
    uint8_t size;    /* byte length of the following wire packet */
} MirrorRecordHeader;

typedef struct __attribute__((__packed__)) {
    uint32_t controllerMillis; /* LE; millis() at flush / scene tick */
    uint16_t count;            /* LE; number of records following */
} MirrorBatchHeader;

#if defined(_MSC_VER)
    #pragma pack(pop)
#endif

#define MIRROR_RECORD_HEADER_SIZE ((uint16_t)sizeof(MirrorRecordHeader))
#define MIRROR_BATCH_HEADER_SIZE  ((uint16_t)sizeof(MirrorBatchHeader))

#ifdef __cplusplus
    }
#endif
