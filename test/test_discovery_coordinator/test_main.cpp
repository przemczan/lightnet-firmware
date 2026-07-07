// Host test for DiscoveryCoordinator — the controller's half of the relay discovery protocol.
// Proves the depth-first resume-stack sequencing: pushing the frontier on a new registration,
// popping on DISCOVERY_DONE, and completing only once the stack unwinds back to the controller
// sentinel. Uses a mock IEdgeLink; the real controller-side UART trunk transport doesn't exist
// yet (see the plan file).
//
// Run with: pio test -e native -f test_discovery_coordinator

#include <unity.h>
#include <string.h>

#include "Core/Relay/DiscoveryCoordinator.hpp"
#include "Core/Relay/DiscoveryTreeBuilder.hpp"
#include "Core/Common/ProtocolMeta.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

// --- Mock -------------------------------------------------------------------

struct MockEdgeLink : public IEdgeLink {
    static const int MAX = 16;
    uint8_t          sentToEdge[MAX];
    uint8_t          sentBuf[MAX][Protocol::MAX_PACKET_SIZE];
    uint8_t          sentSize[MAX];
    int              count = 0;

    void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override
    {
        if (count < MAX) {
            sentToEdge[count] = edgeIndex;
            memcpy(sentBuf[count], packet, size);
            sentSize[count] = size;
            count++;
        }
    }

    const Protocol::PacketMeta *frameAt(int i) const
    {
        return (const Protocol::PacketMeta *)sentBuf[i];
    }
};

static Protocol::PacketRegisterEdge makeReply(uint16_t panelIndex, uint16_t edgeIndex, uint16_t parentEdgeIndex = 0)
{
    Protocol::PacketRegisterEdge reply =
        Protocol::makePacket<Protocol::PacketRegisterEdge>(Protocol::PACKET_REGISTER_EDGE);

    reply.panelIndex      = panelIndex;
    reply.edgeIndex       = edgeIndex;
    reply.parentEdgeIndex = parentEdgeIndex;

    return reply;
}

static Protocol::PacketDiscoveryDone makeDone(uint16_t panelIndex)
{
    Protocol::PacketDiscoveryDone done =
        Protocol::makePacket<Protocol::PacketDiscoveryDone>(Protocol::PACKET_DISCOVERY_DONE);

    done.panelIndex = panelIndex;

    return done;
}

// --- Tests --------------------------------------------------------------------------------

void test_begin_sends_initialization_pull_assigning_index_1_on_the_trunk_edge()
{
    MockEdgeLink link;
    DiscoveryCoordinator coordinator(link);

    coordinator.begin();

    TEST_ASSERT_EQUAL(1, link.count);
    TEST_ASSERT_EQUAL_UINT8(DiscoveryCoordinator::TRUNK_EDGE, link.sentToEdge[0]);
    TEST_ASSERT_EQUAL_UINT8(Protocol::PACKET_INITIALIZATION_PULL, link.frameAt(0)->header.type);

    auto *pull = (const Protocol::PacketInitializationPull *)link.frameAt(0);

    TEST_ASSERT_EQUAL_UINT16(1, pull->panelIndex);
    TEST_ASSERT_FALSE(coordinator.isComplete());
}

void test_single_panel_tree_completes_after_one_registration_and_done()
{
    MockEdgeLink link;
    DiscoveryCoordinator coordinator(link);

    coordinator.begin();

    Protocol::PacketRegisterEdge rootReply = makeReply(1, 0);

    coordinator.onFrameArrived(Protocol::packetMeta(rootReply), sizeof(rootReply));

    TEST_ASSERT_EQUAL(2, link.count);
    TEST_ASSERT_EQUAL_UINT8(Protocol::PACKET_DISCOVERY_ADVANCE, link.frameAt(1)->header.type);

    auto *advance = (const Protocol::PacketDiscoveryAdvance *)link.frameAt(1);

    TEST_ASSERT_EQUAL_UINT16(1, advance->meta.header.targetPanelIndex);  // tell the root to try its own edges
    TEST_ASSERT_EQUAL_UINT16(2, advance->assignIndex);
    TEST_ASSERT_FALSE(coordinator.isComplete());

    // Root has no children -- it reports done straight away.
    Protocol::PacketDiscoveryDone rootDone = makeDone(1);

    coordinator.onFrameArrived(Protocol::packetMeta(rootDone), sizeof(rootDone));

    TEST_ASSERT_EQUAL_MESSAGE(2, link.count, "a DONE that unwinds to the sentinel sends nothing further");
    TEST_ASSERT_TRUE(coordinator.isComplete());
}

