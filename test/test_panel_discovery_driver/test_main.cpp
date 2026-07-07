// Host test for PanelDiscoveryDriver — the panel's half of the relay discovery protocol.
// Covers the local edge-probing sequence (accept/reject/timeout), the loop-rejection reply
// path, and the "stop after an accept, wait for the next ADVANCE" rule that makes the overall
// walk depth-first. Uses a real PanelDiscovery (the existing, unmodified decision table) and a
// mock IEdgeLink.
//
// Run with: pio test -e native -f test_panel_discovery_driver

#include <unity.h>
#include <string.h>

#include "Core/Relay/PanelDiscoveryDriver.hpp"
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

static Protocol::PacketInitializationPull makePull(uint16_t panelIndex)
{
    Protocol::PacketInitializationPull pull =
        Protocol::makePacket<Protocol::PacketInitializationPull>(Protocol::PACKET_INITIALIZATION_PULL);

    pull.panelIndex = panelIndex;

    return pull;
}

static Protocol::PacketRegisterEdge makeReply(uint16_t panelIndex, uint16_t edgeIndex)
{
    Protocol::PacketRegisterEdge reply =
        Protocol::makePacket<Protocol::PacketRegisterEdge>(Protocol::PACKET_REGISTER_EDGE);

    reply.panelIndex = panelIndex;
    reply.edgeIndex  = edgeIndex;

    return reply;
}

static Protocol::PacketDiscoveryAdvance makeAdvance(uint16_t target, uint16_t assign)
{
    Protocol::PacketDiscoveryAdvance advance =
        Protocol::makePacket<Protocol::PacketDiscoveryAdvance>(Protocol::PACKET_DISCOVERY_ADVANCE, target);

    advance.assignIndex = assign;

    return advance;
}

// --- Tests --------------------------------------------------------------------------------

void test_fresh_panel_accepts_pull_and_replies_with_assigned_index()
{
    PanelDiscovery discovery(3);
    MockEdgeLink link;
    PanelDiscoveryDriver driver(discovery, link);

    Protocol::PacketInitializationPull pull = makePull(5);

    driver.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);

    TEST_ASSERT_TRUE(discovery.hasParent());
    TEST_ASSERT_EQUAL_UINT8(0, discovery.parentEdge());

    TEST_ASSERT_EQUAL(1, link.count);
    TEST_ASSERT_EQUAL_UINT8(0, link.sentToEdge[0]);
    TEST_ASSERT_EQUAL_UINT8(Protocol::PACKET_REGISTER_EDGE, link.frameAt(0)->header.type);

    auto *reply = (const Protocol::PacketRegisterEdge *)link.frameAt(0);

    TEST_ASSERT_EQUAL_UINT16(5, reply->panelIndex);
    TEST_ASSERT_EQUAL_UINT16(0, reply->edgeIndex);
}

void test_idempotent_reoffer_on_parent_edge_keeps_the_original_index()
{
    PanelDiscovery discovery(3);
    MockEdgeLink link;
    PanelDiscoveryDriver driver(discovery, link);

    Protocol::PacketInitializationPull first = makePull(5);

    driver.onFrameArrived(0, Protocol::packetMeta(first), sizeof(first), 0);

    Protocol::PacketInitializationPull reoffer = makePull(999);  // must be ignored -- already assigned

    driver.onFrameArrived(0, Protocol::packetMeta(reoffer), sizeof(reoffer), 1);

    TEST_ASSERT_EQUAL(2, link.count);

    auto *reply = (const Protocol::PacketRegisterEdge *)link.frameAt(1);

    TEST_ASSERT_EQUAL_UINT16(5, reply->panelIndex);  // the original assignment, not 999
}

void test_pull_on_a_different_edge_after_registration_is_rejected_as_a_loop()
{
    PanelDiscovery discovery(3);
    MockEdgeLink link;
    PanelDiscoveryDriver driver(discovery, link);

    Protocol::PacketInitializationPull first = makePull(7);

    driver.onFrameArrived(0, Protocol::packetMeta(first), sizeof(first), 0);

    Protocol::PacketInitializationPull loopProbe = makePull(42);

    driver.onFrameArrived(1, Protocol::packetMeta(loopProbe), sizeof(loopProbe), 1);

    TEST_ASSERT_TRUE(discovery.isConnected(0));
    TEST_ASSERT_EQUAL_UINT8(Protocol::PACKET_REGISTER_EDGE, link.frameAt(1)->header.type);

    auto *reply = (const Protocol::PacketRegisterEdge *)link.frameAt(1);

    TEST_ASSERT_EQUAL_UINT16(Protocol::DISCOVERY_REJECTED_INDEX, reply->panelIndex);
    TEST_ASSERT_EQUAL_UINT16(1, reply->edgeIndex);
    TEST_ASSERT_FALSE(discovery.isConnected(1));
}

void test_advance_not_addressed_to_us_is_ignored()
{
    PanelDiscovery discovery(3);
    MockEdgeLink link;
    PanelDiscoveryDriver driver(discovery, link);

    Protocol::PacketInitializationPull pull = makePull(5);

    driver.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);  // link.count -> 1

    Protocol::PacketDiscoveryAdvance advance = makeAdvance(999, 6);

    driver.onFrameArrived(0, Protocol::packetMeta(advance), sizeof(advance), 10);

    TEST_ASSERT_EQUAL_MESSAGE(1, link.count, "an ADVANCE for a different panel must not start a local probe");
}

