// Host test proving the whole relay discovery protocol end-to-end over real exchanged packets:
// DiscoveryCoordinator (controller) + PanelFrameDispatcher/PanelDiscoveryDriver/PanelRouter
// (panel, the exact sequencing real firmware uses). Unlike test_panel_router's discoverFrom()
// helper -- which calls PanelDiscovery::onParentOffer() directly to prove the decision table
// alone -- this fabric only ever calls sendOnEdge()/onFrameArrived()/tick(), the same seams
// real firmware would use.
//
// Physical wiring (undirected), mirroring test_panel_router's 3-node loop fabric plus two
// genuinely empty ports:
//   controller -- N0
//   N0 -- N1
//   N0 -- N2   <-- the extra wire that closes a loop (a real installer mistake)
//   N1 -- N2
//   N1, N2 each have one further edge left completely unwired (an empty port).
//
// Run with: pio test -e native -f test_discovery_end_to_end

#include <unity.h>
#include <string.h>

#include "Core/Relay/DiscoveryCoordinator.hpp"
#include "Core/Relay/DiscoveryTreeBuilder.hpp"
#include "Core/Relay/PanelDiscoveryDriver.hpp"
#include "Core/Relay/PanelFrameDispatcher.hpp"
#include "Core/Relay/PanelRouter.hpp"
#include "Core/Common/ProtocolMeta.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

const int8_t NO_NODE         = -1;
const int8_t CONTROLLER_NODE = -2;
const int NODE_COUNT      = 3;
const uint8_t EDGES_PER_NODE  = 3;

struct FabricTarget {
    int8_t  node;
    uint8_t edge;
};

// wiring[node][edge] -> what's on the other end: a (node, edge) pair, NO_NODE (unwired), or
// CONTROLLER_NODE (the controller's single trunk edge).
FabricTarget wiring[NODE_COUNT][EDGES_PER_NODE] = {
    // N0: edge0 -> controller, edge1 -> N1.edge0, edge2 -> N2.edge1 (loop)
    { { CONTROLLER_NODE, 0 }, { 1, 0 }, { 2, 1 } },
    // N1: edge0 -> N0.edge1, edge1 -> N2.edge0, edge2 -> unwired
    { { 0, 1 }, { 2, 0 }, { NO_NODE, 0 } },
    // N2: edge0 -> N1.edge1, edge1 -> N0.edge2 (loop, other end), edge2 -> unwired
    { { 1, 1 }, { 0, 2 }, { NO_NODE, 0 } },
};

PanelDiscovery *discoveries[NODE_COUNT];
PanelRouter *routers[NODE_COUNT];
PanelDiscoveryDriver *drivers[NODE_COUNT];
PanelFrameDispatcher *dispatchers[NODE_COUNT];
DiscoveryCoordinator *coordinator;
uint32_t fabricNow;

// Set by a test that wants to simulate exactly one lost frame on a specific hop -- self-clearing
// on first match, so it never affects any test that leaves it false (the default).
bool dropNextPullOnN0Edge1        = false;
bool dropNextAdvanceFromController = false;

// --- Deferred delivery -----------------------------------------------------------------------
//
// Frames are queued FIFO and pumped, never delivered by direct recursion. Real hardware gives
// every node strictly sequential frame processing (one RX ring drained from one main loop) plus
// wire latency -- a reply provoked by a frame this node relayed can only ever be processed
// AFTER this node finished processing the frame that provoked it. Synchronous recursion breaks
// that causality: the coordinator's ADVANCE, sent in reaction to a relayed REGISTER_EDGE, would
// re-enter the probing panel's dispatcher before its driver had marked the new child edge
// Connected, and the flood would skip the child that ADVANCE addresses.

struct PendingDelivery {
    int8_t  node;  // CONTROLLER_NODE or a panel node index
    uint8_t edge;
    uint8_t size;
    uint8_t buf[Protocol::MAX_PACKET_SIZE];
};

const int DELIVERY_QUEUE_CAP = 32;

PendingDelivery deliveryQueue[DELIVERY_QUEUE_CAP];
int deliveryHead  = 0;
int deliveryCount = 0;

