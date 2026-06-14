#pragma once

#include <Arduino.h>
#include "../Utils/Crc.hpp"
#include <FastLED.h>
// Pure packet/struct definitions (no Arduino/FastLED) live in the portable core so the
// animation player and the mobile C ABI can include them without pulling hardware headers.
#include "../Core/Common/ProtocolTypes.hpp"
// VERSION + setPacketMeta()/validatePacket() also live in the portable core (no Arduino)
// so the shared scene engine can stamp packets. Re-exposed here for controller/panel.
#include "../Core/Common/ProtocolMeta.hpp"

namespace Protocol {
    // v6: layer compositing. PacketAnimationPrepare gains composeMode (blend mode for
    // source layers / modifier op selector), composeOrder (deterministic stacking index),
    // and startDelayMs (per-panel onset offset — runner sweeps compile to local PULSE with
    // a per-panel delay). PacketAnimationControl gains group_id so a single composited slot
    // can be stopped/paused. Panels and controller must match versions.
    // v5: per-panel brightness removed. PacketAnimationPrepare no longer carries
    // brightnessFrom/brightnessTo — animations express brightness through color.
    // PanelState no longer includes a brightness field.
    // VERSION now lives in Core/Common/ProtocolMeta.hpp (included above).
    // Packet size needs to fit the new SET_PALETTE payload (PALETTE_STOPS * 4 = 64 B)
    // plus PacketMeta (5 B header + 2 B header CRC = 7 B). 80 B leaves margin.
    const uint8_t MAX_PACKET_SIZE = 80;
    const uint8_t PULLING_ADDRESS = 120;

    // Needs FastLED enums (ColorTemperature, LEDColorCorrection) — stays out of the portable core.
    typedef struct PACK {
        PacketMeta         meta;
        bool               useGammaCorrection;
        ColorTemperature   colorTemperature;
        LEDColorCorrection colorCorrection;
    } PacketPanelConfiguration;
    // setPacketMeta() / validatePacket() come from ProtocolMeta.hpp (included above).
}
