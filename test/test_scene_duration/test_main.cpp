#include <unity.h>
#include <string.h>
#include "Core/Controller/SceneDuration.hpp"
#include "Core/Controller/SceneParser.hpp"

using namespace Lightnet;

static SceneParseResult makeScene(uint16_t d0, uint16_t d1 = 0)
{
    SceneParseResult out = {};

    out.layerCount = 1;
    out.layers[0].stepCount = d1 ? 2 : 1;
    out.layers[0].steps[0].durationMs = d0;

    if (d1) out.layers[0].steps[1].durationMs = d1;

    return out;
}

void test_single_layer_sum()
{
    SceneParseResult s = makeScene(1000, 500);

    TEST_ASSERT_EQUAL_UINT32(1500, computeSceneDurationMs(s));
}

void test_multi_layer_max()
{
    SceneParseResult s = makeScene(1000);

    s.layerCount = 2;
    s.layers[1].stepCount = 1;
    s.layers[1].steps[0].durationMs = 3000;

    TEST_ASSERT_EQUAL_UINT32(3000, computeSceneDurationMs(s));
}

void test_zero_duration_steps()
{
    SceneParseResult s = makeScene(0, 0);

    TEST_ASSERT_EQUAL_UINT32(0, computeSceneDurationMs(s));
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

    RUN_TEST(test_single_layer_sum);
    RUN_TEST(test_multi_layer_max);
    RUN_TEST(test_zero_duration_steps);

    return UNITY_END();
}
