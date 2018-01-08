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
volatile List<volatile Panel*> panels;
volatile Panel *currentPanel;

void handlePacket(Protocol::PacketMeta *packet, uint16_t size)
{
    switch (packet->header.type)
    {
        case Protocol::PACKET_REGISTER_PANEL:
            ++lastId;
            Protocol::RegisterPanel *data = (Protocol::RegisterPanel *)packet;
            currentPanel = malloc(sizeof(Panel));
            currentPanel->id = lastId;
            currentPanel->bordersNumber = data->bordersNumber;
            currentPanel->parentBorder = data->parentBorder;
            panels.push(currentPanel);

            Serial.print("register: ");
            Serial.println(lastId);
            break;
    }
}

void onRequest()
{
    Serial.println("request");
    // Wire.write(5);
    // return;
    if (lastId) {
        Protocol::RegisterPanelResponse response;
        Protocol::setPacketMeta(&response, Protocol::PACKET_REGISTER_PANEL_RESPONSE);
        response.panelId = lastId;
        Wire.write((uint8_t *)&response, sizeof(response));



    // for (uint8_t i = 0; i < sizeof(response); i++) {
    //     Serial.print(((uint8_t *)&response)[i], 16);
    //     Serial.print(" ");
    // }
    // Serial.println("");
        // uint8_t buffer[5] = {1, 1, 0xC0, 11, 1};
        // Wire.write(&buffer[0], 5);
        // delayMicroseconds(100);
            // for (uint8_t i = 0; i < 1; i++) {
            //     Serial.print(buffer[i], 16);
            //     Serial.print(" ");
            // }
            // Serial.println("");
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
        break;

        case STATE_READY:
        break;
    }
}
