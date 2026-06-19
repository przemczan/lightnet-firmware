#pragma once

#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)

    #include "Database.hpp"
    #include "FsRandomAccessStorage.hpp"
    #include "../../Utils/StoreLock.hpp"
    #include "../../Utils/Fs/Fs.hpp"

    namespace Lightnet {
        // Shared state and lifecycle for filesystem-backed database stores.
        //
        // Each store composes one FsStoreCore<Codec> instance. The nested Session
        // RAII class acquires the lock, opens the backing file if needed, and
        // closes + resets + releases on destruction — keeping one operation per
        // lock window.
        template<typename Codec>
        struct FsStoreCore {
            mutable StoreLock             lock;
            mutable FsRandomAccessStorage storage;
            mutable Database<Codec>       database;
            mutable uint8_t               scratchBuffer[Codec::RECORD_SIZE];

            // Close storage and reset database to default state.
            void reset() const
            {
                storage.close();
                database = Database<Codec>();
            }

            // Open (or create) the database file. Skips if already open.
            // If dirToCreate is provided, the directory is created first.
            // Returns DB_OK on success, DB_STORAGE_OPEN_FAILED if the file
            // cannot be opened, or a DatabaseResult describing the failure.
            DatabaseResult ensureOpen(const char *filePath, const char *dirToCreate = nullptr) const
            {
                if (storage.isOpen()) return DB_OK;

                if (dirToCreate) Fs::mkdir(dirToCreate);

                if (storage.open(filePath, true) != FS_STORAGE_OK) {
                    return DB_STORAGE_OPEN_FAILED;
                }

                database = Database<Codec>();

                DatabaseResult openResult = database.open(storage);

                if (openResult == DB_OK) return DB_OK;

                const bool missingOrCorrupt =
                    (openResult == DB_FILE_TOO_SHORT) ||
                    (openResult == DB_VERSION_READ_FAILED) ||
                    (openResult == DB_HEADER_READ_FAILED) ||
                    (openResult == DB_DB_VERSION_MISMATCH) ||
                    (openResult == DB_MODEL_VERSION_MISMATCH);

                if (!missingOrCorrupt) {
                    storage.close();

                    return openResult;
                }

                DatabaseResult createResult = database.create(storage);

                if (createResult != DB_OK) {
                    storage.close();

                    return createResult;
                }

                return DB_OK;
            }

            class Session
            {
                public:
                    Session(FsStoreCore& core, const char *filePath, const char *dirToCreate = nullptr)
                        : _core(core), _isReady(false)
                    {
                        _core.lock.acquire();
                        _isReady = (_core.ensureOpen(filePath, dirToCreate) == DB_OK);
                    }

                    ~Session()
                    {
                        _core.reset();
                        _core.lock.release();
                    }

                    bool isReady() const
                    {
                        return _isReady;
                    }

                    Database<Codec>& database() const
                    {
                        return _core.database;
                    }

                    uint8_t *scratchBuffer() const
                    {
                        return _core.scratchBuffer;
                    }

                private:
                    FsStoreCore& _core;
                    bool _isReady;
            };
        };
    } // namespace Lightnet

#endif
