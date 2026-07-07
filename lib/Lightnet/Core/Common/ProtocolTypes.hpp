#pragma once

// ProtocolTypes — the pure, host-compilable subset of the wire protocol.
//
// These packet/struct definitions carry NO Arduino or FastLED dependency, so the
// portable animation core (and the mobile C ABI) can include them directly.
// Common/Protocol.hpp includes this header and adds the hardware-coupled parts
// (version constants, validatePacket()).

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
        // FETCH_STATE/FETCH_ANIM_STATE requests are meta-only; the payload-bearing reply gets
        // its own type (rather than reusing the request's) so a byte-stream receiver can size
        // a frame from its type byte alone. I2C never needed this — each bus transaction (query,
        // then separate response read) carried its own byte count from the Wire layer; the
        // relay's shared UART has no such out-of-band length.
        PACKET_FETCH_STATE_REPLY = 21,
        PACKET_FETCH_ANIM_STATE_REPLY = 22,
        // Relay discovery control plane (see Core/Relay/DiscoveryCoordinator.hpp /
        // PanelDiscoveryDriver.hpp). Only one panel in the tree is ever the active "frontier"
        // exploring its own edges at a time — ADVANCE tells it to try its next one; DONE
        // reports that its whole subtree is fully resolved so the controller can backtrack.
        PACKET_DISCOVERY_ADVANCE = 23,
        PACKET_DISCOVERY_DONE = 24,
        PACKET_RESET_DEVICE = 200,
        PACKET_ENTER_BOOTLOADER = 201,
        // Relay OTA bootloader control plane (lib/Lightnet/Panel/bootloader/). Only exchanged
        // with a panel that has already jumped into its resident bootloader — see the frozen
        // wire contract note on PacketBootloaderWriteChunk below.
        PACKET_BOOTLOADER_PING = 202,
        PACKET_BOOTLOADER_PONG = 203,
        PACKET_BOOTLOADER_WRITE_CHUNK = 204,
        PACKET_BOOTLOADER_WRITE_ACK = 205,
        PACKET_BOOTLOADER_START_APP = 206,
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
    // targetPanelIndex is the relay network's addressing field: 0 means broadcast/general-call
    // (every panel acts on it), any other value means only that one panel does — the wire
    // equivalent of the I2C slave address the old shared-bus transport used instead. It lives
    // here, not on individual packet structs, so every packet gets addressing for free and a
    // receiver's "is this for me" check is one type-independent comparison before the type
    // switch, not a per-type audit. headerCrc already covers the whole PacketHeader by size, so
    // it protects this field with no extra code.
    typedef struct PACK {
        packetType_t type;
        uint16_t     protocolVersion;
        uint16_t     targetPanelIndex;
    } PacketHeader;

    typedef struct PACK {
        PacketHeader header;
        uint16_t     headerCrc;
    } PacketMeta;  // 7 bytes
    // END

    // BEGIN Packets definitions
    // parentEdgeIndex is the sender's own edge this PULL travels out on (the controller's fixed
    // trunk edge, or the probing panel's local edge — see PanelDiscoveryDriver::tryNextEdge()).
    // The receiving panel has no other way to learn it (PanelRouter forwards frames verbatim, and
    // the controller is several hops away from most panels) — it's echoed back unchanged in the
    // PacketRegisterEdge reply below so the controller can learn both sides of the link it just
    // discovered without a second, independently-timed frame (see DiscoveryCoordinator's topology
    // accumulation and the hardware redesign plan §11 for why a separate report would race the
    // single-active-flow invariant).
    typedef struct PACK {
        PacketMeta meta;
        uint16_t   panelIndex;
        uint16_t   parentEdgeIndex;
    } PacketInitializationPull;

    // A reply's panelIndex is never 0 (indices are assigned starting at 1) — a fresh panel
    // registering reuses this field to mean "rejected, this edge closes a wiring loop" (see
    // Core/Relay/PanelDiscovery's loop-rejection rule) instead of a genuine assignment.
    const uint16_t DISCOVERY_REJECTED_INDEX = 0;

    typedef struct PACK {
        PacketMeta meta;
        uint16_t   panelIndex;
        uint16_t   edgeIndex;        // the replying panel's own edge facing this link
        uint16_t   parentEdgeIndex;  // echoed from the PacketInitializationPull that offered it
    } PacketRegisterEdge;

    // Relay discovery control plane. Only the panel named by meta.header.targetPanelIndex acts
    // on this; every panel still relays it downstream via the ordinary flood rule regardless (no
    // PanelRouter changes needed — this is just another payload flowing through it).
    typedef struct PACK {
        PacketMeta meta;
        uint16_t   assignIndex;
    } PacketDiscoveryAdvance;  // 9 bytes

    // A panel's whole subtree is fully resolved (every edge is Connected or NotConnected) —
    // routes upstream to the controller via the ordinary parent-edge routing rule.
    typedef struct PACK {
        PacketMeta meta;
        uint16_t   panelIndex;
    } PacketDiscoveryDone;  // 9 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    on;
    } PacketTurnOnOff;

    typedef struct PACK {
        PacketMeta meta;
        Color      color;
    } PacketSetColor;

    // Gamma correction, color-temperature tint, and color-correction tint. The latter two travel
    // as raw RGB (not FastLED's ColorTemperature/LEDColorCorrection enums, which are themselves
    // just packed RGB hex constants under the hood — see CRGB's converting constructors) so this
    // struct has no FastLED dependency; the panel reconstructs a CRGB from the raw bytes at the
    // point it actually calls FastLED (RGBController), and the controller does the same
    // conversion in the other direction when sending (PanelsController::sendConfiguration).
    typedef struct PACK {
        PacketMeta meta;
        bool       useGammaCorrection;
        ColorRGB   colorTemperature;
        ColorRGB   colorCorrection;
    } PacketPanelConfiguration;  // 7 + 1 + 3 + 3 = 14 bytes

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
        uint8_t            animates;         // AnimateTarget — what this animation modulates (default TARGET_COLOR)
    } PacketAnimationPrepare;  // 28 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    seq_id;
        uint8_t    group_id;
    } PacketAnimationStart;  // 9 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    cmd;
        uint8_t    group_id;  // 0 = all slots; else the composited slot to target
    } PacketAnimationControl;  // 9 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    seq_id;
        uint8_t    group_id;
        uint8_t    param_type;
        uint8_t    value;
        uint8_t    transitionMs;
    } PacketAnimationUpdateParams;  // 12 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    animType;
        uint8_t    group_id;
        uint16_t   elapsedMs;
        uint16_t   durationMs;
        uint8_t    queueLen;
    } PacketAnimationStatus;  // 14 bytes

    // Replace the panel's current palette. Sent via General Call for scene-level
    // palette (all panels), or unicast for per-layer overrides.
    // count must be 1..PALETTE_STOPS. Only `count` stops are read.
    typedef struct PACK {
        PacketMeta             meta;
        uint8_t                count;
        Lightnet::GradientStop stops[Lightnet::PALETTE_STOPS];
    } PacketSetPalette;  // 7 + 1 + 64 = 72 bytes

    // Replace the panel's 3 base colors (primary, secondary, tertiary).
    typedef struct PACK {
        PacketMeta meta;
        ColorRGB   colors[Lightnet::BASE_COLORS_COUNT];
    } PacketSetBaseColors;  // 7 + 9 = 16 bytes

    // Replace the panel's global brightness multiplier (0..255).
    // Sent via General Call so all panels receive simultaneously.
    typedef struct PACK {
        PacketMeta meta;
        uint8_t    value;
    } PacketSetGlobalBrightness;  // 8 bytes

    // Scene compositor base colour: the panel's layer fold starts from this colour
    // instead of black, and a panel with no active layers displays it. Sent once
    // (General Call) at scene start. Default black reproduces pre-v6 behaviour.
    typedef struct PACK {
        PacketMeta meta;
        ColorRGB   color;
    } PacketSetBackground;  // 10 bytes

    // END

    // Both controller and panel must agree on this value.
    // Panel side: checked in handleEnterBootloader().
    // Controller side: written into PacketEnterBootloader.token.
    const uint8_t BOOTLOADER_ENTRY_TOKEN = 0xB0;

    // token must equal BOOTLOADER_ENTRY_TOKEN to prevent accidental triggering
    typedef struct PACK {
        PacketMeta meta;
        uint8_t    token;
    } PacketEnterBootloader;  // 8 bytes

    // Relay OTA bootloader wire contract (lib/Lightnet/Panel/bootloader/) — a deliberately
    // frozen layout, exempt from this file's protocolVersion bump history above: flashing is how
    // a protocolVersion mismatch gets resolved, so the resident bootloader must stay readable
    // regardless of which app protocolVersion the controller was built with (it skips the
    // version check every other packet type gets — see PacketFramer's validateProtocolVersion
    // constructor parameter). headerCrc covers only PacketHeader (see PacketMeta above), so
    // PacketBootloaderWriteChunk carries its own CRC over its data payload — a corrupted chunk
    // must never reach boot_page_fill() unnoticed.
    const uint8_t BOOTLOADER_CHUNK_SIZE = 64;

    typedef struct PACK {
        PacketMeta meta;
        uint8_t    bootloaderVersion;
        uint16_t   pageSize;   // bytes per flash page (SPM_PAGESIZE)
        uint16_t   flashSize;  // programmable application bytes, i.e. BOOTLOADER_START
    } PacketBootloaderPong;  // 7 + 1 + 2 + 2 = 12 bytes

    enum bootloaderWriteStatus_t: uint8_t {
        BOOTLOADER_WRITE_OK = 0,
        BOOTLOADER_WRITE_BAD_CRC = 1,
        BOOTLOADER_WRITE_BAD_ADDRESS = 2,
    };

    // One flash-write chunk. Chunked at BOOTLOADER_CHUNK_SIZE rather than a full SPM_PAGESIZE
    // page so the whole struct still fits under MAX_PACKET_SIZE; the bootloader accumulates
    // chunks into one page buffer and commits it once a chunk completes that page's SPM write.
    typedef struct PACK {
        PacketMeta meta;
        uint16_t   address;                    // byte offset into flash, from 0
        uint8_t    length;                      // 1..BOOTLOADER_CHUNK_SIZE valid bytes in data[]
        uint8_t    data[BOOTLOADER_CHUNK_SIZE];
        uint16_t   dataCrc;                     // CRC-16 over data[0..length-1]
    } PacketBootloaderWriteChunk;  // 7 + 2 + 1 + 64 + 2 = 76 bytes

    typedef struct PACK {
        PacketMeta meta;
        uint16_t   address;
        uint8_t    status;  // bootloaderWriteStatus_t
    } PacketBootloaderWriteAck;  // 7 + 2 + 1 = 10 bytes

    const uint8_t MIN_PACKET_SIZE = sizeof(PacketMeta);

    // Largest wire packet (PacketSetPalette: 7 B meta + 1 + 64 = 72 B) plus margin. Lives in
    // the portable core (not Common/Protocol.hpp) so the relay's byte-stream framer can size
    // its buffer without pulling Arduino/FastLED.
    const uint8_t MAX_PACKET_SIZE = 80;

    namespace Colors {
        const Color RED = { { 255, 0, 0 } };
        const Color GREEN = { { 0, 255, 0 } };
        const Color BLUE = { { 0, 0, 255 } };
        const Color WHITE = { { 255, 255, 255 } };
    }
}  // namespace Protocol
