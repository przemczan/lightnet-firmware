// Host test for PanelRouter — the flood/route forwarding decision, plus a small in-memory
// multi-node fabric that proves the property the loop-rejection rule actually exists for:
// a flood through a topology with a deliberate wiring loop terminates (each node is
// reached a bounded number of times) rather than circulating forever.
//
// This proves logic only — nothing here says anything about real mux/USART timing.
//
// Run with: pio test -e native -f test_panel_router

#include <unity.h>

#include "Core/Relay/PanelDiscovery.hpp"
#include "Core/Relay/PanelRouter.hpp"
#include "Core/Common/ProtocolMeta.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

// --- Mock ------------------------------------------------------------------

struct MockEdgeLink : public IEdgeLink {
    static const int MAX = 16;
    uint8_t          sentToEdge[MAX];
    int              count = 0;

    void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override
    {
        (void)packet;
        (void)size;

        if (count < MAX) {
            sentToEdge[count] = edgeIndex;
            count++;
        }
    }

    bool wasSentTo(uint8_t edgeIndex) const
    {
        for (int i = 0; i < count; i++) if (sentToEdge[i] == edgeIndex) return true;

        return false;
    }
};

// --- Single-node routing rule -----------------------------------------------

void test_broadcast_from_parent_edge_floods_every_other_connected_edge()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);             // edge 0 = parent
    discovery.onChildProbeAccepted(1, 2);   // edge 1 = child panel 2
    discovery.onChildProbeAccepted(2, 4);   // edge 2 = child panel 4

    MockEdgeLink link;
    PanelRouter router(discovery, link);

    // makeMeta() defaults targetPanelIndex to 0 = broadcast.
    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_SET_COLOR);

    router.onFrameArrived(0, &packet, sizeof(packet));

    TEST_ASSERT_EQUAL(2, link.count);
    TEST_ASSERT_TRUE(link.wasSentTo(1));
    TEST_ASSERT_TRUE(link.wasSentTo(2));
    TEST_ASSERT_FALSE(link.wasSentTo(0));  // never echoed back the way it arrived
}

// --- Branch routing for addressed frames ------------------------------------
//
// Pre-order DFS numbering of the Y topology (1 -> 2 -> 3, 1 -> 4 -> 5) seen from panel 1:
// edge 1 leads to child 2 (subtree 2-3), edge 2 leads to child 4 (subtree 4-5).

static PanelDiscovery makeYRootDiscovery()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);
    discovery.onChildProbeAccepted(1, 2);
    discovery.onChildProbeAccepted(2, 4);

    return discovery;
}

void test_addressed_frame_routes_to_the_single_matching_branch()
{
    PanelDiscovery discovery = makeYRootDiscovery();
    MockEdgeLink link;
    PanelRouter router(discovery, link);

    // Target 3 lives inside child 2's subtree (largest child index <= 3 is 2).
    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_SET_COLOR, 3);

    router.onFrameArrived(0, &packet, sizeof(packet));

    TEST_ASSERT_EQUAL(1, link.count);
    TEST_ASSERT_TRUE(link.wasSentTo(1));
    TEST_ASSERT_FALSE(link.wasSentTo(2));  // the sibling branch stays silent
}

void test_addressed_frame_routes_to_the_deeper_branch_by_subtree_range()
{
    PanelDiscovery discovery = makeYRootDiscovery();
    MockEdgeLink link;
    PanelRouter router(discovery, link);

    // Target 5 lives inside child 4's subtree.
    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_SET_COLOR, 5);

    router.onFrameArrived(0, &packet, sizeof(packet));

    TEST_ASSERT_EQUAL(1, link.count);
    TEST_ASSERT_TRUE(link.wasSentTo(2));
}

void test_addressed_frame_to_a_child_itself_routes_to_that_child()
{
    PanelDiscovery discovery = makeYRootDiscovery();
    MockEdgeLink link;
    PanelRouter router(discovery, link);

    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_SET_COLOR, 4);

    router.onFrameArrived(0, &packet, sizeof(packet));

    TEST_ASSERT_EQUAL(1, link.count);
    TEST_ASSERT_TRUE(link.wasSentTo(2));
}

void test_addressed_frame_outside_every_subtree_is_dropped()
{
    PanelDiscovery discovery = makeYRootDiscovery();
    MockEdgeLink link;
    PanelRouter router(discovery, link);

    // Target 1 is below every child index -- not in this panel's subtree at all
    // (a self-addressed frame never reaches the router; see PanelFrameDispatcher).
    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_SET_COLOR, 1);

    router.onFrameArrived(0, &packet, sizeof(packet));

    TEST_ASSERT_EQUAL(0, link.count);
}

