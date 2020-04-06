#ifndef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

void setup()
{
    wdt_disable();

    #if DEBUG
    Serial.begin(115200);
    #endif

    PRINTLN("");

    LNPanel.addEdge(PB1);
    LNPanel.addEdge(PB2);
    LNPanel.addEdge(PB3);

    LNPanel.configure({ .interruptPinNo = PD2 });

    PRINTLN("===> [PANEL]");
}

void loop()
{
    LNPanel.run();
}

#endif