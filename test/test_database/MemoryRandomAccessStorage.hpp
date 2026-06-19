#pragma once

#include "Common/Database/IRandomAccessStorage.hpp"
#include <vector>
#include <cstring>

namespace Lightnet {
    class MemoryRandomAccessStorage : public IRandomAccessStorage
    {
        public:
            MemoryRandomAccessStorage() : _seekPosition(0)
            {
            }

            void reset()
            {
                _fileBytes.clear();
                _seekPosition = 0;
            }

            const std::vector<uint8_t>& fileBytes() const
            {
                return _fileBytes;
            }

            StorageResult seek(size_t offset) override
            {
                if (offset > _fileBytes.size()) return STORAGE_SEEK_OUT_OF_RANGE;

                _seekPosition = offset;

                return STORAGE_OK;
            }

            size_t read(void *buffer, size_t length) override
            {
                if (!buffer || length == 0) return 0;

                if (_seekPosition >= _fileBytes.size()) return 0;

                size_t bytesAvailable = _fileBytes.size() - _seekPosition;
                size_t bytesToRead    = (length < bytesAvailable) ? length : bytesAvailable;

                memcpy(buffer, _fileBytes.data() + _seekPosition, bytesToRead);
                _seekPosition += bytesToRead;

                return bytesToRead;
            }

            size_t write(const void *buffer, size_t length) override
            {
                if (!buffer || length == 0) return 0;

                if (_seekPosition + length > _fileBytes.size()) {
                    _fileBytes.resize(_seekPosition + length, 0);
                }

                memcpy(_fileBytes.data() + _seekPosition, buffer, length);
                _seekPosition += length;

                return length;
            }

            size_t size() const override
            {
                return _fileBytes.size();
            }

            StorageResult truncate(size_t newSize) override
            {
                _fileBytes.resize(newSize, 0);

                if (_seekPosition > newSize) _seekPosition = newSize;

                return STORAGE_OK;
            }

        private:
            std::vector<uint8_t> _fileBytes;
            size_t _seekPosition;
    };
}  // namespace Lightnet
