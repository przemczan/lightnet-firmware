#include "LightnetBus.hpp"

LightnetBus::LightnetBus()
{
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);
}

void LightnetBus::begin(uint8_t address)
{
    Wire.begin(address);
}

void LightnetBus::begin()
{
    Wire.begin();
}

void LightnetBus::end()
{
    Wire.endTransmission(true);
}

uint8_t LightnetBus::registerPanel(uint8_t edgesNumber, uint8_t parentEdge)
{
    Protocol::PacketRegisterPanel registerPanelPacket;
    Protocol::PacketRegisterPanelResponse response;

    registerPanelPacket.edgesNumber = edgesNumber;
    registerPanelPacket.parentEdge = parentEdge;

    PRINT("Sending register request... ");

    uint8_t result = this->sendPacketWithResponse(
        Protocol::CONTROLLER_ADDRESS,
        &registerPanelPacket,
        sizeof(registerPanelPacket),
        Protocol::PACKET_REGISTER_PANEL,
        &response,
        sizeof(Protocol::PacketRegisterPanelResponse)
    );

    if (result == 0) {
        PRINTKV("success", response.panelId);

        return response.panelId;
    }

    PRINTLN("failed.");

    return 0;
}

uint8_t LightnetBus::respondToRegisterPanel(uint8_t id)
{
    Protocol::PacketRegisterPanelResponse response;
    response.panelId = id;

    uint8_t result = this->sendResponsePacket(&response, sizeof(response), Protocol::PACKET_REGISTER_PANEL_RESPONSE);

    return result;
}

uint8_t LightnetBus::setColorAndBrightness(uint8_t address, Protocol::Color *color, uint8_t brightness)
{
    Protocol::PacketSetColorAndBrightness packet;

    packet.color = *color;
    packet.brightness = brightness;

    return this->sendPacket(address, &packet, sizeof(packet), Protocol::PACKET_SET_COLOR_AND_BRIGHTNESS);
}

uint8_t LightnetBus::setColor(uint8_t address, Protocol::Color *color)
{
    Protocol::PacketSetColor packet;

    packet.color = *color;

    return this->sendPacket(address, &packet, sizeof(packet), Protocol::PACKET_SET_COLOR);
}

uint8_t LightnetBus::setBrightness(uint8_t address, uint8_t brightness)
{
    Protocol::PacketSetBrightness packet;

    packet.brightness = brightness;

    return this->sendPacket(address, &packet, sizeof(packet), Protocol::PACKET_SET_BRIGHTNESS);
}

uint8_t LightnetBus::turnOnOff(uint8_t address, uint8_t on)
{
    Protocol::PacketTurnOnOff packet;

    packet.on = on;

    return this->sendPacket(address, &packet, sizeof(packet), Protocol::PACKET_TURN_ON_OFF);
}

uint8_t LightnetBus::turnOn(uint8_t address)
{
    return this->turnOnOff(address, 1);
}

uint8_t LightnetBus::turnOff(uint8_t address)
{
    return this->turnOnOff(address, 0);
}

void LightnetBus::onReceive(int size)
{
    if (LightnetBus::onPacketReceivedCallback) {
        uint8_t buffer[size];
        Wire.readBytes(&buffer[0], size);

        if (Protocol::validatePacket(&buffer[0], size) == 0) {
            LightnetBus::onPacketReceivedCallback((Protocol::PacketMeta *)&buffer, size);
        }
    }
}

void LightnetBus::onRequest()
{
    if (LightnetBus::onPacketRequestedCallback) {
        LightnetBus::onPacketRequestedCallback();
    }
}

void LightnetBus::setOnPacketReceived(onPacketReceived_t callback)
{
    LightnetBus::onPacketReceivedCallback = callback;
}

void LightnetBus::setOnPacketRequested(onPacketRequested_t callback)
{
    LightnetBus::onPacketRequestedCallback = callback;
}

uint8_t LightnetBus::sendPacket(uint8_t address, void *packet, uint8_t size, Protocol::packetType_t type)
{
    Protocol::setPacketMeta(packet, type);

    Wire.beginTransmission(address);
    Wire.write((uint8_t *)packet, size);

    return Wire.endTransmission();
}

uint8_t LightnetBus::sendResponsePacket(void *packet, uint8_t size, Protocol::packetType_t type)
{
    Protocol::setPacketMeta(packet, type);

    Wire.write((uint8_t *)packet, size);
}

uint8_t LightnetBus::requestPacket(uint8_t address, void *buffer, uint8_t size)
{
    uint8_t receivedSize = Wire.requestFrom(address, size, (uint8_t)true);

    if (receivedSize != size) {
        return 1;
    }

    Wire.readBytes((uint8_t *)buffer, size);

    if (Protocol::validatePacket(buffer, receivedSize) == 0) {
        return 0;
    }

    return 2;
}

uint8_t LightnetBus::sendPacketWithResponse(
    uint8_t address,
    void *packet,
    uint8_t packetSize,
    Protocol::packetType_t packetType,
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

void LightnetBus::flush()
{
    while (Wire.available()) {
        Wire.read();
    }
}

LightnetBus::onPacketReceived_t LightnetBus::onPacketReceivedCallback;
LightnetBus::onPacketRequested_t LightnetBus::onPacketRequestedCallback;

LightnetBus LNBus;
