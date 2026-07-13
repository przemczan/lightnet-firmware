// Host test for EdgeFrameReceiver — tags a completed frame with the edge it arrived on, given
// one shared USART + mux rather than one receiver per edge (hardware redesign plan §11.2).
//
// Run with: pio test -e native -f test_edge_frame_receiver

#include <unity.h>

#include "Core/Relay/EdgeFrameReceiver.hpp"
#include "Core/Common/ProtocolMeta.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

static void feedFrame(EdgeFrameReceiver &receiver, const Protocol::PacketMeta *packet, uint8_t size, uint32_t nowMs)
{
    const uint8_t *bytes = (const uint8_t *)packet;

    for (uint8_t i = 0; i + 1 < size; i++) {
        TEST_ASSERT_FALSE(receiver.onByte(bytes[i], nowMs));
    }

    TEST_ASSERT_TRUE(receiver.onByte(bytes[size - 1], nowMs));
}

// --- onEdgeWake --------------------------------------------------------------------------

void test_wake_claims_edge_when_idle()
{
    EdgeFrameReceiver receiver;

    TEST_ASSERT_TRUE(receiver.onEdgeWake(2, 0));
}

void test_wake_on_different_edge_ignored_while_claimed()
{
    EdgeFrameReceiver receiver;

    TEST_ASSERT_TRUE(receiver.onEdgeWake(0, 0));
    TEST_ASSERT_FALSE(receiver.onEdgeWake(1, 1));
}

void test_wake_on_same_edge_ignored_while_already_claimed()
{
    EdgeFrameReceiver receiver;

    TEST_ASSERT_TRUE(receiver.onEdgeWake(1, 0));
    TEST_ASSERT_FALSE(receiver.onEdgeWake(1, 1));
}

// --- onByte --------------------------------------------------------------------------------

void test_byte_dropped_without_active_claim()
{
    EdgeFrameReceiver receiver;

    TEST_ASSERT_FALSE(receiver.onByte(0xAA, 0));
}

void test_full_frame_tagged_with_claimed_edge()
{
    EdgeFrameReceiver receiver;
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);

    TEST_ASSERT_TRUE(receiver.onEdgeWake(2, 0));
    feedFrame(receiver, &ack, sizeof(ack), 5);

    TEST_ASSERT_EQUAL_UINT8(2, receiver.fromEdge());
    TEST_ASSERT_EQUAL_UINT8(sizeof(ack), receiver.frameSize());
    TEST_ASSERT_EQUAL_MEMORY(&ack, receiver.frame(), sizeof(ack));
}

void test_claim_released_after_frame_completes()
{
    EdgeFrameReceiver receiver;
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);

    TEST_ASSERT_TRUE(receiver.onEdgeWake(0, 0));
    feedFrame(receiver, &ack, sizeof(ack), 5);

    // The claim is released the instant the frame completes -- a wake on a different edge
    // (or the same one) must succeed right away, with no separate "unclaim" step needed.
    TEST_ASSERT_TRUE(receiver.onEdgeWake(1, 6));
}

void test_second_frame_tagged_with_its_own_edge()
{
    EdgeFrameReceiver receiver;
    Protocol::PacketMeta ack  = Protocol::makeMeta(Protocol::PACKET_ACK);
    Protocol::PacketMeta ack2 = Protocol::makeMeta(Protocol::PACKET_ACK);

    receiver.onEdgeWake(0, 0);
    feedFrame(receiver, &ack, sizeof(ack), 5);
    TEST_ASSERT_EQUAL_UINT8(0, receiver.fromEdge());

    receiver.onEdgeWake(1, 10);
    feedFrame(receiver, &ack2, sizeof(ack2), 15);
    TEST_ASSERT_EQUAL_UINT8(1, receiver.fromEdge());
}

void test_corrupted_header_crc_does_not_release_claim()
{
    EdgeFrameReceiver receiver;
    Protocol::PacketSetColor packet =
        Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);

    packet.meta.headerCrc ^= 0xFF;  // corrupt the header CRC itself

    const uint8_t *bytes = (const uint8_t *)&packet;

    TEST_ASSERT_TRUE(receiver.onEdgeWake(2, 0));

    bool ready = false;

    for (uint8_t i = 0; i < sizeof(packet); i++) {
        ready = receiver.onByte(bytes[i], 0);
    }

    TEST_ASSERT_FALSE_MESSAGE(ready, "a corrupted header CRC must never surface as a ready frame");

    // Still claimed -- a bad CRC is resolved by the underlying PacketFramer resyncing within the
    // same ongoing transmission, not by releasing the edge back to the mux.
    TEST_ASSERT_FALSE(receiver.onEdgeWake(0, 1));
}

