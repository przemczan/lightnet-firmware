// Host test for PanelFrameDispatcher â€” the one decision LightnetPanel's dispatch loop needs per
// arrived frame (hardware redesign plan Â§11.3/Â§11.4 step 4a): relay via PanelRouter first
// (skipping PACKET_INITIALIZATION_PULL and frames addressed to this panel â€” see the class
// comment on why relay order and the self-addressed rule protect probe replies), then feed the
// frame to PanelDiscoveryDriver, and report whether the caller's own application-packet switch
// should act on it locally (protocol v10's targetPanelIndex, once this panel has been assigned
// an index).
//
// Uses the real PanelDiscovery/PanelDiscoveryDriver/PanelRouter (all pure themselves) behind a
// mock IEdgeLink, so this proves the real sequencing, not a stand-in for it.
//
// Run with: pio test -e native -f test_panel_frame_dispatcher

#include <unity.h>
#include <string.h>

#include "Core/Relay/PanelFrameDispatcher.hpp"
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

static Protocol::PacketTurnOnOff makeTurnOnOff(uint16_t targetPanelIndex)
{
    return Protocol::makePacket<Protocol::PacketTurnOnOff>(Protocol::PACKET_TURN_ON_OFF, targetPanelIndex);
}

static Protocol::PacketDiscoveryAdvance makeAdvance(uint16_t targetPanelIndex, uint16_t assignIndex)
{
    Protocol::PacketDiscoveryAdvance advance =
        Protocol::makePacket<Protocol::PacketDiscoveryAdvance>(Protocol::PACKET_DISCOVERY_ADVANCE, targetPanelIndex);

    advance.assignIndex = assignIndex;

    return advance;
}

static Protocol::PacketRegisterEdge makeRejectedRegisterEdge()
{
    Protocol::PacketRegisterEdge reply =
        Protocol::makePacket<Protocol::PacketRegisterEdge>(Protocol::PACKET_REGISTER_EDGE);

    reply.panelIndex = Protocol::DISCOVERY_REJECTED_INDEX;

    return reply;
}

// --- Tests --------------------------------------------------------------------------------

void test_dispatch_false_when_unassigned()
{
    MockEdgeLink link;
    PanelDiscovery discovery(3);
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    Protocol::PacketTurnOnOff onOff = makeTurnOnOff(0);  // broadcast

    TEST_ASSERT_FALSE(dispatcher.onFrameArrived(0, Protocol::packetMeta(onOff), sizeof(onOff), 0));
}

void test_pull_is_never_routed()
{
    MockEdgeLink link;
    PanelDiscovery discovery(3);
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    // First PULL: accepted, becomes this panel's parent edge (edge 0). The driver replies
    // directly -- 1 send.
    Protocol::PacketInitializationPull pull1 = makePull(5);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(pull1), sizeof(pull1), 0);
    TEST_ASSERT_EQUAL(1, link.count);

    // Second PULL arrives on a *different*, non-parent edge -- this panel already has a parent,
    // so PanelDiscovery rejects it as a loop. If this frame were (bug) also fed to PanelRouter,
    // its upstream rule ("arrived on any non-parent edge -> route to parent") would relay it to
    // edge 0 -- a third send. It must not: only the driver's own direct rejection reply (on
    // edge 2) is expected.
    Protocol::PacketInitializationPull pull2 = makePull(99);

    dispatcher.onFrameArrived(2, Protocol::packetMeta(pull2), sizeof(pull2), 1);

    TEST_ASSERT_EQUAL_MESSAGE(2, link.count, "PACKET_INITIALIZATION_PULL must never reach PanelRouter");
}

void test_dispatch_true_for_broadcast_once_assigned()
{
    MockEdgeLink link;
    PanelDiscovery discovery(3);
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    Protocol::PacketInitializationPull pull = makePull(7);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);

    Protocol::PacketTurnOnOff onOff = makeTurnOnOff(0);  // broadcast

    TEST_ASSERT_TRUE(dispatcher.onFrameArrived(0, Protocol::packetMeta(onOff), sizeof(onOff), 1));
}

void test_dispatch_true_for_own_target_once_assigned()
{
    MockEdgeLink link;
    PanelDiscovery discovery(3);
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    Protocol::PacketInitializationPull pull = makePull(7);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);

    Protocol::PacketTurnOnOff onOff = makeTurnOnOff(7);  // this panel's own index

    TEST_ASSERT_TRUE(dispatcher.onFrameArrived(0, Protocol::packetMeta(onOff), sizeof(onOff), 1));
}

void test_dispatch_false_for_other_panels_target_but_frame_still_routed()
{
    MockEdgeLink link;
    PanelDiscovery discovery(3);
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    Protocol::PacketInitializationPull pull = makePull(7);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);  // parent = edge 0
    discovery.onChildProbeAccepted(1, 8);                                      // edge 1 = child panel 8

    int sendsBefore = link.count;

    Protocol::PacketTurnOnOff onOff = makeTurnOnOff(42);  // some other panel's index

    bool shouldDispatchLocally = dispatcher.onFrameArrived(0, Protocol::packetMeta(onOff), sizeof(onOff), 2);

    TEST_ASSERT_FALSE(shouldDispatchLocally);
    TEST_ASSERT_EQUAL_MESSAGE(
        sendsBefore + 1,
        link.count,
        "not-for-me traffic must still be relayed downstream by PanelRouter"
    );
    TEST_ASSERT_EQUAL_UINT8(1, link.sentToEdge[link.count - 1]);  // flooded out the connected child edge
}

