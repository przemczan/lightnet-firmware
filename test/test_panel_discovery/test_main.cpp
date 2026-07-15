// Host test for PanelDiscovery — the per-panel loop-rejection rule.
//
// A panel adopts exactly one parent edge, ever. A second "you are now my child" offer
// arriving on a different edge means the wiring closes a loop back to an already-discovered
// panel, and must be rejected rather than accepted as a new parent.
//
// Run with: pio test -e native -f test_panel_discovery

#include <unity.h>

#include "Core/Relay/PanelDiscovery.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

void test_starts_with_no_parent_and_all_edges_unexplored()
{
    PanelDiscovery discovery(3);

    TEST_ASSERT_FALSE(discovery.hasParent());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EdgeLinkState::Unexplored, (uint8_t)discovery.edgeState(0));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EdgeLinkState::Unexplored, (uint8_t)discovery.edgeState(1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EdgeLinkState::Unexplored, (uint8_t)discovery.edgeState(2));
}

void test_first_offer_is_accepted_and_becomes_parent()
{
    PanelDiscovery discovery(3);

    bool accepted = discovery.onParentOffer(1);

    TEST_ASSERT_TRUE(accepted);
    TEST_ASSERT_TRUE(discovery.hasParent());
    TEST_ASSERT_EQUAL(1, discovery.parentEdge());
    TEST_ASSERT_TRUE(discovery.isConnected(1));
}

void test_repeat_offer_on_the_same_parent_edge_is_idempotent()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);

    bool accepted = discovery.onParentOffer(0);

    TEST_ASSERT_TRUE(accepted);
    TEST_ASSERT_EQUAL(0, discovery.parentEdge());
    TEST_ASSERT_TRUE(discovery.isConnected(0));
}

void test_second_offer_on_a_different_edge_is_rejected_as_a_loop()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);

    bool accepted = discovery.onParentOffer(2);

    TEST_ASSERT_FALSE(accepted);
    TEST_ASSERT_EQUAL(0, discovery.parentEdge());  // original parent unchanged
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EdgeLinkState::NotConnected, (uint8_t)discovery.edgeState(2));
}

void test_child_probe_accepted_marks_edge_connected_and_records_child_index()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);

    discovery.onChildProbeAccepted(1, 7);

    TEST_ASSERT_TRUE(discovery.isConnected(1));
    TEST_ASSERT_EQUAL_UINT16(7, discovery.childIndex(1));
    TEST_ASSERT_EQUAL_UINT16(0, discovery.childIndex(0));  // parent edge has no child index
    TEST_ASSERT_EQUAL_UINT16(0, discovery.childIndex(2));  // unexplored edge has none either
}

void test_child_probe_failed_clears_any_recorded_child_index()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);
    discovery.onChildProbeAccepted(1, 7);

    discovery.onChildProbeFailed(1);

    TEST_ASSERT_EQUAL_UINT16(0, discovery.childIndex(1));
}

void test_child_probe_failed_marks_edge_not_connected()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);

    discovery.onChildProbeFailed(1);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)EdgeLinkState::NotConnected, (uint8_t)discovery.edgeState(1));
    TEST_ASSERT_FALSE(discovery.isConnected(1));
}

void test_rejected_loop_and_genuinely_empty_edge_collapse_to_the_same_state()
{
    PanelDiscovery rejectedLoop(3);

    rejectedLoop.onParentOffer(0);
    rejectedLoop.onParentOffer(2);  // rejected: loop

    PanelDiscovery emptyEdge(3);

    emptyEdge.onParentOffer(0);
    emptyEdge.onChildProbeFailed(2);  // nothing wired there

    TEST_ASSERT_EQUAL_UINT8((uint8_t)rejectedLoop.edgeState(2), (uint8_t)emptyEdge.edgeState(2));
    TEST_ASSERT_FALSE(rejectedLoop.isConnected(2));
    TEST_ASSERT_FALSE(emptyEdge.isConnected(2));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_starts_with_no_parent_and_all_edges_unexplored);
    RUN_TEST(test_first_offer_is_accepted_and_becomes_parent);
    RUN_TEST(test_repeat_offer_on_the_same_parent_edge_is_idempotent);
    RUN_TEST(test_second_offer_on_a_different_edge_is_rejected_as_a_loop);
    RUN_TEST(test_child_probe_accepted_marks_edge_connected_and_records_child_index);
    RUN_TEST(test_child_probe_failed_clears_any_recorded_child_index);
    RUN_TEST(test_child_probe_failed_marks_edge_not_connected);
    RUN_TEST(test_rejected_loop_and_genuinely_empty_edge_collapse_to_the_same_state);

    return UNITY_END();
}
