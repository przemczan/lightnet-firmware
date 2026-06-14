// Native unit tests for lib/Lightnet/Core/Controller/Scene/TopologyIndex.hpp
// Run with: pio test -e native -f test_topology
//
// Fixture is the "worked topology" from docs/animations/scene-authoring.md Â§2:
//
//             [1]  root            depth 0
//            /   \
//         [2]     [3]              depth 1
//         /         \
//       [4]         [5]            depth 2
//                     \
//                     [6]          depth 3
//
// Connector indices are chosen so the canonical (DFS, connector-order) traversal
// is 1, 2, 4, 3, 5, 6 â€” matching the doc.

#include <unity.h>
#include <string.h>
#include "Core/Controller/Scene/PanelGraph.hpp"
#include "Core/Controller/Scene/TopologyIndex.hpp"

using namespace Lightnet;

static const uint8_t PANELS[] = { 1, 2, 3, 4, 5, 6 };

static const TopoLink LINKS[] = {
    { 1, 0, 2, 0 },  // root edge0 â†’ 2
    { 1, 1, 3, 0 },  // root edge1 â†’ 3
    { 2, 1, 4, 0 },
    { 3, 1, 5, 0 },
    { 5, 1, 6, 0 },
};

static PanelGraph graph;
static TopologyIndex topo;

static uint8_t slot(uint8_t panelIndex)
{
    uint8_t s = 0xFF;

    topo.slotOf(panelIndex, s);

    return s;
}

static void buildWorked(uint8_t root)
{
    TEST_ASSERT_TRUE(graph.build(PANELS, 6, LINKS, 5));
    TEST_ASSERT_TRUE(topo.build(graph, root));
}

// ---------------------------------------------------------------------------
// Rooted at the physical root (panel 1)
// ---------------------------------------------------------------------------

void test_count_and_slots()
{
    buildWorked(1);

    TEST_ASSERT_EQUAL_UINT8(6, topo.count());
    TEST_ASSERT_EQUAL_UINT8(1, topo.panelAt(slot(1)));
    TEST_ASSERT_EQUAL_UINT8(6, topo.panelAt(slot(6)));
    TEST_ASSERT_EQUAL_UINT8(slot(1), topo.root());
}

void test_depth_root1()
{
    buildWorked(1);

    TEST_ASSERT_EQUAL_UINT8(0, topo.depthOf(slot(1)));
    TEST_ASSERT_EQUAL_UINT8(1, topo.depthOf(slot(2)));
    TEST_ASSERT_EQUAL_UINT8(1, topo.depthOf(slot(3)));
    TEST_ASSERT_EQUAL_UINT8(2, topo.depthOf(slot(4)));
    TEST_ASSERT_EQUAL_UINT8(2, topo.depthOf(slot(5)));
    TEST_ASSERT_EQUAL_UINT8(3, topo.depthOf(slot(6)));
    TEST_ASSERT_EQUAL_UINT8(3, topo.maxDepth());
}

void test_leaf_branch_root1()
{
    buildWorked(1);

    TEST_ASSERT_TRUE(topo.isLeaf(slot(4)));
    TEST_ASSERT_TRUE(topo.isLeaf(slot(6)));
    TEST_ASSERT_FALSE(topo.isLeaf(slot(1)));
    TEST_ASSERT_FALSE(topo.isLeaf(slot(2)));
    TEST_ASSERT_FALSE(topo.isLeaf(slot(3)));
    TEST_ASSERT_FALSE(topo.isLeaf(slot(5)));

    TEST_ASSERT_TRUE(topo.isBranch(slot(1)));
    TEST_ASSERT_FALSE(topo.isBranch(slot(2)));
    TEST_ASSERT_FALSE(topo.isBranch(slot(3)));
    TEST_ASSERT_FALSE(topo.isBranch(slot(5)));
}

void test_canonical_order_root1()
{
    buildWorked(1);

    TEST_ASSERT_EQUAL_UINT8(0, topo.canonicalPos(slot(1)));
    TEST_ASSERT_EQUAL_UINT8(1, topo.canonicalPos(slot(2)));
    TEST_ASSERT_EQUAL_UINT8(2, topo.canonicalPos(slot(4)));
    TEST_ASSERT_EQUAL_UINT8(3, topo.canonicalPos(slot(3)));
    TEST_ASSERT_EQUAL_UINT8(4, topo.canonicalPos(slot(5)));
    TEST_ASSERT_EQUAL_UINT8(5, topo.canonicalPos(slot(6)));
}

void test_neighbors()
{
    buildWorked(1);

    // Panel 3 is wired to panels 1 and 5.
    TEST_ASSERT_EQUAL_UINT8(2, topo.degree(slot(3)));

    bool sees1 = false, sees5 = false;

    for (uint8_t i = 0; i < topo.degree(slot(3)); i++) {
        uint8_t ns = topo.neighborSlot(slot(3), i);

        if (ns == slot(1)) sees1 = true;

        if (ns == slot(5)) sees5 = true;
    }

    TEST_ASSERT_TRUE(sees1);
    TEST_ASSERT_TRUE(sees5);
}

void test_subtree()
{
    buildWorked(1);

    PanelSet sub;

    sub.clearAll();
    topo.fillSubtree(slot(3), sub);

    // subtree:3 = {3, 5, 6}
    TEST_ASSERT_EQUAL_UINT8(3, sub.popcount(topo.count()));
    TEST_ASSERT_TRUE(sub.test(slot(3)));
    TEST_ASSERT_TRUE(sub.test(slot(5)));
    TEST_ASSERT_TRUE(sub.test(slot(6)));
    TEST_ASSERT_FALSE(sub.test(slot(1)));
    TEST_ASSERT_FALSE(sub.test(slot(2)));
    TEST_ASSERT_FALSE(sub.test(slot(4)));
}

