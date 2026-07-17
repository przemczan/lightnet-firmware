// Host test for the link-ARQ layer's pure pieces (Core/Relay/LinkArq.hpp): the
// PACKET_LINK_ACK wire entry, the isLinkAckedType() policy table, the LinkAckMatcher
// micro-framer a sender's ack window drains bytes through, and the LinkDedup duplicate
// window a receiver consults before dispatching.
//
// Run with: pio test -e native -f test_link_arq

#include <unity.h>
#include <string.h>

#include "Core/Relay/LinkArq.hpp"
#include "Core/Common/ProtocolMeta.hpp"
#include "Utils/Crc.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

// --- wire protocol entries ------------------------------------------------------------------

void test_link_ack_is_sized_and_version_exempt()
{
    TEST_ASSERT_EQUAL_UINT8(
        sizeof(Protocol::PacketLinkAck),
        Protocol::packetSizeForType(Protocol::PACKET_LINK_ACK)
    );
    TEST_ASSERT_TRUE(Protocol::isVersionExemptType(Protocol::PACKET_LINK_ACK));
}

void test_link_acked_policy_covers_control_and_replies()
{
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_ACK));
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_TURN_ON_OFF));
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_ANIMATION_PREPARE));
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_ANIMATION_START));
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_SET_PALETTE));
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_FETCH_STATE));
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_FETCH_STATE_REPLY));
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_ENTER_BOOTLOADER));

    // BL-originated replies are hop-acked at panel-to-panel hops.
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_BOOTLOADER_PONG));
    TEST_ASSERT_TRUE(Protocol::isLinkAckedType(Protocol::PACKET_BOOTLOADER_WRITE_ACK));
}

void test_link_acked_policy_exemptions()
{
    // 60 fps self-healing stream.
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_SET_COLOR));

    // Discovery control plane has its own per-hop retries.
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_INITIALIZATION_PULL));
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_REGISTER_EDGE));
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_DISCOVERY_ADVANCE));
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_DISCOVERY_DONE));

    // BL-bound types: their receiver (the resident bootloader) does not speak link-ARQ.
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_BOOTLOADER_PING));
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_BOOTLOADER_WRITE_CHUNK));
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_BOOTLOADER_START_APP));

    // Never ack an ack.
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_LINK_ACK));
    TEST_ASSERT_FALSE(Protocol::isLinkAckedType(Protocol::PACKET_NOOP));
}

// --- LinkAckMatcher -------------------------------------------------------------------------

static Protocol::PacketLinkAck makeLinkAck(uint16_t frameCrc)
{
    Protocol::PacketLinkAck ack =
        Protocol::makePacket<Protocol::PacketLinkAck>(Protocol::PACKET_LINK_ACK);

    ack.frameCrc = frameCrc;

    return ack;
}

// Feeds `bytes` one at a time; returns true if the matcher completed, leaving the echoed crc
// in *outCrc.
static bool feed(LinkAckMatcher &matcher, const uint8_t *bytes, uint8_t count, uint16_t *outCrc)
{
    for (uint8_t i = 0; i < count; i++) {
        if (matcher.pushByte(bytes[i], outCrc)) {
            return true;
        }
    }

    return false;
}

void test_matcher_recognizes_valid_ack()
{
    LinkAckMatcher matcher;
    Protocol::PacketLinkAck ack = makeLinkAck(0xBEEF);
    uint16_t crc = 0;

    TEST_ASSERT_TRUE(feed(matcher, (const uint8_t *)&ack, sizeof(ack), &crc));
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, crc);
}

void test_matcher_skips_preamble_noise()
{
    LinkAckMatcher matcher;
    Protocol::PacketLinkAck ack = makeLinkAck(0x1234);
    uint8_t stream[2 + sizeof(ack)];

    stream[0] = 0xFF;
    stream[1] = 0xFF;
    memcpy(stream + 2, &ack, sizeof(ack));

    uint16_t crc = 0;

    TEST_ASSERT_TRUE(feed(matcher, stream, sizeof(stream), &crc));
    TEST_ASSERT_EQUAL_HEX16(0x1234, crc);
}

void test_matcher_rejects_corrupt_header_crc()
{
    LinkAckMatcher matcher;
    Protocol::PacketLinkAck ack = makeLinkAck(0x1234);

    ack.meta.headerCrc ^= 0x0001;

    uint16_t crc = 0;

    TEST_ASSERT_FALSE(feed(matcher, (const uint8_t *)&ack, sizeof(ack), &crc));
}

void test_matcher_ignores_foreign_frame_bytes()
{
    // Foreign bytes in the window (e.g. an early reply) must be consumed as noise without ever
    // completing -- and a genuine ack right after them must still match. Noise chosen with no
    // byte equal to PACKET_LINK_ACK, so nothing false-starts the matcher.
    LinkAckMatcher matcher;
    const uint8_t noise[] = { 0x01, 0x0D, 0x00, 0x03, 0x00, 0x42, 0x99 };
    uint16_t crc = 0;

    TEST_ASSERT_FALSE(feed(matcher, noise, sizeof(noise), &crc));

    Protocol::PacketLinkAck ack = makeLinkAck(0xA55A);

    TEST_ASSERT_TRUE(feed(matcher, (const uint8_t *)&ack, sizeof(ack), &crc));
    TEST_ASSERT_EQUAL_HEX16(0xA55A, crc);
}

