// Host test for PacketFramer — recovers packet framing from a raw byte stream.
// Also covers Protocol::packetSizeForType(), the size table the
// framer is built on.
//
// Run with: pio test -e native -f test_packet_framer

#include <unity.h>
#include <string.h>

#include "Core/Relay/PacketFramer.hpp"
#include "Utils/Crc.hpp"

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

    // Relay OTA bootloader control plane — an intermediate app-mode panel must be able to size
    // (and so correctly relay) these even though it never acts on them itself.
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketMeta), Protocol::packetSizeForType(Protocol::PACKET_BOOTLOADER_PING));
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketBootloaderPong), Protocol::packetSizeForType(Protocol::PACKET_BOOTLOADER_PONG));
    TEST_ASSERT_EQUAL_UINT8(
        sizeof(Protocol::PacketBootloaderWriteChunk),
        Protocol::packetSizeForType(Protocol::PACKET_BOOTLOADER_WRITE_CHUNK)
    );
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketBootloaderWriteAck), Protocol::packetSizeForType(Protocol::PACKET_BOOTLOADER_WRITE_ACK));
    TEST_ASSERT_EQUAL_UINT8(sizeof(Protocol::PacketMeta), Protocol::packetSizeForType(Protocol::PACKET_BOOTLOADER_START_APP));
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

void test_has_partial_frame_tracks_in_progress_accumulation()
{
    Protocol::PacketSetColor packet = Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);
    const uint8_t *bytes = (const uint8_t *)&packet;
    PacketFramer framer;

    TEST_ASSERT_FALSE_MESSAGE(framer.hasPartialFrame(), "nothing accumulated yet");

    framer.pushByte(bytes[0]);
    TEST_ASSERT_TRUE_MESSAGE(framer.hasPartialFrame(), "type byte accepted, frame not yet complete");

    for (uint8_t i = 1; i + 1 < sizeof(packet); i++) {
        framer.pushByte(bytes[i]);
        TEST_ASSERT_TRUE(framer.hasPartialFrame());
    }

    framer.pushByte(bytes[sizeof(packet) - 1]);
    TEST_ASSERT_FALSE_MESSAGE(framer.hasPartialFrame(), "a completed frame is not \"partial\"");
}

void test_has_partial_frame_false_after_noise_byte()
{
    PacketFramer framer;

    framer.pushByte(0xFF);  // not a recognized type -- never starts accumulating
    TEST_ASSERT_FALSE(framer.hasPartialFrame());
}

void test_has_partial_frame_false_after_reset()
{
    Protocol::PacketSetColor packet = Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);
    const uint8_t *bytes = (const uint8_t *)&packet;
    PacketFramer framer;

    framer.pushByte(bytes[0]);
    TEST_ASSERT_TRUE(framer.hasPartialFrame());

    framer.reset();
    TEST_ASSERT_FALSE(framer.hasPartialFrame());
}

// --- validateProtocolVersion bypass (relay OTA bootloader only) ----------------------------

void test_framer_rejects_version_mismatch_by_default()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);

    ack.header.protocolVersion ^= 0xFF;
    ack.headerCrc = crc16(&ack.header, sizeof(ack.header));  // keep the header CRC itself valid

    const uint8_t *bytes = (const uint8_t *)&ack;
    PacketFramer framer;  // default: validateProtocolVersion = true
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(ack); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_FALSE_MESSAGE(ready, "a protocol-version mismatch must be rejected by default");
}

void test_framer_accepts_version_mismatch_when_bypassed()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK);

    ack.header.protocolVersion ^= 0xFF;
    ack.headerCrc = crc16(&ack.header, sizeof(ack.header));

    const uint8_t *bytes = (const uint8_t *)&ack;
    PacketFramer framer(/* validateProtocolVersion = */ false);
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(ack); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_TRUE_MESSAGE(ready, "the relay bootloader must accept a frame regardless of protocolVersion");
    TEST_ASSERT_EQUAL_MEMORY(&ack, framer.frame(), sizeof(ack));
}

// PACKET_RESET_DEVICE/PACKET_ENTER_BOOTLOADER must reach a version-mismatched panel even through
// a *default*-constructed (validateProtocolVersion = true) framer, the one every normal RX path
// uses — a stuck panel has to be resettable/reflashable without every other type also going
// unchecked.

void test_framer_accepts_reset_device_despite_version_mismatch_by_default()
{
    Protocol::PacketMeta reset = Protocol::makeMeta(Protocol::PACKET_RESET_DEVICE);

    reset.header.protocolVersion ^= 0xFF;
    reset.headerCrc = crc16(&reset.header, sizeof(reset.header));

    const uint8_t *bytes = (const uint8_t *)&reset;
    PacketFramer framer;  // default: validateProtocolVersion = true
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(reset); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_TRUE_MESSAGE(ready, "PACKET_RESET_DEVICE must be exempt from version validation");
    TEST_ASSERT_EQUAL_MEMORY(&reset, framer.frame(), sizeof(reset));
}

void test_framer_accepts_enter_bootloader_despite_version_mismatch_by_default()
{
    Protocol::PacketEnterBootloader enter =
        Protocol::makePacket<Protocol::PacketEnterBootloader>(Protocol::PACKET_ENTER_BOOTLOADER);

    enter.token = Protocol::BOOTLOADER_ENTRY_TOKEN;
    enter.meta.header.protocolVersion ^= 0xFF;
    enter.meta.headerCrc = crc16(&enter.meta.header, sizeof(enter.meta.header));

    const uint8_t *bytes = (const uint8_t *)&enter;
    PacketFramer framer;  // default: validateProtocolVersion = true
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(enter); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_TRUE_MESSAGE(ready, "PACKET_ENTER_BOOTLOADER must be exempt from version validation");
    TEST_ASSERT_EQUAL_MEMORY(&enter, framer.frame(), sizeof(enter));
}

