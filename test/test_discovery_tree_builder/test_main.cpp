// Host test for DiscoveryTreeBuilder — accumulates DiscoveryCoordinator's DFS walk into the
// TopoLink[]/indices[]/edgeCounts[] shape PanelGraph::build() consumes (hardware redesign plan
// §11). DiscoveryCoordinator itself is exercised in test_discovery_coordinator/
// test_discovery_end_to_end — this suite covers only the accumulation logic in isolation.
//
// Run with: pio test -e native -f test_discovery_tree_builder

#include <unity.h>

#include "Core/Relay/DiscoveryTreeBuilder.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

void test_root_alone_has_no_links()
{
    DiscoveryTreeBuilder builder(3);

    builder.addRoot(1);

    TEST_ASSERT_EQUAL_UINT8(1, builder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(1, builder.indices()[0]);
    TEST_ASSERT_EQUAL_UINT8(3, builder.edgeCounts()[0]);
    TEST_ASSERT_EQUAL_UINT8(0, builder.linkCount());
}

void test_link_records_both_sides_edge_indices()
{
    DiscoveryTreeBuilder builder(3);

    builder.addRoot(1);
    builder.addLink(/*parent*/ 1, /*parentEdge*/ 2, /*child*/ 2, /*childEdge*/ 0);

    TEST_ASSERT_EQUAL_UINT8(2, builder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(2, builder.indices()[1]);
    TEST_ASSERT_EQUAL_UINT8(1, builder.linkCount());

    const TopoLink &link = builder.links()[0];

    TEST_ASSERT_EQUAL_UINT8(1, link.panelA);
    TEST_ASSERT_EQUAL_UINT8(2, link.edgeA);
    TEST_ASSERT_EQUAL_UINT8(2, link.panelB);
    TEST_ASSERT_EQUAL_UINT8(0, link.edgeB);
}

void test_chain_of_three_builds_two_links_in_order()
{
    DiscoveryTreeBuilder builder(3);

    builder.addRoot(1);
    builder.addLink(1, 0, 2, 1);
    builder.addLink(2, 2, 3, 0);

    TEST_ASSERT_EQUAL_UINT8(3, builder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(1, builder.indices()[0]);
    TEST_ASSERT_EQUAL_UINT8(2, builder.indices()[1]);
    TEST_ASSERT_EQUAL_UINT8(3, builder.indices()[2]);
    TEST_ASSERT_EQUAL_UINT8(2, builder.linkCount());

    TEST_ASSERT_EQUAL_UINT8(1, builder.links()[0].panelA);
    TEST_ASSERT_EQUAL_UINT8(2, builder.links()[0].panelB);
    TEST_ASSERT_EQUAL_UINT8(2, builder.links()[1].panelA);
    TEST_ASSERT_EQUAL_UINT8(3, builder.links()[1].panelB);
}

void test_branching_tree_two_children_of_root()
{
    DiscoveryTreeBuilder builder(3);

    builder.addRoot(1);
    builder.addLink(1, 0, 2, 0);  // first child, on root's edge 0
    builder.addLink(1, 1, 3, 0);  // second child, on root's edge 1

    TEST_ASSERT_EQUAL_UINT8(3, builder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(2, builder.linkCount());
    TEST_ASSERT_EQUAL_UINT8(0, builder.links()[0].edgeA);
    TEST_ASSERT_EQUAL_UINT8(1, builder.links()[1].edgeA);
}

void test_reset_clears_accumulated_state()
{
    DiscoveryTreeBuilder builder(3);

    builder.addRoot(1);
    builder.addLink(1, 0, 2, 1);
    builder.reset();

    TEST_ASSERT_EQUAL_UINT8(0, builder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(0, builder.linkCount());
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_root_alone_has_no_links);
    RUN_TEST(test_link_records_both_sides_edge_indices);
    RUN_TEST(test_chain_of_three_builds_two_links_in_order);
    RUN_TEST(test_branching_tree_two_children_of_root);
    RUN_TEST(test_reset_clears_accumulated_state);

    return UNITY_END();
}
