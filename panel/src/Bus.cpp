#include "Bus.hpp"

using namespace Protocol;

void Bus::begin(uint8_t address)
{
    Wire.begin(address);
}

void Bus::begin()
{
    Wire.begin();
}

void Bus::end()
{
    Wire.end();
}

uint8_t Bus::registerPanel(uint8_t bordersNumber, uint8_t parentBorder)
{
    RegisterPanel registerPanelPacket;
    RegisterPanelResponse response;

    registerPanelPacket.bordersNumber = bordersNumber;
    registerPanelPacket.parentBorder = parentBorder;

    PRINT("Sending register request... ");

    uint8_t result = this->sendPacketWithResponse(
        CONTROLLER_ADDRESS,
        &registerPanelPacket,
        sizeof(RegisterPanel),
        PACKET_REGISTER_PANEL,
        &response,
        sizeof(RegisterPanelResponse)
    );

    if (result == 0) {
        PRINTKV("success", response.panelId);

        return response.panelId;
    }

    PRINTLN("failed.");

    return 0;
}

void Bus::setOnReceive((void *)(*)(PacketMeta *) callback)
{
    this->onReceiveCallback = callback;
    Wire.onReceive(this->onReceive);
}

uint8_t Bus::sendPacket(uint8_t address, void *packet, uint8_t size, uint8_t type)
{
    setPacketMeta(packet, type);

    Wire.beginTransmission(address);
    Wire.write((uint8_t *)packet, size);

    return Wire.endTransmission();
}

uint8_t Bus::requestPacket(uint8_t address, void *buffer, uint8_t size)
{
    uint8_t receivedSize = Wire.requestFrom(address, size, (uint8_t)true);

    if (receivedSize != size) {
        return 1;
    }

    Wire.readBytes((uint8_t *)buffer, size);

    if (validatePacket(buffer, receivedSize) == 0) {
        return 0;
    }

    return 2;
}

uint8_t Bus::sendPacketWithResponse(
    uint8_t address,
    void *packet,
    uint8_t packetSize,
    uint8_t packetType,
    void *responseBuffer,
    uint8_t responseSize
) {
    if (this->sendPacket(address, packet, packetSize, packetType) != 0) {
        return 1;
    }

    if (this->requestPacket(address, responseBuffer, responseSize) != 0) {
        return 2;
    }

    return 0;
}

void Bus::flush()
{
    while (Wire.available()) {
        Wire.read();
    }
}
