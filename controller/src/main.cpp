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
float R;

uint8_t brightnessMap[256];

void setup() {
    #if DEBUG
    Serial.begin(115200);
    #endif

    PRINTLN("");

    delay(1000);
    LNPanelsInitializer.start(CONTROLLER_EDGE_PIN_NO, CONTROLLER_EDGE_INTERRUPT_PIN_NO);

    //LNController.resetDevices();

    delay(1500);

    PRINTLN("===> [CONTROLLER]");

    pinMode(13, OUTPUT);
    digitalWrite(13, HIGH);

    R = 255 * log10(2) / log10(255);

    uint8_t index = 255;
    do {
        brightnessMap[index] = pow(2, index / R) - 1;
    } while (index--);
}

void loop() {
    uint8_t val;
    uint8_t brightness1, brightness2;
    uint16_t prevIndex;

    switch (state)
    {
        case STATE_BOOT:

            LNPanelsInitializer.doInitialize();

            if (LNPanelsInitializer.isReady()) {
                state = STATE_READY;
                digitalWrite(13, LOW);
                PRINTLN("CONTROLLER is ready!");

                delay(500);
            }
        break;

        case STATE_READY:

            for (uint8_t i = 0; i < LNPanelsInitializer.getPanels()->getSize(); i++) {
                uint8_t panelIndex = LNPanelsInitializer.getPanels()->get(i)->index;

                prevIndex = i
                    ? LNPanelsInitializer.getPanels()->get(i - 1)->index
                    : LNPanelsInitializer.getPanels()->last()->index;

                PRINTLN3("Testing", panelIndex, prevIndex);

                c.rgb.r = 255;
                c.rgb.g = 255;
                c.rgb.b = 255;

                LNController.setColorAndBrightness(panelIndex, &c, 0);
                LNController.turnOn(panelIndex);
                delay(5);
                // delay(100);
                // LNController.turnOff(panelIndex);

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

                brightness1 = 0;
                brightness2 = 255;
                do {
                    LNController.setBrightness(panelIndex, brightnessMap[brightness1++]);
                    LNController.setBrightness(prevIndex, brightnessMap[brightness2--]);
                } while (brightness1 != 0);
            }

        break;
    }
}
