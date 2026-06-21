// Native unit tests for the single-record config codecs (appearance / configuration /
// app state) and the fixed-slot Database round-trip used by SingleRecordStore.
// Run with: pio test -e native -f test_config_codecs

#include <unity.h>
#include <cstring>
#include "Common/Database/Database.hpp"
#include "../test_database/MemoryRandomAccessStorage.hpp"
#include "Controller/Appearance/Store/AppearanceCodec.hpp"
#include "Controller/Configuration/Store/ConfigurationCodec.hpp"
#include "Controller/AppState/Store/AppStateCodec.hpp"

using namespace Lightnet;

// Mirrors SingleRecordStore: insert on first save, replace thereafter, read at fixed slot.
template<typename Codec>
static typename Codec::Model roundTrip(const typename Codec::Model& in)
{
    MemoryRandomAccessStorage storage;
    Database<Codec> database;

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.create(storage));

    uint8_t scratch[Codec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.insert(in, scratch, nullptr));

    RecordRef recordRef{ (uint32_t)RECORDS_START_OFFSET };
    typename Codec::Model out{};

    TEST_ASSERT_EQUAL_UINT8(DB_OK, database.read(recordRef, out, scratch));

    return out;
}

void test_appearance_round_trip()
{
    AppearanceRecord in{};

    in.brightness    = 123;
    in.baseColors[0] = { 0x11, 0x22, 0x33 };
    in.baseColors[1] = { 0x44, 0x55, 0x66 };
    in.baseColors[2] = { 0x77, 0x88, 0x99 };
    strcpy(in.palette, "sunset");

    AppearanceRecord out = roundTrip<AppearanceCodec>(in);

    TEST_ASSERT_EQUAL_UINT8(123, out.brightness);
    TEST_ASSERT_EQUAL_UINT8(0x22, out.baseColors[0].g);
    TEST_ASSERT_EQUAL_UINT8(0x99, out.baseColors[2].b);
    TEST_ASSERT_EQUAL_STRING("sunset", out.palette);
}

void test_appearance_deserialize_terminates_palette()
{
    uint8_t buffer[AppearanceCodec::RECORD_SIZE];

    memset(buffer, 'A', sizeof(buffer)); // unterminated palette field

    AppearanceRecord out{};

    TEST_ASSERT_EQUAL_UINT8(APPEARANCE_CODEC_OK,
                            AppearanceCodec::deserialize(buffer, sizeof(buffer), out));
    TEST_ASSERT_EQUAL_size_t(MAX_PALETTE_NAME_LENGTH, strlen(out.palette));
}

void test_configuration_round_trip()
{
    ConfigurationRecord in{ POWER_LAST_STATE };
    ConfigurationRecord out = roundTrip<ConfigurationCodec>(in);

    TEST_ASSERT_EQUAL_UINT8(POWER_LAST_STATE, out.powerStateOnBoot);
}

void test_configuration_rejects_out_of_range()
{
    ConfigurationRecord in{ 99 };
    uint8_t buffer[ConfigurationCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(CONFIGURATION_CODEC_INVALID,
                            ConfigurationCodec::serialize(in, buffer, sizeof(buffer)));
}

void test_app_state_round_trip()
{
    AppStateRecord in{};

    in.isOn = 0;
    strcpy(in.lastPlayedSceneId, "abc123");
    in.lastPlayedSceneIsStored = 1;

    AppStateRecord out = roundTrip<AppStateCodec>(in);

    TEST_ASSERT_EQUAL_UINT8(0, out.isOn);
    TEST_ASSERT_EQUAL_STRING("abc123", out.lastPlayedSceneId);
    TEST_ASSERT_EQUAL_UINT8(1, out.lastPlayedSceneIsStored);
}

void test_app_state_deserialize_terminates_id()
{
    uint8_t buffer[AppStateCodec::RECORD_SIZE];

    memset(buffer, 'Z', sizeof(buffer)); // unterminated id field

    AppStateRecord out{};

    TEST_ASSERT_EQUAL_UINT8(APP_STATE_CODEC_OK,
                            AppStateCodec::deserialize(buffer, sizeof(buffer), out));
    TEST_ASSERT_EQUAL_size_t(ENTRY_ID_MAX, strlen(out.lastPlayedSceneId));
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

    RUN_TEST(test_appearance_round_trip);
    RUN_TEST(test_appearance_deserialize_terminates_palette);
    RUN_TEST(test_configuration_round_trip);
    RUN_TEST(test_configuration_rejects_out_of_range);
    RUN_TEST(test_app_state_round_trip);
    RUN_TEST(test_app_state_deserialize_terminates_id);

    return UNITY_END();
}
