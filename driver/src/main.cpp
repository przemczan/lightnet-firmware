#include "main.hpp"

#define READY_PIN 19

bool ready = false;

void setup() {
    Serial.begin(115200);
    pinMode(READY_PIN, INPUT);
    PRINTLN("===> [DRIVER]");
}

void loop() {
    if (!ready digitalRead(READY_PIN)) {
        PRINTLN("Getting panels info...");

        ready = true;

        LNBus.begin();

        Protocol::PacketMeta firstPanelEdgePacket;
        Protocol::PacketMeta nextPanelPacket;
        Protocol::PacketPanelEdgeInfo edgeInfo;
        uint8_t error;

        error = LNBus.sendPacket(Protocol::POLLING_ADDRESS, &firstPanelEdgePacket, sizeof(firstPanelEdgePacket), Protocol::PACKET_GET_FIRST_PANEL_EDGE_INFO);

        if (error) {
            PRINTKV("Packet sending error", error);

            return;
        }

        do {
            error = LNBus.requestPacket(Protocol::POLLING_ADDRESS, &edgeInfo, sizeof(edgeInfo));

            if (error) {
                PRINTLN("Invalid packet responded (no more data?)");
                PRINTLN4(error, edgeInfo.panelIndex, edgeInfo.edgeIndex, edgeInfo.connectedPanelIndex);
            } else if (edgeInfo.panelIndex) {
                PRINTLN3(edgeInfo.panelIndex, edgeInfo.edgeIndex, edgeInfo.connectedPanelIndex);
                LNBus.sendPacket(Protocol::POLLING_ADDRESS, &nextPanelPacket, sizeof(nextPanelPacket), Protocol::PACKET_GET_NEXT_PANEL_EDGE_INFO);
            } else {
                PRINTLN("Done");
            }
        } while (!error && edgeInfo.panelIndex);

        return;
    }
}
