#ifndef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

void setup()
{
    wdt_disable();

    pinMode(PD6, OUTPUT);
    digitalWrite(PD6, 0);
    
    #if DEBUG
    Serial.begin(57600);
    PRINTLN("");
    #endif

    LNPanel.addEdge(9);
    LNPanel.addEdge(10);
    LNPanel.addEdge(11);

    LNPanel.configure({ .interruptPinNo = 2 });

    PRINTLN("===> [PANEL]");

    digitalWrite(PD6, 1);
}

void loop()
{
    LNPanel.run();
}

#endif