void test_two_panel_chain_backtracks_correctly_before_completing()
{
    MockEdgeLink link;
    DiscoveryCoordinator coordinator(link);

    coordinator.begin();  // [0] PULL{1}

    Protocol::PacketRegisterEdge rootReply = makeReply(1, 0);

    coordinator.onFrameArrived(Protocol::packetMeta(rootReply), sizeof(rootReply));  // [1] ADVANCE{target=1, assign=2}

    Protocol::PacketRegisterEdge childReply = makeReply(2, 1);

    coordinator.onFrameArrived(Protocol::packetMeta(childReply), sizeof(childReply));  // [2] ADVANCE{target=2, assign=3}

    TEST_ASSERT_EQUAL(3, link.count);

    auto *toChild = (const Protocol::PacketDiscoveryAdvance *)link.frameAt(2);

    TEST_ASSERT_EQUAL_UINT16(2, toChild->meta.header.targetPanelIndex);
    TEST_ASSERT_EQUAL_UINT16(3, toChild->assignIndex);
    TEST_ASSERT_FALSE(coordinator.isComplete());

    // Child has no further edges of its own -- backtrack to the root, same pending assign index.
    Protocol::PacketDiscoveryDone childDone = makeDone(2);

    coordinator.onFrameArrived(Protocol::packetMeta(childDone), sizeof(childDone));  // [3] ADVANCE{target=1, assign=3}

    TEST_ASSERT_EQUAL(4, link.count);

    auto *backToRoot = (const Protocol::PacketDiscoveryAdvance *)link.frameAt(3);

    TEST_ASSERT_EQUAL_UINT16(1, backToRoot->meta.header.targetPanelIndex);
    TEST_ASSERT_EQUAL_UINT16(3, backToRoot->assignIndex);  // unconsumed -- reused, not incremented
    TEST_ASSERT_FALSE(coordinator.isComplete());

    // Root, too, has no more edges -- the whole tree is resolved.
    Protocol::PacketDiscoveryDone rootDone = makeDone(1);

    coordinator.onFrameArrived(Protocol::packetMeta(rootDone), sizeof(rootDone));

    TEST_ASSERT_EQUAL_MESSAGE(4, link.count, "unwinding to the sentinel sends nothing further");
    TEST_ASSERT_TRUE(coordinator.isComplete());
}

void test_rejected_reply_is_ignored_not_pushed_or_advanced()
{
    MockEdgeLink link;
    DiscoveryCoordinator coordinator(link);

    coordinator.begin();

    Protocol::PacketRegisterEdge rejected = makeReply(Protocol::DISCOVERY_REJECTED_INDEX, 0);

    coordinator.onFrameArrived(Protocol::packetMeta(rejected), sizeof(rejected));

    TEST_ASSERT_EQUAL_MESSAGE(1, link.count, "a rejection must never be forwarded to the coordinator's stack");
    TEST_ASSERT_FALSE(coordinator.isComplete());
}

void test_tree_builder_records_root_and_link_when_provided()
{
    MockEdgeLink link;
    DiscoveryTreeBuilder treeBuilder(3);
    DiscoveryCoordinator coordinator(link, &treeBuilder);

    coordinator.begin();

    Protocol::PacketRegisterEdge rootReply = makeReply(1, /*edgeIndex*/ 0, /*parentEdgeIndex*/ 0);

    coordinator.onFrameArrived(Protocol::packetMeta(rootReply), sizeof(rootReply));

    TEST_ASSERT_EQUAL_UINT8(1, treeBuilder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(1, treeBuilder.indices()[0]);
    TEST_ASSERT_EQUAL_UINT8(0, treeBuilder.linkCount());  // root has no PanelGraph link of its own

    Protocol::PacketRegisterEdge childReply = makeReply(2, /*edgeIndex*/ 0, /*parentEdgeIndex*/ 2);

    coordinator.onFrameArrived(Protocol::packetMeta(childReply), sizeof(childReply));

    TEST_ASSERT_EQUAL_UINT8(2, treeBuilder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(1, treeBuilder.linkCount());

    const TopoLink &link0 = treeBuilder.links()[0];

    TEST_ASSERT_EQUAL_UINT8(1, link0.panelA);  // root
    TEST_ASSERT_EQUAL_UINT8(2, link0.edgeA);   // root's own edge the child was probed on
    TEST_ASSERT_EQUAL_UINT8(2, link0.panelB);  // new child
    TEST_ASSERT_EQUAL_UINT8(0, link0.edgeB);   // child's own edge facing the root
}

void test_rejected_reply_is_never_recorded_by_tree_builder()
{
    MockEdgeLink link;
    DiscoveryTreeBuilder treeBuilder(3);
    DiscoveryCoordinator coordinator(link, &treeBuilder);

    coordinator.begin();

    Protocol::PacketRegisterEdge rejected = makeReply(Protocol::DISCOVERY_REJECTED_INDEX, 0);

    coordinator.onFrameArrived(Protocol::packetMeta(rejected), sizeof(rejected));

    TEST_ASSERT_EQUAL_UINT8(0, treeBuilder.panelCount());
    TEST_ASSERT_EQUAL_UINT8(0, treeBuilder.linkCount());
}

void test_unrelated_packet_type_is_ignored()
{
    MockEdgeLink link;
    DiscoveryCoordinator coordinator(link);

    coordinator.begin();

    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);

    coordinator.onFrameArrived(&ack, sizeof(ack));

    TEST_ASSERT_EQUAL(1, link.count);
    TEST_ASSERT_FALSE(coordinator.isComplete());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_begin_sends_initialization_pull_assigning_index_1_on_the_trunk_edge);
    RUN_TEST(test_single_panel_tree_completes_after_one_registration_and_done);
    RUN_TEST(test_two_panel_chain_backtracks_correctly_before_completing);
    RUN_TEST(test_rejected_reply_is_ignored_not_pushed_or_advanced);
    RUN_TEST(test_tree_builder_records_root_and_link_when_provided);
    RUN_TEST(test_rejected_reply_is_never_recorded_by_tree_builder);
    RUN_TEST(test_unrelated_packet_type_is_ignored);

    return UNITY_END();
}
