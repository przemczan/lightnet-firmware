// Native unit tests for lib/Lightnet/Utils/SimpleJson.hpp
// Run with: pio test -e native -f test_simplejson

#include <unity.h>
#include <string.h>
#include "Utils/SimpleJson.hpp"

using namespace Lightnet;

// ---------------------------------------------------------------------------
// jsonFindKey — the regression that bit us (depth-0 search ignored everything
// inside the outer object). These tests would have caught it instantly.
// ---------------------------------------------------------------------------

void test_findKey_finds_key_inside_object()
{
    const char *json = "{\"name\":\"foo\",\"value\":42}";
    const char *p = jsonFindKey(json, strlen(json), "name");

    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_CHAR('"', *p);
}

void test_findKey_finds_second_key()
{
    const char *json = "{\"name\":\"foo\",\"value\":42}";
    const char *p = jsonFindKey(json, strlen(json), "value");

    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_CHAR('4', *p);
}

void test_findKey_returns_null_for_missing_key()
{
    const char *json = "{\"name\":\"foo\"}";

    TEST_ASSERT_NULL(jsonFindKey(json, strlen(json), "missing"));
}

void test_findKey_skips_keys_inside_nested_objects()
{
    const char *json = "{\"outer\":{\"name\":\"hidden\"},\"name\":\"found\"}";
    const char *p = jsonFindKey(json, strlen(json), "name");

    TEST_ASSERT_NOT_NULL(p);
    // Should land on "found", not "hidden"
    TEST_ASSERT_EQUAL_STRING_LEN("\"found\"", p, 7);
}

void test_findKey_tolerates_whitespace_around_outer_brace()
{
    const char *json = "  \n  { \"name\":\"foo\" }";

    TEST_ASSERT_NOT_NULL(jsonFindKey(json, strlen(json), "name"));
}

void test_findKey_tolerates_whitespace_around_colon()
{
    const char *json = "{\"name\"  :  \"foo\"}";
    const char *p = jsonFindKey(json, strlen(json), "name");

    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_CHAR('"', *p);
}

// ---------------------------------------------------------------------------
// SimpleJson accessor class — depends on jsonFindKey being correct.
// ---------------------------------------------------------------------------

void test_getInt_returns_integer()
{
    const char *json = "{\"value\":255}";
    SimpleJson j(json, strlen(json));

    TEST_ASSERT_EQUAL_INT(255, j.getInt("value"));
}

void test_getInt_returns_negative_one_for_missing()
{
    const char *json = "{\"value\":255}";
    SimpleJson j(json, strlen(json));

    TEST_ASSERT_EQUAL_INT(-1, j.getInt("missing"));
}

