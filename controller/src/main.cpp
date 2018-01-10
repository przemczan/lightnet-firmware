#include "Config.hpp"
#include <Arduino.h>
#include "PanelsInitializer.hpp"

#define STATE_BOOT 0
#define STATE_READY 1

uint8_t state = STATE_BOOT;

void updateEdgeState()
{
    LNPanelsInitializer.updateEdgeState();
}

void setup() {
    Serial.begin(9600);
    Serial.println("CONTROLLER");

    delay(200);
    LNPanelsInitializer.start(CONTROLLER_EDGE_PIN_NO);

    attachInterrupt(0, updateEdgeState, CHANGE);
    interrupts();
}

void loop() {
    switch (state)
    {
        case STATE_BOOT:
            LNPanelsInitializer.doInitialize();

            if (LNPanelsInitializer.isReady()) {
                state = STATE_READY;
                PRINTLN("CONTROLLER is ready!");
            }
        break;

        case STATE_READY:
        break;
    }
}
