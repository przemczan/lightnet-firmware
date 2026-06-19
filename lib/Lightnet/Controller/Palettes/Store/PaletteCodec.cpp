#include "PaletteCodec.hpp"
#include <string.h>

namespace Lightnet {
    namespace {
        bool isValidRecord(const PaletteRecord& record)
        {
            if (!isValidPaletteName(record.name)) return false;

            if (record.stopsCount == 0 || record.stopsCount > PALETTE_STOPS) return false;

            return true;
        }
    } // anonymous namespace

    uint8_t PaletteCodec::serialize(const PaletteRecord& record, uint8_t *buffer, size_t capacity)
    {
        if (capacity < RECORD_SIZE) return PALETTE_CODEC_BUF_TOO_SMALL;

        if (!isValidRecord(record)) {
            if (!isValidPaletteName(record.name)) return PALETTE_CODEC_INVALID_NAME;

            return PALETTE_CODEC_INVALID_STOPS;
        }

        memset(buffer, 0, capacity);
        memcpy(buffer, &record, RECORD_SIZE);

        return PALETTE_CODEC_OK;
    }

    uint8_t PaletteCodec::deserialize(const uint8_t *buffer, size_t length, PaletteRecord& out)
    {
        if (length < RECORD_SIZE) return PALETTE_CODEC_BUF_TOO_SMALL;

        memcpy(&out, buffer, RECORD_SIZE);
        out.name[MAX_PALETTE_NAME_LENGTH] = '\0';

        if (!isValidRecord(out)) {
            if (!isValidPaletteName(out.name)) return PALETTE_CODEC_INVALID_NAME;

            return PALETTE_CODEC_INVALID_STOPS;
        }

        return PALETTE_CODEC_OK;
    }
}  // namespace Lightnet
