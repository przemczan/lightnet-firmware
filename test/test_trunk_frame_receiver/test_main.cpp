// Host test for TrunkFrameReceiver — PacketFramer plus an inter-byte idle-gap reset, used by the
// controller's un-muxed trunk RX paths (ControllerDiscoveryService, ControllerRelayPacketSink).
//
// Reproduces the field failure this class exists to fix: a stray leading byte (e.g. line noise
// from a panel's own power-up reset) makes a bare PacketFramer latch onto a phony type/size and
// silently swallow every real frame that follows until enough bytes happen to realign. See
// TrunkFrameReceiver.hpp's own class comment.
//
// Run with: pio test -e native -f test_trunk_frame_receiver

#include <unity.h>

#include "Core/Relay/TrunkFrameReceiver.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

static bool feed(TrunkFrameReceiver &receiver, const uint8_t *bytes, uint8_t size, uint32_t nowMs)
{
    bool ready = false;

    for (uint8_t i = 0; i < size; i++) {
        ready = receiver.onByte(bytes[i], nowMs);
    }

    return ready;
}

void test_clean_frame_parses_with_no_gap()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);
    const uint8_t *bytes = (const uint8_t *)&ack;
    TrunkFrameReceiver receiver;

    TEST_ASSERT_TRUE(feed(receiver, bytes, sizeof(ack), 1000));
    TEST_ASSERT_EQUAL_MEMORY(&ack, receiver.frame(), sizeof(ack));
}

void test_small_inter_byte_gaps_do_not_reset_an_in_progress_frame()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);
    const uint8_t *bytes = (const uint8_t *)&ack;
    TrunkFrameReceiver receiver;
    uint32_t nowMs = 1000;
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(ack); i++) {
        ready = receiver.onByte(bytes[i], nowMs);
        nowMs += TrunkFrameReceiver::IDLE_GAP_RESET_MS - 1;  // always just under the threshold
    }

    TEST_ASSERT_TRUE_MESSAGE(ready, "a gap under the threshold must never discard an in-flight frame");
    TEST_ASSERT_EQUAL_MEMORY(&ack, receiver.frame(), sizeof(ack));
}

void test_mid_frame_gap_at_or_above_threshold_discards_the_partial_frame()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);
    const uint8_t *bytes = (const uint8_t *)&ack;
    TrunkFrameReceiver receiver;

    // Only the type byte lands -- a genuine frame in flight, per PacketFramer's own rules.
    TEST_ASSERT_FALSE(receiver.onByte(bytes[0], 1000));

    // A gap at least the reset threshold, then the frame's own remaining bytes arrive right on
    // schedule. Without the idle-gap reset, PacketFramer has no notion of time and would happily
    // reassemble these into the very same valid ack frame -- accidentally masking whether a
    // reset actually happened. Feeding a byte that reads as noise instead (0xFF is never a
    // recognized packetType_t) makes this properly outcome-distinguishing: reset means "scanning
    // for a fresh type byte, sees noise, never completes"; no reset would mean "byte 2 of 7 of
    // the still-live ack frame" -- which stays incomplete either way at this point, but only the
    // reset case is guaranteed to keep behaving this way as more bytes arrive.
    TEST_ASSERT_FALSE(receiver.onByte(0xFF, 1000 + TrunkFrameReceiver::IDLE_GAP_RESET_MS));

    bool ready = feed(receiver, bytes, sizeof(ack), 1000 + TrunkFrameReceiver::IDLE_GAP_RESET_MS);

    TEST_ASSERT_TRUE_MESSAGE(ready, "must resync on a fresh type byte, not stay stuck mid a stale frame");
    TEST_ASSERT_EQUAL_MEMORY(&ack, receiver.frame(), sizeof(ack));
}

