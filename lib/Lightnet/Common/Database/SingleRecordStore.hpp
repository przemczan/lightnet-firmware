#pragma once

#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)

    #include "FsStoreCore.hpp"

    namespace Lightnet {
        // Persists exactly one record in a Database file, sharing the binary format used by
        // palettes/scenes. The record always lives in the first slot (RECORDS_START_OFFSET),
        // so there is no id or lookup — load/save target it directly. Used by the
        // single-record config stores (appearance, configuration, app state).
        template<typename Codec>
        class SingleRecordStore
        {
            public:
                typedef typename Codec::Model Model;

                SingleRecordStore(const char *filePath, const char *dirToCreate = nullptr)
                    : _core(filePath, dirToCreate)
                {
                }

                // Loads the stored record into `out`. Returns false if the database has no
                // record yet (or on any error) — the caller keeps its in-memory defaults.
                bool load(Model& out) const
                {
                    Session session(_core);

                    if (!session.isReady() || session.database().liveCount() == 0) return false;

                    RecordRef recordRef{ (uint32_t)RECORDS_START_OFFSET };

                    return session.database().read(recordRef, out, session.scratchBuffer()) == DB_OK;
                }

                // Writes `record` to the single slot, inserting it on first save and replacing
                // it thereafter. Returns true on success.
                bool save(const Model& record)
                {
                    Session session(_core);

                    if (!session.isReady()) return false;

                    if (session.database().liveCount() == 0) {
                        return session.database().insert(
                            record, session.scratchBuffer(), nullptr) == DB_OK;
                    }

                    RecordRef recordRef{ (uint32_t)RECORDS_START_OFFSET };

                    return session.database().replace(
                        recordRef, record, session.scratchBuffer()) == DB_OK;
                }

            private:
                typedef typename FsStoreCore<Codec>::Session Session;

                mutable FsStoreCore<Codec> _core;
        };
    } // namespace Lightnet

#endif
