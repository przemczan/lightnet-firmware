#include "Config.hpp"
#include <Arduino.h>
#include "PanelsInitializer.hpp"
#include "Macros.hpp"

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

                LNPanelsInitializer.startMastering();
                delay(2000);
            }
        break;

        case STATE_READY:

        c.rgb.b = 255;
        LNBus.setColorAndBrightness(11, &c, 255);
        LNBus.turnOn(11);
        delay(250);
        LNBus.turnOff(11);
        delay(250);
        LNBus.turnOn(11);
        delay(1000);

        uint8_t index = 255;

        do {
            c.rgb.b--;
            LNBus.setColor(11, &c);
            delay(5);
        } while (c.rgb.b);

        do {
            c.rgb.b++;
            LNBus.setColor(11, &c);
            delay(5);
        } while (c.rgb.b < 255);

        c.rgb.b = 255;
        LNBus.setColor(11, &c);
        delay(1000);

        index = 255;
        do {
            LNBus.setBrightness(11, --index);
            delay(5);
        } while (index);
        delay(2000);

        break;
    }
}
