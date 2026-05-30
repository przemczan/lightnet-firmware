#ifdef LIGHTNET_TARGET_CONTROLLER

#include "main.h"
#if DEMO_ENABLED
    #include "demo.hpp"
#endif

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
WebsocketServer *websocketServer;
WebsocketHandler *websocketHandler;
AppServer *appServer;
Lightnet::AnimationScheduler *animScheduler    = nullptr;
Lightnet::PaletteStore *paletteStore     = nullptr;
Lightnet::AppearanceStore *appearance       = nullptr;
Lightnet::SceneStore *sceneStore       = nullptr;
Lightnet::ScenePlayer *scenePlayer      = nullptr;
Lightnet::AnimationService *animService      = nullptr;
Lightnet::AppearanceServer *appearanceServer = nullptr;
Lightnet::PaletteServer *paletteServer    = nullptr;
Lightnet::SceneServer *sceneServer      = nullptr;
Lightnet::AnimationServer *animServer       = nullptr;
Lightnet::PanelServer *panelServer      = nullptr;
TwibootClient *twibootClient    = nullptr;
PanelFlasher *panelFlasher     = nullptr;
FirmwareUpdateServer *fwUpdateServer   = nullptr;
SerialFirmwareReceiver *serialFwReceiver = nullptr;

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

// Send an ANIM_FADE packet to one panel then fire a General Call START.
// Each call uses a unique groupId so the START only triggers this panel.
static void sendPanelFade(
    uint8_t  addr,
    uint16_t durationMs,
    uint8_t  brightFrom,
    uint8_t  brightTo,
    uint8_t  groupId,
    uint8_t  seqId
)
{
    Protocol::PacketAnimationPrepare prep;

    prep.animType       = Lightnet::ANIM_FADE;
    prep.group_id       = groupId;
    prep.flags          = 0;
    prep.transitionMs   = 0;
    prep.durationMs     = durationMs;
    prep.colorFrom      = Lightnet::ColorRef_rgb(255, 255, 255);
    prep.colorTo        = Lightnet::ColorRef_rgb(255, 255, 255);  // ANIM_FADE holds colorTo
    prep.brightnessFrom = brightFrom;
    prep.brightnessTo   = brightTo;
    prep.param1         = 0;
    prep.param2         = 0;
    LNBus.sendPacketAck(addr, &prep, sizeof(prep), Protocol::PACKET_ANIMATION_PREPARE);

    delayMicroseconds(300);

    Protocol::PacketAnimationStart start;

    start.seq_id   = seqId;
    start.group_id = groupId;

    for (uint8_t retry = 0; retry < 2; retry++) {
        LNBus.sendPacketNack(0x00, &start, sizeof(start), Protocol::PACKET_ANIMATION_START);
        delayMicroseconds(200);
    }
}

void selfTest()
{
    PRINTLN("[SELF TEST BEGIN]");

    uint16_t panelCount = LNPanelsInitializer.getPanels()->getSize();

    if (panelCount == 0) {
        PRINTLN("[SELF TEST END]");

        return;
    }

    // Divide 1 s evenly; clamp so each in-panel animation has enough resolution.
    uint16_t stepMs = 1000 / panelCount;

    if (stepMs < 10) stepMs = 10;

    uint8_t groupId = 1;
    uint8_t seqId   = 1;

    // Fade in: forward order — each panel fades 0→255 over stepMs
    for (uint16_t i = 0; i < panelCount; i++) {
        uint8_t addr = LNPanelsInitializer.getPanels()->get(i)->index;

        panelsController->setBrightness(addr, 0);
        panelsController->turnOn(addr);
        sendPanelFade(addr, stepMs, 0, 255, groupId++, seqId++);

        if (seqId == 0) seqId = 1;

        delay(stepMs);
    }

    // Fade out: reverse order — each panel fades 255→0 over stepMs
    for (uint16_t i = panelCount; i-- > 0; ) {
        uint8_t addr = LNPanelsInitializer.getPanels()->get(i)->index;

        sendPanelFade(addr, stepMs, 255, 0, groupId++, seqId++);

        if (seqId == 0) seqId = 1;

        delay(stepMs);
    }

    // Restore panels to off state ready for normal use
    for (uint16_t i = 0; i < panelCount; i++) {
        uint8_t addr = LNPanelsInitializer.getPanels()->get(i)->index;

        panelsController->turnOff(addr);
        panelsController->setBrightness(addr, 128);
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
            { .useGammaCorrection = true, .colorTemperature = Halogen, .colorCorrection = TypicalLEDStrip }
            // { .useGammaCorrection = false, .colorTemperature = UncorrectedTemperature , .colorCorrection = UncorrectedColor}

        );
    }
}

void setupOTA()
{
    // Reuse the same hostname already registered with MDNS
    char buffer[20];

    #ifdef ARDUINO_ARCH_ESP8266
    sprintf(buffer, "lightnet-%04X", ESP.getChipId());
    #else
    sprintf(buffer, "lightnet-%08X", (uint32_t)ESP.getEfuseMac());
    #endif

    ArduinoOTA.setHostname(buffer);
    ArduinoOTA.onStart([]() {
        PRINTLN("[OTA] controller update starting");
    });
    ArduinoOTA.onEnd([]() {
        PRINTLN("[OTA] controller update done — rebooting");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        PRINTF("[OTA] error %u\n", error);
    });
    ArduinoOTA.begin();
    PRINTLN("[OTA] ArduinoOTA ready");
}

