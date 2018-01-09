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
    Protocol::RegisterPanel registerPanelPacket;
    Protocol::RegisterPanelResponse response;

    registerPanelPacket.edgesNumber = edgesNumber;
    registerPanelPacket.parentEdge = parentEdge;

    PRINT("Sending register request... ");

    uint8_t result = this->sendPacketWithResponse(
        Protocol::CONTROLLER_ADDRESS,
        &registerPanelPacket,
        sizeof(registerPanelPacket),
        Protocol::PACKET_REGISTER_PANEL,
        &response,
        sizeof(Protocol::RegisterPanelResponse)
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
    Protocol::RegisterPanelResponse response;
    response.panelId = id;

    uint8_t result = this->sendResponsePacket(&response, sizeof(response), Protocol::PACKET_REGISTER_PANEL_RESPONSE);

    return result;
}

void LightnetBus::onReceive(int size)
{
    if (LightnetBus::onPacketReceivedCallback) {
        uint8_t buffer[size];
        Wire.readBytes(&buffer[0], size);

        if (Protocol::validatePacket(&buffer[0], size) == 0) {
            LightnetBus::onPacketReceivedCallback((Protocol::PacketMeta *)&buffer);
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

uint8_t LightnetBus::sendPacket(uint8_t address, void *packet, uint8_t size, uint8_t type)
{
    Protocol::setPacketMeta(packet, type);

    Wire.beginTransmission(address);
    Wire.write((uint8_t *)packet, size);

    return Wire.endTransmission();
}

uint8_t LightnetBus::sendResponsePacket(void *packet, uint8_t size, uint8_t type)
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

void LightnetBus::flush()
{
    while (Wire.available()) {
        Wire.read();
    }
}

LightnetBus::onPacketReceived_t LightnetBus::onPacketReceivedCallback;
LightnetBus::onPacketRequested_t LightnetBus::onPacketRequestedCallback;

LightnetBus LNBus;
