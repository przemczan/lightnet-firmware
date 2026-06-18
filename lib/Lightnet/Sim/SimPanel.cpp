#ifdef SIM_MODE

#include "SimPanel.hpp"

SimPanel::SimPanel()
{
}

void SimPanel::setIndex(uint8_t idx)
{
    panelIndex = idx;
    rgb.setPanelIndex(idx);
}

uint8_t SimPanel::getIndex() const
{
    return panelIndex;
}

void SimPanel::handlePacket(const Protocol::PacketMeta *packet, uint8_t size)
{
    if (size < sizeof(Protocol::PacketMeta)) return;

    switch (packet->header.type) {
        case Protocol::PACKET_ANIMATION_PREPARE:

            if (size >= sizeof(Protocol::PacketAnimationPrepare))
                player.prepare((const Protocol::PacketAnimationPrepare *)packet);

            break;
        case Protocol::PACKET_ANIMATION_START:
        {
            if (size >= sizeof(Protocol::PacketAnimationStart)) {
                const Protocol::PacketAnimationStart *pkt = (const Protocol::PacketAnimationStart *)packet;

                player.start(pkt->seq_id, pkt->group_id, (uint16_t)millis());
            }

            break;
        }
        case Protocol::PACKET_ANIMATION_CONTROL:

            if (size >= sizeof(Protocol::PacketAnimationControl)) {
                const Protocol::PacketAnimationControl *pkt = (const Protocol::PacketAnimationControl *)packet;

                player.control(pkt->cmd, pkt->group_id, (uint16_t)millis());
            }

            break;
        case Protocol::PACKET_ANIMATION_UPDATE_PARAMS:

            if (size >= sizeof(Protocol::PacketAnimationUpdateParams)) {
                const Protocol::PacketAnimationUpdateParams *pkt = (const Protocol::PacketAnimationUpdateParams *)packet;

                player.updateParams(pkt->seq_id, pkt->group_id, pkt->param_type, pkt->value, pkt->transitionMs, (uint16_t)millis());
            }

            break;
        case Protocol::PACKET_SET_PALETTE:

            if (size >= sizeof(Protocol::PacketSetPalette)) {
                const Protocol::PacketSetPalette *pkt = (const Protocol::PacketSetPalette *)packet;

                player.setPalette(pkt->stops, pkt->count);
            }

            break;
        case Protocol::PACKET_SET_BASE_COLORS:

            if (size >= sizeof(Protocol::PacketSetBaseColors)) {
                const Protocol::PacketSetBaseColors *pkt = (const Protocol::PacketSetBaseColors *)packet;

                player.setBaseColors(pkt->colors);
            }

            break;
        case Protocol::PACKET_SET_GLOBAL_BRIGHTNESS:

            if (size >= sizeof(Protocol::PacketSetGlobalBrightness)) {
                const Protocol::PacketSetGlobalBrightness *pkt = (const Protocol::PacketSetGlobalBrightness *)packet;

                rgb.globalBrightness(pkt->value);
            }

            break;
        case Protocol::PACKET_SET_BACKGROUND:

            if (size >= sizeof(Protocol::PacketSetBackground)) {
                const Protocol::PacketSetBackground *pkt = (const Protocol::PacketSetBackground *)packet;

                player.setBackground(pkt->color);
            }

            break;
        case Protocol::PACKET_TURN_ON_OFF:

            if (size >= sizeof(Protocol::PacketTurnOnOff)) {
                const Protocol::PacketTurnOnOff *pkt = (const Protocol::PacketTurnOnOff *)packet;

                pkt->on ? rgb.turnOn() : rgb.turnOff();
            }

            break;
        case Protocol::PACKET_SET_COLOR:

            if (size >= sizeof(Protocol::PacketSetColor)) {
                const Protocol::PacketSetColor *pkt = (const Protocol::PacketSetColor *)packet;

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