void test_self_addressed_frame_not_routed()
{
    MockEdgeLink link;
    PanelDiscovery discovery(3);
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    Protocol::PacketInitializationPull pull = makePull(7);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);  // parent = edge 0
    discovery.onChildProbeAccepted(1, 8);                                      // edge 1 = child panel 8

    int sendsBefore = link.count;

    Protocol::PacketTurnOnOff onOff = makeTurnOnOff(7);  // this panel's own index

    bool shouldDispatchLocally = dispatcher.onFrameArrived(0, Protocol::packetMeta(onOff), sizeof(onOff), 1);

    TEST_ASSERT_TRUE(shouldDispatchLocally);
    TEST_ASSERT_EQUAL_MESSAGE(
        sendsBefore,
        link.count,
        "a frame addressed to this panel terminates here -- it must not be relayed downstream"
    );
}

void test_broadcast_still_routed_and_dispatched()
{
    MockEdgeLink link;
    PanelDiscovery discovery(3);
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    Protocol::PacketInitializationPull pull = makePull(7);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);  // parent = edge 0
    discovery.onChildProbeAccepted(1, 8);                                      // edge 1 = child panel 8

    int sendsBefore = link.count;

    Protocol::PacketTurnOnOff onOff = makeTurnOnOff(0);  // broadcast: for me AND everyone below

    bool shouldDispatchLocally = dispatcher.onFrameArrived(0, Protocol::packetMeta(onOff), sizeof(onOff), 1);

    TEST_ASSERT_TRUE(shouldDispatchLocally);
    TEST_ASSERT_EQUAL_MESSAGE(
        sendsBefore + 1,
        link.count,
        "a broadcast is acted on locally but must still flood to the connected child"
    );
    TEST_ASSERT_EQUAL_UINT8(1, link.sentToEdge[link.count - 1]);
}

void test_advance_to_self_probes_without_flooding()
{
    MockEdgeLink link;
    PanelDiscovery discovery(3);
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    Protocol::PacketInitializationPull pull = makePull(7);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);  // parent = edge 0
    discovery.onChildProbeAccepted(1, 8);                                      // edge 1 = child panel 8

    int sendsBefore = link.count;

    // ADVANCE addressed to this panel: the driver must probe the remaining unexplored edge
    // (edge 2), and that PULL must be the ONLY transmission -- flooding the ADVANCE down the
    // connected child edge would still be on the wire (self-echo mask held) when the probed
    // child's reply arrives, and the reply would be discarded.
    Protocol::PacketDiscoveryAdvance advance = makeAdvance(7, 9);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(advance), sizeof(advance), 1);

    TEST_ASSERT_EQUAL_MESSAGE(
        sendsBefore + 1,
        link.count,
        "a self-addressed ADVANCE must produce exactly one send: the probe PULL, no flood"
    );
    TEST_ASSERT_EQUAL_UINT8(2, link.sentToEdge[link.count - 1]);
    TEST_ASSERT_EQUAL_UINT8(
        Protocol::PACKET_INITIALIZATION_PULL,
        link.frameAt(link.count - 1)->header.type
    );
}

void test_relay_transmitted_before_drivers_own_reaction()
{
    MockEdgeLink link;
    PanelDiscovery discovery(4);  // parent + connected child + two unexplored edges
    PanelDiscoveryDriver driver(discovery, link);
    PanelRouter router(discovery, link);
    PanelFrameDispatcher dispatcher(driver, router);

    Protocol::PacketInitializationPull pull = makePull(7);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(pull), sizeof(pull), 0);  // parent = edge 0
    discovery.onChildProbeAccepted(1, 8);                                      // edge 1 = child panel 8

    Protocol::PacketDiscoveryAdvance advance = makeAdvance(7, 9);

    dispatcher.onFrameArrived(0, Protocol::packetMeta(advance), sizeof(advance), 1);  // probing edge 2

    int sendsBefore = link.count;

    // A loop-rejection reply arrives on the probed edge. Two sends result: PanelRouter relays
    // the reply upstream (parent, edge 0), and the driver moves on to probe edge 3. The relay
    // must hit the wire FIRST -- transmitted after the new PULL, it would overlap the probed
    // child's reply while the transport's self-echo mask discards all RX.
    Protocol::PacketRegisterEdge rejection = makeRejectedRegisterEdge();

    dispatcher.onFrameArrived(2, Protocol::packetMeta(rejection), sizeof(rejection), 2);

    TEST_ASSERT_EQUAL(sendsBefore + 2, link.count);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        Protocol::PACKET_REGISTER_EDGE,
        link.frameAt(sendsBefore)->header.type,
        "the upstream relay must be transmitted before the driver's next probe PULL"
    );
    TEST_ASSERT_EQUAL_UINT8(0, link.sentToEdge[sendsBefore]);
    TEST_ASSERT_EQUAL_UINT8(Protocol::PACKET_INITIALIZATION_PULL, link.frameAt(sendsBefore + 1)->header.type);
    TEST_ASSERT_EQUAL_UINT8(3, link.sentToEdge[sendsBefore + 1]);
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_dispatch_false_when_unassigned);
    RUN_TEST(test_pull_is_never_routed);
    RUN_TEST(test_dispatch_true_for_broadcast_once_assigned);
    RUN_TEST(test_dispatch_true_for_own_target_once_assigned);
    RUN_TEST(test_dispatch_false_for_other_panels_target_but_frame_still_routed);
    RUN_TEST(test_self_addressed_frame_not_routed);
    RUN_TEST(test_broadcast_still_routed_and_dispatched);
    RUN_TEST(test_advance_to_self_probes_without_flooding);
    RUN_TEST(test_relay_transmitted_before_drivers_own_reaction);

    return UNITY_END();
}
