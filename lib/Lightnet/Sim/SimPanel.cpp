#ifdef SIM_MODE

#include "SimPanel.hpp"

void SimPanel::handlePacket(const void *data, uint8_t size)
{
    if (size < sizeof(Protocol::PacketMeta)) return;

    const Protocol::PacketMeta *meta = (const Protocol::PacketMeta *)data;

    switch (meta->header.type) {
        case Protocol::PACKET_ANIMATION_PREPARE:

            if (size >= sizeof(Protocol::PacketAnimationPrepare))
                player.prepare((const Protocol::PacketAnimationPrepare *)data);

            break;
        case Protocol::PACKET_ANIMATION_START:
        {
            if (size >= sizeof(Protocol::PacketAnimationStart)) {
                const Protocol::PacketAnimationStart *pkt = (const Protocol::PacketAnimationStart *)data;

                player.start(pkt->seq_id, pkt->group_id, (uint16_t)millis());
            }

            break;
        }
        case Protocol::PACKET_ANIMATION_CONTROL:

            if (size >= sizeof(Protocol::PacketAnimationControl)) {
                const Protocol::PacketAnimationControl *pkt = (const Protocol::PacketAnimationControl *)data;

                player.control(pkt->cmd, pkt->group_id, (uint16_t)millis());
            }

            break;
        case Protocol::PACKET_ANIMATION_UPDATE_PARAMS:

            if (size >= sizeof(Protocol::PacketAnimationUpdateParams)) {
                const Protocol::PacketAnimationUpdateParams *pkt = (const Protocol::PacketAnimationUpdateParams *)data;

                player.updateParams(pkt->seq_id, pkt->group_id, pkt->param_type, pkt->value, pkt->transitionMs, (uint16_t)millis());
            }

            break;
        case Protocol::PACKET_SET_PALETTE:

            if (size >= sizeof(Protocol::PacketSetPalette)) {
                const Protocol::PacketSetPalette *pkt = (const Protocol::PacketSetPalette *)data;

                player.setPalette(pkt->stops, pkt->count);
            }

            break;
        case Protocol::PACKET_SET_BASE_COLORS:

            if (size >= sizeof(Protocol::PacketSetBaseColors)) {
                const Protocol::PacketSetBaseColors *pkt = (const Protocol::PacketSetBaseColors *)data;

                player.setBaseColors(pkt->colors);
            }

            break;
        case Protocol::PACKET_SET_GLOBAL_BRIGHTNESS:

            if (size >= sizeof(Protocol::PacketSetGlobalBrightness)) {
                const Protocol::PacketSetGlobalBrightness *pkt = (const Protocol::PacketSetGlobalBrightness *)data;

                rgb.globalBrightness(pkt->value);
            }

            break;
        case Protocol::PACKET_SET_BACKGROUND:

            if (size >= sizeof(Protocol::PacketSetBackground)) {
                const Protocol::PacketSetBackground *pkt = (const Protocol::PacketSetBackground *)data;

                player.setBackground(pkt->color);
            }

            break;
        case Protocol::PACKET_TURN_ON_OFF:

            if (size >= sizeof(Protocol::PacketTurnOnOff)) {
                const Protocol::PacketTurnOnOff *pkt = (const Protocol::PacketTurnOnOff *)data;

                pkt->on ? rgb.turnOn() : rgb.turnOff();
            }

            break;
        case Protocol::PACKET_SET_COLOR:

            if (size >= sizeof(Protocol::PacketSetColor)) {
                const Protocol::PacketSetColor *pkt = (const Protocol::PacketSetColor *)data;

                // Route through the player (single colour authority); tick() mirrors to rgb.
                player.setColorDirect(pkt->color.rgb);
            }

            break;
        default:
            break;
    }
}

void SimPanel::getState(Protocol::PanelState *state) const
{
    state->panelIndex = panelIndex;
    state->state      = rgb.on();
    state->color      = rgb.color();
}

void SimPanel::tick()
{
    player.tick((uint16_t)millis());

    // Mirror the player's current colour to the virtual LED (single colour authority).
    if (player.takeDirty()) {
        Protocol::ColorRGB c = player.currentColor();

        rgb.color(c.r, c.g, c.b);
    }
}

#endif  // SIM_MODE
