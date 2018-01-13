#include "Config.hpp"
#include <Arduino.h>
#include <Macros.hpp>
#include "LightnetPanel.hpp"
#include "LightnetBus.hpp"

#define STATE_BOOT 0
#define STATE_READY 1

volatile uint8_t state = STATE_BOOT;

void updateEdgesStates()
{
    LNPanel.updateEdgesStates();
}

void setup()
{
    #if DEBUG
    Serial.begin(9600);
    #endif

    LNPanel.init(3, 3, 3);

    LNPanel.addEdge(8);
    LNPanel.addEdge(9);
    //LNPanel.addEdge(10);

    attachInterrupt(0, updateEdgesStates, CHANGE);
    interrupts();

    PRINTLN("PANEL setup completed.");
}

void loop()
{
    switch (state)
    {
        case STATE_BOOT:
            LNPanel.boot();

            if (LNPanel.isReady()) {
                state = STATE_READY;
                LNPanel.startListening();
                PRINTLN("Panel is READY");
            }
        break;

        case STATE_READY:
            // nothing to do for now because LNPanel is using interrupts to communicate
        break;
    }
}
