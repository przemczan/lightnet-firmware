// Native unit tests for SceneCodec (portable record serialize/deserialize).
// Run with: pio test -e native -f test_scene_codec

#include <unity.h>
#include <cstring>
#include "Controller/Scenes/Store/SceneCodec.hpp"
#include "Core/Controller/ScenePlayer.hpp"

using namespace Lightnet;

void test_scene_record_size_is_reasonable()
{
    TEST_ASSERT_TRUE(sizeof(SceneRecord) < 4096);
    TEST_ASSERT_EQUAL_UINT(SceneCodec::RECORD_SIZE, sizeof(SceneRecord));
}

void test_scene_record_round_trip()
{
    SceneRecord in = {};

    strncpy(in.id, "abcd1234", sizeof(in.id) - 1);
    strncpy(in.name, "Sunset Walk", sizeof(in.name) - 1);
    in.duration       = 1500;
    in.hidden         = 0;
    in.schemaVersion  = SCENE_SCHEMA_VERSION;
    in.layerCount     = 1;
    in.layers[0].groupId   = 1;
    in.layers[0].stepCount = 1;
    in.layers[0].steps[0].animType    = ANIM_SOLID;
    in.layers[0].steps[0].durationMs  = 1500;
    in.layers[0].steps[0].colorTo     = ColorRef_rgb(0xFF, 0, 0);

    uint8_t buf[SceneCodec::RECORD_SIZE];
    SceneRecord out = {};

    TEST_ASSERT_EQUAL_UINT8(SCENE_CODEC_OK, SceneCodec::serialize(in, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8(SCENE_CODEC_OK, SceneCodec::deserialize(buf, sizeof(buf), out));
    TEST_ASSERT_EQUAL_STRING(in.id, out.id);
    TEST_ASSERT_EQUAL_STRING(in.name, out.name);
    TEST_ASSERT_EQUAL_UINT32(in.duration, out.duration);
    TEST_ASSERT_EQUAL_UINT8(in.layerCount, out.layerCount);
}

void test_scene_record_rejects_empty_name()
{
    SceneRecord bad = {};

    strncpy(bad.id, "abcd1234", sizeof(bad.id) - 1);
    bad.layerCount = 1;

    uint8_t buf[SceneCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(SCENE_CODEC_INVALID, SceneCodec::serialize(bad, buf, sizeof(buf)));
}

void test_scene_record_rejects_long_name()
{
    SceneRecord bad = {};

    strncpy(bad.id, "abcd1234", sizeof(bad.id) - 1);
    memset(bad.name, 'a', SCENE_NAME_MAX);
    bad.name[SCENE_NAME_MAX]     = 'b';
    bad.name[SCENE_NAME_MAX + 1] = '\0';
    bad.layerCount = 1;

    uint8_t buf[SceneCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(SCENE_CODEC_INVALID, SceneCodec::serialize(bad, buf, sizeof(buf)));
}

void test_scene_codec_record_id_matches()
{
    SceneRecord in = {};

    strncpy(in.id, "abcd1234", sizeof(in.id) - 1);
    strncpy(in.name, "Test", sizeof(in.name) - 1);
    in.layerCount = 1;

    uint8_t buf[SceneCodec::RECORD_SIZE];

    TEST_ASSERT_EQUAL_UINT8(SCENE_CODEC_OK, SceneCodec::serialize(in, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(SceneCodec::recordIdMatches(buf, sizeof(buf), "abcd1234"));
    TEST_ASSERT_FALSE(SceneCodec::recordIdMatches(buf, sizeof(buf), "zzzz9999"));
    TEST_ASSERT_FALSE(SceneCodec::recordIdMatches(buf, 4, "abcd1234"));
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

    RUN_TEST(test_scene_record_size_is_reasonable);
    RUN_TEST(test_scene_record_round_trip);
    RUN_TEST(test_scene_record_rejects_empty_name);
    RUN_TEST(test_scene_record_rejects_long_name);
    RUN_TEST(test_scene_codec_record_id_matches);

    return UNITY_END();
}
