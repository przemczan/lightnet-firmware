#include <Arduino.h>
#include <Wire.h>
#include "Protocol.hpp"
#include "List.hpp"
#include "Crc.hpp"

#define STATE_BOOT 0
#define STATE_READY 1

uint8_t state = STATE_BOOT;

struct Panel
{
    uint8_t id;
    uint8_t bordersNumber;
    uint8_t parentBorder;
};

volatile uint8_t lastId = 0;
volatile Panel *currentPanel = 0;
List<Panel*> panels;

void handlePacket(Protocol::PacketMeta *packet, uint16_t size)
{
    switch (packet->header.type)
    {
        case Protocol::PACKET_REGISTER_PANEL:
            ++lastId;
            Protocol::RegisterPanel *data = (Protocol::RegisterPanel *)packet;
            currentPanel = (volatile Panel *)malloc(sizeof(Panel));
            currentPanel->id = lastId;
            currentPanel->bordersNumber = data->bordersNumber;
            currentPanel->parentBorder = data->parentBorder;

            Serial.print("register: ");
            Serial.println(lastId);
            break;
    }
}

void onRequest()
{
    if (lastId) {
        Protocol::RegisterPanelResponse response;
        Protocol::setPacketMeta(&response, Protocol::PACKET_REGISTER_PANEL_RESPONSE);
        response.panelId = lastId;
        Wire.write((uint8_t *)&response, sizeof(response));
    }
}

void onReceive(int size)
{
    uint8_t buffer[size];
    Wire.readBytes(&buffer[0], size);

    if (Protocol::validatePacket(&buffer[0], size) == 0) {
        handlePacket((Protocol::PacketMeta *)&buffer, size);
    }
}

void setup() {
    Serial.begin(9600);
    Serial.println("CONTROLLER");

    Wire.begin(Protocol::CONTROLLER_ADDRESS);
    Wire.onRequest(onRequest);
    Wire.onReceive(onReceive);
}

void loop() {
    switch (state)
    {
        case STATE_BOOT:
            if (currentPanel && currentPanel->id) {
                panels.push((Panel *)currentPanel);
                currentPanel->id = 0;
            }
        break;

        case STATE_READY:
        break;
    }
}
