#include "Config.hpp"
#include <Arduino.h>
#include "PanelsInitializer.hpp"

#define STATE_BOOT 0
#define STATE_READY 1

uint8_t state = STATE_BOOT;

Protocol::Color c;

void updateEdgeState()
{
    LNPanelsInitializer.updateEdgeState();
}

void setup() {
    #if DEBUG
    Serial.begin(9600);
    Serial.println("CONTROLLER");
    #endif

    delay(500);
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

        if (c.rgb.r) {
            c.rgb.b++;
            if (c.rgb.b == 255) {
                c.rgb.r = 0;
            }
        } else {
            c.rgb.b--;
            if (c.rgb.b == 0) {
                c.rgb.r = 1;
            }
        }

        LNBus.setColorAndBrightness(11, &c, 128);
        delay(2);

        break;
    }
}