void enqueueDelivery(int8_t node, uint8_t edge, const Protocol::PacketMeta *frame, uint8_t size)
{
    TEST_ASSERT_TRUE_MESSAGE(deliveryCount < DELIVERY_QUEUE_CAP, "fabric delivery queue overflow");

    PendingDelivery &slot = deliveryQueue[(deliveryHead + deliveryCount) % DELIVERY_QUEUE_CAP];

    slot.node = node;
    slot.edge = edge;
    slot.size = size;
    memcpy(slot.buf, frame, size);
    deliveryCount++;
}

// Every panel-bound frame goes through the node's real PanelFrameDispatcher -- the exact
// sequencing real firmware uses (relay via PanelRouter first, skipping
// PACKET_INITIALIZATION_PULL and self-addressed frames, then the driver -- see
// PanelFrameDispatcher.hpp's rules). In particular this proves the walk still completes even
// though a frame addressed to a panel (its own ADVANCE) is consumed there without being
// flooded further down its subtree.
void pumpDeliveries()
{
    while (deliveryCount > 0) {
        PendingDelivery slot = deliveryQueue[deliveryHead];  // copy -- processing enqueues more

        deliveryHead = (deliveryHead + 1) % DELIVERY_QUEUE_CAP;
        deliveryCount--;

        if (slot.node == CONTROLLER_NODE) {
            coordinator->onFrameArrived((const Protocol::PacketMeta *)slot.buf, slot.size, fabricNow);
        } else {
            dispatchers[slot.node]->onFrameArrived(
                slot.edge,
                (const Protocol::PacketMeta *)slot.buf,
                slot.size,
                fabricNow
            );
        }
    }
}

struct FabricLink : public IEdgeLink {
    int nodeIndex;

    void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override
    {
        if (dropNextPullOnN0Edge1 && this->nodeIndex == 0 && edgeIndex == 1
            && packet->header.type == Protocol::PACKET_INITIALIZATION_PULL) {
            dropNextPullOnN0Edge1 = false;  // exactly one lost frame, then the wire behaves again

            return;
        }

        FabricTarget target = wiring[this->nodeIndex][edgeIndex];

        if (target.node == NO_NODE) {
            return;
        }

        if (target.node == CONTROLLER_NODE) {
            enqueueDelivery(CONTROLLER_NODE, 0, packet, size);

            return;
        }

        enqueueDelivery(target.node, target.edge, packet, size);
    }
};

struct ControllerLink : public IEdgeLink {
    void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override
    {
        (void)edgeIndex;  // the controller has exactly one edge in this fabric

        if (dropNextAdvanceFromController && packet->header.type == Protocol::PACKET_DISCOVERY_ADVANCE) {
            dropNextAdvanceFromController = false;

            return;
        }

        enqueueDelivery(0, 0, packet, size);  // N0 is wired directly to the controller on its own edge 0
    }
};

FabricLink fabricLinks[NODE_COUNT];

