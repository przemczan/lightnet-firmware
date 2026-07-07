// Host test for PanelFrameDispatcher — the one decision LightnetPanel's new dispatch loop needs
// per arrived frame (hardware redesign plan §11.3/§11.4 step 4a): feed every frame to
// PanelDiscoveryDriver, feed everything except PACKET_INITIALIZATION_PULL to PanelRouter too, and
// report whether the caller's own application-packet switch should act on it locally (protocol
// v10's targetPanelIndex, once this panel has been assigned an index).
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
    discovery.onChildProbeAccepted(1);                                         // edge 1 = child

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

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_dispatch_false_when_unassigned);
    RUN_TEST(test_pull_is_never_routed);
    RUN_TEST(test_dispatch_true_for_broadcast_once_assigned);
    RUN_TEST(test_dispatch_true_for_own_target_once_assigned);
    RUN_TEST(test_dispatch_false_for_other_panels_target_but_frame_still_routed);

    return UNITY_END();
}
