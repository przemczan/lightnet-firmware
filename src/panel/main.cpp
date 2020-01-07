#ifndef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

void setup()
{
    wdt_disable();

    #if DEBUG
    Serial.begin(230400);
    #endif

    PRINTLN("");

    LNPanel.addEdge(8);
    LNPanel.addEdge(9);
    LNPanel.addEdge(10);

    LNPanel.configure({ .interruptPinNo = 2 });

    PRINTLN("===> [PANEL]");
}

void loop()
{
    LNPanel.run();
}

#endif