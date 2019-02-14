#include <Arduino.h>
#include "PanelsInitializer.hpp"
#include "PanelsController.hpp"

// #define STATE_BOOT 0
// #define STATE_READY 1
//uint8_t state = STATE_BOOT;

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    #define INITIALIZER_EDGE_PIN_NO 12
    #define INITIALIZER_EDGE_INTERRUPT_PIN_NO 13
    #define LED_PIN 2
    #define IIC_SDA_PIN 4
    #define IIC_SCL_PIN 5
#else
    #define INITIALIZER_EDGE_PIN_NO 8
    #define INITIALIZER_EDGE_INTERRUPT_PIN_NO 2
    #define LED_PIN 13
    #define IIC_SDA_PIN 4
    #define IIC_SCL_PIN 5
#endif

Protocol::Color c;
PanelsController LNController;
float R;
uint8_t brightnessMap[256];

void setup() {
    #if DEBUG
    Serial.begin(115200);
    #endif

    PRINTLN("\n[HARDWARE INIT] start");

    delay(500);
    LNPanelsInitializer.configure({
        .sdaPinNo = IIC_SDA_PIN,
        .sclPinNo = IIC_SCL_PIN,
        .edgePinNo = INITIALIZER_EDGE_PIN_NO,
        .intPinNo = INITIALIZER_EDGE_INTERRUPT_PIN_NO
    });
    LNPanelsInitializer.start();

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    PRINTLN("[HARDWARE INIT] end");

    //LNController.resetDevices();

    delay(500);

    PRINTLN("===> [INITIALIZER]");

    R = 255 * log10(2) / log10(255);

    uint8_t index = 255;
    do {
        brightnessMap[index] = pow(2, index / R) - 1;
    } while (index--);

    digitalWrite(LED_PIN, HIGH);

    delay(2000);
    PRINTLN("Initializing...");
}

void loop() {

    LNPanelsInitializer.doInitialize();

    uint8_t val;
    uint8_t brightness1, brightness2;
    uint16_t prevIndex;

    if (LNPanelsInitializer.isReady()) {
         digitalWrite(LED_PIN, LOW);

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

             brightness1 = 0;
             brightness2 = 255;
             do {
                 LNController.setBrightness(panelIndex, brightnessMap[brightness1++]);
                 LNController.setBrightness(prevIndex, brightnessMap[brightness2--]);
             } while (brightness1 != 0);
         }
    }

    //
    // uint8_t val;
    // uint8_t brightness1, brightness2;
    // uint16_t prevIndex;
    //
    // switch (state)
    // {
    //     case STATE_BOOT:
    //
    //         LNPanelsInitializer.doInitialize();
    //
    //         if (LNPanelsInitializer.isReady()) {
    //             state = STATE_READY;
    //             digitalWrite(13, LOW);
    //             PRINTLN("INITIALIZER is ready!");
    //
    //             delay(500);
    //         }
    //     break;
    //
    //     case STATE_READY:
    //
    //         for (uint8_t i = 0; i < LNPanelsInitializer.getPanels()->getSize(); i++) {
    //             uint8_t panelIndex = LNPanelsInitializer.getPanels()->get(i)->index;
    //
    //             prevIndex = i
    //                 ? LNPanelsInitializer.getPanels()->get(i - 1)->index
    //                 : LNPanelsInitializer.getPanels()->last()->index;
    //
    //             PRINTLN3("Testing", panelIndex, prevIndex);
    //
    //             c.rgb.r = 255;
    //             c.rgb.g = 255;
    //             c.rgb.b = 255;
    //
    //             LNController.setColorAndBrightness(panelIndex, &c, 0);
    //             LNController.turnOn(panelIndex);
    //             delay(5);
    //             // delay(100);
    //             // LNController.turnOff(panelIndex);
    //
    //             //
    //             // do {
    //             //     c.rgb.b--;
    //             //     LNController.setColor(panelIndex, &c);
    //             //     delay(2);
    //             // } while (c.rgb.b);
    //             //
    //             // do {
    //             //     c.rgb.b++;
    //             //     LNController.setColor(panelIndex, &c);
    //             //     delay(2);
    //             // } while (c.rgb.b < 255);
    //             //
    //             // c.rgb.b = 255;
    //             // LNController.setColor(panelIndex, &c);
    //             // delay(1000);
    //
    //             brightness1 = 0;
    //             brightness2 = 255;
    //             do {
    //                 LNController.setBrightness(panelIndex, brightnessMap[brightness1++]);
    //                 LNController.setBrightness(prevIndex, brightnessMap[brightness2--]);
    //             } while (brightness1 != 0);
    //         }
    //
    //     break;
    // }
}