void setupWiFi()
{
    WiFi.mode(WIFI_STA);

    webServer = new AsyncWebServer(SERVER_PORT);
    // Ensure the DNS server is started on the standard DNS port 53
    // pointing all traffic to the ESP8266 AP IP (192.168.4.1)
    wifiManager = new AsyncWiFiManager(webServer, &dns);

    webServer->begin();

    websocketServer = new WebsocketServer(webServer);
    websocketHandler = new WebsocketHandler(websocketServer, panelsController, animScheduler);
    appServer = new AppServer(webServer);

    setupMDNS();

    wifiManager->setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);

    // This will block for 30 seconds if it can't connect.
    // If you want it non-blocking, you'd need to use startConfigPortal() instead.
    if (!wifiManager->autoConnect("Lightnet-Controller")) {
        Serial.println("Failed to connect and hit timeout");
    }

    setupOTA();

    twibootClient    = new TwibootClient();
    panelFlasher     = new PanelFlasher(panelsController, &LNPanelsInitializer, twibootClient);
    fwUpdateServer   = new FirmwareUpdateServer(webServer, panelFlasher);
    serialFwReceiver = new SerialFirmwareReceiver(panelFlasher);
}

void setup()
{
    #ifdef SIM_SERIAL_BAUD
    Serial.begin(SIM_SERIAL_BAUD);
    #else
    Serial.begin(57600);
    #endif

    LNPanelsInitializer.configure({ .sdaPinNo = IIC_SDA_PIN,
                                    .sclPinNo = IIC_SCL_PIN,
                                    .edgePinNo = INITIALIZER_EDGE_PIN_NO,
                                    .intPinNo = INITIALIZER_EDGE_INTERRUPT_PIN_NO });
    LNPanelsInitializer.start();

    pinMode(LED_PIN, OUTPUT);
    // digitalWrite(LED_PIN, LOW);

    pinMode(PANELS_POWER_PIN, OUTPUT);
    PRINTLN("reseting panels power...");
    digitalWrite(PANELS_POWER_PIN, LOW);
    delay(100);
    digitalWrite(PANELS_POWER_PIN, HIGH);
    PRINTLN("waiting for panels to boot");
    delay(500);
    PRINTLN("Initializing...");

    panelsController = new PanelsController();

    // not needed if panels power controll work
    // will send reset command to N devices to reset them if they are running
    // panelsController->resetDevices(50);
    // panels have 100ms delay on startup, we need to wait for them to initialize
    // additional time is needed if they were reset by command above (up to 100ms)
    // delay(300);

    digitalWrite(LED_PIN, HIGH);
}

void loop()
{
    LNPanelsInitializer.boot();

    if (LNPanelsInitializer.isFinished()) {
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

                animScheduler = new Lightnet::AnimationScheduler();
                animScheduler->initialize();

                // SPIFFS used to be mounted inside AppServer (in setupWiFi).
                // Hoist here so PaletteStore/AppearanceStore can read /config/ and
                // /palettes/ before the WiFi captive portal can block for 30 s.
                #ifdef ARDUINO_ARCH_ESP32
                SPIFFS.begin(true);
                #else
                SPIFFS.begin();
                #endif

                paletteStore = new Lightnet::PaletteStore();
                appearance   = new Lightnet::AppearanceStore(*animScheduler, *paletteStore);
                appearance->loadAndApply();   // broadcasts brightness, base colors, palette to panels

                sceneStore  = new Lightnet::SceneStore();
                scenePlayer = new Lightnet::ScenePlayer(*animScheduler, LNPanelsInitializer, *paletteStore);
                animService = new Lightnet::AnimationService(*sceneStore, *scenePlayer);

                #if DEMO_ENABLED
                initDemos(*animService, *sceneStore, *scenePlayer, *animScheduler,
                          *panelsController, LNPanelsInitializer);
                #endif

                setupWiFi();

                appearanceServer = new Lightnet::AppearanceServer(*webServer, *appearance, *paletteStore);
                appearanceServer->begin();
                paletteServer = new Lightnet::PaletteServer(*webServer, *paletteStore, *appearance);
                paletteServer->begin();
                sceneServer = new Lightnet::SceneServer(*webServer, *scenePlayer, *animService);
                sceneServer->begin();
                animServer = new Lightnet::AnimationServer(*webServer, *animService, *animScheduler, *appearance);
                animServer->begin();
                panelServer = new Lightnet::PanelServer(*webServer, *panelsController);
                panelServer->begin();
                break;

            case 1:
                #ifdef ARDUINO_ARCH_ESP8266
                MDNS.update();
                #endif
                ArduinoOTA.handle();

                if (serialFwReceiver) serialFwReceiver->run();

                if (panelFlasher) panelFlasher->run();

                if (!panelFlasher || !panelFlasher->isActive()) {
                    websocketHandler->handleIncommingMessages();

                    if (animScheduler) animScheduler->tick(millis());

                    if (scenePlayer)   scenePlayer->tick(millis());

                    #ifdef SIM_MODE
                    {
                        static uint32_t lastSimTick = 0;
                        uint32_t now = millis();
                        if ((uint32_t)(now - lastSimTick) >= 16) {
                            lastSimTick = now;
                            SimPanels.tick();
                        }
                    }
                    #endif
                    #if DEMO_ENABLED
                    runDemos();
                    #endif
                }

                break;
        }
    }
}

#endif