void test_getString_copies_string_value()
{
    const char *json = "{\"name\":\"mypalette\"}";
    SimpleJson j(json, strlen(json));
    char out[20];

    TEST_ASSERT_TRUE(j.getString("name", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("mypalette", out);
}

void test_hasKey_true_when_present()
{
    const char *json = "{\"a\":1,\"b\":2}";
    SimpleJson j(json, strlen(json));

    TEST_ASSERT_TRUE(j.hasKey("a"));
    TEST_ASSERT_TRUE(j.hasKey("b"));
    TEST_ASSERT_FALSE(j.hasKey("c"));
}

// ---------------------------------------------------------------------------
// Hex color parsing
// ---------------------------------------------------------------------------

void test_parseHexColor_valid()
{
    uint8_t r, g, b;

    TEST_ASSERT_TRUE(jsonParseHexColor("#FF8040", 7, &r, &g, &b));
    TEST_ASSERT_EQUAL_UINT8(0xFF, r);
    TEST_ASSERT_EQUAL_UINT8(0x80, g);
    TEST_ASSERT_EQUAL_UINT8(0x40, b);
}

void test_parseHexColor_lowercase()
{
    uint8_t r, g, b;

    TEST_ASSERT_TRUE(jsonParseHexColor("#aabbcc", 7, &r, &g, &b));
    TEST_ASSERT_EQUAL_UINT8(0xAA, r);
}

void test_parseHexColor_rejects_missing_hash()
{
    uint8_t r, g, b;

    TEST_ASSERT_FALSE(jsonParseHexColor("FF0000", 6, &r, &g, &b));
}

void test_parseHexColor_rejects_wrong_length()
{
    uint8_t r, g, b;

    TEST_ASSERT_FALSE(jsonParseHexColor("#FFF", 4, &r, &g, &b));
}

// ---------------------------------------------------------------------------
// Cursor-based iterators (used by parsePaletteJson and SceneParser)
// ---------------------------------------------------------------------------

void test_enterObject_advances_past_brace()
{
    const char *json = "  { \"a\":1 }";
    const char *p = json;

    TEST_ASSERT_TRUE(jsonEnterObject(p, json + strlen(json)));
    // p should be just after '{'
    TEST_ASSERT_EQUAL_CHAR(' ', *p);
}

void test_nextKey_iterates_top_level_keys()
{
    const char *json = "{\"a\":1,\"b\":2,\"c\":3}";
    const char *p = json;
    const char *end = json + strlen(json);

    TEST_ASSERT_TRUE(jsonEnterObject(p, end));

    char key[8];
    int count = 0;

    while (jsonNextKey(p, end, key, sizeof(key))) {
        count++;
        jsonSkipValue(p, end);
    }

    TEST_ASSERT_EQUAL_INT(3, count);
}

void test_skipValue_handles_nested_array()
{
    const char *json = "{\"a\":[[1,2],[3,4]],\"b\":99}";
    const char *p = json;
    const char *end = json + strlen(json);

    TEST_ASSERT_TRUE(jsonEnterObject(p, end));

    char key[8];

    TEST_ASSERT_TRUE(jsonNextKey(p, end, key, sizeof(key)));
    TEST_ASSERT_EQUAL_STRING("a", key);
    TEST_ASSERT_TRUE(jsonSkipValue(p, end));

    // After skipping "a"'s nested array, we should land on "b".
    TEST_ASSERT_TRUE(jsonNextKey(p, end, key, sizeof(key)));
    TEST_ASSERT_EQUAL_STRING("b", key);
}

void test_readFloat_parses_decimal()
{
    const char *json = "2.5";
    const char *p = json;
    float out;

    TEST_ASSERT_TRUE(jsonReadFloat(p, json + strlen(json), &out));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, out);
}

void test_readFloat_parses_negative()
{
    const char *json = "-1.25";
    const char *p = json;
    float out;

    TEST_ASSERT_TRUE(jsonReadFloat(p, json + strlen(json), &out));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.25f, out);
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_findKey_finds_key_inside_object);
    RUN_TEST(test_findKey_finds_second_key);
    RUN_TEST(test_findKey_returns_null_for_missing_key);
    RUN_TEST(test_findKey_skips_keys_inside_nested_objects);
    RUN_TEST(test_findKey_tolerates_whitespace_around_outer_brace);
    RUN_TEST(test_findKey_tolerates_whitespace_around_colon);

    RUN_TEST(test_getInt_returns_integer);
    RUN_TEST(test_getInt_returns_negative_one_for_missing);
    RUN_TEST(test_getString_copies_string_value);
    RUN_TEST(test_hasKey_true_when_present);

    RUN_TEST(test_parseHexColor_valid);
    RUN_TEST(test_parseHexColor_lowercase);
    RUN_TEST(test_parseHexColor_rejects_missing_hash);
    RUN_TEST(test_parseHexColor_rejects_wrong_length);

    RUN_TEST(test_enterObject_advances_past_brace);
    RUN_TEST(test_nextKey_iterates_top_level_keys);
    RUN_TEST(test_skipValue_handles_nested_array);
    RUN_TEST(test_readFloat_parses_decimal);
    RUN_TEST(test_readFloat_parses_negative);

    return UNITY_END();
}
