#include <unity.h>
#include <string.h>
#include "Controller/Palettes/PaletteMeta.hpp"

using namespace Lightnet;

void test_palette_meta_round_trip_user()
{
    const char *json = "{\"schemaVersion\":1,\"id\":\"abcd1234\",\"name\":\"My Cool Palette!\"}";
    PaletteMeta meta = {};

    TEST_ASSERT_TRUE(parsePaletteMeta(json, strlen(json), meta));
    TEST_ASSERT_EQUAL_STRING("abcd1234", meta.id);
    TEST_ASSERT_EQUAL_STRING("My Cool Palette!", meta.name);
    TEST_ASSERT_FALSE(meta.builtin);

    char buf[128];
    int n = serializePaletteMeta(meta, buf, sizeof(buf));

    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":\"abcd1234\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"My Cool Palette!\""));
}

void test_palette_meta_builtin()
{
    const char *json = "{\"schemaVersion\":1,\"id\":\"abcd1234\",\"name\":\"Rainbow\",\"builtin\":true}";
    PaletteMeta meta = {};

    TEST_ASSERT_TRUE(parsePaletteMeta(json, strlen(json), meta));
    TEST_ASSERT_TRUE(meta.builtin);
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

    RUN_TEST(test_palette_meta_round_trip_user);
    RUN_TEST(test_palette_meta_builtin);

    return UNITY_END();
}