void test_addressed_frame_falls_back_to_flood_when_a_child_has_no_recorded_index()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);
    discovery.onChildProbeAccepted(1, 2);
    discovery.onChildProbeAccepted(2, 0);  // connected, but no index recorded

    MockEdgeLink link;
    PanelRouter router(discovery, link);

    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_SET_COLOR, 3);

    router.onFrameArrived(0, &packet, sizeof(packet));

    // Routing must not engage on partial knowledge -- the target could sit behind the
    // unindexed edge, so the legacy flood keeps it reachable.
    TEST_ASSERT_EQUAL(2, link.count);
    TEST_ASSERT_TRUE(link.wasSentTo(1));
    TEST_ASSERT_TRUE(link.wasSentTo(2));
}

void test_frame_from_a_child_edge_routes_upstream_only()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);
    discovery.onChildProbeAccepted(1, 2);
    discovery.onChildProbeAccepted(2, 4);

    MockEdgeLink link;
    PanelRouter router(discovery, link);

    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_FETCH_STATE);

    router.onFrameArrived(1, &packet, sizeof(packet));  // reply arriving from child on edge 1

    TEST_ASSERT_EQUAL(1, link.count);
    TEST_ASSERT_TRUE(link.wasSentTo(0));   // ...goes to the parent
    TEST_ASSERT_FALSE(link.wasSentTo(1));  // never back out the edge it arrived on
    TEST_ASSERT_FALSE(link.wasSentTo(2));  // never sideways to another child
}

void test_not_connected_edges_never_receive_anything()
{
    PanelDiscovery discovery(3);

    discovery.onParentOffer(0);
    discovery.onChildProbeAccepted(1, 2);
    discovery.onChildProbeFailed(2);  // rejected loop or genuinely empty

    MockEdgeLink link;
    PanelRouter router(discovery, link);

    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_SET_COLOR);

    router.onFrameArrived(0, &packet, sizeof(packet));

    TEST_ASSERT_EQUAL(1, link.count);
    TEST_ASSERT_TRUE(link.wasSentTo(1));
    TEST_ASSERT_FALSE(link.wasSentTo(2));
}

void test_undiscovered_panel_forwards_nothing()
{
    PanelDiscovery discovery(3);  // never given a parent

    MockEdgeLink link;
    PanelRouter router(discovery, link);

    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_SET_COLOR);

    router.onFrameArrived(0, &packet, sizeof(packet));

    TEST_ASSERT_EQUAL(0, link.count);
}

// --- Multi-node fabric: does a flood through a *looped* topology terminate? -----------
//
// Physical wiring (undirected):
//   controller -- N0
//   N0 -- N1
//   N1 -- N2
//   N2 -- N0   <-- the extra wire that closes a loop (a real installer mistake)
//
// Every node has 3 edges. Edge indices used below are arbitrary but fixed per node.

struct FabricTarget {
    int8_t  node;
    uint8_t edge;
};

const int8_t NO_NODE = -1;
const int NODE_COUNT = 3;
const uint8_t EDGES_PER_NODE = 3;

// wiring[node][edge] -> what's on the other end of that edge, or {NO_NODE, 0} if nothing.
FabricTarget wiring[NODE_COUNT][EDGES_PER_NODE] = {
    // N0: edge0 -> controller (handled separately), edge1 -> N1, edge2 -> N2 (loop)
    { { NO_NODE, 0 }, { 1, 0 }, { 2, 1 } },
    // N1: edge0 -> N0, edge1 -> N2, edge2 -> unwired
    { { 0, 1 }, { 2, 0 }, { NO_NODE, 0 } },
    // N2: edge0 -> N1, edge1 -> N0 (loop, other end), edge2 -> unwired
    { { 1, 1 }, { 0, 2 }, { NO_NODE, 0 } },
};

PanelDiscovery *discoveries[NODE_COUNT];
PanelRouter *routers[NODE_COUNT];
int deliveryCount;

const int SAFETY_CAP = 50;  // real tree has 2 links; anything near this means a runaway flood

struct FabricLink : public IEdgeLink {
    int nodeIndex;

    void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override
    {
        FabricTarget target = wiring[this->nodeIndex][edgeIndex];

        if (target.node == NO_NODE) {
            return;
        }

        deliveryCount++;
        TEST_ASSERT_TRUE_MESSAGE(deliveryCount < SAFETY_CAP, "flood did not terminate — loop rejection failed");

        routers[target.node]->onFrameArrived(target.edge, packet, size);
    }
};

