// Host test for PacketFramer — recovers packet framing from a raw byte stream, the piece
// I2C never needed (each bus transaction carried its own length from the Wire layer) but the
// relay's shared UART does. Also covers Protocol::packetSizeForType(), the size table the
// framer is built on.
//
// Run with: pio test -e native -f test_packet_framer

#include <unity.h>
#include <string.h>

#include "Core/Relay/PacketFramer.hpp"

using namespace Lightnet;

void setUp()
{
}

void tearDown()
{
}

// --- packetSizeForType --------------------------------------------------------------------

void test_packet_size_for_type_known_types()
{
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketMeta), Protocol::packetSizeForType(Protocol::PACKET_ACK));
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketSetColor), Protocol::packetSizeForType(Protocol::PACKET_SET_COLOR));
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketRegisterEdge), Protocol::packetSizeForType(Protocol::PACKET_REGISTER_EDGE));
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketSetPalette), Protocol::packetSizeForType(Protocol::PACKET_SET_PALETTE));

    // Request and reply no longer share a type, so each resolves to its own distinct size.
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketMeta), Protocol::packetSizeForType(Protocol::PACKET_FETCH_STATE));
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketPanelState), Protocol::packetSizeForType(Protocol::PACKET_FETCH_STATE_REPLY));
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketMeta), Protocol::packetSizeForType(Protocol::PACKET_FETCH_ANIM_STATE));
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketAnimationStatus), Protocol::packetSizeForType(Protocol::PACKET_FETCH_ANIM_STATE_REPLY));

    // Now fully portable (raw RGB, no FastLED enum types) — sized like everything else.
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketPanelConfiguration), Protocol::packetSizeForType(Protocol::PACKET_PANEL_CONFIGURATION));
}

void test_packet_size_for_type_unknown_returns_zero()
{
    // Not a real enum value at all.
    TEST_ASSERT_EQUAL_UINT8(0, Protocol::packetSizeForType((Protocol::packetType_t)250));
}

// --- PacketFramer --------------------------------------------------------------------------

void test_framer_single_frame_byte_by_byte()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);
    const uint8_t *bytes = (const uint8_t *)&ack;

    PacketFramer framer;

    for (uint8_t i = 0; i + 1 < sizeof(ack); i++) {
        TEST_ASSERT_FALSE(framer.pushByte(bytes[i]));
    }

    TEST_ASSERT_TRUE(framer.pushByte(bytes[sizeof(ack) - 1]));
    TEST_ASSERT_EQUAL_UINT8(sizeof(ack), framer.frameSize());
    TEST_ASSERT_EQUAL_MEMORY(&ack, framer.frame(), sizeof(ack));
}

void test_framer_multi_field_packet_fed_all_at_once()
{
    Protocol::PacketSetColor packet = Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);

    packet.color.rgb = { 10, 20, 30 };

    const uint8_t *bytes = (const uint8_t *)&packet;
    PacketFramer framer;
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(packet); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_TRUE(ready);
    TEST_ASSERT_EQUAL_UINT8(sizeof(packet), framer.frameSize());
    TEST_ASSERT_EQUAL_MEMORY(&packet, framer.frame(), sizeof(packet));
}

void test_framer_corrupted_header_crc_resyncs()
{
    Protocol::PacketSetColor packet = Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);

    packet.meta.headerCrc ^= 0xFF;  // corrupt the header CRC itself

    uint8_t *bytes = (uint8_t *)&packet;
    PacketFramer framer;
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(packet); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_FALSE_MESSAGE(ready, "a corrupted header CRC must never surface as a ready frame");

    // Framer must have reset itself, not be stuck waiting for a frame that will never complete.
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);
    const uint8_t *ackBytes = (const uint8_t *)&ack;

    for (uint8_t i = 0; i + 1 < sizeof(ack); i++) {
        TEST_ASSERT_FALSE(framer.pushByte(ackBytes[i]));
    }

    TEST_ASSERT_TRUE(framer.pushByte(ackBytes[sizeof(ack) - 1]));
    TEST_ASSERT_EQUAL_MEMORY(&ack, framer.frame(), sizeof(ack));
}

void test_framer_skips_unrecognized_type_bytes()
{
    PacketFramer framer;

    // Noise: a handful of bytes that aren't any known packet type.
    TEST_ASSERT_FALSE(framer.pushByte(250));
    TEST_ASSERT_FALSE(framer.pushByte(0xFF));
    TEST_ASSERT_FALSE(framer.pushByte(99));

    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);
    const uint8_t *bytes = (const uint8_t *)&ack;

    for (uint8_t i = 0; i + 1 < sizeof(ack); i++) {
        TEST_ASSERT_FALSE(framer.pushByte(bytes[i]));
    }

    TEST_ASSERT_TRUE(framer.pushByte(bytes[sizeof(ack) - 1]));
    TEST_ASSERT_EQUAL_MEMORY(&ack, framer.frame(), sizeof(ack));
}

void test_framer_back_to_back_frames_with_no_explicit_reset()
{
    Protocol::PacketTurnOnOff first = Protocol::makePacket<Protocol::PacketTurnOnOff>(Protocol::PACKET_TURN_ON_OFF);

    first.on = 1;

    Protocol::PacketTurnOnOff second = Protocol::makePacket<Protocol::PacketTurnOnOff>(Protocol::PACKET_TURN_ON_OFF);

    second.on = 0;

    const uint8_t *firstBytes  = (const uint8_t *)&first;
    const uint8_t *secondBytes = (const uint8_t *)&second;

    PacketFramer framer;
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(first); i++) {
        ready = framer.pushByte(firstBytes[i]);
    }

    TEST_ASSERT_TRUE(ready);
    TEST_ASSERT_EQUAL_MEMORY(&first, framer.frame(), sizeof(first));

    // No framer.reset() call — the next pushByte() must start a fresh frame on its own.
    ready = false;

    for (uint8_t i = 0; i < sizeof(second); i++) {
        ready = framer.pushByte(secondBytes[i]);
    }

    TEST_ASSERT_TRUE(ready);
    TEST_ASSERT_EQUAL_MEMORY(&second, framer.frame(), sizeof(second));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_packet_size_for_type_known_types);
    RUN_TEST(test_packet_size_for_type_unknown_returns_zero);
    RUN_TEST(test_framer_single_frame_byte_by_byte);
    RUN_TEST(test_framer_multi_field_packet_fed_all_at_once);
    RUN_TEST(test_framer_corrupted_header_crc_resyncs);
    RUN_TEST(test_framer_skips_unrecognized_type_bytes);
    RUN_TEST(test_framer_back_to_back_frames_with_no_explicit_reset);

    return UNITY_END();
}
