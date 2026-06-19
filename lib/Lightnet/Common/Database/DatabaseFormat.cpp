#include "DatabaseFormat.hpp"
#include "IRandomAccessStorage.hpp"

namespace Lightnet {
    DatabaseFormatResult readVersion(IRandomAccessStorage& storage, DatabaseFilePrefix& prefixOut)
    {
        if (storage.size() < sizeof(DatabaseFilePrefix)) return DB_FORMAT_FILE_TOO_SHORT;

        if (storage.seek(0) != STORAGE_OK) return DB_FORMAT_SEEK_FAILED;

        if (storage.read(&prefixOut, sizeof(prefixOut)) != sizeof(prefixOut)) {
            return DB_FORMAT_READ_FAILED;
        }

        return DB_FORMAT_OK;
    }

    DatabaseFormatResult readHeader(IRandomAccessStorage& storage, DatabaseHeader& headerOut)
    {
        if (storage.size() < RECORDS_START_OFFSET) return DB_FORMAT_FILE_TOO_SHORT;

        if (storage.seek(sizeof(DatabaseFilePrefix)) != STORAGE_OK) return DB_FORMAT_SEEK_FAILED;

        if (storage.read(&headerOut, sizeof(headerOut)) != sizeof(headerOut)) {
            return DB_FORMAT_READ_FAILED;
        }

        return DB_FORMAT_OK;
    }

    DatabaseFormatResult writeHeader(IRandomAccessStorage& storage, const DatabaseHeader& header)
    {
        if (storage.seek(sizeof(DatabaseFilePrefix)) != STORAGE_OK) return DB_FORMAT_SEEK_FAILED;

        if (storage.write(&header, sizeof(header)) != sizeof(header)) {
            return DB_FORMAT_WRITE_FAILED;
        }

        return DB_FORMAT_OK;
    }

    DatabaseFormatResult writeVersion(IRandomAccessStorage& storage, uint8_t databaseVersion)
    {
        DatabaseFilePrefix filePrefix = { databaseVersion };

        if (storage.seek(0) != STORAGE_OK) return DB_FORMAT_SEEK_FAILED;

        if (storage.write(&filePrefix, sizeof(filePrefix)) != sizeof(filePrefix)) {
            return DB_FORMAT_WRITE_FAILED;
        }

        return DB_FORMAT_OK;
    }
}  // namespace Lightnet
