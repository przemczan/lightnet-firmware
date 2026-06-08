// Native unit tests for lib/Lightnet/Controller/Topology/PanelGraph.hpp
// Run with: pio test -e native -f test_panel_graph
//
// Same worked topology as test_topology (docs/design/scene-portability.md §2):
//
//             [1]  root            depth 0
//            /   \
//         [2]     [3]              depth 1
//         /         \
//       [4]         [5]            depth 2
//                     \
//                     [6]          depth 3
//
// PanelGraph is the root-independent raw adjacency that TopologyIndex and
// PanelGeometry both build their rooted views on top of (see PanelGraph.hpp) —
// this suite exercises that shared surface directly: slot↔panel-index mapping,
// CSR adjacency, and the per-side connector indices the rooted views need.

#include <unity.h>
#include <string.h>
#include "Controller/Topology/PanelGraph.hpp"

using namespace Lightnet;

static const uint8_t PANELS[] = { 1, 2, 3, 4, 5, 6 };

static const TopoLink LINKS[] = {
    { 1, 0, 2, 0 },  // root edge0 → 2
    { 1, 1, 3, 0 },  // root edge1 → 3
    { 2, 1, 4, 0 },
    { 3, 1, 5, 0 },
    { 5, 1, 6, 0 },
};

static PanelGraph graph;

static uint8_t slot(uint8_t panelIndex)
{
    uint8_t s = 0xFF;

    graph.slotOf(panelIndex, s);

    return s;
}

// Offset i in [0, degree(s)) such that neighborSlot(s, i) == targetSlot, or 0xFF.
static uint8_t neighborOffset(uint8_t s, uint8_t targetSlot)
{
    for (uint8_t i = 0; i < graph.degree(s); i++) {
        if (graph.neighborSlot(s, i) == targetSlot) return i;
    }

    return 0xFF;
}

void test_count_and_slot_roundtrip()
{
    TEST_ASSERT_EQUAL_UINT8(6, graph.count());

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t s;

        TEST_ASSERT_TRUE(graph.slotOf(PANELS[i], s));
        TEST_ASSERT_EQUAL_UINT8(PANELS[i], graph.panelAt(s));
    }
}

void test_slot_of_unknown_panel_fails()
{
    uint8_t s;

    TEST_ASSERT_FALSE(graph.slotOf(99, s));
}

void test_lowest_slot_is_panel_1()
{
    TEST_ASSERT_EQUAL_UINT8(slot(1), graph.lowestSlot());
}

void test_degree_matches_tree_shape()
{
    // Branch panels (1, 2, 3, 5) have 2 neighbours; leaves (4, 6) have 1.
    TEST_ASSERT_EQUAL_UINT8(2, graph.degree(slot(1)));
    TEST_ASSERT_EQUAL_UINT8(2, graph.degree(slot(2)));
    TEST_ASSERT_EQUAL_UINT8(2, graph.degree(slot(3)));
    TEST_ASSERT_EQUAL_UINT8(1, graph.degree(slot(4)));
    TEST_ASSERT_EQUAL_UINT8(2, graph.degree(slot(5)));
    TEST_ASSERT_EQUAL_UINT8(1, graph.degree(slot(6)));
}

void test_neighbor_slots_are_symmetric()
{
    // Undirected graph: every adjacency entry has a matching reverse entry.
    for (uint8_t s = 0; s < graph.count(); s++) {
        for (uint8_t i = 0; i < graph.degree(s); i++) {
            uint8_t v = graph.neighborSlot(s, i);

            TEST_ASSERT_TRUE(neighborOffset(v, s) != 0xFF);
        }
    }
}

void test_neighbor_edges_match_links()
{
    // Link {1,edge0 → 2,edge0}: from 1's side, my edge is 0 (edgeA), peer edge is 0 (edgeB).
    uint8_t i = neighborOffset(slot(1), slot(2));

    TEST_ASSERT_TRUE(i != 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0, graph.neighborMyEdge(slot(1), i));
    TEST_ASSERT_EQUAL_UINT8(0, graph.neighborPeerEdge(slot(1), i));

    // Symmetric view from 2's side toward 1: my edge is edgeB(=0), peer edge is edgeA(=0).
    uint8_t j = neighborOffset(slot(2), slot(1));

    TEST_ASSERT_TRUE(j != 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0, graph.neighborMyEdge(slot(2), j));
    TEST_ASSERT_EQUAL_UINT8(0, graph.neighborPeerEdge(slot(2), j));

    // Link {1,edge1 → 3,edge0}: from 1's side toward 3, my edge is 1 (edgeA), peer edge is 0 (edgeB).
    uint8_t k = neighborOffset(slot(1), slot(3));

    TEST_ASSERT_TRUE(k != 0xFF);
    TEST_ASSERT_EQUAL_UINT8(1, graph.neighborMyEdge(slot(1), k));
    TEST_ASSERT_EQUAL_UINT8(0, graph.neighborPeerEdge(slot(1), k));

    // From 3's side toward 1: my edge is edgeB(=0), peer edge is edgeA(=1).
    uint8_t l = neighborOffset(slot(3), slot(1));

    TEST_ASSERT_TRUE(l != 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0, graph.neighborMyEdge(slot(3), l));
    TEST_ASSERT_EQUAL_UINT8(1, graph.neighborPeerEdge(slot(3), l));
}

void test_single_panel()
{
    static const uint8_t one[] = { 7 };
    PanelGraph g;

    TEST_ASSERT_TRUE(g.build(one, 1, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT8(1, g.count());
    TEST_ASSERT_EQUAL_UINT8(0, g.lowestSlot());
    TEST_ASSERT_EQUAL_UINT8(7, g.panelAt(g.lowestSlot()));
    TEST_ASSERT_EQUAL_UINT8(0, g.degree(g.lowestSlot()));
}

void test_rejects_empty()
{
    PanelGraph g;

    TEST_ASSERT_FALSE(g.build(PANELS, 0, LINKS, 0));
    TEST_ASSERT_EQUAL_UINT8(0, g.count());
}

void test_rejects_over_capacity()
{
    static uint8_t many[LIGHTNET_MAX_PANELS + 1];

    for (uint16_t i = 0; i < sizeof(many); i++) many[i] = (uint8_t)((i % 0xFF) + 1);

    PanelGraph g;

    TEST_ASSERT_FALSE(g.build(many, sizeof(many), nullptr, 0));
    TEST_ASSERT_EQUAL_UINT8(0, g.count());
}

void setUp(void)
{
    graph.build(PANELS, 6, LINKS, 5);
}

void tearDown(void)
{
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_count_and_slot_roundtrip);
    RUN_TEST(test_slot_of_unknown_panel_fails);
    RUN_TEST(test_lowest_slot_is_panel_1);
    RUN_TEST(test_degree_matches_tree_shape);
    RUN_TEST(test_neighbor_slots_are_symmetric);
    RUN_TEST(test_neighbor_edges_match_links);
    RUN_TEST(test_single_panel);
    RUN_TEST(test_rejects_empty);
    RUN_TEST(test_rejects_over_capacity);

    return UNITY_END();
}
