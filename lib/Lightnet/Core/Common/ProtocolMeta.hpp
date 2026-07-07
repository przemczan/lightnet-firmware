#pragma once

// ProtocolMeta — pure PacketMeta stamping/validation + the protocol VERSION.
//
// Split out of Common/Protocol.hpp so the shared scene engine and the mobile C ABI
// can stamp/validate packets host-side without pulling Arduino/FastLED. The header CRC
// uses the pure Utils/Crc. Common/Protocol.hpp includes this and re-exposes it, so
// controller/panel call sites (`Protocol::setPacketMeta`, `Protocol::VERSION`) are
// unchanged.

#include <stddef.h>
#include <stdint.h>
#include "ProtocolTypes.hpp"

namespace Protocol {
    // I2C protocol version. Changing it requires flashing both controller and all
    // panels together. v6: layer compositing (composeMode/composeOrder/startDelayMs).
    // v7: FETCH_STATE/FETCH_ANIM_STATE replies use distinct wire types
    // (PACKET_FETCH_STATE_REPLY/PACKET_FETCH_ANIM_STATE_REPLY) instead of reusing the
    // request's type, and packetSizeForType() lets a byte-stream receiver size a frame from
    // its type byte alone — both needed for the panel relay's shared UART transport, which
    // has no per-transaction length the way I2C did.
    // v8: relay discovery control plane (PACKET_DISCOVERY_ADVANCE/PACKET_DISCOVERY_DONE) —
    // see Core/Relay/DiscoveryCoordinator.hpp / PanelDiscoveryDriver.hpp.
    // v9: PacketPanelConfiguration's colorTemperature/colorCorrection fields changed from
    // FastLED's ColorTemperature/LEDColorCorrection enums to raw Protocol::ColorRGB, so the
    // struct has no FastLED dependency and packetSizeForType() can size it like every other
    // packet (previously the one type this function didn't recognize).
    // v10: PacketHeader gained targetPanelIndex (0 = broadcast/general-call, else one specific
    // panel) — the relay network's addressing field, since flooding has no physical-bus-address
    // equivalent to fall back on. PacketDiscoveryAdvance's own bespoke targetPanelIndex payload
    // field folded into this (one addressing mechanism, not two).
    // v11: PacketInitializationPull/PacketRegisterEdge gained parentEdgeIndex — the probing
    // panel's (or controller trunk's) own edge index for the link being offered, echoed back
    // unchanged in the reply so the controller can learn both sides of every discovered link
    // (needed to build PanelGraph's TopoLink[] — see Core/Relay/DiscoveryTreeBuilder.hpp) without
    // a second, independently-timed upstream frame.
    // v12: added the relay OTA bootloader control plane (PACKET_BOOTLOADER_PING/PONG/
    // WRITE_CHUNK/WRITE_ACK/START_APP — see Core/Common/ProtocolTypes.hpp and
    // lib/Lightnet/Panel/bootloader/). An intermediate panel still built before this version
    // can't frame/relay these new types at all (its packetSizeForType() doesn't recognize them),
    // so flashing a panel more than zero hops away still needs every panel between it and the
    // controller updated — even though the bootloader itself, once resident, deliberately does
    // not validate protocolVersion (flashing is how a version mismatch gets resolved).
    const uint16_t VERSION = 12;

    // Stamp a packet's PacketMeta header in place: type + protocolVersion + targetPanelIndex +
    // headerCrc. targetPanelIndex defaults to 0 (broadcast/general-call); pass the destination
    // panel index for a packet meant for one specific panel.
    void setPacketMeta(PacketMeta *meta, packetType_t type, uint16_t targetPanelIndex = 0);

    // Meta-only wire packets (FETCH_STATE request, RESET, ACK, …).
    PacketMeta makeMeta(packetType_t type, uint16_t targetPanelIndex = 0);

    // Full packet structs (meta is the first member). Zero-initializes payload fields.
    template<typename PacketT>
    inline PacketT makePacket(packetType_t type, uint16_t targetPanelIndex = 0)
    {
        PacketT pkt = {};

        setPacketMeta(packetMeta(pkt), type, targetPanelIndex);

        return pkt;
    }

    // All wire packets are standard-layout structs with PacketMeta at offset 0.
    template<typename PacketT>
    inline const PacketMeta *packetMeta(const PacketT &pkt)
    {
        static_assert(offsetof(PacketT, meta) == 0, "packet struct must start with PacketMeta");

        return &pkt.meta;
    }

    template<typename PacketT>
    inline PacketMeta *packetMeta(PacketT &pkt)
    {
        static_assert(offsetof(PacketT, meta) == 0, "packet struct must start with PacketMeta");

        return &pkt.meta;
    }

    // PACKET_RESET_DEVICE/PACKET_ENTER_BOOTLOADER must reach a panel regardless of protocolVersion
    // — flashing (or a reset that clears the way for a fresh flash) is how a version mismatch
    // gets resolved, so gating either packet on the version that mismatch created would make a
    // stuck panel unrecoverable over the relay. validatePacket() consults this unconditionally
    // (even when validateProtocolVersion=true, the default every normal RX path uses) — it is not
    // something a caller opts into per call.
    bool isVersionExemptType(packetType_t type);

    // Validate a received packet's header. 0 = ok; 1 = too short; 2 = bad header CRC;
    // 3 = protocol-version mismatch. validateProtocolVersion=false (the relay OTA bootloader
    // only, see PacketFramer's constructor) skips the version check for every type, not just the
    // two isVersionExemptType() always exempts.
    uint8_t validatePacket(const PacketMeta *packet, uint8_t size, bool validateProtocolVersion = true);

    // The fixed wire size of a packet, given only its type byte — the whole struct, meta
    // included. Every packet type has exactly one wire shape (see the comment on
    // PACKET_FETCH_STATE_REPLY/PACKET_FETCH_ANIM_STATE_REPLY above for why request and reply
    // can't share a type). Returns 0 for a type this function doesn't recognize. Used to
    // recover frame boundaries from a raw byte stream, which (unlike I2C) has no out-of-band
    // length.
    uint8_t packetSizeForType(packetType_t type);
}  // namespace Protocol