// --- tick (stalled-claim recovery) ------------------------------------------------------------

void test_tick_leaves_claim_intact_before_timeout()
{
    EdgeFrameReceiver receiver;

    receiver.onEdgeWake(0, 0);
    receiver.tick(EdgeFrameReceiver::FRAME_TIMEOUT_MS - 1);

    TEST_ASSERT_FALSE(receiver.onEdgeWake(1, EdgeFrameReceiver::FRAME_TIMEOUT_MS - 1));
}

void test_tick_releases_stalled_claim_after_timeout()
{
    EdgeFrameReceiver receiver;

    receiver.onEdgeWake(0, 0);
    receiver.tick(EdgeFrameReceiver::FRAME_TIMEOUT_MS);

    TEST_ASSERT_TRUE(receiver.onEdgeWake(1, EdgeFrameReceiver::FRAME_TIMEOUT_MS));
}

void test_byte_activity_resets_the_timeout_clock()
{
    EdgeFrameReceiver receiver;

    receiver.onEdgeWake(0, 0);
    receiver.onByte(Protocol::PACKET_ACK, EdgeFrameReceiver::FRAME_TIMEOUT_MS - 1);

    // A byte just arrived one tick before the deadline -- the claim must survive past the
    // original deadline, since activity resets the clock.
    receiver.tick(EdgeFrameReceiver::FRAME_TIMEOUT_MS);

    TEST_ASSERT_FALSE(receiver.onEdgeWake(1, EdgeFrameReceiver::FRAME_TIMEOUT_MS));
}

void test_claim_force_released_after_max_claim_duration_despite_continuous_activity()
{
    EdgeFrameReceiver receiver;

    receiver.onEdgeWake(0, 0);

    // A byte arrives just before every FRAME_TIMEOUT_MS deadline, forever -- the gap-based
    // recovery above can never fire on its own. A continuously noisy/floating claimed edge must
    // not be able to starve every other edge's real wake indefinitely (see MAX_CLAIM_MS's own
    // comment), so the claim must still force-release once total claim duration elapses.
    for (uint32_t t = EdgeFrameReceiver::FRAME_TIMEOUT_MS - 1; t < EdgeFrameReceiver::MAX_CLAIM_MS;
         t += EdgeFrameReceiver::FRAME_TIMEOUT_MS - 1) {
        receiver.onByte(0xAA, t);
        receiver.tick(t);
    }

    TEST_ASSERT_FALSE_MESSAGE(
        receiver.onEdgeWake(1, EdgeFrameReceiver::MAX_CLAIM_MS - 1),
        "claim must still be held just before MAX_CLAIM_MS elapses"
    );

    receiver.tick(EdgeFrameReceiver::MAX_CLAIM_MS);

    TEST_ASSERT_TRUE_MESSAGE(
        receiver.onEdgeWake(1, EdgeFrameReceiver::MAX_CLAIM_MS),
        "claim must be force-released once MAX_CLAIM_MS elapses, even with continuous activity"
    );
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_wake_claims_edge_when_idle);
    RUN_TEST(test_wake_on_different_edge_ignored_while_claimed);
    RUN_TEST(test_wake_on_same_edge_ignored_while_already_claimed);
    RUN_TEST(test_byte_dropped_without_active_claim);
    RUN_TEST(test_full_frame_tagged_with_claimed_edge);
    RUN_TEST(test_claim_released_after_frame_completes);
    RUN_TEST(test_second_frame_tagged_with_its_own_edge);
    RUN_TEST(test_corrupted_header_crc_does_not_release_claim);
    RUN_TEST(test_tick_leaves_claim_intact_before_timeout);
    RUN_TEST(test_tick_releases_stalled_claim_after_timeout);
    RUN_TEST(test_byte_activity_resets_the_timeout_clock);
    RUN_TEST(test_claim_force_released_after_max_claim_duration_despite_continuous_activity);

    return UNITY_END();
}
