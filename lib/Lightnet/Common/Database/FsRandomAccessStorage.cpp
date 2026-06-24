#include "FsRandomAccessStorage.hpp"

#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)

    #include "../../Utils/Fs/Fs.hpp"
    #include <Arduino.h>
    #include <string.h>
    #include <stdlib.h>

    namespace Lightnet {
        FsRandomAccessStorage::FsRandomAccessStorage() : _platformFile(nullptr), _seekPosition(0)
        {
            _filePath[0] = '\0';
        }

        FsStorageResult FsRandomAccessStorage::open(const char *filePath, bool readWrite)
        {
            close();

            if (!filePath) return FS_STORAGE_NULL_PATH;

            strncpy(_filePath, filePath, sizeof(_filePath) - 1);
            _filePath[sizeof(_filePath) - 1] = '\0';

            if (!Fs::exists(_filePath)) {
                File createdFile = Fs::open(_filePath, "w");

                if (!createdFile) return FS_STORAGE_CREATE_FAILED;

                createdFile.close();
            }

            return reopen(readWrite ? "r+" : "r") ? FS_STORAGE_OK : FS_STORAGE_OPEN_FAILED;
        }

        void FsRandomAccessStorage::close()
        {
            if (_platformFile) {
                static_cast<File *>(_platformFile)->close();
                delete static_cast<File *>(_platformFile);
                _platformFile = nullptr;
            }

            _seekPosition = 0;
        }

        bool FsRandomAccessStorage::isOpen() const
        {
            return _platformFile != nullptr;
        }

        StorageResult FsRandomAccessStorage::seek(size_t offset)
        {
            if (!_platformFile) return STORAGE_NOT_OPEN;

            _seekPosition = offset;

            return static_cast<File *>(_platformFile)->seek(offset, SeekSet)
            ? STORAGE_OK
            : STORAGE_SEEK_OUT_OF_RANGE;
        }

        StorageResult FsRandomAccessStorage::seekForward(size_t delta)
        {
            if (!_platformFile) return STORAGE_NOT_OPEN;

            File *openFile = static_cast<File *>(_platformFile);

            if (!openFile->seek(delta, SeekCur)) return STORAGE_SEEK_OUT_OF_RANGE;

            _seekPosition = openFile->position();

            return STORAGE_OK;
        }

        size_t FsRandomAccessStorage::read(void *buffer, size_t length)
        {
            if (!_platformFile || !buffer || length == 0) return 0;

            size_t got = static_cast<File *>(_platformFile)->readBytes((char *)buffer, length);

            _seekPosition += got;

            return got;
        }

        size_t FsRandomAccessStorage::write(const void *buffer, size_t length)
        {
            if (!_platformFile || !buffer || length == 0) return 0;

            size_t got = static_cast<File *>(_platformFile)->write((const uint8_t *)buffer, length);

            _seekPosition += got;

            return got;
        }

        size_t FsRandomAccessStorage::size() const
        {
            if (!_platformFile) return 0;

            return static_cast<File *>(_platformFile)->size();
        }

        StorageResult FsRandomAccessStorage::truncate(size_t newSize)
        {
            if (!_platformFile) return STORAGE_NOT_OPEN;

            File *openFile        = static_cast<File *>(_platformFile);
            size_t currentFileSize = openFile->size();

            if (currentFileSize == newSize) {
                return (seek(_seekPosition) == STORAGE_OK) ? STORAGE_OK : STORAGE_TRUNCATE_FAILED;
            }

            if (currentFileSize > newSize) {
                uint8_t *shrinkBuffer = (uint8_t *)malloc(newSize);

                if (!shrinkBuffer) return STORAGE_NO_MEMORY;

                if (!openFile->seek(0, SeekSet) ||
                    openFile->readBytes((char *)shrinkBuffer, newSize) != newSize) {
                    free(shrinkBuffer);

                    return STORAGE_TRUNCATE_FAILED;
                }

                openFile->close();
                delete openFile;
                _platformFile = nullptr;

                File rewriteFile = Fs::open(_filePath, "w");

                if (!rewriteFile) {
                    free(shrinkBuffer);

                    return STORAGE_TRUNCATE_FAILED;
                }

                bool writeSucceeded = rewriteFile.write(shrinkBuffer, newSize) == newSize;

                free(shrinkBuffer);
                rewriteFile.close();

                if (!writeSucceeded) return STORAGE_TRUNCATE_FAILED;

                if (!reopen("r+")) return STORAGE_TRUNCATE_FAILED;

                return seek(_seekPosition);
            }

            if (!openFile->seek(0, SeekEnd)) return STORAGE_TRUNCATE_FAILED;

            size_t grownSize = openFile->position();
            uint8_t zeroByte   = 0;

            while (grownSize < newSize) {
                if (openFile->write(&zeroByte, 1) != 1) return STORAGE_TRUNCATE_FAILED;

                grownSize++;
            }

            return seek(_seekPosition);
        }

        bool FsRandomAccessStorage::reopen(const char *mode)
        {
            File *openedFile = new File(Fs::open(_filePath, mode));

            if (!*openedFile) {
                delete openedFile;

                return false;
            }

            _platformFile  = openedFile;
            _seekPosition  = 0;

            return true;
        }
    } // namespace Lightnet

#endif