void test_full_local_sequence_probe_accept_reject_and_done()
{
    PanelDiscovery discovery(3);
    MockEdgeLink link;
    PanelDiscoveryDriver driver(discovery, link);

    // Becomes panel 5, parented on edge 0.
    Protocol::PacketInitializationPull pull = makePull(5);

    driver.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);
    TEST_ASSERT_EQUAL(1, link.count);

    // Told to advance -- probes its first non-parent Unexplored edge (1).
    Protocol::PacketDiscoveryAdvance advance1 = makeAdvance(5, 6);

    driver.onFrameArrived(0, Protocol::packetMeta(advance1), sizeof(advance1), 10);

    TEST_ASSERT_EQUAL(2, link.count);
    TEST_ASSERT_EQUAL_UINT8(1, link.sentToEdge[1]);
    TEST_ASSERT_EQUAL_UINT8(Protocol::PACKET_INITIALIZATION_PULL, link.frameAt(1)->header.type);
    TEST_ASSERT_EQUAL_UINT16(6, ((const Protocol::PacketInitializationPull *)link.frameAt(1))->panelIndex);

    // A new panel (8) accepts on edge 1 -- marked connected; the driver itself sends nothing
    // more (no explicit upstream relay -- that's PanelRouter's job when the caller also runs
    // this same frame through it, see PanelDiscoveryDriver.hpp point 3), and does NOT
    // immediately probe edge 2 on its own.
    Protocol::PacketRegisterEdge accepted = makeReply(8, 1);

    driver.onFrameArrived(1, Protocol::packetMeta(accepted), sizeof(accepted), 15);

    TEST_ASSERT_TRUE(discovery.isConnected(1));
    TEST_ASSERT_EQUAL_MESSAGE(2, link.count, "acceptance sends nothing itself and does not chain into the next edge");

    // Controller comes back once child 8's subtree is done -- this panel tries its next edge (2).
    Protocol::PacketDiscoveryAdvance advance2 = makeAdvance(5, 9);

    driver.onFrameArrived(0, Protocol::packetMeta(advance2), sizeof(advance2), 20);

    TEST_ASSERT_EQUAL(3, link.count);
    TEST_ASSERT_EQUAL_UINT8(2, link.sentToEdge[2]);
    TEST_ASSERT_EQUAL_UINT16(9, ((const Protocol::PacketInitializationPull *)link.frameAt(2))->panelIndex);

    // Edge 2 rejects (a loop on the far side) -- retried locally, no edges remain, reports done.
    Protocol::PacketRegisterEdge rejected = makeReply(Protocol::DISCOVERY_REJECTED_INDEX, 2);

    driver.onFrameArrived(2, Protocol::packetMeta(rejected), sizeof(rejected), 25);

    TEST_ASSERT_FALSE(discovery.isConnected(2));
    TEST_ASSERT_EQUAL(4, link.count);
    TEST_ASSERT_EQUAL_UINT8(0, link.sentToEdge[3]);
    TEST_ASSERT_EQUAL_UINT8(Protocol::PACKET_DISCOVERY_DONE, link.frameAt(3)->header.type);
    TEST_ASSERT_EQUAL_UINT16(5, ((const Protocol::PacketDiscoveryDone *)link.frameAt(3))->panelIndex);
}

void test_probe_timeout_marks_edge_not_connected_and_reports_done()
{
    PanelDiscovery discovery(2);
    MockEdgeLink link;
    PanelDiscoveryDriver driver(discovery, link);

    Protocol::PacketInitializationPull pull = makePull(3);

    driver.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);

    Protocol::PacketDiscoveryAdvance advance = makeAdvance(3, 4);

    driver.onFrameArrived(0, Protocol::packetMeta(advance), sizeof(advance), 0);  // probes edge 1 at t=0

    TEST_ASSERT_EQUAL(2, link.count);

    driver.tick(10);  // well before PROBE_TIMEOUT_MS
    TEST_ASSERT_EQUAL_MESSAGE(2, link.count, "must not time out early");

    driver.tick(PanelDiscoveryDriver::PROBE_TIMEOUT_MS + 10);

    TEST_ASSERT_FALSE(discovery.isConnected(1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EdgeLinkState::NotConnected, (uint8_t)discovery.edgeState(1));
    TEST_ASSERT_EQUAL(3, link.count);
    TEST_ASSERT_EQUAL_UINT8(Protocol::PACKET_DISCOVERY_DONE, link.frameAt(2)->header.type);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_fresh_panel_accepts_pull_and_replies_with_assigned_index);
    RUN_TEST(test_idempotent_reoffer_on_parent_edge_keeps_the_original_index);
    RUN_TEST(test_pull_on_a_different_edge_after_registration_is_rejected_as_a_loop);
    RUN_TEST(test_advance_not_addressed_to_us_is_ignored);
    RUN_TEST(test_full_local_sequence_probe_accept_reject_and_done);
    RUN_TEST(test_probe_timeout_marks_edge_not_connected_and_reports_done);

    return UNITY_END();
}
