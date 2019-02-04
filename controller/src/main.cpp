#include "Config.hpp"
#include <Arduino.h>
#include "PanelsInitializer.hpp"
#include "Macros.hpp"
#include "PanelsController.hpp"

#define STATE_BOOT 0
#define STATE_READY 1

uint8_t state = STATE_BOOT;

#define CONTROLLER_EDGE_PIN_NO 8
#define CONTROLLER_EDGE_INTERRUPT_PIN_NO 2

Protocol::Color c;
PanelsController LNController;

void updateEdgeState()
{
    LNPanelsInitializer.updateEdgeState();
}

void setup() {
    #if DEBUG
    Serial.begin(115200);
    Serial.println("CONTROLLER");
    #endif

    delay(500);
    LNPanelsInitializer.start(CONTROLLER_EDGE_PIN_NO);

    pinMode(CONTROLLER_EDGE_INTERRUPT_PIN_NO, INPUT);
    attachInterrupt(digitalPinToInterrupt(CONTROLLER_EDGE_INTERRUPT_PIN_NO), updateEdgeState, CHANGE);
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

                delay(2000);
            }
        break;

        case STATE_READY:

        c.rgb.b = 255;
        LNController.setColorAndBrightness(0, &c, 255);
        LNController.turnOn(0);
        delay(250);
        LNController.turnOff(0);
        delay(250);
        LNController.turnOn(0);
        delay(1000);

        uint8_t index = 255;

        do {
            c.rgb.b--;
            LNController.setColor(0, &c);
            delay(5);
        } while (c.rgb.b);

        do {
            c.rgb.b++;
            LNController.setColor(0, &c);
            delay(5);
        } while (c.rgb.b < 255);

        c.rgb.b = 255;
        LNController.setColor(0, &c);
        delay(1000);

        index = 255;
        do {
            LNController.setBrightness(0, --index);
            delay(5);
        } while (index);
        delay(2000);

        break;
    }
}
