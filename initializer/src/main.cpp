
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

PanelsController LNController;
uint8_t state = 0;

AsyncWebServer *webServer;
DNSServer dns;
AsyncWiFiManager *wifiManager;
CommandServer *cmdServer;

void setup()
{
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

    digitalWrite(LED_PIN, HIGH);

    delay(500);
    PRINTLN("Initializing...");

    webServer = new AsyncWebServer(81);
    wifiManager = new AsyncWiFiManager(webServer, &dns);
    cmdServer = new CommandServer(webServer);
}

void fadeIn(uint16_t panelIndex) {
    PRINTKV("[FADE IN]", panelIndex);
    uint8_t brightness = 0;

    while (++brightness < 0xFF) {
        LNController.setBrightness(panelIndex, brightness);
        delayMicroseconds(50);
    }
}

void fadeOut(uint16_t panelIndex) {
    PRINTKV("[FADE OUT]", panelIndex);
    uint8_t brightness = 0xFF;

    while (brightness--) {
        LNController.setBrightness(panelIndex, brightness);
        delayMicroseconds(50);
    }
}

void selfTest()
{
    PRINTLN("[SELF TEST BEGIN]");

    Panel *panel;
    uint16_t panelCount = LNPanelsInitializer.getPanels()->getSize();
    uint16_t panelNum = panelCount;

    while (panelNum--) {
        panel = LNPanelsInitializer.getPanels()->get(panelNum);

        LNController.turnOn(panel->index);
        fadeIn(panel->index);
    }

    delay(500);

    panelNum = 0;

    while (panelNum < panelCount) {
        panel = LNPanelsInitializer.getPanels()->get(panelNum);

        LNController.turnOn(panel->index);
        fadeOut(panel->index);

        panelNum++;
    }

    PRINTLN("[SELF TEST END]");
}

void loop()
{
    LNPanelsInitializer.boot();

    if (LNPanelsInitializer.isReady()) {
        digitalWrite(LED_PIN, LOW);

        switch (state) {
            case 0:
                delay(500);

                state = 1;

                selfTest();

                wifiManager->autoConnect();
                cmdServer->start();
                webServer->begin();
                break;

            case 1:
                cmdServer->loop();
                break;
        }
    }
}
