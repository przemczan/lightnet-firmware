#include "Config.hpp"
#include <Arduino.h>
#include "PanelsInitializer.hpp"
#include "Macros.hpp"
#include "PanelsController.hpp"

#define STATE_BOOT 0
#define STATE_READY 1

uint8_t state = STATE_BOOT;

#ifdef ARDUINO_ARCH_ESP32
    #define CONTROLLER_EDGE_PIN_NO 7
    #define CONTROLLER_EDGE_INTERRUPT_PIN_NO 6
#else
    #define CONTROLLER_EDGE_PIN_NO 8
    #define CONTROLLER_EDGE_INTERRUPT_PIN_NO 2
#endif

Protocol::Color c;
PanelsController LNController;

void setup() {
    #if DEBUG
    Serial.begin(115200);
    #endif

    PRINTLN("");

    delay(1000);
    LNPanelsInitializer.start(CONTROLLER_EDGE_PIN_NO, CONTROLLER_EDGE_INTERRUPT_PIN_NO);

    PRINTLN("===> [CONTROLLER]");
}

void loop() {
    switch (state)
    {
        case STATE_BOOT:
            LNPanelsInitializer.doInitialize();

            if (LNPanelsInitializer.isReady()) {
                state = STATE_READY;
                PRINTLN("CONTROLLER is ready!");

                delay(500);
            }
        break;

        case STATE_READY:

            for (uint8_t i = 0; i < LNPanelsInitializer.getPanels()->getSize(); i++) {
                uint8_t panelIndex = LNPanelsInitializer.getPanels()->get(i)->index;

                PRINTKV("Testing", panelIndex);

                c.rgb.b = 255;
                LNController.setColorAndBrightness(panelIndex, &c, 100);
                LNController.turnOn(panelIndex);
                delay(100);
                LNController.turnOff(panelIndex);

                // uint8_t index = 255;
                //
                // do {
                //     c.rgb.b--;
                //     LNController.setColor(panelIndex, &c);
                //     delay(2);
                // } while (c.rgb.b);
                //
                // do {
                //     c.rgb.b++;
                //     LNController.setColor(panelIndex, &c);
                //     delay(2);
                // } while (c.rgb.b < 255);
                //
                // c.rgb.b = 255;
                // LNController.setColor(panelIndex, &c);
                // delay(1000);
                //
                // index = 255;
                // do {
                //     LNController.setBrightness(panelIndex, --index);
                //     delay(5);
                // } while (index);
                // delay(2000);
            }

        break;
    }
}
