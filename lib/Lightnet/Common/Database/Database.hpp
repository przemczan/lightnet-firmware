#pragma once

#include "DatabaseFormat.hpp"
#include "IRandomAccessStorage.hpp"
#include "IRecordCodec.hpp"
#include <stdint.h>
#include <string.h>

namespace Lightnet {
    enum DatabaseResult : uint8_t {
        DB_OK                    = 0,
        DB_NOT_OPEN              = 1,
        DB_NULL_ARG              = 2,
        DB_TRUNCATE_FAILED       = 3,
        DB_VERSION_READ_FAILED   = 4,
        DB_DB_VERSION_MISMATCH   = 5,
        DB_HEADER_READ_FAILED    = 6,
        DB_MODEL_VERSION_MISMATCH = 7,
        DB_HEADER_WRITE_FAILED   = 8,
        DB_VERSION_WRITE_FAILED  = 9,
        DB_FILE_TOO_SHORT        = 10,
        DB_SEEK_FAILED           = 11,
        DB_READ_FAILED           = 12,
        DB_WRITE_FAILED          = 13,
        DB_RECORD_DELETED        = 14,
        DB_SERIALIZE_FAILED      = 15,
        DB_DESERIALIZE_FAILED    = 16,
        DB_FOREACH_ABORTED       = 17,
        DB_NO_LIVE_RECORDS       = 18,
        DB_STORAGE_OPEN_FAILED   = 19,
    };

    template <typename Codec>
    class Database
    {
        public:
            typedef typename Codec::Model Model;

            Database() : _backingStorage(nullptr), _liveRecordCount(0), _isOpen(false)
            {
            }

            bool isOpen() const
            {
                return _isOpen;
            }

            uint16_t liveCount() const
            {
                return _liveRecordCount;
            }

            DatabaseResult create(IRandomAccessStorage& backingStorage)
            {
                _backingStorage = &backingStorage;

                if (backingStorage.truncate(RECORDS_START_OFFSET) != STORAGE_OK) {
                    return DB_TRUNCATE_FAILED;
                }

                DatabaseFormatResult versionWriteResult = writeVersion(backingStorage, DB_VERSION);

                if (versionWriteResult != DB_FORMAT_OK) {
                    return DB_VERSION_WRITE_FAILED;
                }

                DatabaseHeader header;

                header.recordModelVersion = Codec::MODEL_VERSION;
                header.recordsCount       = 0;

                DatabaseFormatResult headerWriteResult = writeHeader(backingStorage, header);

                if (headerWriteResult != DB_FORMAT_OK) {
                    return DB_HEADER_WRITE_FAILED;
                }

                _liveRecordCount = 0;
                _isOpen          = true;

                return DB_OK;
            }

            DatabaseResult open(IRandomAccessStorage& backingStorage)
            {
                _backingStorage = &backingStorage;

                if (backingStorage.size() < RECORDS_START_OFFSET) {
                    return DB_FILE_TOO_SHORT;
                }

                DatabaseFilePrefix filePrefix;
                DatabaseFormatResult versionReadResult = readVersion(backingStorage, filePrefix);

                if (versionReadResult != DB_FORMAT_OK) {
                    return DB_VERSION_READ_FAILED;
                }

                if (filePrefix.dbVersion != DB_VERSION) {
                    return DB_DB_VERSION_MISMATCH;
                }

                DatabaseHeader header;
                DatabaseFormatResult headerReadResult = readHeader(backingStorage, header);

                if (headerReadResult != DB_FORMAT_OK) {
                    return DB_HEADER_READ_FAILED;
                }

                if (header.recordModelVersion != Codec::MODEL_VERSION) {
                    return DB_MODEL_VERSION_MISMATCH;
                }

                _liveRecordCount = header.recordsCount;
                _isOpen          = true;

                return DB_OK;
            }