void test_recovers_and_parses_next_valid_frame_after_gap_reset()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);
    const uint8_t *bytes = (const uint8_t *)&ack;
    TrunkFrameReceiver receiver;

    TEST_ASSERT_FALSE(receiver.onByte(bytes[0], 1000));  // partial frame stranded

    uint32_t resumeMs = 1000 + TrunkFrameReceiver::IDLE_GAP_RESET_MS;

    TEST_ASSERT_TRUE_MESSAGE(feed(receiver, bytes, sizeof(ack), resumeMs), "a clean frame after the gap must parse");
    TEST_ASSERT_EQUAL_MEMORY(&ack, receiver.frame(), sizeof(ack));
}

// The exact field failure: a leading byte that happens to equal a real packetType_t value (0x0C
// = PACKET_ANIMATION_PREPARE = 12, the protocolVersion byte of every v12 frame) makes the framer
// commit to a 28-byte frame that will never complete, swallowing whatever real traffic follows
// until enough bytes happen to realign on their own -- observed in production as
// "[BUS] rx type 12 panel 25344 invalid" repeating across multiple discovery pull cycles.

void test_recovers_from_type_12_desync_and_parses_subsequent_frames()
{
    TrunkFrameReceiver receiver;
    uint32_t nowMs = 1000;

    // Garbage byte that happens to be a recognized type (12 = PACKET_ANIMATION_PREPARE, 28 bytes)
    // followed by a handful of bytes that don't complete it -- exactly what a stray version-byte
    // desync looks like on the wire.
    TEST_ASSERT_FALSE(receiver.onByte(12, nowMs));
    TEST_ASSERT_FALSE(receiver.onByte(0, nowMs));
    TEST_ASSERT_FALSE(receiver.onByte(0, nowMs));

    nowMs += TrunkFrameReceiver::IDLE_GAP_RESET_MS;

    Protocol::PacketInitializationPull pull =
        Protocol::makePacket<Protocol::PacketInitializationPull>(Protocol::PACKET_INITIALIZATION_PULL);

    pull.panelIndex = 1;
    pull.parentEdgeIndex = 0;

    const uint8_t *pullBytes = (const uint8_t *)&pull;

    TEST_ASSERT_TRUE_MESSAGE(
        feed(receiver, pullBytes, sizeof(pull), nowMs),
        "the PULL echo must parse once the stale frame is discarded"
    );
    TEST_ASSERT_EQUAL_MEMORY(&pull, receiver.frame(), sizeof(pull));

    Protocol::PacketRegisterEdge reply =
        Protocol::makePacket<Protocol::PacketRegisterEdge>(Protocol::PACKET_REGISTER_EDGE);

    reply.panelIndex = 1;
    reply.edgeIndex = 0;
    reply.parentEdgeIndex = 0;

    const uint8_t *replyBytes = (const uint8_t *)&reply;

    nowMs += TrunkFrameReceiver::IDLE_GAP_RESET_MS;

    TEST_ASSERT_TRUE_MESSAGE(
        feed(receiver, replyBytes, sizeof(reply), nowMs),
        "the REGISTER_EDGE reply must parse cleanly right after"
    );
    TEST_ASSERT_EQUAL_MEMORY(&reply, receiver.frame(), sizeof(reply));
}

void test_reset_discards_in_progress_frame()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);
    const uint8_t *bytes = (const uint8_t *)&ack;
    TrunkFrameReceiver receiver;

    receiver.onByte(bytes[0], 1000);
    receiver.reset();

    // Same nowMs, no gap -- proves reset() itself discarded the partial frame, not the gap logic.
    bool ready = feed(receiver, bytes, sizeof(ack), 1000);

    TEST_ASSERT_TRUE(ready);
    TEST_ASSERT_EQUAL_MEMORY(&ack, receiver.frame(), sizeof(ack));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_clean_frame_parses_with_no_gap);
    RUN_TEST(test_small_inter_byte_gaps_do_not_reset_an_in_progress_frame);
    RUN_TEST(test_mid_frame_gap_at_or_above_threshold_discards_the_partial_frame);
    RUN_TEST(test_recovers_and_parses_next_valid_frame_after_gap_reset);
    RUN_TEST(test_recovers_from_type_12_desync_and_parses_subsequent_frames);
    RUN_TEST(test_reset_discards_in_progress_frame);

    return UNITY_END();
}
