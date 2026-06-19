#pragma once

#include "IContentReader.hpp"
#include "../../Common/Database/IRandomAccessStorage.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Lightnet {
    // Bounded IContentReader over a slice of an IRandomAccessStorage.
    // Seeks to `offset` on each read call (safe to use after the storage
    // cursor was moved by another operation between reads).
    class StorageSliceReader : public IContentReader
    {
        public:
            StorageSliceReader(IRandomAccessStorage& storage, size_t offset, size_t size)
                : _storage(storage), _nextOffset(offset), _remaining(size)
            {
            }

            int read(uint8_t *buf, size_t cap) override
            {
                if (_remaining == 0) return 0;

                size_t n = (_remaining < cap) ? _remaining : cap;

                if (_storage.seek(_nextOffset) != STORAGE_OK) return -1;

                size_t got = _storage.read(buf, n);

                if (got == 0) return -1;

                _nextOffset += got;
                _remaining  -= got;

                return (int)got;
            }

        private:
            IRandomAccessStorage& _storage;
            size_t _nextOffset;
            size_t _remaining;
    };
}  // namespace Lightnet
