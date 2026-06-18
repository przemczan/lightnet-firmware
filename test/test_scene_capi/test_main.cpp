// Host test for the scene-engine C ABI (controller_core_c.h).
//
// Drives the engine purely through the flat C surface the mobile app binds to (create ->
// set_sink -> set_topology -> load_and_play -> tick), capturing the emitted wire packets via the
// sink callback. Proves the ABI compiles and round-trips on the host with no hardware and no
// cmake (the CMake build path mirrors the already-proven anim_core recipe).
//
// Run with: pio test -e native -f test_scene_capi

#include <unity.h>
#include <string.h>
#include "Core/CApi/controller_core_c.h"
#include "Core/Common/MirrorBatch.h"

// ProtocolTypes.hpp ids: ANIMATION_PREPARE = 12, ANIMATION_START = 13, SET_BACKGROUND = 20.
static int prepareCount;
static int startCount;
static int backgroundCount;
static int totalCount;

static void onPacket(void *user, uint8_t address, uint8_t type, const uint8_t *bytes, uint8_t len)
{
    (void)user;
    (void)address;
    (void)bytes;
    (void)len;
    totalCount++;

    if (type == 12) prepareCount++;
    else if (type == 13) startCount++;
    else if (type == 20) backgroundCount++;
}

static void resetCounts()
{
    prepareCount = startCount = backgroundCount = totalCount = 0;
}

static scene_handle makeCore()
{
    scene_handle h = scene_create();

    scene_set_sink(h, onPacket, nullptr);

    // 3-panel line: 1 -- 2 -- 3 (square panels), rooted at 1.
    const uint8_t indices[3]    = { 1, 2, 3 };
    const uint8_t edgeCounts[3] = { 4, 4, 4 };
    const uint8_t links[8]      = { 1, 0, 2, 2, 2, 0, 3, 2 };

    scene_set_topology(h, indices, 3, links, 2, edgeCounts, 1);

    return h;
}

static const char *SOLID_SCENE =
    R"({
  "name": "capi", "loop": false,
  "layers": [ { "group": 1, "panels": "all", "sequence": [
    { "type": "SOLID", "color": "#FF0000", "duration": 1000 }
  ] } ]
})";

void test_capi_load_and_play_emits_packets()
{
    resetCounts();

    scene_handle h = makeCore();

    int ok = scene_load_and_play(h, SOLID_SCENE, (int)strlen(SOLID_SCENE), 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, ok, scene_last_error(h));
    TEST_ASSERT_EQUAL_INT(3, prepareCount);   // one PREPARE per resolved panel
    TEST_ASSERT_TRUE(startCount >= 1);        // general-call START (double-sent)
    TEST_ASSERT_EQUAL_INT(1, backgroundCount);
    TEST_ASSERT_EQUAL_INT(1, scene_is_playing(h));

    scene_destroy(h);
}

void test_capi_rejects_bad_json()
{
    resetCounts();

    scene_handle h = makeCore();

    int ok = scene_load_and_play(h, "{ not json", 10, 0);

    TEST_ASSERT_EQUAL_INT(0, ok);
    TEST_ASSERT_TRUE(strlen(scene_last_error(h)) > 0);
    TEST_ASSERT_EQUAL_INT(0, scene_is_playing(h));

    scene_destroy(h);
}

void test_capi_stop_emits_control()
{
    resetCounts();

    scene_handle h = makeCore();

    scene_load_and_play(h, SOLID_SCENE, (int)strlen(SOLID_SCENE), 0);

    int before = totalCount;

    scene_stop(h, 0);

    TEST_ASSERT_EQUAL_INT(0, scene_is_playing(h));
    TEST_ASSERT_TRUE(totalCount > before);  // STOP broadcast went through the sink

    scene_destroy(h);
}

// scene_drain returns the emitted packets as a MIRROR_BATCH payload the mobile bindings decode.
void test_capi_drain_returns_mirror_batch()
{
    resetCounts();

    scene_handle h = makeCore();

    scene_load_and_play(h, SOLID_SCENE, (int)strlen(SOLID_SCENE), 0);

    uint8_t buf[512];
    int len = scene_drain(h, buf, sizeof(buf));

    TEST_ASSERT_TRUE(len > (int)MIRROR_BATCH_HEADER_SIZE);

    MirrorBatchHeader hdr;

    memcpy(&hdr, buf, sizeof hdr);
    TEST_ASSERT_EQUAL_INT(totalCount, hdr.count);
    TEST_ASSERT_EQUAL_INT((int)MIRROR_BATCH_HEADER_SIZE, scene_drain(h, buf, sizeof(buf)));

    scene_destroy(h);
}

void setUp()
{
}

void tearDown()
{
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_capi_load_and_play_emits_packets);
    RUN_TEST(test_capi_rejects_bad_json);
    RUN_TEST(test_capi_stop_emits_control);
    RUN_TEST(test_capi_drain_returns_mirror_batch);

    return UNITY_END();
}
