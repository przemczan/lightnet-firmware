#include <unity.h>
#include <string.h>
#include "Controller/Scenes/SceneMeta.hpp"

using namespace Lightnet;

void test_scene_meta_round_trip()
{
    const char *json = "{\"schemaVersion\":1,\"id\":\"abcd1234\",\"name\":\"Sunset Walk\",\"layersNum\":2,\"duration\":15000}";
    SceneMeta meta = {};

    TEST_ASSERT_TRUE(parseSceneMeta(json, strlen(json), meta));
    TEST_ASSERT_EQUAL_STRING("abcd1234", meta.id);
    TEST_ASSERT_EQUAL_STRING("Sunset Walk", meta.name);
    TEST_ASSERT_EQUAL_UINT8(2, meta.layersNum);
    TEST_ASSERT_EQUAL_UINT32(15000, meta.duration);

    char buf[192];
    int n = serializeSceneMeta(meta, buf, sizeof(buf));

    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"layersNum\":2"));
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

    RUN_TEST(test_scene_meta_round_trip);

    return UNITY_END();
}
