#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    enum StorageResult : uint8_t {
        STORAGE_OK               = 0,
        STORAGE_NOT_OPEN           = 1,
        STORAGE_SEEK_OUT_OF_RANGE  = 2,
        STORAGE_TRUNCATE_FAILED    = 3,
        STORAGE_NO_MEMORY          = 4,
    };

    class IRandomAccessStorage
    {
        public:
            virtual ~IRandomAccessStorage()
            {
            }

            virtual StorageResult seek(size_t offset)              = 0;
            virtual size_t     read(void *buf, size_t len)      = 0;
            virtual size_t     write(const void *buf, size_t len) = 0;
            virtual size_t     size() const = 0;
            virtual StorageResult truncate(size_t newSize)         = 0;
    };
}  // namespace Lightnet
