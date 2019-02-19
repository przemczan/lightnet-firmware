#include "PanelsController.hpp"

uint8_t PanelsController::setColorAndBrightness(uint8_t address, Protocol::Color *color, uint8_t brightness)
{
    Protocol::PacketSetColorAndBrightness packet;

    packet.color = *color;
    packet.brightness = brightness;

    return LNBus.sendPacketAck(address, &packet, sizeof(packet), Protocol::PACKET_SET_COLOR_AND_BRIGHTNESS);
}

uint8_t PanelsController::setColor(uint8_t address, Protocol::Color *color)
{
    Protocol::PacketSetColor packet;

    packet.color = *color;

    return LNBus.sendPacketAck(address, &packet, sizeof(packet), Protocol::PACKET_SET_COLOR);
}

uint8_t PanelsController::setBrightness(uint8_t address, uint8_t brightness)
{
    Protocol::PacketSetBrightness packet;

    packet.brightness = brightness;

    return LNBus.sendPacketNack(address, &packet, sizeof(packet), Protocol::PACKET_SET_BRIGHTNESS);
}

uint8_t PanelsController::turnOnOff(uint8_t address, uint8_t on)
{
    Protocol::PacketTurnOnOff packet;

    packet.on = on;

    return LNBus.sendPacketAck(address, &packet, sizeof(packet), Protocol::PACKET_TURN_ON_OFF);
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
    Protocol::PacketMeta resetPacket;
    do {
        LNBus.sendPacketAck(maxAddress, &resetPacket, sizeof(resetPacket), Protocol::PACKET_RESET_DEVICE);
    } while (maxAddress--);
}

uint8_t PanelsController::fetchState(uint8_t address, Protocol::PacketPanelState *state)
{
    Protocol::PacketMeta packet;

    return LNBus.sendPacketWithResponse(
        address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_FETCH_STATE,
        state,
        sizeof(*state)
    );
}
