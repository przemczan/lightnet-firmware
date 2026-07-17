#include "AnimationScheduler.hpp"
#include "../Common/ProtocolMeta.hpp"

namespace Lightnet {
    AnimationScheduler::AnimationScheduler(IPacketSink& _sink)
        : sink(_sink), nextSeqId(1)
    {
    }

    void AnimationScheduler::sendPrepareToPanel(
        PanelIndex      panelAddress,
        uint8_t         group_id,
        uint8_t         animType,
        uint8_t         flags,
        uint16_t        durationMs,
        const ColorRef& colorFrom,
        const ColorRef& colorTo,
        uint8_t         param1,
        uint8_t         param2,
        uint8_t         composeMode,
        uint8_t         composeOrder,
        uint16_t        startDelayMs,
        uint8_t         animates
    )
    {
        Protocol::PacketAnimationPrepare prepare =
            Protocol::makePacket<Protocol::PacketAnimationPrepare>(Protocol::PACKET_ANIMATION_PREPARE);

        prepare.animType     = animType;
        prepare.group_id     = group_id;
        prepare.flags        = flags;
        prepare.transitionMs = 0;
        prepare.durationMs   = durationMs;
        prepare.colorFrom    = colorFrom;
        prepare.colorTo      = colorTo;
        prepare.param1       = param1;
        prepare.param2       = param2;
        prepare.composeMode  = composeMode;
        prepare.composeOrder = composeOrder;
        prepare.startDelayMs = startDelayMs;
        prepare.animates     = animates;

        sink.send(panelAddress, Protocol::packetMeta(prepare), sizeof(prepare), /*wantAck=*/ false);
    }

    void AnimationScheduler::sendGroupStart(uint8_t group_id)
    {
        // Send General Call START twice for reliability. Both share one seq_id so the
        // panel's duplicate guard runs exactly one execution and absorbs the redundant copy.
        Protocol::PacketAnimationStart startPkt =
            Protocol::makePacket<Protocol::PacketAnimationStart>(Protocol::PACKET_ANIMATION_START);

        startPkt.seq_id   = nextSeqId;
        startPkt.group_id = group_id;
        nextSeqId++;

        if (nextSeqId == 0) nextSeqId = 1;

        // General Call has no ack — send twice (shared seq_id) for reliability, pacing
        // between so panels settle. Off-device sinks make pace() a no-op.
        for (uint8_t retry = 0; retry < 2; retry++) {
            sink.send(0x00, Protocol::packetMeta(startPkt), sizeof(startPkt), /*wantAck=*/ false);
            sink.pace(200);
        }
    }

    void AnimationScheduler::playOnPanels(
        uint8_t           group_id,
        uint8_t           animType,
        uint8_t           flags,
        uint16_t          durationMs,
        const ColorRef&   colorFrom,
        const ColorRef&   colorTo,
        uint8_t           param1,
        uint8_t           param2,
        const PanelIndex *panelAddresses,
        uint8_t           panelCount,
        uint8_t           composeMode,
        uint8_t           composeOrder,
        uint8_t           animates
    )
    {
        // Same PREPARE to every panel (uniform startDelay = 0), then one general-call START.
        for (uint8_t i = 0; i < panelCount; i++) {
            sendPrepareToPanel(
                panelAddresses[i],
                group_id,
                animType,
                flags,
                durationMs,
                colorFrom,
                colorTo,
                param1,
                param2,
                composeMode,
                composeOrder,                             /*startDelayMs=*/
                0,
                animates
            );
        }

        // Give panels time to process their PREPARE before START arrives.
        sink.pace(300);

        sendGroupStart(group_id);
    }

    void AnimationScheduler::broadcastBlack()
    {
        Protocol::PacketSetColor pkt = Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);

        pkt.color.rgb = { 0, 0, 0 };
        sink.send(0x00, Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
    }

