#pragma once

// ProtocolTypes — the pure, host-compilable subset of the wire protocol.
//
// These packet/struct definitions carry NO Arduino or FastLED dependency, so the
// portable animation core (and the mobile C ABI) can include them directly.
// Common/Protocol.hpp includes this header and adds the hardware-coupled parts
// (PacketPanelConfiguration's FastLED enums, version constants, validatePacket()).

#include <stdint.h>
#include "LightnetConfig.hpp"
#include "Palette.hpp"
#include "ColorRef.hpp"

#define PACK __attribute__((__packed__))

namespace Protocol {
    enum packetType_t: uint8_t {
        PACKET_NOOP = 0,
        PACKET_ACK = 1,
        PACKET_INITIALIZATION_PULL = 2,
        PACKET_REGISTER_EDGE = 3,
        PACKET_TURN_ON_OFF = 4,
        PACKET_SET_COLOR = 5,
        PACKET_REGISTER_EDGE_ACK = 8,
        PACKET_PANEL_EDGE_INFO = 9,
        PACKET_FETCH_STATE = 10,
        PACKET_PANEL_CONFIGURATION = 11,
        PACKET_ANIMATION_PREPARE = 12,       // animation framework
        PACKET_ANIMATION_START = 13,         // animation framework (General Call)
        PACKET_ANIMATION_CONTROL = 14,       // animation framework
        PACKET_FETCH_ANIM_STATE = 15,        // animation framework
        PACKET_ANIMATION_UPDATE_PARAMS = 16, // animation framework (General Call)
        PACKET_SET_PALETTE = 17,             // unicast or General Call — 16-stop palette
        PACKET_SET_BASE_COLORS = 18,         // unicast or General Call — 3 RGB triples
        PACKET_SET_GLOBAL_BRIGHTNESS = 19,   // General Call — 1 byte multiplier
        PACKET_SET_BACKGROUND = 20,          // unicast or General Call — scene compositor base colour
        PACKET_RESET_DEVICE = 200,
        PACKET_ENTER_BOOTLOADER = 201,
    };

    typedef struct PACK {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } ColorRGB;

    // there were plans for supporting FastLED's HSV but... to much effort
    typedef struct PACK {
        union {
            ColorRGB rgb;
        };
    } Color;

    typedef struct PACK {
        uint16_t panelIndex;
        uint8_t  state;
        ColorRGB color;
    } PanelState;

    // BEGIN Common packet structures
    typedef struct PACK {
        packetType_t type;
        uint16_t     protocolVersion;
    } PacketHeader;

    typedef struct PACK {
        PacketHeader header;
        uint16_t     headerCrc;
    } PacketMeta;
    // END

    // BEGIN Packets definitions
    typedef struct PACK {
        PacketMeta meta;
        uint16_t   panelIndex;
    } PacketInitializationPull;

    typedef struct PACK {
        PacketMeta meta;
        uint16_t   panelIndex;
        uint16_t   edgeIndex;
    } PacketRegisterEdge;

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    on;
    } PacketTurnOnOff;

    typedef struct PACK {
        PacketMeta meta;
        Color      color;
    } PacketSetColor;

    typedef struct PACK {
        PacketMeta meta;
        uint16_t   panelIndex;
        uint8_t    edgeIndex;
        uint16_t   connectedPanelIndex;
    } PacketPanelEdgeInfo;

    typedef struct PACK {
        PacketMeta meta;
        PanelState panelState;
    } PacketPanelState;

    // Animation Framework Packets (see AnimationTypes.hpp for details)
    typedef struct PACK {
        PacketMeta         meta;
        uint8_t            animType;
        uint8_t            group_id;
        uint8_t            flags;
        uint8_t            transitionMs;
        uint16_t           durationMs;
        Lightnet::ColorRef colorFrom;       // 4 B — panel resolves at frame time
        Lightnet::ColorRef colorTo;         // 4 B
        uint8_t            param1;
        uint8_t            param2;
        uint8_t            composeMode;      // ComposeMode (blend for source layers)
        uint8_t            composeOrder;     // layer array index — deterministic stacking
        uint16_t           startDelayMs;     // per-panel onset offset (runner sweep phase)
    } PacketAnimationPrepare;  // 25 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    seq_id;
        uint8_t    group_id;
    } PacketAnimationStart;  // 7 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    cmd;
        uint8_t    group_id;  // 0 = all slots; else the composited slot to target
    } PacketAnimationControl;  // 7 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    seq_id;
        uint8_t    group_id;
        uint8_t    param_type;
        uint8_t    value;
        uint8_t    transitionMs;
    } PacketAnimationUpdateParams;  // 10 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    animType;
        uint8_t    group_id;
        uint16_t   elapsedMs;
        uint16_t   durationMs;
        uint8_t    queueLen;
    } PacketAnimationStatus;  // 11 bytes

    // Replace the panel's current palette. Sent via General Call for scene-level
    // palette (all panels), or unicast for per-layer overrides.
    // count must be 1..PALETTE_STOPS. Only `count` stops are read.
    typedef struct PACK {
        PacketMeta             meta;
        uint8_t                count;
        Lightnet::GradientStop stops[Lightnet::PALETTE_STOPS];
    } PacketSetPalette;  // 5 + 1 + 64 = 70 bytes

    // Replace the panel's 3 base colors (primary, secondary, tertiary).
    typedef struct PACK {
        PacketMeta meta;
        ColorRGB   colors[Lightnet::BASE_COLORS_COUNT];
    } PacketSetBaseColors;  // 5 + 9 = 14 bytes

    // Replace the panel's global brightness multiplier (0..255).
    // Sent via General Call so all panels receive simultaneously.
    typedef struct PACK {
        PacketMeta meta;
        uint8_t    value;
    } PacketSetGlobalBrightness;  // 6 bytes

    // Scene compositor base colour: the panel's layer fold starts from this colour
    // instead of black, and a panel with no active layers displays it. Sent once
    // (General Call) at scene start. Default black reproduces pre-v6 behaviour.
    typedef struct PACK {
        PacketMeta meta;
        ColorRGB   color;
    } PacketSetBackground;  // 8 bytes

    // END

    // Both controller and panel must agree on this value.
    // Panel side: checked in handleEnterBootloader().
    // Controller side: written into PacketEnterBootloader.token.
    const uint8_t BOOTLOADER_ENTRY_TOKEN = 0xB0;

    // token must equal BOOTLOADER_ENTRY_TOKEN to prevent accidental triggering
    typedef struct PACK {
        PacketMeta meta;
        uint8_t    token;
    } PacketEnterBootloader;  // 6 bytes

    const uint8_t MIN_PACKET_SIZE = sizeof(PacketMeta);

    namespace Colors {
        const Color RED = { { 255, 0, 0 } };
        const Color GREEN = { { 0, 255, 0 } };
        const Color BLUE = { { 0, 0, 255 } };
        const Color WHITE = { { 255, 255, 255 } };
    }
}  // namespace Protocol
