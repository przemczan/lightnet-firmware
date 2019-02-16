#include "main.h"

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
WiFiManager wifiManager;
uint8_t state = 0;
WebSocketApi wsApi(81);

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

    c.rgb.r = 255;
    c.rgb.g = 255;
    c.rgb.b = 255;

    digitalWrite(LED_PIN, HIGH);

    delay(2000);
    PRINTLN("Initializing...");

    wifiManager.autoConnect();
    wsApi.start();
}

void loop() {
    wsApi.loop();

    LNPanelsInitializer.boot();

    uint8_t val;
    uint16_t brightness1, brightness2;
    uint16_t prevIndex;

    if (LNPanelsInitializer.isReady()) {
        digitalWrite(LED_PIN, LOW);

        switch (state) {
            case 0:
                delay(1000);

                for (uint8_t i = 0; i < LNPanelsInitializer.getPanels()->getSize(); i++) {
                    LNController.turnOn(LNPanelsInitializer.getPanels()->get(i)->index);
                }

                state = 1;

                break;

            case 1:
                for (uint8_t i = 0; i < LNPanelsInitializer.getPanels()->getSize(); i++) {
                    uint8_t panelIndex = LNPanelsInitializer.getPanels()->get(i)->index;

                    prevIndex = i
                        ? LNPanelsInitializer.getPanels()->get(i - 1)->index
                        : LNPanelsInitializer.getPanels()->last()->index;

                    PRINTKV("Testing", panelIndex);

                    LNController.setColorAndBrightness(panelIndex, &c, 0);

                    brightness1 = 0;
                    brightness2 = 255;
                    do {
                        LNController.setBrightness(panelIndex, brightnessMap[brightness1]);
                        LNController.setBrightness(prevIndex, brightnessMap[brightness2]);
                        brightness2 -= 1;
                        brightness1 += 1;
                    } while (brightness1 < 250);
                }
                break;
        }
    }
}
