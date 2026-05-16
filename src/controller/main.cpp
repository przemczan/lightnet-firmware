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
#define CONFIG_PORTAL_TIMEOUT 120

uint8_t state = 0;
DNSServer dns;
PanelsController *panelsController;
AsyncWebServer *webServer;
AsyncWiFiManager *wifiManager;
MessageServer *messageServer;
MessageHandler *messageHandler;
AppServer *appServer;
Lightnet::AnimationScheduler *animScheduler = nullptr;

static uint8_t DEMO_PANELS = 0;
static uint8_t demoPanelAddrs[30];

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
            // { .useGammaCorrection = false, .colorTemperature = Halogen, .colorCorrection = TypicalLEDStrip}
            { .useGammaCorrection = false, .colorTemperature = UncorrectedTemperature , .colorCorrection = UncorrectedColor}

        );
    }
}

// ============================================================================
// Demo Effects
// ============================================================================

// All panels breathe warm orange together — one slow 4-second cycle.
void demoAllBreathe()
{
    PRINTLN("[DEMO] All Breathe");
    Protocol::ColorRGB warmOrange = {255, 80, 10};
    Protocol::Color c;
    c.rgb = warmOrange;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    animScheduler->playOnPanels(1, Lightnet::ANIM_BREATHE, 0, 4000,
        warmOrange, warmOrange, 0, 220, 0, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(4100);
}

// All panels cycle through the full rainbow — no FLAG_LOOP so turnOff() wins.
void demoRainbow()
{
    PRINTLN("[DEMO] Rainbow");
    Protocol::ColorRGB black = {0, 0, 0};
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    // speed=12 → one full hue cycle ≈ 1275 ms → ~5.5 cycles in 7 seconds.
    animScheduler->playOnPanels(2, Lightnet::ANIM_HUE_CYCLE, 0, 7000,
        black, black, 0, 200, 12, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(7000);

    // Fade out over 1 s from whatever hue the cycle landed on.
    // FLAG_CURRENT_COLOR_TO keeps the hue fixed; FLAG_CURRENT_BRIGHTNESS_FROM
    // picks up the live brightness so the fade starts exactly where the cycle left off.
    uint8_t fadeFlags = Lightnet::FLAG_CURRENT_COLOR_TO | Lightnet::FLAG_CURRENT_COLOR_FROM | Lightnet::FLAG_CURRENT_BRIGHTNESS_FROM;
    animScheduler->playOnPanels(3, Lightnet::ANIM_FADE, fadeFlags, 1000,
        black, black, 0, 0, 0, 0,
        demoPanelAddrs, DEMO_PANELS);
    delay(1000);
}

// Panels fire a red pulse one at a time, 4 rounds.
void demoStaggeredPulse()
{
    PRINTLN("[DEMO] Staggered Pulse");
    Protocol::ColorRGB fire = {255, 30, 0};
    Protocol::ColorRGB black = {0, 0, 0};
    for (uint8_t rep = 0; rep < 4; rep++) {
        for (uint8_t i = 0; i < DEMO_PANELS; i++) {
            panelsController->turnOn(demoPanelAddrs[i]);
            // param1=51 → ~20% rise, param2=128 → ~50% fall, hold fills the rest
            animScheduler->playOnPanels(10 + i, Lightnet::ANIM_PULSE, 0, 600,
                black, fire, 0, 255, 51, 128,
                &demoPanelAddrs[i], 1);
            delay(250);
        }
        delay(500);
    }
}

// Cyan dot chases across panels — 4 passes, controller-computed.
void demoChaseLight()
{
    PRINTLN("[DEMO] Chase");
    Protocol::ColorRGB cyan = {0, 200, 255};
    Protocol::Color c;
    c.rgb = cyan;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    for (uint8_t pass = 0; pass < 4; pass++) {
        Lightnet::ChaseRunner runner(20 + pass, demoPanelAddrs, DEMO_PANELS, 1200, cyan);
        while (!runner.isFinished()) {
            runner.tick(millis());
            delay(8);
        }
    }
}

// Warm-white brightness wave sweeps across panels — 3 passes, controller-computed.
void demoColorWave()
{
    PRINTLN("[DEMO] Color Wave");
    Protocol::ColorRGB warm = {255, 220, 160};
    Protocol::Color c;
    c.rgb = warm;
    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
        panelsController->setColorAndBrightness(demoPanelAddrs[i], c, 0);
        panelsController->turnOn(demoPanelAddrs[i]);
    }
    for (uint8_t pass = 0; pass < 3; pass++) {
        Lightnet::WaveRunner runner(30 + pass, demoPanelAddrs, DEMO_PANELS, 1500, 2, warm);
        while (!runner.isFinished()) {
            runner.tick(millis());
            delay(8);
        }
        delay(200);
    }
}

void runDemos()
{
    demoAllBreathe();
    delay(300);
    demoRainbow();
    delay(300);
    demoStaggeredPulse();
    delay(300);
    demoChaseLight();
    delay(300);
    demoColorWave();
    delay(300);
}

// ============================================================================

void setupWiFi()
{
    WiFi.mode(WIFI_STA);

    webServer = new AsyncWebServer(SERVER_PORT);
    // Ensure the DNS server is started on the standard DNS port 53
    // pointing all traffic to the ESP8266 AP IP (192.168.4.1)
    wifiManager = new AsyncWiFiManager(webServer, &dns);
    
    webServer->begin();
    
    messageServer = new MessageServer(webServer);
    messageHandler = new MessageHandler(messageServer, panelsController);
    appServer = new AppServer(webServer);
    
    setupMDNS();
    
    wifiManager->setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
    // This will block for 30 seconds if it can't connect.
    // If you want it non-blocking, you'd need to use startConfigPortal() instead.
    if (!wifiManager->autoConnect("Lightnet-Controller")) {
        Serial.println("Failed to connect and hit timeout");
    }
}

void setup()
{
    Serial.begin(57600);

    LNPanelsInitializer.configure({.sdaPinNo = IIC_SDA_PIN,
                                   .sclPinNo = IIC_SCL_PIN,
                                   .edgePinNo = INITIALIZER_EDGE_PIN_NO,
                                   .intPinNo = INITIALIZER_EDGE_INTERRUPT_PIN_NO});
    LNPanelsInitializer.start();

    pinMode(LED_PIN, OUTPUT);
    // digitalWrite(LED_PIN, LOW);

    pinMode(PANELS_POWER_PIN, OUTPUT);
    PRINTLN("reseting panels power...");
    digitalWrite(PANELS_POWER_PIN, LOW);
    delay(100);
    digitalWrite(PANELS_POWER_PIN, HIGH);
    PRINTLN("waiting for panels to boot");
    delay(300);
    PRINTLN("Initializing...");

    panelsController = new PanelsController();

    // not needed if panels power controll work
    // will send reset command to N devices to reset them if they are running
    //panelsController->resetDevices(50);
    // panels have 100ms delay on startup, we need to wait for them to initialize
    // additional time is needed if they were reset by command above (up to 100ms)
    // delay(300);

    digitalWrite(LED_PIN, HIGH);
}

void loop()
{
    LNPanelsInitializer.boot();

    if (LNPanelsInitializer.isReady()) {
        digitalWrite(LED_PIN, LOW);

        // IMPORTANT: WiFiManager needs the DNS server to process requests 
        // to trigger the Captive Portal redirect.
        if (wifiManager != nullptr) {
            dns.processNextRequest();
        }

        switch (state) {
            case 0:
                delay(500);

                state = 1;

                sendConfiguration();
                selfTest();

                // Collect up to 3 discovered panel addresses for the demo loop.
                {
                    uint16_t count = LNPanelsInitializer.getPanels()->getSize();
                    DEMO_PANELS = (uint8_t)((count > 3) ? 3 : count);
                    for (uint8_t i = 0; i < DEMO_PANELS; i++) {
                        demoPanelAddrs[i] = LNPanelsInitializer.getPanels()->get(i)->index;
                    }
                }
                animScheduler = new Lightnet::AnimationScheduler();
                animScheduler->initialize();

                setupWiFi();
                break;

            case 1:
                #ifdef ARDUINO_ARCH_ESP8266
                MDNS.update();
                #endif
                messageHandler->handleIncommingMessages();
                if (DEMO_PANELS > 0) {
                    runDemos();
                }
                break;
        }
    }
}

#endif