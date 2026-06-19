#pragma once

#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)

    #include "IRandomAccessStorage.hpp"

    namespace Lightnet {
        enum FsStorageResult : uint8_t {
            FS_STORAGE_OK            = 0,
            FS_STORAGE_NULL_PATH     = 1,
            FS_STORAGE_CREATE_FAILED = 2,
            FS_STORAGE_OPEN_FAILED   = 3,
        };

        class FsRandomAccessStorage : public IRandomAccessStorage
        {
            public:
                FsRandomAccessStorage();

                FsStorageResult open(const char *filePath, bool readWrite);
                void            close();
                bool            isOpen() const;

                StorageResult seek(size_t offset) override;
                size_t        read(void *buffer, size_t length) override;
                size_t        write(const void *buffer, size_t length) override;
                size_t        size() const override;
                StorageResult truncate(size_t newSize) override;

            private:
                char _filePath[128];
                void *_platformFile;
                size_t _seekPosition;

                bool reopen(const char *mode);
        };
    } // namespace Lightnet

#endif
