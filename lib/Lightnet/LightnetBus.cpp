#include "LightnetBus.hpp"

LightnetBus::LightnetBus()
{
#if !IS_ESP
    Wire.onReceive(LightnetBus::onReceiveService);
    Wire.onRequest(LightnetBus::onRequestService);
#endif
    Wire.setClock(400000);
}

void LightnetBus::onReceiveService(int size)
{
    LNBus.onReceive(size);
}

void LightnetBus::onRequestService()
{
    LNBus.onRequest();
}

void LightnetBus::onReceive(int size)
{
    if (!this->onPacketReceivedCallback) {
        this->flush();

        return;
    }

    uint8_t buffer[size];

    Wire.readBytes(&buffer[0], size);

    if (!Protocol::validatePacket(&buffer[0], size)) {
        this->onPacketReceivedCallback((Protocol::PacketMeta *)&buffer[0], size);
    }
}

void LightnetBus::onRequest()
{
    if (this->onPacketRequestedCallback) {
        this->onPacketRequestedCallback();
    }
}

void LightnetBus::setOnPacketReceived(onPacketReceived_t callback)
{
    this->onPacketReceivedCallback = callback;
}

void LightnetBus::setOnPacketRequested(onPacketRequested_t callback)
{
    this->onPacketRequestedCallback = callback;
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
#if IS_ESP
    //twi_stop();
#else
    Wire.end();
#endif
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

    return Wire.write((uint8_t *)packet, size);
}

uint8_t LightnetBus::requestPacket(uint8_t address, void *buffer, uint8_t size)
{
    uint8_t receivedSize = this->requestData(address, buffer, size);

    if (!receivedSize || receivedSize != size) {
        return 1;
    }

    if (Protocol::validatePacket(buffer, receivedSize) == 0) {
        return 0;
    }

    return 2;
}

uint8_t LightnetBus::requestData(uint8_t address, void *buffer, uint8_t maxSize)
{
    uint8_t receivedSize = Wire.requestFrom(address, maxSize, (uint8_t)true);

    if (receivedSize > maxSize) {
        PRINTLN("Max data length exceeded");
        return 0;
    }

    Wire.readBytes((uint8_t *)buffer, receivedSize);

    return receivedSize;
}

void LightnetBus::flush()
{
    while (Wire.available()) {
        Wire.read();
    }
}

LightnetBus LNBus;
