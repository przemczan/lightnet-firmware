// Native round-trip tests: parse → serialize → parse.
// Run with: pio test -e native -f test_scene_writer

#include <unity.h>
#include <string.h>
#include "Core/Controller/SceneParser.hpp"
#include "Core/Controller/SceneWriter.hpp"

using namespace Lightnet;

static const char *SOLID_SCENE =
    R"({
  "name": "host",
  "loop": false,
  "colors": { "primary": "#FF0000", "secondary": "#00FF00", "tertiary": "#0000FF" },
  "layers": [
    { "group": 1, "panels": "all", "sequence": [
        { "type": "SOLID", "color": "#FF0000", "duration": 1000 }
    ] }
  ]
})";

void test_parse_serialize_parse_round_trip()
{
    SceneRecord a = {};
    char errA[64];

    TEST_ASSERT_TRUE(parseScene(SOLID_SCENE, strlen(SOLID_SCENE), a, errA, sizeof(errA)));

    char json[4096];
    int n = serializeScene(a, json, sizeof(json));

    TEST_ASSERT_TRUE(n > 0);

    SceneRecord b = {};
    char errB[64];

    TEST_ASSERT_TRUE(parseScene(json, (size_t)n, b, errB, sizeof(errB)));
    TEST_ASSERT_EQUAL_STRING(a.name, b.name);
    TEST_ASSERT_EQUAL_UINT8(a.layerCount, b.layerCount);
    TEST_ASSERT_EQUAL_UINT8(a.layers[0].stepCount, b.layers[0].stepCount);
    TEST_ASSERT_EQUAL_UINT16(a.layers[0].steps[0].durationMs, b.layers[0].steps[0].durationMs);
}

void test_round_trip_scene_name_with_quote()
{
    const char *json =
        "{\"name\":\"test\\\"\",\"layers\":[{\"group\":1,\"panels\":\"all\",\"sequence\":"
        "[{\"type\":\"SOLID\",\"color\":\"#FF0000\",\"duration\":1000}]}]}";

    SceneRecord a = {};
    char errA[64];

    TEST_ASSERT_TRUE(parseScene(json, strlen(json), a, errA, sizeof(errA)));
    TEST_ASSERT_EQUAL_STRING("test\"", a.name);

    char out[4096];
    int n = serializeScene(a, out, sizeof(out));

    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"test\\\"\""));

    SceneRecord b = {};
    char errB[64];

    TEST_ASSERT_TRUE(parseScene(out, (size_t)n, b, errB, sizeof(errB)));
    TEST_ASSERT_EQUAL_STRING("test\"", b.name);
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

    RUN_TEST(test_parse_serialize_parse_round_trip);
    RUN_TEST(test_round_trip_scene_name_with_quote);

    return UNITY_END();
}