// Discovery's own control plane must also survive a version mismatch — otherwise a controller
// reboot that re-runs discovery at a newer protocolVersion than a not-yet-flashed panel is still
// running would never even find that panel: it would look identical to an empty, unwired port
// and drop out of the discovered tree with no way back in (see docs/ota.md's "Flashing order and
// reboot safety").

void test_framer_accepts_initialization_pull_despite_version_mismatch_by_default()
{
    Protocol::PacketInitializationPull pull =
        Protocol::makePacket<Protocol::PacketInitializationPull>(Protocol::PACKET_INITIALIZATION_PULL);

    pull.panelIndex = 3;
    pull.parentEdgeIndex = 1;
    pull.meta.header.protocolVersion ^= 0xFF;
    pull.meta.headerCrc = crc16(&pull.meta.header, sizeof(pull.meta.header));

    const uint8_t *bytes = (const uint8_t *)&pull;
    PacketFramer framer;  // default: validateProtocolVersion = true
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(pull); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_TRUE_MESSAGE(ready, "PACKET_INITIALIZATION_PULL must be exempt from version validation");
    TEST_ASSERT_EQUAL_MEMORY(&pull, framer.frame(), sizeof(pull));
}

void test_framer_accepts_register_edge_despite_version_mismatch_by_default()
{
    Protocol::PacketRegisterEdge reply =
        Protocol::makePacket<Protocol::PacketRegisterEdge>(Protocol::PACKET_REGISTER_EDGE);

    reply.panelIndex = 3;
    reply.edgeIndex = 0;
    reply.parentEdgeIndex = 1;
    reply.meta.header.protocolVersion ^= 0xFF;
    reply.meta.headerCrc = crc16(&reply.meta.header, sizeof(reply.meta.header));

    const uint8_t *bytes = (const uint8_t *)&reply;
    PacketFramer framer;  // default: validateProtocolVersion = true
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(reply); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_TRUE_MESSAGE(ready, "PACKET_REGISTER_EDGE must be exempt from version validation");
    TEST_ASSERT_EQUAL_MEMORY(&reply, framer.frame(), sizeof(reply));
}

void test_framer_accepts_discovery_advance_despite_version_mismatch_by_default()
{
    Protocol::PacketDiscoveryAdvance advance =
        Protocol::makePacket<Protocol::PacketDiscoveryAdvance>(Protocol::PACKET_DISCOVERY_ADVANCE);

    advance.assignIndex = 4;
    advance.meta.header.protocolVersion ^= 0xFF;
    advance.meta.headerCrc = crc16(&advance.meta.header, sizeof(advance.meta.header));

    const uint8_t *bytes = (const uint8_t *)&advance;
    PacketFramer framer;  // default: validateProtocolVersion = true
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(advance); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_TRUE_MESSAGE(ready, "PACKET_DISCOVERY_ADVANCE must be exempt from version validation");
    TEST_ASSERT_EQUAL_MEMORY(&advance, framer.frame(), sizeof(advance));
}

void test_framer_accepts_discovery_done_despite_version_mismatch_by_default()
{
    Protocol::PacketDiscoveryDone done =
        Protocol::makePacket<Protocol::PacketDiscoveryDone>(Protocol::PACKET_DISCOVERY_DONE);

    done.panelIndex = 3;
    done.meta.header.protocolVersion ^= 0xFF;
    done.meta.headerCrc = crc16(&done.meta.header, sizeof(done.meta.header));

    const uint8_t *bytes = (const uint8_t *)&done;
    PacketFramer framer;  // default: validateProtocolVersion = true
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(done); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_TRUE_MESSAGE(ready, "PACKET_DISCOVERY_DONE must be exempt from version validation");
    TEST_ASSERT_EQUAL_MEMORY(&done, framer.frame(), sizeof(done));
}

void test_framer_still_rejects_other_types_with_version_mismatch()
{
    Protocol::PacketTurnOnOff packet =
        Protocol::makePacket<Protocol::PacketTurnOnOff>(Protocol::PACKET_TURN_ON_OFF);

    packet.meta.header.protocolVersion ^= 0xFF;
    packet.meta.headerCrc = crc16(&packet.meta.header, sizeof(packet.meta.header));

    const uint8_t *bytes = (const uint8_t *)&packet;
    PacketFramer framer;
    bool ready = false;

    for (uint8_t i = 0; i < sizeof(packet); i++) {
        ready = framer.pushByte(bytes[i]);
    }

    TEST_ASSERT_FALSE_MESSAGE(ready, "the version exemption must not leak to other packet types");
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
    RUN_TEST(test_has_partial_frame_tracks_in_progress_accumulation);
    RUN_TEST(test_has_partial_frame_false_after_noise_byte);
    RUN_TEST(test_has_partial_frame_false_after_reset);
    RUN_TEST(test_framer_rejects_version_mismatch_by_default);
    RUN_TEST(test_framer_accepts_version_mismatch_when_bypassed);
    RUN_TEST(test_framer_accepts_reset_device_despite_version_mismatch_by_default);
    RUN_TEST(test_framer_accepts_enter_bootloader_despite_version_mismatch_by_default);
    RUN_TEST(test_framer_accepts_initialization_pull_despite_version_mismatch_by_default);
    RUN_TEST(test_framer_accepts_register_edge_despite_version_mismatch_by_default);
    RUN_TEST(test_framer_accepts_discovery_advance_despite_version_mismatch_by_default);
    RUN_TEST(test_framer_accepts_discovery_done_despite_version_mismatch_by_default);
    RUN_TEST(test_framer_still_rejects_other_types_with_version_mismatch);

    return UNITY_END();
}
