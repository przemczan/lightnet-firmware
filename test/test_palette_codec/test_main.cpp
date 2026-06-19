// Native unit tests for PaletteCodec (portable record serialize/deserialize).
// Run with: pio test -e native -f test_palette_codec

#include <unity.h>
#include <cstring>
#include "Controller/Palettes/Store/PaletteCodec.hpp"

using namespace Lightnet;

void test_palette_record_round_trip()
{
    PaletteRecord in = {};

    strncpy(in.name, "Sunset", sizeof(in.name) - 1);
    in.builtin    = false;
    in.stopsCount = 2;
    in.stops[0]   = { 0, 0x10, 0, 0x40 };
    in.stops[1]   = { 255, 0xFF, 0xE0, 0x40 };

    uint8_t buf[PaletteCodec::RECORD_SIZE];
    PaletteRecord out = {};

    TEST_ASSERT_EQUAL_UINT8(PALETTE_CODEC_OK, PaletteCodec::serialize(in, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8(PALETTE_CODEC_OK, PaletteCodec::deserialize(buf, sizeof(buf), out));
    TEST_ASSERT_EQUAL_STRING(in.name, out.name);
    TEST_ASSERT_EQUAL_UINT8(in.builtin, out.builtin);
    TEST_ASSERT_EQUAL_UINT8(in.stopsCount, out.stopsCount);
    TEST_ASSERT_EQUAL_UINT8(in.stops[0].r, out.stops[0].r);
    TEST_ASSERT_EQUAL_UINT8(in.stops[1].b, out.stops[1].b);
}

void test_palette_record_round_trip_builtin()
{
    PaletteRecord in = {};

    strncpy(in.name, "Rainbow", sizeof(in.name) - 1);
    in.builtin    = true;
    in.stopsCount = 1;
    in.stops[0]   = { 0, 0xFF, 0, 0 };

    uint8_t buf[PaletteCodec::RECORD_SIZE];
    PaletteRecord out = {};

    TEST_ASSERT_EQUAL_UINT8(PALETTE_CODEC_OK, PaletteCodec::serialize(in, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8(PALETTE_CODEC_OK, PaletteCodec::deserialize(buf, sizeof(buf), out));
    TEST_ASSERT_EQUAL_STRING("Rainbow", out.name);
    TEST_ASSERT_TRUE(out.builtin);
}

void test_palette_record_rejects_invalid()
{
    PaletteRecord bad = {};

    strncpy(bad.name, "Bad", sizeof(bad.name) - 1);
    bad.stopsCount = 0;

    uint8_t buf[PaletteCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(PALETTE_CODEC_INVALID_STOPS, PaletteCodec::serialize(bad, buf, sizeof(buf)));
}

void test_palette_record_rejects_empty_name()
{
    PaletteRecord bad = {};

    bad.name[0]   = '\0';
    bad.stopsCount = 1;
    bad.stops[0]   = { 0, 0x10, 0, 0x40 };

    uint8_t buf[PaletteCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(PALETTE_CODEC_INVALID_NAME, PaletteCodec::serialize(bad, buf, sizeof(buf)));
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

    RUN_TEST(test_palette_record_round_trip);
    RUN_TEST(test_palette_record_round_trip_builtin);
    RUN_TEST(test_palette_record_rejects_invalid);
    RUN_TEST(test_palette_record_rejects_empty_name);

    return UNITY_END();
}
