// Native unit tests for lib/Lightnet/Core/Controller/Scene/PanelField.hpp
// Run with: pio test -e native -f test_panel_field
//
// Worked topology (docs/animations/scene-authoring.md Â§2), rooted at panel 1.

#include <unity.h>
#include <string.h>
#include "Core/Controller/Scene/PanelGraph.hpp"
#include "Core/Controller/Scene/PanelField.hpp"

using namespace Lightnet;

static const uint8_t PANELS[] = { 1, 2, 3, 4, 5, 6 };

static const TopoLink LINKS[] = {
    { 1, 0, 2, 0 },
    { 1, 1, 3, 0 },
    { 2, 1, 4, 0 },
    { 3, 1, 5, 0 },
    { 5, 1, 6, 0 },
};

static PanelGraph graph;
static TopologyIndex topo;

// Resolve the field over all 6 panels and compare coord[] + maxCoord.
static void checkField(
    uint8_t        kind,
    uint8_t        arg,
    bool           reverse,
    const uint8_t *expectCoord,
    uint8_t        expectMax
)
{
    uint8_t coord[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeDistanceField(topo, PANELS, 6, kind, arg, reverse, coord);

    TEST_ASSERT_EQUAL_UINT8(expectMax, maxC);

    for (uint8_t i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(expectCoord[i], coord[i]);
}

void test_source_root()
{
    uint8_t e[] = { 0, 1, 1, 2, 2, 3 }; // hop distance from panel 1

    checkField(SRC_ROOT, 0, false, e, 3);
}

void test_source_leaves()
{
    uint8_t e[] = { 2, 1, 2, 0, 1, 0 }; // distance to nearest leaf (4 or 6)

    checkField(SRC_LEAVES, 0, false, e, 2);
}

void test_source_panel()
{
    uint8_t e[] = { 1, 2, 0, 3, 1, 2 }; // hop distance from panel 3

    checkField(SRC_PANEL, 3, false, e, 3);
}

void test_source_all_is_uniform()
{
    uint8_t e[] = { 0, 0, 0, 0, 0, 0 };

    checkField(SRC_ALL, 0, false, e, 0);
}

void test_reverse_flips_field()
{
    // root field [0,1,1,2,2,3] reversed about maxCoord 3 â†’ [3,2,2,1,1,0]
    uint8_t e[] = { 3, 2, 2, 1, 1, 0 };

    checkField(SRC_ROOT, 0, true, e, 3);
}

void test_missing_panel_falls_back_to_root()
{
    // SRC_PANEL naming a panel that doesn't exist â†’ empty source â†’ root fallback
    uint8_t e[] = { 0, 1, 1, 2, 2, 3 };

    checkField(SRC_PANEL, 99, false, e, 3);
}

void test_maxcoord_is_over_targeted_subset()
{
    // Target only leaves {4,6}; from root their coords are 2 and 3 â†’ maxCoord 3.
    static const uint8_t subset[] = { 4, 6 };
    uint8_t coord[LIGHTNET_MAX_PANELS];
    uint8_t maxC = computeDistanceField(topo, subset, 2, SRC_ROOT, 0, false, coord);

    TEST_ASSERT_EQUAL_UINT8(3, maxC);
    TEST_ASSERT_EQUAL_UINT8(2, coord[0]);
    TEST_ASSERT_EQUAL_UINT8(3, coord[1]);
}

void setUp(void)
{
    graph.build(PANELS, 6, LINKS, 5);
    topo.build(graph, 1);
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_source_root);
    RUN_TEST(test_source_leaves);
    RUN_TEST(test_source_panel);
    RUN_TEST(test_source_all_is_uniform);
    RUN_TEST(test_reverse_flips_field);
    RUN_TEST(test_missing_panel_falls_back_to_root);
    RUN_TEST(test_maxcoord_is_over_targeted_subset);

    return UNITY_END();
}
