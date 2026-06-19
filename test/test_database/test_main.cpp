// Native unit tests for lib/Lightnet/Common/Database/
// Run with: pio test -e native -f test_database

#include <unity.h>
#include <vector>
#include <cstring>
#include "Common/Database/Database.hpp"
#include "MemoryRandomAccessStorage.hpp"

using namespace Lightnet;

struct TestModel {
    uint8_t a;
    uint8_t b;
};

enum TestCodecResult : uint8_t {
    TEST_CODEC_OK        = 0,
    TEST_CODEC_BUF_SMALL = 1,
    TEST_CODEC_CORRUPT   = 2,
};

struct TestCodec {
    typedef TestModel Model;

    static constexpr uint8_t MODEL_VERSION = 1;
    static constexpr size_t  RECORD_SIZE     = 8;

    static uint8_t serialize(const TestModel& record, uint8_t *buffer, size_t capacity)
    {
        if (capacity < RECORD_SIZE) return TEST_CODEC_BUF_SMALL;

        memset(buffer, 0, capacity);
        buffer[0] = record.a;
        buffer[1] = record.b;

        return TEST_CODEC_OK;
    }

    static uint8_t deserialize(const uint8_t *buffer, size_t length, TestModel& recordOut)
    {
        if (length < 2) return TEST_CODEC_CORRUPT;

        recordOut.a = buffer[0];
        recordOut.b = buffer[1];

        return TEST_CODEC_OK;
    }
};

typedef Database<TestCodec> TestDatabase;

static TestModel makeTestModel(uint8_t a, uint8_t b)
{
    TestModel record;

    record.a = a;
    record.b = b;

    return record;
}

void test_create_and_open()
{
    MemoryRandomAccessStorage backingStorage;
    TestDatabase database;
    uint8_t scratchBuffer[TestCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.create(backingStorage));
    TEST_ASSERT_TRUE(database.isOpen());
    TEST_ASSERT_EQUAL_UINT16(0, database.liveCount());

    TestDatabase reopenedDatabase;

    TEST_ASSERT_EQUAL_UINT8(DB_OK, reopenedDatabase.open(backingStorage));
    TEST_ASSERT_EQUAL_UINT16(0, reopenedDatabase.liveCount());

    RecordRef insertedRecordRef;
    TestModel insertedRecord = makeTestModel(1, 2);

    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.insert(insertedRecord, scratchBuffer, &insertedRecordRef));
    TEST_ASSERT_EQUAL_UINT16(1, database.liveCount());

    TestModel readRecord = {};

    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.read(insertedRecordRef, readRecord, scratchBuffer));
    TEST_ASSERT_EQUAL_UINT8(1, readRecord.a);
    TEST_ASSERT_EQUAL_UINT8(2, readRecord.b);
}

void test_insert_foreach_and_replace()
{
    MemoryRandomAccessStorage backingStorage;
    TestDatabase database;
    uint8_t scratchBuffer[TestCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.create(backingStorage));

    RecordRef recordRefs[3];

    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.insert(makeTestModel(10, 11), scratchBuffer, &recordRefs[0]));
    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.insert(makeTestModel(20, 21), scratchBuffer, &recordRefs[1]));
    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.insert(makeTestModel(30, 31), scratchBuffer, &recordRefs[2]));
    TEST_ASSERT_EQUAL_UINT16(3, database.liveCount());

    std::vector<TestModel> seenRecords;

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.foreachLive(
                                [&](RecordRef, const uint8_t *recordBuffer) {
        TestModel record = {};

        TestCodec::deserialize(recordBuffer, TestCodec::RECORD_SIZE, record);
        seenRecords.push_back(record);

        return DB_OK;
    }));

    TEST_ASSERT_EQUAL(size_t(3), seenRecords.size());
    TEST_ASSERT_EQUAL_UINT8(10, seenRecords[0].a);
    TEST_ASSERT_EQUAL_UINT8(20, seenRecords[1].a);
    TEST_ASSERT_EQUAL_UINT8(30, seenRecords[2].a);

    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.replace(recordRefs[1], makeTestModel(55, 56), scratchBuffer));

    TestModel replacedRecord = {};

    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.read(recordRefs[1], replacedRecord, scratchBuffer));
    TEST_ASSERT_EQUAL_UINT8(55, replacedRecord.a);
    TEST_ASSERT_EQUAL_UINT8(56, replacedRecord.b);
}