void test_distances_from_root()
{
    buildWorked(1);

    PanelSet src;

    src.clearAll();
    src.set(slot(1));

    uint8_t dist[LIGHTNET_MAX_PANELS];

    topo.distancesFrom(src, dist);

    // Ï† source:root numerators (doc Â§6.1)
    TEST_ASSERT_EQUAL_UINT8(0, dist[slot(1)]);
    TEST_ASSERT_EQUAL_UINT8(1, dist[slot(2)]);
    TEST_ASSERT_EQUAL_UINT8(1, dist[slot(3)]);
    TEST_ASSERT_EQUAL_UINT8(2, dist[slot(4)]);
    TEST_ASSERT_EQUAL_UINT8(2, dist[slot(5)]);
    TEST_ASSERT_EQUAL_UINT8(3, dist[slot(6)]);
}

void test_distances_from_leaves()
{
    buildWorked(1);

    PanelSet src;

    src.clearAll();
    src.set(slot(4));
    src.set(slot(6));

    uint8_t dist[LIGHTNET_MAX_PANELS];

    topo.distancesFrom(src, dist);

    // Ï† source:leaves numerators (doc Â§6.1): 4â†’0 6â†’0 5â†’1 2â†’1 1â†’2 3â†’2
    TEST_ASSERT_EQUAL_UINT8(0, dist[slot(4)]);
    TEST_ASSERT_EQUAL_UINT8(0, dist[slot(6)]);
    TEST_ASSERT_EQUAL_UINT8(1, dist[slot(5)]);
    TEST_ASSERT_EQUAL_UINT8(1, dist[slot(2)]);
    TEST_ASSERT_EQUAL_UINT8(2, dist[slot(1)]);
    TEST_ASSERT_EQUAL_UINT8(2, dist[slot(3)]);
}

// ---------------------------------------------------------------------------
// Re-rooting (docs Â§4.1)
// ---------------------------------------------------------------------------

void test_reroot_at_panel_3()
{
    buildWorked(3);

    TEST_ASSERT_EQUAL_UINT8(slot(3), topo.root());

    // Depths re-centre on panel 3.
    TEST_ASSERT_EQUAL_UINT8(0, topo.depthOf(slot(3)));
    TEST_ASSERT_EQUAL_UINT8(1, topo.depthOf(slot(1)));
    TEST_ASSERT_EQUAL_UINT8(1, topo.depthOf(slot(5)));
    TEST_ASSERT_EQUAL_UINT8(2, topo.depthOf(slot(2)));
    TEST_ASSERT_EQUAL_UINT8(2, topo.depthOf(slot(6)));
    TEST_ASSERT_EQUAL_UINT8(3, topo.depthOf(slot(4)));

    // Panel 3 now forks (children 1 and 5); the old root (1) no longer branches.
    TEST_ASSERT_TRUE(topo.isBranch(slot(3)));
    TEST_ASSERT_FALSE(topo.isBranch(slot(1)));

    // Leaves of the graph are unchanged (4 and 6 still childless).
    TEST_ASSERT_TRUE(topo.isLeaf(slot(4)));
    TEST_ASSERT_TRUE(topo.isLeaf(slot(6)));
}

void test_unknown_root_falls_back_to_smallest()
{
    buildWorked(200); // panel 200 doesn't exist â†’ fall back to physical root (index 1)

    TEST_ASSERT_EQUAL_UINT8(slot(1), topo.root());
    TEST_ASSERT_EQUAL_UINT8(0, topo.depthOf(slot(1)));
    TEST_ASSERT_EQUAL_UINT8(3, topo.depthOf(slot(6)));
}

// ---------------------------------------------------------------------------
// Degenerate: a single panel, no links
// ---------------------------------------------------------------------------

void test_single_panel()
{
    static const uint8_t one[] = { 7 };

    TEST_ASSERT_TRUE(graph.build(one, 1, nullptr, 0));
    TEST_ASSERT_TRUE(topo.build(graph, 7));
    TEST_ASSERT_EQUAL_UINT8(1, topo.count());
    TEST_ASSERT_EQUAL_UINT8(slot(7), topo.root());
    TEST_ASSERT_EQUAL_UINT8(0, topo.depthOf(slot(7)));
    TEST_ASSERT_EQUAL_UINT8(0, topo.canonicalPos(slot(7)));
    TEST_ASSERT_TRUE(topo.isLeaf(slot(7)));
    TEST_ASSERT_FALSE(topo.isBranch(slot(7)));
}

void test_rejects_empty()
{
    graph.build(PANELS, 0, LINKS, 0);
    TEST_ASSERT_FALSE(topo.build(graph, 1));
}

// ---------------------------------------------------------------------------
// Runner
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

    RUN_TEST(test_count_and_slots);
    RUN_TEST(test_depth_root1);
    RUN_TEST(test_leaf_branch_root1);
    RUN_TEST(test_canonical_order_root1);
    RUN_TEST(test_neighbors);
    RUN_TEST(test_subtree);
    RUN_TEST(test_distances_from_root);
    RUN_TEST(test_distances_from_leaves);

    RUN_TEST(test_reroot_at_panel_3);
    RUN_TEST(test_unknown_root_falls_back_to_smallest);

    RUN_TEST(test_single_panel);
    RUN_TEST(test_rejects_empty);

    return UNITY_END();
}
