#include "PanelsController.hpp"

uint8_t PanelsController::setColor(uint8_t address, Protocol::Color color)
{
    Protocol::PacketSetColor packet = Protocol::makePacket<Protocol::PacketSetColor>(Protocol::PACKET_SET_COLOR);

    packet.color = color;

    return LNBus.sendPacketAck(address, Protocol::packetMeta(packet), sizeof(packet));
}

uint8_t PanelsController::turnOnOff(uint8_t address, uint8_t on)
{
    Protocol::PacketTurnOnOff packet = Protocol::makePacket<Protocol::PacketTurnOnOff>(Protocol::PACKET_TURN_ON_OFF);

    packet.on = on;

    return LNBus.sendPacketAck(address, Protocol::packetMeta(packet), sizeof(packet));
}

uint8_t PanelsController::turnOn(uint8_t address)
{
    return this->turnOnOff(address, 1);
}

uint8_t PanelsController::turnOff(uint8_t address)
{
    return this->turnOnOff(address, 0);
}

void PanelsController::resetDevices(uint16_t maxAddress)
{
    Protocol::PacketMeta resetPacket = Protocol::makeMeta(Protocol::PACKET_RESET_DEVICE);

    do {
        LNBus.sendPacketNack(maxAddress, &resetPacket, sizeof(resetPacket));
    } while (maxAddress--);
}

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

void PanelsController::enterBootloader(uint8_t address)
{
    Protocol::PacketEnterBootloader packet = Protocol::makePacket<Protocol::PacketEnterBootloader>(Protocol::PACKET_ENTER_BOOTLOADER);

    packet.token = Protocol::BOOTLOADER_ENTRY_TOKEN;
    LNBus.sendPacketNack(address, Protocol::packetMeta(packet), sizeof(packet));
}

uint8_t PanelsController::sendConfiguration(uint8_t address, panelConfiguration_t config)
{
    Protocol::PacketPanelConfiguration packet =
        Protocol::makePacket<Protocol::PacketPanelConfiguration>(Protocol::PACKET_PANEL_CONFIGURATION);

    packet.useGammaCorrection = config.useGammaCorrection;
    packet.colorTemperature = config.colorTemperature;
    packet.colorCorrection = config.colorCorrection;

    return LNBus.sendPacketAck(address, Protocol::packetMeta(packet), sizeof(packet));
}