void test_matcher_false_start_recovers_on_retransmitted_ack()
{
    // A noise byte equal to PACKET_LINK_ACK starts a bogus 9-byte candidate that swallows the
    // real ack's leading bytes -- that ack is lost to the window (accepted cost, see the
    // matcher's class comment), but the sender's retransmission must then match cleanly.
    LinkAckMatcher matcher;
    Protocol::PacketLinkAck ack = makeLinkAck(0xC3C3);
    uint16_t crc = 0;

    uint8_t falseStart = (uint8_t)Protocol::PACKET_LINK_ACK;

    TEST_ASSERT_FALSE(matcher.pushByte(falseStart, &crc));
    TEST_ASSERT_FALSE(feed(matcher, (const uint8_t *)&ack, sizeof(ack), &crc));

    // Peer re-acks the retransmitted frame.
    TEST_ASSERT_TRUE(feed(matcher, (const uint8_t *)&ack, sizeof(ack), &crc));
    TEST_ASSERT_EQUAL_HEX16(0xC3C3, crc);
}

void test_matcher_back_to_back_acks()
{
    LinkAckMatcher matcher;
    Protocol::PacketLinkAck first  = makeLinkAck(0x0001);
    Protocol::PacketLinkAck second = makeLinkAck(0x0002);
    uint16_t crc = 0;

    TEST_ASSERT_TRUE(feed(matcher, (const uint8_t *)&first, sizeof(first), &crc));
    TEST_ASSERT_EQUAL_HEX16(0x0001, crc);
    TEST_ASSERT_TRUE(feed(matcher, (const uint8_t *)&second, sizeof(second), &crc));
    TEST_ASSERT_EQUAL_HEX16(0x0002, crc);
}

void test_matcher_reset_discards_partial()
{
    LinkAckMatcher matcher;
    Protocol::PacketLinkAck ack = makeLinkAck(0x7777);
    uint16_t crc = 0;

    // Half the ack, then reset -- the remaining bytes must not complete anything.
    TEST_ASSERT_FALSE(feed(matcher, (const uint8_t *)&ack, sizeof(ack) / 2, &crc));
    matcher.reset();
    TEST_ASSERT_FALSE(
        feed(
            matcher,
            (const uint8_t *)&ack + sizeof(ack) / 2,
            sizeof(ack) - sizeof(ack) / 2,
            &crc
        )
    );
}

// --- LinkDedup ------------------------------------------------------------------------------

void test_dedup_first_sighting_is_not_duplicate()
{
    LinkDedup<3> dedup;

    TEST_ASSERT_FALSE(dedup.checkAndNote(0, 0x1111, 1000));
}

void test_dedup_repeat_within_window_is_duplicate()
{
    LinkDedup<3> dedup;

    dedup.checkAndNote(1, 0x2222, 1000);
    TEST_ASSERT_TRUE(dedup.checkAndNote(1, 0x2222, 1000 + LINK_DEDUP_WINDOW_MS - 1));
}

void test_dedup_repeat_after_window_is_fresh()
{
    // An end-to-end retry is byte-identical but arrives far outside the window -- it MUST be
    // dispatched again.
    LinkDedup<3> dedup;

    dedup.checkAndNote(1, 0x2222, 1000);
    TEST_ASSERT_FALSE(dedup.checkAndNote(1, 0x2222, 1000 + LINK_DEDUP_WINDOW_MS));
}

void test_dedup_different_crc_is_fresh()
{
    // Consecutive distinct frames can share a header (same type+target) -- only the full-frame
    // crc distinguishes them, and it must.
    LinkDedup<3> dedup;

    dedup.checkAndNote(0, 0x3333, 1000);
    TEST_ASSERT_FALSE(dedup.checkAndNote(0, 0x4444, 1001));
}

void test_dedup_edges_are_independent()
{
    LinkDedup<3> dedup;

    dedup.checkAndNote(0, 0x5555, 1000);
    TEST_ASSERT_FALSE(dedup.checkAndNote(2, 0x5555, 1001));
}

void test_dedup_repeats_extend_their_own_window()
{
    // A retransmission burst keeps refreshing the stamp, so its own later members still dedup
    // even if the first sighting has aged past the window.
    LinkDedup<3> dedup;

    dedup.checkAndNote(0, 0x6666, 1000);
    TEST_ASSERT_TRUE(dedup.checkAndNote(0, 0x6666, 1000 + LINK_DEDUP_WINDOW_MS - 10));
    TEST_ASSERT_TRUE(dedup.checkAndNote(0, 0x6666, 1000 + 2 * LINK_DEDUP_WINDOW_MS - 20));
}

void test_dedup_out_of_range_edge_is_never_duplicate()
{
    LinkDedup<3> dedup;

    TEST_ASSERT_FALSE(dedup.checkAndNote(7, 0x1111, 1000));
    TEST_ASSERT_FALSE(dedup.checkAndNote(7, 0x1111, 1001));
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_link_ack_is_sized_and_version_exempt);
    RUN_TEST(test_link_acked_policy_covers_control_and_replies);
    RUN_TEST(test_link_acked_policy_exemptions);

    RUN_TEST(test_matcher_recognizes_valid_ack);
    RUN_TEST(test_matcher_skips_preamble_noise);
    RUN_TEST(test_matcher_rejects_corrupt_header_crc);
    RUN_TEST(test_matcher_ignores_foreign_frame_bytes);
    RUN_TEST(test_matcher_back_to_back_acks);
    RUN_TEST(test_matcher_reset_discards_partial);

    RUN_TEST(test_dedup_first_sighting_is_not_duplicate);
    RUN_TEST(test_dedup_repeat_within_window_is_duplicate);
    RUN_TEST(test_dedup_repeat_after_window_is_fresh);
    RUN_TEST(test_dedup_different_crc_is_fresh);
    RUN_TEST(test_dedup_edges_are_independent);
    RUN_TEST(test_dedup_repeats_extend_their_own_window);
    RUN_TEST(test_dedup_out_of_range_edge_is_never_duplicate);

    return UNITY_END();
}
