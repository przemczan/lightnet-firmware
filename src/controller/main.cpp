#ifdef LIGHTNET_TARGET_CONTROLLER

#include "main.h"

#if defined(ARDUINO_ARCH_ESP8266)
    #define INITIALIZER_EDGE_PIN_NO 13
    #define INITIALIZER_EDGE_INTERRUPT_PIN_NO 12
    #define LED_PIN 2
    #define IIC_SDA_PIN 4
    #define IIC_SCL_PIN 5
    #define PANELS_POWER_PIN 14
#elif defined(ARDUINO_ARCH_ESP32)
    #define INITIALIZER_EDGE_PIN_NO 12
    #define INITIALIZER_EDGE_INTERRUPT_PIN_NO 13
    #define LED_PIN 2
    #define IIC_SDA_PIN 4
    #define IIC_SCL_PIN 5
    #define PANELS_POWER_PIN 21
#else
    #define INITIALIZER_EDGE_PIN_NO 8
    #define INITIALIZER_EDGE_INTERRUPT_PIN_NO 2
    #define LED_PIN 13
    #define IIC_SDA_PIN 4
    #define IIC_SCL_PIN 5
    #define PANELS_POWER_PIN 3
#endif

uint16_t const SERVER_PORT = 80;
#define CONFIG_PORTAL_TIMEOUT 60

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

    #ifdef ARDUINO_ARCH_ESP8266
    sprintf(buffer, "lightnet-%04X\0", ESP.getChipId());
    #else
    sprintf(buffer, "lightnet-%08X\0", ESP.getEfuseMac());
    #endif

    MDNS.begin(&buffer[0]);
    MDNS.addService("lightnet", "tcp", SERVER_PORT);
}

void fadeIn(uint16_t panelIndex)
{
    PRINTKV("[FADE IN]", panelIndex);
    uint8_t brightness = 0;

    while (++brightness < 50) {
        panelsController->setBrightness(panelIndex, brightness * 5);
        delay(3);
    }
}

void fadeOut(uint16_t panelIndex)
{
    PRINTKV("[FADE OUT]", panelIndex);
    uint8_t brightness = 50;

    while (brightness--) {
        panelsController->setBrightness(panelIndex, brightness * 5);
        delay(3);
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
        panelsController->setBrightness(panel->index, 255);
    }

    PRINTLN("[SELF TEST END]");
}

void sendConfiguration()
{
    Panel *panel;
    uint16_t panelNum = LNPanelsInitializer.getPanels()->getSize();

    while (panelNum--) {
        panel = LNPanelsInitializer.getPanels()->get(panelNum);

        panelsController->sendConfiguration(
            panel->index,
            { .useGammaCorrection = true, .colorTemperature = Halogen, .colorCorrection = TypicalLEDStrip}
        );
    }
}

void setupWiFi()
{
    WiFi.mode(WIFI_STA);

    webServer = new AsyncWebServer(SERVER_PORT);
    wifiManager = new AsyncWiFiManager(webServer, &dns);
    messageServer = new MessageServer(webServer);
    messageHandler = new MessageHandler(messageServer, panelsController);
    appServer = new AppServer(webServer);
    setupMDNS();
    wifiManager->setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
    wifiManager->autoConnect("Lightnet-Controller");
    webServer->begin();
}

void setup()
{
    #if DEBUG
    Serial.begin(57600);
    #endif

    LNPanelsInitializer.configure({.sdaPinNo = IIC_SDA_PIN,
                                   .sclPinNo = IIC_SCL_PIN,
                                   .edgePinNo = INITIALIZER_EDGE_PIN_NO,
                                   .intPinNo = INITIALIZER_EDGE_INTERRUPT_PIN_NO});
    LNPanelsInitializer.start();

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    //pinMode(PANELS_POWER_PIN, OUTPUT);
    PRINTLN("reseting panels power...");
    //digitalWrite(PANELS_POWER_PIN, LOW);
    delay(100);
    //digitalWrite(PANELS_POWER_PIN, HIGH);
    PRINTLN("waiting for panels to boot");
    delay(150);
    PRINTLN("Initializing...");

    panelsController = new PanelsController();

    // not needed if panels power controll work
    // will send reset command to N devices to reset them if they are running
    panelsController->resetDevices(50);
    // panels have 100ms delay on startup, we need to wait for them to initialize
    // additional time is needed if they were reset by command above (up to 100ms)
    delay(300);

    digitalWrite(LED_PIN, HIGH);
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

                sendConfiguration();
                selfTest();

                setupWiFi();
                break;

            case 1:
                #ifdef ARDUINO_ARCH_ESP8266
                MDNS.update();
                #endif
                messageHandler->handleIncommingMessages();
                break;
        }
    }
}

#endif