#ifdef LIGHTNET_TARGET_CONTROLLER

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

uint16_t const SERVER_PORT = 80;

uint8_t state = 0;
DNSServer dns;
PanelsController *panelsController;
AsyncWebServer *webServer;
AsyncWiFiManager *wifiManager;
MessageServer *messageServer;
MessageHandler *messageHandler;
AppServer *appServer;

void setupMDNS()
{
    char buffer[20];

    sprintf(buffer, "lightnet-%04X\0", ESP.getChipId());

    MDNS.begin(&buffer[0]);
    MDNS.addService("lightnet", "tcp", 80);
}

void setup()
{
    #if DEBUG
    Serial.begin(115200);
    #endif
    PRINTLN("\n[HARDWARE INIT] start");

    delay(500);
    LNPanelsInitializer.configure({.sdaPinNo = IIC_SDA_PIN,
                                   .sclPinNo = IIC_SCL_PIN,
                                   .edgePinNo = INITIALIZER_EDGE_PIN_NO,
                                   .intPinNo = INITIALIZER_EDGE_INTERRUPT_PIN_NO});
    LNPanelsInitializer.start();

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    PRINTLN("[HARDWARE INIT] end");

    //panelsController.resetDevices();

    delay(500);

    PRINTLN("===> [INITIALIZER]");

    digitalWrite(LED_PIN, HIGH);

    delay(500);
    PRINTLN("Initializing...");

    panelsController = new PanelsController();
    webServer = new AsyncWebServer(SERVER_PORT);
    wifiManager = new AsyncWiFiManager(webServer, &dns);
    messageServer = new MessageServer(webServer);
    messageHandler = new MessageHandler(messageServer, panelsController);
    appServer = new AppServer(webServer);

    setupMDNS();
}

void fadeIn(uint16_t panelIndex)
{
    PRINTKV("[FADE IN]", panelIndex);
    uint8_t brightness = 0;

    while (++brightness < 0xFF) {
        panelsController->setBrightness(panelIndex, brightness);
        delayMicroseconds(25);
    }
}

void fadeOut(uint16_t panelIndex)
{
    PRINTKV("[FADE OUT]", panelIndex);
    uint8_t brightness = 0xFF;

    while (brightness--) {
        panelsController->setBrightness(panelIndex, brightness);
        delayMicroseconds(25);
    }
}

void selfTest()
{
    PRINTLN("[SELF TEST BEGIN]");

    Panel *panel;
    uint16_t panelCount = LNPanelsInitializer.getPanels()->getSize();
    uint16_t panelNum = 0;

    while (panelNum < panelCount) {
        panel = LNPanelsInitializer.getPanels()->get(panelNum);

        panelsController->turnOn(panel->index);
        fadeIn(panel->index);

        panelNum++;
    }

    delay(250);

    panelNum = panelCount;

    while (panelNum--) {
        panel = LNPanelsInitializer.getPanels()->get(panelNum);
        fadeOut(panel->index);
    }

    panelNum = panelCount;

    while (panelNum--) {
        panel = LNPanelsInitializer.getPanels()->get(panelNum);
        panelsController->turnOff(panel->index);
        panelsController->setBrightness(panel->index, 0xFF);
    }

    PRINTLN("[SELF TEST END]");
}

void loop()
{
    MDNS.update();
    LNPanelsInitializer.boot();

    if (LNPanelsInitializer.isReady()) {
        digitalWrite(LED_PIN, LOW);

        switch (state) {
            case 0:
                delay(500);

                state = 1;

                selfTest();

                wifiManager->autoConnect();
                webServer->begin();
                break;

            case 1:
                messageHandler->handleIncommingMessages();
                break;
        }
    }
}

#endif