void test_discovery_completes_end_to_end_with_a_loop_and_two_empty_ports()
{
    for (int i = 0; i < NODE_COUNT; i++) {
        discoveries[i]        = new PanelDiscovery(EDGES_PER_NODE);
        fabricLinks[i].nodeIndex = i;
        drivers[i]             = new PanelDiscoveryDriver(*discoveries[i], fabricLinks[i]);
        routers[i]             = new PanelRouter(*discoveries[i], fabricLinks[i]);
        dispatchers[i]         = new PanelFrameDispatcher(*drivers[i], *routers[i]);
    }

    ControllerLink controllerLink;
    DiscoveryTreeBuilder treeBuilder(EDGES_PER_NODE);

    coordinator   = new DiscoveryCoordinator(controllerLink, &treeBuilder);
    fabricNow     = 0;
    deliveryHead  = 0;
    deliveryCount = 0;

    coordinator->begin();
    pumpDeliveries();

    const int SAFETY_CAP = 200;  // a real, un-stuck walk on 3 nodes finishes in a handful of steps
    int iterations = 0;

    while (!coordinator->isComplete() && iterations < SAFETY_CAP) {
        // Nudge time forward past the probe timeout and let every node's driver react -- a
        // no-op for any node that isn't currently mid-probe.
        fabricNow += PanelDiscoveryDriver::PROBE_TIMEOUT_MS + 1;

        for (int i = 0; i < NODE_COUNT; i++) {
            drivers[i]->tick(fabricNow);
        }

        pumpDeliveries();

        iterations++;
    }

    TEST_ASSERT_TRUE_MESSAGE(coordinator->isComplete(), "discovery did not complete -- stuck or looping");

    // Depth-first order is deterministic given the wiring above: the controller reaches N0
    // first, N0 tries edge1 (N1) before edge2 (the loop), and N1 tries edge1 (N2) before its
    // own empty edge2.
    TEST_ASSERT_EQUAL_UINT16(1, drivers[0]->assignedPanelIndex());
    TEST_ASSERT_EQUAL_UINT16(2, drivers[1]->assignedPanelIndex());
    TEST_ASSERT_EQUAL_UINT16(3, drivers[2]->assignedPanelIndex());

    TEST_ASSERT_TRUE(discoveries[0]->hasParent());
    TEST_ASSERT_TRUE(discoveries[1]->hasParent());
    TEST_ASSERT_TRUE(discoveries[2]->hasParent());

    // The real tree links are connected on both ends...
    TEST_ASSERT_TRUE(discoveries[0]->isConnected(1));  // N0 <-> N1
    TEST_ASSERT_TRUE(discoveries[1]->isConnected(0));
    TEST_ASSERT_TRUE(discoveries[1]->isConnected(1));  // N1 <-> N2
    TEST_ASSERT_TRUE(discoveries[2]->isConnected(0));

    // ...each parent recorded its child's assigned index (what PanelRouter's branch routing
    // resolves addressed frames against)...
    TEST_ASSERT_EQUAL_UINT16(2, discoveries[0]->childIndex(1));  // N0's edge1 leads to panel 2
    TEST_ASSERT_EQUAL_UINT16(3, discoveries[1]->childIndex(1));  // N1's edge1 leads to panel 3
    TEST_ASSERT_EQUAL_UINT16(0, discoveries[0]->childIndex(0));  // parent edge: no child index

    // ...the loop-closing edge is rejected on both sides...
    TEST_ASSERT_FALSE(discoveries[0]->isConnected(2));
    TEST_ASSERT_FALSE(discoveries[2]->isConnected(1));

    // ...and the two genuinely empty ports timed out to NotConnected rather than staying
    // Unexplored or hanging the whole walk.
    TEST_ASSERT_FALSE(discoveries[1]->isConnected(2));
    TEST_ASSERT_FALSE(discoveries[2]->isConnected(2));

    // The controller-side DiscoveryTreeBuilder should have accumulated exactly the real spanning
    // tree (2 links for 3 panels) -- the loop-closing reply is rejected and never reaches
    // handleRegisterEdgeReply, so it must never appear here either.
    TEST_ASSERT_EQUAL_UINT8(3, treeBuilder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(1, treeBuilder.indices()[0]);
    TEST_ASSERT_EQUAL_UINT8(2, treeBuilder.indices()[1]);
    TEST_ASSERT_EQUAL_UINT8(3, treeBuilder.indices()[2]);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, treeBuilder.linkCount(), "the rejected loop edge must not be recorded as a link");

    const TopoLink &link0 = treeBuilder.links()[0];  // N0(1) <-> N1(2)

    TEST_ASSERT_EQUAL_UINT8(1, link0.panelA);
    TEST_ASSERT_EQUAL_UINT8(1, link0.edgeA);  // N0's edge1 (see wiring[0][1])
    TEST_ASSERT_EQUAL_UINT8(2, link0.panelB);
    TEST_ASSERT_EQUAL_UINT8(0, link0.edgeB);  // N1's edge0 (see wiring[1][0])

    const TopoLink &link1 = treeBuilder.links()[1];  // N1(2) <-> N2(3)

    TEST_ASSERT_EQUAL_UINT8(2, link1.panelA);
    TEST_ASSERT_EQUAL_UINT8(1, link1.edgeA);  // N1's edge1 (see wiring[1][1])
    TEST_ASSERT_EQUAL_UINT8(3, link1.panelB);
    TEST_ASSERT_EQUAL_UINT8(0, link1.edgeB);  // N2's edge0 (see wiring[2][0])

    for (int i = 0; i < NODE_COUNT; i++) {
        delete dispatchers[i];
        delete routers[i];
        delete drivers[i];
        delete discoveries[i];
    }

    delete coordinator;
}

