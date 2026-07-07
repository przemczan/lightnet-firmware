#pragma once

// Arduino.h isn't actually used by anything in this file (no Serial/F()/digitalWrite/etc.) —
// kept for the controller build; the panel build has no Arduino.h at all, and everything this
// file needs (uint8_t etc.) already comes from ProtocolTypes.hpp's own <stdint.h>.
#ifdef LIGHTNET_TARGET_CONTROLLER
    #include <Arduino.h>
#endif
#include "../Utils/Crc.hpp"
// Pure packet/struct definitions (no Arduino/FastLED) live in the portable core so the
// animation player and the mobile C ABI can include them without pulling hardware headers.
// PacketPanelConfiguration lives there too — its colorTemperature/colorCorrection fields are
// raw RGB, not FastLED's ColorTemperature/LEDColorCorrection enums (see ProtocolTypes.hpp).
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
    // MAX_PACKET_SIZE now lives in Core/Common/ProtocolTypes.hpp (included above via
    // ProtocolMeta.hpp) so the portable core's byte-stream framer can size its buffer.
    const uint8_t PULLING_ADDRESS = 120;
    // setPacketMeta() / validatePacket() come from ProtocolMeta.hpp (included above).
}
