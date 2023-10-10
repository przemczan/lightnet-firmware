#ifndef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

#define EDGE_1_PIN 9
#define EDGE_2_PIN 10
#define EDGE_3_PIN 11

void setup()
{
    wdt_reset();
    wdt_disable();

    pinMode(PD6, OUTPUT);
    digitalWrite(PD6, 0);
    
    #if DEBUG
    Serial.begin(57600);
    PRINTLN("");
    #endif

    LNPanel.addEdge(EDGE_1_PIN);
    LNPanel.addEdge(EDGE_2_PIN);
    LNPanel.addEdge(EDGE_3_PIN);

    LNPanel.configure({});

    PRINTLN("===> [PANEL]");

    // PCIE0 is for PB port
    PCICR |= (1 << PCIE0);
    // enable PC INTs for Edge pins 1-3
    PCMSK0 |= (1 << PCINT1) | (1 << PCINT2) | (1 << PCINT3);

    delay(100);

    digitalWrite(PD6, 1);
}

void loop()
{
    LNPanel.run();
}

ISR (PCINT0_vect)
{ 
    LNPanel.updateEdgesStates(); 
    digitalWrite(PD6, 0);
}

#endif