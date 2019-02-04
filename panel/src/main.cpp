#include "Config.hpp"
#include <Arduino.h>
#include "LightnetPanel.hpp"

void updateEdgesStates()
{
    LNPanel.updateEdgesStates();
}

void setup()
{
    #if DEBUG
    Serial.begin(115200);
    #endif

    LNPanel.configure({
        .rPinNo = 3,
        .gPinNo = 3,
        .bPinNo = 3
    });

    LNPanel.addEdge(8);

    pinMode(2, INPUT);
    attachInterrupt(digitalPinToInterrupt(2), updateEdgesStates, CHANGE);
    interrupts();

    PRINTLN("PANEL setup completed.");
}

void loop()
{
    LNPanel.run();
}