            // Iterates live (non-deleted) records without buffering payloads.
            // For each live record, seeks the backing storage to payloadOffset
            // and invokes callback(RecordRef, IRandomAccessStorage&, payloadOffset, payloadSize).
            // The storage cursor is positioned at payloadOffset when the callback fires.
            // payloadSize equals Codec::RECORD_SIZE. Callback must complete all reads
            // before returning. Returns DB_FOREACH_ABORTED if callback returns non-DB_OK.
            template <typename Callback>
            DatabaseResult foreachLive(Callback callback) const
            {
                if (!_isOpen || !_backingStorage) return DB_NOT_OPEN;

                const size_t recordSlotByteSize = recordSlotSize();
                const size_t fileSize           = _backingStorage->size();

                if (fileSize < RECORDS_START_OFFSET) return DB_FILE_TOO_SHORT;

                for (size_t slotOffset = RECORDS_START_OFFSET;
                     slotOffset + recordSlotByteSize <= fileSize;
                     slotOffset += recordSlotByteSize) {
                    uint8_t slotFlags = 0;

                    DatabaseResult readResult = readSlotFlagsAt(slotOffset, slotFlags);

                    if (readResult != DB_OK) return readResult;

                    if (slotFlags & FLAG_DELETED) continue;

                    const size_t payloadOffset = slotOffset + 1;
                    RecordRef recordRef     = { (uint32_t)slotOffset };

                    if (_backingStorage->seek(payloadOffset) != STORAGE_OK) return DB_SEEK_FAILED;

                    if (callback(recordRef, *_backingStorage, payloadOffset, Codec::RECORD_SIZE) != DB_OK) {
                        return DB_FOREACH_ABORTED;
                    }
                }

                return DB_OK;
            }

            DatabaseResult read(RecordRef recordRef, Model& record, uint8_t *scratchBuffer) const
            {
                if (!_isOpen || !_backingStorage) return DB_NOT_OPEN;

                if (!scratchBuffer) return DB_NULL_ARG;

                uint8_t slotFlags = 0;

                DatabaseResult readResult = readSlotFlagsAt(recordRef.offset, slotFlags);

                if (readResult != DB_OK) return readResult;

                if (slotFlags & FLAG_DELETED) return DB_RECORD_DELETED;

                readResult = readRecordPayloadAt(recordRef.offset, scratchBuffer);

                if (readResult != DB_OK) return readResult;

                if (Codec::deserialize(scratchBuffer, Codec::RECORD_SIZE, record) != 0) {
                    return DB_DESERIALIZE_FAILED;
                }

                return DB_OK;
            }

            DatabaseResult insert(const Model& record, uint8_t *scratchBuffer, RecordRef *outRecordRef)
            {
                if (!_isOpen || !_backingStorage) return DB_NOT_OPEN;

                if (!scratchBuffer) return DB_NULL_ARG;

                if (Codec::serialize(record, scratchBuffer, Codec::RECORD_SIZE) != 0) {
                    return DB_SERIALIZE_FAILED;
                }

                const size_t recordSlotByteSize = recordSlotSize();
                const size_t fileSize           = _backingStorage->size();
                size_t targetSlotOffset   = 0;
                bool reuseTombstone     = false;

                if (fileSize >= RECORDS_START_OFFSET) {
                    for (size_t slotOffset = RECORDS_START_OFFSET;
                         slotOffset + recordSlotByteSize <= fileSize;
                         slotOffset += recordSlotByteSize) {
                        uint8_t slotFlags = 0;

                        DatabaseResult readResult = readSlotFlagsAt(slotOffset, slotFlags);

                        if (readResult != DB_OK) return readResult;

                        if (slotFlags & FLAG_DELETED) {
                            targetSlotOffset = slotOffset;
                            reuseTombstone   = true;
                            break;
                        }
                    }
                }

                DatabaseResult writeResult;

                if (!reuseTombstone) {
                    targetSlotOffset = (fileSize >= RECORDS_START_OFFSET) ? fileSize
                                                                          : RECORDS_START_OFFSET;
                    writeResult = appendRecordSlot(targetSlotOffset, scratchBuffer);
                } else {
                    writeResult = writeLiveRecordSlot(targetSlotOffset, scratchBuffer);
                }

                if (writeResult != DB_OK) return writeResult;

                _liveRecordCount++;

                writeResult = persistLiveRecordCount();

                if (writeResult != DB_OK) return writeResult;

                if (outRecordRef) outRecordRef->offset = (uint32_t)targetSlotOffset;

                return DB_OK;
            }

            DatabaseResult replace(RecordRef recordRef, const Model& record, uint8_t *scratchBuffer)
            {
                if (!_isOpen || !_backingStorage) return DB_NOT_OPEN;

                if (!scratchBuffer) return DB_NULL_ARG;

                uint8_t slotFlags = 0;

                DatabaseResult readResult = readSlotFlagsAt(recordRef.offset, slotFlags);

                if (readResult != DB_OK) return readResult;

                if (slotFlags & FLAG_DELETED) return DB_RECORD_DELETED;

                if (Codec::serialize(record, scratchBuffer, Codec::RECORD_SIZE) != 0) {
                    return DB_SERIALIZE_FAILED;
                }

                return writeRecordPayloadAt(recordRef.offset, scratchBuffer);
            }

