// Native unit tests for lib/Lightnet/Controller/Palettes/PaletteJson.hpp
// Run with: pio test -e native -f test_palette_parser

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "Controller/Palettes/PaletteJson.hpp"

using namespace Lightnet;

static GradientStop stops[PALETTE_STOPS];
static uint8_t count;
static char name[20];

// ---------------------------------------------------------------------------
// Happy paths
// ---------------------------------------------------------------------------

void test_parses_minimal_palette()
{
    const char *json = "{\"stops\":[[0,\"#FF0000\"],[255,\"#0000FF\"]]}";

    TEST_ASSERT_TRUE(parsePaletteJson(json, strlen(json), stops, count));
    TEST_ASSERT_EQUAL_UINT8(2, count);
    TEST_ASSERT_EQUAL_UINT8(0, stops[0].pos);
    TEST_ASSERT_EQUAL_HEX8(0xFF, stops[0].r);
    TEST_ASSERT_EQUAL_HEX8(0x00, stops[0].g);
    TEST_ASSERT_EQUAL_HEX8(0x00, stops[0].b);
    TEST_ASSERT_EQUAL_UINT8(255, stops[1].pos);
    TEST_ASSERT_EQUAL_HEX8(0x00, stops[1].r);
    TEST_ASSERT_EQUAL_HEX8(0x00, stops[1].g);
    TEST_ASSERT_EQUAL_HEX8(0xFF, stops[1].b);
}

void test_parses_palette_with_name()
{
    const char *json = "{\"schemaVersion\":1,\"name\":\"mypalette\","
                       "\"stops\":[[0,\"#000000\"],[128,\"#FF4400\"],[255,\"#FFFFFF\"]]}";

    TEST_ASSERT_TRUE(parsePaletteJson(json, strlen(json), stops, count, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("mypalette", name);
    TEST_ASSERT_EQUAL_UINT8(3, count);
    TEST_ASSERT_EQUAL_UINT8(128, stops[1].pos);
    TEST_ASSERT_EQUAL_HEX8(0xFF, stops[1].r);
    TEST_ASSERT_EQUAL_HEX8(0x44, stops[1].g);
    TEST_ASSERT_EQUAL_HEX8(0x00, stops[1].b);
}

void test_parses_palette_with_whitespace()
{
    // Pretty-printed JSON — the bug that originally bit us.
    const char *json = "{\n"
                       "  \"schemaVersion\": 1,\n"
                       "  \"name\": \"pretty\",\n"
                       "  \"stops\": [\n"
                       "    [0, \"#000000\"],\n"
                       "    [255, \"#FFFFFF\"]\n"
                       "  ]\n"
                       "}";

    TEST_ASSERT_TRUE(parsePaletteJson(json, strlen(json), stops, count, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("pretty", name);
    TEST_ASSERT_EQUAL_UINT8(2, count);
}

void test_parses_with_name_after_stops()
{
    // Key order shouldn't matter.
    const char *json = "{\"stops\":[[0,\"#000000\"],[255,\"#FFFFFF\"]],\"name\":\"reverse\"}";

    TEST_ASSERT_TRUE(parsePaletteJson(json, strlen(json), stops, count, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("reverse", name);
    TEST_ASSERT_EQUAL_UINT8(2, count);
}

void test_ignores_unknown_keys()
{
    const char *json = "{\"schemaVersion\":1,\"extra\":\"junk\",\"name\":\"x\","
                       "\"stops\":[[0,\"#000000\"],[255,\"#FFFFFF\"]]}";

    TEST_ASSERT_TRUE(parsePaletteJson(json, strlen(json), stops, count, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("x", name);
}

// ---------------------------------------------------------------------------
// Failure modes
// ---------------------------------------------------------------------------

void test_rejects_empty_input()
{
    TEST_ASSERT_FALSE(parsePaletteJson("", 0, stops, count));
    TEST_ASSERT_FALSE(parsePaletteJson(nullptr, 0, stops, count));
}

void test_rejects_missing_stops()
{
    const char *json = "{\"name\":\"foo\"}";

    TEST_ASSERT_FALSE(parsePaletteJson(json, strlen(json), stops, count));
}

void test_rejects_empty_stops_array()
{
    const char *json = "{\"stops\":[]}";

    TEST_ASSERT_FALSE(parsePaletteJson(json, strlen(json), stops, count));
}

void test_rejects_position_out_of_range()
{
    const char *json = "{\"stops\":[[0,\"#000000\"],[256,\"#FFFFFF\"]]}";

    TEST_ASSERT_FALSE(parsePaletteJson(json, strlen(json), stops, count));
}

void test_rejects_bad_hex_color()
{
    const char *json = "{\"stops\":[[0,\"red\"],[255,\"#FFFFFF\"]]}";

    TEST_ASSERT_FALSE(parsePaletteJson(json, strlen(json), stops, count));
}

void test_rejects_stop_pair_with_extra_element()
{
    const char *json = "{\"stops\":[[0,\"#000000\",99],[255,\"#FFFFFF\"]]}";

    TEST_ASSERT_FALSE(parsePaletteJson(json, strlen(json), stops, count));
}

void test_rejects_missing_name_when_requested()
{
    // Caller passed outName → name is required.
    const char *json = "{\"stops\":[[0,\"#000000\"],[255,\"#FFFFFF\"]]}";

    TEST_ASSERT_FALSE(parsePaletteJson(json, strlen(json), stops, count, name, sizeof(name)));
}

void test_accepts_missing_name_when_not_requested()
{
    // Caller passed nullptr → name not required (used by PaletteStore::resolve).
    const char *json = "{\"stops\":[[0,\"#000000\"],[255,\"#FFFFFF\"]]}";

    TEST_ASSERT_TRUE(parsePaletteJson(json, strlen(json), stops, count));
}

void test_caps_at_palette_stops_max()
{
    // PALETTE_STOPS is 16 — feed it 20 stops and ensure no overflow.
    char json[1024];
    int n = snprintf(json, sizeof(json), "{\"stops\":[");

    for (int i = 0; i < 20; i++) {
        n += snprintf(json + n, sizeof(json) - n, "%s[%d,\"#FF0000\"]", i ? "," : "", i * 12);
    }

    snprintf(json + n, sizeof(json) - n, "]}");

    TEST_ASSERT_TRUE(parsePaletteJson(json, strlen(json), stops, count));
    TEST_ASSERT_EQUAL_UINT8(PALETTE_STOPS, count);
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

void setUp(void)
{
    count = 0;
    memset(stops, 0, sizeof(stops));
    memset(name, 0, sizeof(name));
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_parses_minimal_palette);
    RUN_TEST(test_parses_palette_with_name);
    RUN_TEST(test_parses_palette_with_whitespace);
    RUN_TEST(test_parses_with_name_after_stops);
    RUN_TEST(test_ignores_unknown_keys);

    RUN_TEST(test_rejects_empty_input);
    RUN_TEST(test_rejects_missing_stops);
    RUN_TEST(test_rejects_empty_stops_array);
    RUN_TEST(test_rejects_position_out_of_range);
    RUN_TEST(test_rejects_bad_hex_color);
    RUN_TEST(test_rejects_stop_pair_with_extra_element);
    RUN_TEST(test_rejects_missing_name_when_requested);
    RUN_TEST(test_accepts_missing_name_when_not_requested);
    RUN_TEST(test_caps_at_palette_stops_max);

    return UNITY_END();
}
