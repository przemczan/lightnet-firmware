#include <Wire.h>
#include "Bus.hpp"
#include "Config.hpp"
#include "Crc.hpp"

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

    uint8_t result = this->sendPacketWithResponse(
        CONTROLLER_ADDRESS,
        &registerPanelPacket,
        sizeof(RegisterPanel),
        PACKET_REGISTER_PANEL,
        &response,
        sizeof(RegisterPanelResponse)
    );

    if (result == 0) {
        return response.panelId;
    }

    return 0;
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
    Serial.print("received: "); Serial.println(receivedSize);
        //
        // for (uint8_t i = 0; i < receivedSize; i++) {
        //     Serial.print(Wire.read(), 16);
        //     Serial.print(" ");
        // }
        // Serial.println("");

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