// Same fabric, but the very first ADVANCE the controller sends (to N0) and the very first PULL
// N0 sends probing N1 are each dropped exactly once -- proving PanelDiscoveryDriver's probe
// retries and DiscoveryCoordinator's ADVANCE resends actually recover a real single-frame loss
// on the wire, not just avoid corrupting state when nothing is lost.
void test_discovery_completes_despite_one_dropped_advance_and_one_dropped_probe()
{
    for (int i = 0; i < NODE_COUNT; i++) {
        discoveries[i]        = new PanelDiscovery(EDGES_PER_NODE);
        fabricLinks[i].nodeIndex = i;
        drivers[i]             = new PanelDiscoveryDriver(*discoveries[i], fabricLinks[i]);
        routers[i]             = new PanelRouter(*discoveries[i], fabricLinks[i]);
        dispatchers[i]         = new PanelFrameDispatcher(*drivers[i], *routers[i]);
    }

    ControllerLink controllerLink;
    DiscoveryTreeBuilder treeBuilder(EDGES_PER_NODE);

    coordinator   = new DiscoveryCoordinator(controllerLink, &treeBuilder);
    fabricNow     = 0;
    deliveryHead  = 0;
    deliveryCount = 0;

    dropNextPullOnN0Edge1        = true;
    dropNextAdvanceFromController = true;

    coordinator->begin(fabricNow);
    pumpDeliveries();

    const int SAFETY_CAP = 200;
    int iterations = 0;

    while (!coordinator->isComplete() && iterations < SAFETY_CAP) {
        fabricNow += PanelDiscoveryDriver::PROBE_TIMEOUT_MS + 1;

        for (int i = 0; i < NODE_COUNT; i++) {
            drivers[i]->tick(fabricNow);
        }

        coordinator->tick(fabricNow);  // drives the ADVANCE resend the dropped one needs

        pumpDeliveries();

        iterations++;
    }

    TEST_ASSERT_TRUE_MESSAGE(coordinator->isComplete(), "discovery did not complete -- stuck or looping");
    TEST_ASSERT_FALSE_MESSAGE(coordinator->walkStalled(), "should have recovered via retry, not stalled");
    TEST_ASSERT_FALSE_MESSAGE(dropNextPullOnN0Edge1, "the drop-once probe must actually have fired during the walk");
    TEST_ASSERT_FALSE_MESSAGE(dropNextAdvanceFromController, "the drop-once ADVANCE must actually have fired during the walk");

    // Same final topology as the lossless run -- the two dropped frames only cost extra time.
    TEST_ASSERT_EQUAL_UINT16(1, drivers[0]->assignedPanelIndex());
    TEST_ASSERT_EQUAL_UINT16(2, drivers[1]->assignedPanelIndex());
    TEST_ASSERT_EQUAL_UINT16(3, drivers[2]->assignedPanelIndex());

    TEST_ASSERT_TRUE(discoveries[0]->isConnected(1));  // N0 <-> N1, despite the dropped first probe
    TEST_ASSERT_TRUE(discoveries[1]->isConnected(0));
    TEST_ASSERT_TRUE(discoveries[1]->isConnected(1));  // N1 <-> N2
    TEST_ASSERT_TRUE(discoveries[2]->isConnected(0));

    TEST_ASSERT_EQUAL_UINT8(3, treeBuilder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(2, treeBuilder.linkCount());

    for (int i = 0; i < NODE_COUNT; i++) {
        delete dispatchers[i];
        delete routers[i];
        delete drivers[i];
        delete discoveries[i];
    }

    delete coordinator;

    dropNextPullOnN0Edge1        = false;
    dropNextAdvanceFromController = false;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_discovery_completes_end_to_end_with_a_loop_and_two_empty_ports);
    RUN_TEST(test_discovery_completes_despite_one_dropped_advance_and_one_dropped_probe);

    return UNITY_END();
}
