#ifndef SIM_MODE
#include "LightnetBus.hpp"

LightnetBus::LightnetBus()
{
    #if !IS_ESP
        Wire.onReceive(LightnetBus::onReceiveService);
        Wire.onRequest(LightnetBus::onRequestService);
    #endif
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
    this->onPacketReceivedCallback((Protocol::PacketMeta *)&buffer[0], size);
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
    Wire.setClock(BUS_FREQUENCY);
}

void LightnetBus::begin(uint8_t sdaPin, uint8_t sclPin, uint8_t address)
{
    #if IS_ESP32
        Wire.begin(sdaPin, sclPin, address);
    #else
        Wire.begin(address);
    #endif
    Wire.setClock(BUS_FREQUENCY);
}

void LightnetBus::begin()
{
    #if IS_ESP
        Wire.begin();
        #if IS_ESP8266
            Wire.setClockStretchLimit(1500);
        #endif
    #else
        Wire.begin();
    #endif
    Wire.setClock(BUS_FREQUENCY);
}

void LightnetBus::begin(uint8_t sdaPin, uint8_t sclPin)
{
    #if IS_ESP
        Wire.begin(sdaPin, sclPin);
        #if IS_ESP8266
            Wire.setClockStretchLimit(1500);
        #endif
    #else
        Wire.begin();
    #endif
    Wire.setClock(BUS_FREQUENCY);
}

void LightnetBus::end()
{
    #if IS_ESP8266
        twi_stop();
    #elif !IS_ESP
        Wire.end();
    #endif
}

uint8_t LightnetBus::sendPacket(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size, bool end)
{
    if (this->onPacketSentCallback) {
        this->onPacketSentCallback(address, packet, size);
    }

    return this->sendData(address, packet, size, end);
}

uint8_t LightnetBus::sendData(uint8_t address, const Protocol::PacketMeta *data, uint8_t size, bool end)
{
    delayMicroseconds(3);
    Wire.beginTransmission(address);
    Wire.write((const uint8_t *)data, size);

    return Wire.endTransmission(end);
}

uint8_t LightnetBus::sendPacketAck(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size)
{
    Protocol::PacketMeta ack;

    return this->sendPacketWithResponse(address, packet, size, &ack, sizeof(ack));
}

uint8_t LightnetBus::sendPacketNack(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size)
{
    return this->sendPacket(address, packet, size, true);
}

uint8_t LightnetBus::sendPacketWithResponse(
    uint8_t                     address,
    const Protocol::PacketMeta *packet,
    uint8_t                     packetSize,
    Protocol::PacketMeta *      responseBuffer,
    uint8_t                     responseSize
)
{
    uint8_t writeErr = this->sendPacket(address, packet, packetSize, false);

    if (writeErr != 0) {
        DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTF("[BUS] write err=%d addr=0x%02X\n", writeErr, address));

        return 1;
    }

    delayMicroseconds(3);

    uint8_t readErr = this->requestPacket(address, responseBuffer, responseSize);

    if (readErr != 0) {
        DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTF("[BUS] read err=%d addr=0x%02X\n", readErr, address));

        return 2;
    }

    return 0;
}

uint8_t LightnetBus::sendResponsePacket(Protocol::PacketMeta *packet, uint8_t size)
{
    return Wire.write((const uint8_t *)packet, size);
}

uint8_t LightnetBus::sendResponseData(const Protocol::PacketMeta *data, uint8_t size)
{
    return Wire.write((const uint8_t *)data, size);
}

uint8_t LightnetBus::requestPacket(uint8_t address, void *buffer, uint8_t size)
{
    uint8_t receivedSize = this->requestData(address, buffer, size);

    if (!receivedSize || receivedSize != size) {
        DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTF("[BUS] ack size got=%d expected=%d\n", receivedSize, size));

        return 1;
    }

    uint8_t vErr = Protocol::validatePacket(static_cast<const Protocol::PacketMeta *>(buffer), receivedSize);

    if (vErr == 0) {
        return 0;
    }

    DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTF("[BUS] ack validate err=%d\n", vErr));

    return 2;
}

uint8_t LightnetBus::requestData(uint8_t address, void *buffer, uint8_t maxSize)
{
    uint8_t receivedSize = Wire.requestFrom(address, maxSize, (uint8_t)true);

    if (receivedSize > maxSize) {
        DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTLN("Max data length exceeded"));

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
#endif  // SIM_MODE
