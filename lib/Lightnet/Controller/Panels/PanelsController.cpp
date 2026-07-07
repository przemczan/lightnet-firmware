#include "PanelsController.hpp"

#ifdef SIM_MODE
    PanelsController::PanelsController(Lightnet::IPacketSink &sink) : sink(sink)
    {
    }

#else
    PanelsController::PanelsController(Lightnet::IPacketSink &sink, Lightnet::ControllerRelayPacketSink &relaySink)
        : sink(sink), relaySink(relaySink)
    {
    }

#endif

uint8_t PanelsController::setColor(uint8_t address, Protocol::Color color)
{
    Protocol::PacketSetColor packet = Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);

    packet.color = color;

    this->sink.send(address, Protocol::packetMeta(packet), sizeof(packet), false);

    return 0;
}

uint8_t PanelsController::turnOnOff(uint8_t address, uint8_t on)
{
    Protocol::PacketTurnOnOff packet = Protocol::makePacket<Protocol::PacketTurnOnOff>(Protocol::PACKET_TURN_ON_OFF);

    packet.on = on;

    // Rare, low-frequency operation -- kept acked in intent (hardware redesign plan §3), even
    // though ControllerRelayPacketSink doesn't yet act on wantAck (flagged in its class comment).
    this->sink.send(address, Protocol::packetMeta(packet), sizeof(packet), true);

    return 0;
}

uint8_t PanelsController::turnOn(uint8_t address)
{
    return this->turnOnOff(address, 1);
}

uint8_t PanelsController::turnOff(uint8_t address)
{
    return this->turnOnOff(address, 0);
}

#ifdef SIM_MODE
    // Sim panels only ever respond to LightnetBus-routed commands -- unchanged from before the
    // controller cutover.
    uint8_t PanelsController::fetchState(uint8_t address, Protocol::PanelState *state)
    {
        Protocol::PacketMeta packet = Protocol::makeMeta(Protocol::PACKET_FETCH_STATE);
        Protocol::PacketPanelState response;

        uint8_t error = LNBus.sendPacketWithResponse(
            address,
            &packet,
            sizeof(packet),
            Protocol::packetMeta(response),
            sizeof(response)
        );

        if (!error) {
            memcpy(state, &response.panelState, sizeof(*state));

            return 0;
        }

        return error;
    }

#else
    uint8_t PanelsController::fetchState(uint8_t address, Protocol::PanelState *state)
    {
        Protocol::PacketMeta request = Protocol::makeMeta(Protocol::PACKET_FETCH_STATE);
        Protocol::PacketPanelState response;

        bool ok = this->relaySink.requestReply(
            address,
            &request,
            sizeof(request),
            Protocol::PACKET_FETCH_STATE_REPLY,
            Protocol::packetMeta(response),
            sizeof(response)
        );

        if (!ok || response.panelState.panelIndex != address) {
            return 1;
        }

        memcpy(state, &response.panelState, sizeof(*state));

        return 0;
    }

#endif

void PanelsController::enterBootloader(uint8_t address)
{
    Protocol::PacketEnterBootloader packet = Protocol::makePacket<Protocol::PacketEnterBootloader>(Protocol::PACKET_ENTER_BOOTLOADER);

    packet.token = Protocol::BOOTLOADER_ENTRY_TOKEN;

    this->sink.send(address, Protocol::packetMeta(packet), sizeof(packet), false);
}

uint8_t PanelsController::sendConfiguration(uint8_t address, panelConfiguration_t config)
{
    Protocol::PacketPanelConfiguration packet =
        Protocol::makePacket<Protocol::PacketPanelConfiguration>(Protocol::PACKET_PANEL_CONFIGURATION);

    // The wire format carries raw RGB (Protocol::ColorRGB), not FastLED's ColorTemperature/
    // LEDColorCorrection enums — those are just packed RGB hex constants under the hood, so
    // CRGB's converting constructor extracts the same bytes FastLED itself would.
    CRGB temperatureRgb = config.colorTemperature;
    CRGB correctionRgb  = config.colorCorrection;

    packet.useGammaCorrection = config.useGammaCorrection;
    packet.colorTemperature   = { temperatureRgb.r, temperatureRgb.g, temperatureRgb.b };
    packet.colorCorrection    = { correctionRgb.r, correctionRgb.g, correctionRgb.b };

    this->sink.send(address, Protocol::packetMeta(packet), sizeof(packet), true);

    return 0;
}

void PanelsController::resetDevices()
{
    // Header-level addressing (protocol v10) makes this a single broadcast flood instead of a
    // unicast spray to every address from maxIndex down to 0 -- see the hardware redesign plan §11.3.
    Protocol::PacketMeta resetPacket = Protocol::makeMeta(Protocol::PACKET_RESET_DEVICE);

    this->sink.send(0, &resetPacket, sizeof(resetPacket), false);
}