            DatabaseResult remove(RecordRef recordRef)
            {
                if (!_isOpen || !_backingStorage) return DB_NOT_OPEN;

                if (_liveRecordCount == 0) return DB_NO_LIVE_RECORDS;

                uint8_t slotFlags = 0;

                DatabaseResult readResult = readSlotFlagsAt(recordRef.offset, slotFlags);

                if (readResult != DB_OK) return readResult;

                if (slotFlags & FLAG_DELETED) return DB_RECORD_DELETED;

                slotFlags |= FLAG_DELETED;

                DatabaseResult writeResult = writeSlotFlagsAt(recordRef.offset, slotFlags);

                if (writeResult != DB_OK) return writeResult;

                _liveRecordCount--;

                return persistLiveRecordCount();
            }

        private:
            IRandomAccessStorage *_backingStorage;
            uint16_t _liveRecordCount;
            bool _isOpen;

            static size_t recordSlotSize()
            {
                return 1 + Codec::RECORD_SIZE;
            }

            DatabaseResult persistLiveRecordCount()
            {
                DatabaseHeader header;

                header.recordModelVersion = Codec::MODEL_VERSION;
                header.recordsCount       = _liveRecordCount;

                DatabaseFormatResult headerWriteResult = writeHeader(*_backingStorage, header);

                if (headerWriteResult != DB_FORMAT_OK) {
                    return DB_HEADER_WRITE_FAILED;
                }

                return DB_OK;
            }

            DatabaseResult readSlotFlagsAt(size_t slotOffset, uint8_t& slotFlagsOut) const
            {
                if (_backingStorage->seek(slotOffset) != STORAGE_OK) return DB_SEEK_FAILED;

                if (_backingStorage->read(&slotFlagsOut, 1) != 1) return DB_READ_FAILED;

                return DB_OK;
            }

            DatabaseResult writeSlotFlagsAt(size_t slotOffset, uint8_t slotFlags)
            {
                if (_backingStorage->seek(slotOffset) != STORAGE_OK) return DB_SEEK_FAILED;

                if (_backingStorage->write(&slotFlags, 1) != 1) return DB_WRITE_FAILED;

                return DB_OK;
            }

            DatabaseResult readRecordPayloadAt(size_t slotOffset, uint8_t *recordBuffer) const
            {
                if (_backingStorage->seek(slotOffset + 1) != STORAGE_OK) return DB_SEEK_FAILED;

                if (_backingStorage->read(recordBuffer, Codec::RECORD_SIZE) != Codec::RECORD_SIZE) {
                    return DB_READ_FAILED;
                }

                return DB_OK;
            }

            DatabaseResult writeRecordPayloadAt(size_t slotOffset, const uint8_t *recordBuffer)
            {
                if (_backingStorage->seek(slotOffset + 1) != STORAGE_OK) return DB_SEEK_FAILED;

                if (_backingStorage->write(recordBuffer, Codec::RECORD_SIZE) != Codec::RECORD_SIZE) {
                    return DB_WRITE_FAILED;
                }

                return DB_OK;
            }

            DatabaseResult writeLiveRecordSlot(size_t slotOffset, const uint8_t *recordBuffer)
            {
                const uint8_t liveSlotFlags = 0;

                if (_backingStorage->seek(slotOffset) != STORAGE_OK) return DB_SEEK_FAILED;

                if (_backingStorage->write(&liveSlotFlags, 1) != 1) return DB_WRITE_FAILED;

                return writeRecordPayloadAt(slotOffset, recordBuffer);
            }

            DatabaseResult appendRecordSlot(size_t slotOffset, const uint8_t *recordBuffer)
            {
                const size_t requiredFileSize = slotOffset + recordSlotSize();

                if (_backingStorage->size() < requiredFileSize &&
                    _backingStorage->truncate(requiredFileSize) != STORAGE_OK) {
                    return DB_TRUNCATE_FAILED;
                }

                return writeLiveRecordSlot(slotOffset, recordBuffer);
            }
    };
}  // namespace Lightnet