void test_delete_reuses_tombstone()
{
    MemoryRandomAccessStorage backingStorage;
    TestDatabase database;
    uint8_t scratchBuffer[TestCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.create(backingStorage));

    RecordRef firstRecordRef;
    RecordRef secondRecordRef;

    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.insert(makeTestModel(1, 1), scratchBuffer, &firstRecordRef));
    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.insert(makeTestModel(2, 2), scratchBuffer, &secondRecordRef));
    TEST_ASSERT_EQUAL_UINT16(2, database.liveCount());

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.remove(firstRecordRef));
    TEST_ASSERT_EQUAL_UINT16(1, database.liveCount());

    TestModel deletedRecord = {};

    TEST_ASSERT_EQUAL_UINT8(
        DB_RECORD_DELETED, database.read(firstRecordRef, deletedRecord, scratchBuffer));

    RecordRef reusedRecordRef;

    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.insert(makeTestModel(9, 9), scratchBuffer, &reusedRecordRef));
    TEST_ASSERT_EQUAL_UINT32(firstRecordRef.offset, reusedRecordRef.offset);
    TEST_ASSERT_EQUAL_UINT16(2, database.liveCount());

    TestModel insertedRecord = {};

    TEST_ASSERT_EQUAL_UINT8(
        DB_OK, database.read(reusedRecordRef, insertedRecord, scratchBuffer));
    TEST_ASSERT_EQUAL_UINT8(9, insertedRecord.a);
}

void test_version_mismatch_rejected()
{
    MemoryRandomAccessStorage backingStorage;

    TEST_ASSERT_EQUAL(STORAGE_OK, backingStorage.truncate(RECORDS_START_OFFSET));
    TEST_ASSERT_EQUAL_UINT8(DB_FORMAT_OK, writeVersion(backingStorage, 99));

    DatabaseHeader header;

    header.recordModelVersion = TestCodec::MODEL_VERSION;
    header.recordsCount       = 0;
    TEST_ASSERT_EQUAL_UINT8(DB_FORMAT_OK, writeHeader(backingStorage, header));

    TestDatabase database;

    TEST_ASSERT_EQUAL_UINT8(DB_DB_VERSION_MISMATCH, database.open(backingStorage));
}

void test_model_version_mismatch_rejected()
{
    MemoryRandomAccessStorage backingStorage;

    TEST_ASSERT_EQUAL(STORAGE_OK, backingStorage.truncate(RECORDS_START_OFFSET));
    TEST_ASSERT_EQUAL_UINT8(DB_FORMAT_OK, writeVersion(backingStorage, DB_VERSION));

    DatabaseHeader header;

    header.recordModelVersion = 99;
    header.recordsCount       = 0;
    TEST_ASSERT_EQUAL_UINT8(DB_FORMAT_OK, writeHeader(backingStorage, header));

    TestDatabase database;

    TEST_ASSERT_EQUAL_UINT8(DB_MODEL_VERSION_MISMATCH, database.open(backingStorage));
}

void test_read_version_helper()
{
    MemoryRandomAccessStorage backingStorage;
    TestDatabase database;

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.create(backingStorage));

    DatabaseFilePrefix filePrefix = {};

    TEST_ASSERT_EQUAL_UINT8(DB_FORMAT_OK, readVersion(backingStorage, filePrefix));
    TEST_ASSERT_EQUAL_UINT8(DB_VERSION, filePrefix.dbVersion);
}

void test_truncated_file_rejected()
{
    MemoryRandomAccessStorage backingStorage;

    TEST_ASSERT_EQUAL(STORAGE_OK, backingStorage.truncate(2));

    TestDatabase database;

    TEST_ASSERT_EQUAL_UINT8(DB_FILE_TOO_SHORT, database.open(backingStorage));
}

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_create_and_open);
    RUN_TEST(test_insert_foreach_and_replace);
    RUN_TEST(test_delete_reuses_tombstone);
    RUN_TEST(test_version_mismatch_rejected);
    RUN_TEST(test_model_version_mismatch_rejected);
    RUN_TEST(test_read_version_helper);
    RUN_TEST(test_truncated_file_rejected);

    return UNITY_END();
}