FabricLink fabricLinks[NODE_COUNT];

// Recursively discovers `nodeIndex`'s still-unexplored edges, applying the loop-rejection
// rule exactly as PanelDiscovery::onParentOffer defines it.
void discoverFrom(int nodeIndex)
{
    PanelDiscovery *discovery = discoveries[nodeIndex];

    for (uint8_t edge = 0; edge < EDGES_PER_NODE; edge++) {
        if (discovery->edgeState(edge) != EdgeLinkState::Unexplored) {
            continue;  // already resolved — either our own parent edge, or settled by the far side
        }

        FabricTarget target = wiring[nodeIndex][edge];

        if (target.node == NO_NODE) {
            discovery->onChildProbeFailed(edge);
            continue;
        }

        PanelDiscovery *targetDiscovery = discoveries[target.node];
        bool wasAlreadyDiscovered = targetDiscovery->hasParent();
        bool accepted = targetDiscovery->onParentOffer(target.edge);

        if (!wasAlreadyDiscovered) {
            TEST_ASSERT_TRUE_MESSAGE(accepted, "a genuinely new panel must accept its first parent offer");
            // This fabric doesn't assign panel indices (index 0 = none recorded); the flood
            // below is a broadcast, which never consults child indices anyway.
            discovery->onChildProbeAccepted(edge, 0);
            discoverFrom(target.node);
        } else {
            TEST_ASSERT_FALSE_MESSAGE(accepted, "an already-discovered panel must reject a second parent edge");
            discovery->onChildProbeFailed(edge);
        }
    }
}

void test_looped_topology_discovers_as_a_tree_and_flood_terminates()
{
    for (int i = 0; i < NODE_COUNT; i++) {
        discoveries[i] = new PanelDiscovery(EDGES_PER_NODE);
        fabricLinks[i].nodeIndex = i;
    }

    for (int i = 0; i < NODE_COUNT; i++) {
        routers[i] = new PanelRouter(*discoveries[i], fabricLinks[i]);
    }

    // The controller offers N0 its parent edge (edge 0) directly — the controller itself
    // isn't a PanelDiscovery instance, it has exactly one trunk port and no loop concern.
    discoveries[0]->onParentOffer(0);

    discoverFrom(0);

    // Topology must have resolved to a genuine tree: 2 links for 3 nodes, and the
    // loop-closing edge rejected on *both* ends.
    TEST_ASSERT_TRUE(discoveries[0]->hasParent());
    TEST_ASSERT_TRUE(discoveries[1]->hasParent());
    TEST_ASSERT_TRUE(discoveries[2]->hasParent());

    TEST_ASSERT_TRUE(discoveries[0]->isConnected(1));   // N0 <-> N1: real tree link
    TEST_ASSERT_TRUE(discoveries[1]->isConnected(1));   // N1 <-> N2: real tree link
    TEST_ASSERT_FALSE(discoveries[0]->isConnected(2));  // N0's side of the loop: rejected
    TEST_ASSERT_FALSE(discoveries[2]->isConnected(1));  // N2's side of the loop: rejected

    // Inject a flood arriving at N0 from the controller (edge 0) and let it propagate.
    deliveryCount = 0;

    Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_ANIMATION_START);

    routers[0]->onFrameArrived(0, &packet, sizeof(packet));

    // Exactly 2 deliveries: N0->N1, N1->N2. The loop link never fires (it's NotConnected),
    // and nothing bounces back — this is the flood-termination property itself, not just
    // the decision table it depends on.
    TEST_ASSERT_EQUAL(2, deliveryCount);

    for (int i = 0; i < NODE_COUNT; i++) {
        delete routers[i];
        delete discoveries[i];
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_broadcast_from_parent_edge_floods_every_other_connected_edge);
    RUN_TEST(test_addressed_frame_routes_to_the_single_matching_branch);
    RUN_TEST(test_addressed_frame_routes_to_the_deeper_branch_by_subtree_range);
    RUN_TEST(test_addressed_frame_to_a_child_itself_routes_to_that_child);
    RUN_TEST(test_addressed_frame_outside_every_subtree_is_dropped);
    RUN_TEST(test_addressed_frame_falls_back_to_flood_when_a_child_has_no_recorded_index);
    RUN_TEST(test_frame_from_a_child_edge_routes_upstream_only);
    RUN_TEST(test_not_connected_edges_never_receive_anything);
    RUN_TEST(test_undiscovered_panel_forwards_nothing);
    RUN_TEST(test_looped_topology_discovers_as_a_tree_and_flood_terminates);

    return UNITY_END();
}