    void AnimationScheduler::broadcastStop()
    {
        Protocol::PacketAnimationControl pkt =
            Protocol::makePacket<Protocol::PacketAnimationControl>(Protocol::PACKET_ANIMATION_CONTROL);

        pkt.cmd      = Lightnet::ANIM_CTRL_STOP;
        pkt.group_id = 0; // all slots
        sink.send(0x00, Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
    }

    void AnimationScheduler::clearAllPanelQueues()
    {
        Protocol::PacketAnimationControl pkt =
            Protocol::makePacket<Protocol::PacketAnimationControl>(Protocol::PACKET_ANIMATION_CONTROL);

        pkt.cmd      = Lightnet::ANIM_CTRL_CLEAR_QUEUE;
        pkt.group_id = 0; // all slots
        sink.send(0x00, Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
    }

    void AnimationScheduler::triggerGroup(uint8_t group_id, uint8_t value)
    {
        // Send General Call UPDATE_PARAMS with TRIGGER
        sendGeneralCallUpdateParams(group_id, Lightnet::PARAM_TRIGGER, value);
    }

    void AnimationScheduler::sendGeneralCallUpdateParams(uint8_t group_id, uint8_t param_type, uint8_t value)
    {
        Protocol::PacketAnimationUpdateParams params =
            Protocol::makePacket<Protocol::PacketAnimationUpdateParams>(Protocol::PACKET_ANIMATION_UPDATE_PARAMS);

        params.seq_id = nextSeqId;
        params.group_id = group_id;
        params.param_type = param_type;
        params.value = value;
        params.transitionMs = 10;

        // Send to General Call address (0x00)
        sink.send(0x00, Protocol::packetMeta(params), sizeof(params), /*wantAck=*/ false);

        nextSeqId++;

        if (nextSeqId == 0) nextSeqId = 1;
    }

    void AnimationScheduler::sendControlToPanels(uint8_t group_id, uint8_t cmd, const PanelIndex *panelAddresses, uint8_t panelCount)
    {
        Protocol::PacketAnimationControl control =
            Protocol::makePacket<Protocol::PacketAnimationControl>(Protocol::PACKET_ANIMATION_CONTROL);

        control.cmd      = cmd;
        control.group_id = group_id;

        for (uint8_t i = 0; i < panelCount; i++) {
            sink.send(panelAddresses[i], Protocol::packetMeta(control), sizeof(control), /*wantAck=*/ false);
        }
    }

    // ============================================================================
    // RGB convenience overload — wraps inline RGB in ColorRefs
    // ============================================================================

    void AnimationScheduler::playOnPanels(
        uint8_t                   group_id,
        uint8_t                   animType,
        uint8_t                   flags,
        uint16_t                  durationMs,
        const Protocol::ColorRGB& colorFrom,
        const Protocol::ColorRGB& colorTo,
        uint8_t                   param1,
        uint8_t                   param2,
        const PanelIndex *        panelAddresses,
        uint8_t                   panelCount,
        uint8_t                   composeMode,
        uint8_t                   composeOrder,
        uint8_t                   animates
    )
    {
        ColorRef from = ColorRef_rgb(colorFrom.r, colorFrom.g, colorFrom.b);
        ColorRef to   = ColorRef_rgb(colorTo.r, colorTo.g, colorTo.b);

        playOnPanels(
            group_id,
            animType,
            flags,
            durationMs,
            from,
            to,
            param1,
            param2,
            panelAddresses,
            panelCount,
            composeMode,
            composeOrder,
            animates
        );
    }

    // ============================================================================
    // Appearance broadcasts
    // ============================================================================

    // makePacket() zero-initializes the whole struct, so unused stops stay zeroed.
    Protocol::PacketSetPalette AnimationScheduler::makeSetPalettePacket(const GradientStop *stops, uint8_t count)
    {
        if (count > PALETTE_STOPS) count = PALETTE_STOPS;

        Protocol::PacketSetPalette pkt = Protocol::makePacket<Protocol::PacketSetPalette>(Protocol::PACKET_SET_PALETTE);

        pkt.count = count;

        for (uint8_t i = 0; i < count; i++) {
            pkt.stops[i] = stops[i];
        }

        return pkt;
    }

    void AnimationScheduler::broadcastPalette(const GradientStop *stops, uint8_t count)
    {
        if (count == 0) return;

        Protocol::PacketSetPalette pkt = makeSetPalettePacket(stops, count);

        // General Call — all panels apply simultaneously.
        sink.send(0x00, Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
    }

    void AnimationScheduler::unicastPaletteToPanels(
        const GradientStop *stops,
        uint8_t             count,
        const PanelIndex *  panelAddresses,
        uint8_t             panelCount
    )
    {
        if (count == 0) return;

        Protocol::PacketSetPalette pkt = makeSetPalettePacket(stops, count);

        for (uint8_t i = 0; i < panelCount; i++) {
            sink.send(panelAddresses[i], Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
        }
    }

    void AnimationScheduler::turnOnPanels(const PanelIndex *panelAddresses, uint8_t panelCount)
    {
        Protocol::PacketTurnOnOff pkt = Protocol::makePacket<Protocol::PacketTurnOnOff>(Protocol::PACKET_TURN_ON_OFF);

        pkt.on = 1;

        for (uint8_t i = 0; i < panelCount; i++) {
            sink.send(panelAddresses[i], Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
        }
    }

    void AnimationScheduler::broadcastBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT])
    {
        Protocol::PacketSetBaseColors pkt =
            Protocol::makePacket<Protocol::PacketSetBaseColors>(Protocol::PACKET_SET_BASE_COLORS);

        for (uint8_t i = 0; i < BASE_COLORS_COUNT; i++) {
            pkt.colors[i] = colors[i];
        }

        sink.send(0x00, Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
    }

    void AnimationScheduler::broadcastGlobalBrightness(uint8_t value)
    {
        Protocol::PacketSetGlobalBrightness pkt =
            Protocol::makePacket<Protocol::PacketSetGlobalBrightness>(Protocol::PACKET_SET_GLOBAL_BRIGHTNESS);

        pkt.value = value;
        sink.send(0x00, Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
    }

    void AnimationScheduler::broadcastBackground(const Protocol::ColorRGB& color)
    {
        Protocol::PacketSetBackground pkt =
            Protocol::makePacket<Protocol::PacketSetBackground>(Protocol::PACKET_SET_BACKGROUND);

        pkt.color = color;
        sink.send(0x00, Protocol::packetMeta(pkt), sizeof(pkt), /*wantAck=*/ false);
    }
}  // namespace Lightnet
