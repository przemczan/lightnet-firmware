#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    class IRandomAccessStorage;

    static constexpr uint8_t DB_VERSION = 1;

    static constexpr uint8_t FLAG_DELETED = 0x01;

    enum DatabaseFormatResult : uint8_t {
        DB_FORMAT_OK             = 0,
        DB_FORMAT_FILE_TOO_SHORT = 1,
        DB_FORMAT_SEEK_FAILED    = 2,
        DB_FORMAT_READ_FAILED    = 3,
        DB_FORMAT_WRITE_FAILED   = 4,
    };

    struct DatabaseFilePrefix {
        uint8_t dbVersion;
    };

    static_assert(sizeof(DatabaseFilePrefix) == 1, "DatabaseFilePrefix must stay 1 byte at offset 0");

    struct DatabaseHeader {
        uint8_t  recordModelVersion;
        uint16_t recordsCount;
    } __attribute__((packed));

    static_assert(sizeof(DatabaseHeader) == 3, "DatabaseHeader wire layout");

    static constexpr size_t RECORDS_START_OFFSET =
        sizeof(DatabaseFilePrefix) + sizeof(DatabaseHeader);

    struct RecordRef {
        uint32_t offset;
    };

    DatabaseFormatResult readVersion(IRandomAccessStorage& storage, DatabaseFilePrefix& prefixOut);
    DatabaseFormatResult readHeader(IRandomAccessStorage& storage, DatabaseHeader& headerOut);
    DatabaseFormatResult writeHeader(IRandomAccessStorage& storage, const DatabaseHeader& header);
    DatabaseFormatResult writeVersion(IRandomAccessStorage& storage, uint8_t databaseVersion);
}  // namespace Lightnet
