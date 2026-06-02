#ifdef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

uint8_t state = 0;
DNSServer dns;
PanelsController *panelsController;
AsyncWebServer *webServer;
AsyncWiFiManager *wifiManager;
WebsocketServer *websocketServer;
WebsocketHandler *websocketHandler;
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
Lightnet::ConfigurationStore *configStore    = nullptr;
Lightnet::AppStateStore *appStateStore       = nullptr;
Lightnet::ConfigurationServer *configServer  = nullptr;
Lightnet::StateServer *stateServer           = nullptr;
TwibootClient *twibootClient    = nullptr;
PanelFlasher *panelFlasher     = nullptr;
FirmwareUpdateServer *fwUpdateServer   = nullptr;
SerialFirmwareReceiver *serialFwReceiver = nullptr;

// Always-on (not gated by DEBUG) so rare production resets can be diagnosed
// from the serial log: the reset reason is printed once at every boot.
void logBootDiagnostics()
{
    Serial.println();
    Serial.print("[BOOT] reset reason: ");
    #ifdef ARDUINO_ARCH_ESP8266
        Serial.println(ESP.getResetReason());
        Serial.print("[BOOT] reset info: ");
        Serial.println(ESP.getResetInfo());
        Serial.print("[BOOT] free heap / frag% / maxBlock: ");
        Serial.print(ESP.getFreeHeap());
        Serial.print(" / ");
        Serial.print(ESP.getHeapFragmentation());
        Serial.print(" / ");
        Serial.println(ESP.getMaxFreeBlockSize());
    #else
        Serial.println((int)esp_reset_reason());   // see esp_reset_reason_t enum
        Serial.print("[BOOT] free heap / minFree / maxAlloc: ");
        Serial.print(ESP.getFreeHeap());
        Serial.print(" / ");
        Serial.print(ESP.getMinFreeHeap());
        Serial.print(" / ");
        Serial.println(ESP.getMaxAllocHeap());
    #endif
}

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

void selfTest()
{
    DEBUG_IF(DEBUG_INIT, D_PRINTLN("[SELF TEST BEGIN]"));

    uint8_t panelCount = (uint8_t)LNPanelsInitializer.getPanels()->getSize();

    if (panelCount == 0) {
        DEBUG_IF(DEBUG_INIT, D_PRINTLN("[SELF TEST END]"));

        return;
    }

    uint8_t addrs[Lightnet::LIGHTNET_MAX_PANELS];

    Protocol::Color black;

    black.rgb = { 0, 0, 0 };

    for (uint8_t i = 0; i < panelCount; i++) {
        addrs[i] = LNPanelsInitializer.getPanels()->get(i)->index;
        panelsController->setColor(addrs[i], black);
        panelsController->turnOn(addrs[i]);
    }

    Lightnet::WaveRunner wave(1, addrs, panelCount, 1000, 3, { 255, 255, 255 });

    while (!wave.isFinished()) {
        wave.tick(millis());
        delay(8);
    }

    for (uint8_t i = 0; i < panelCount; i++) {
        panelsController->turnOff(addrs[i]);
    }

    DEBUG_IF(DEBUG_INIT, D_PRINTLN("[SELF TEST END]"));
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
        DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[OTA] controller update starting"));
    });
    ArduinoOTA.onEnd([]() {
        DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[OTA] controller update done — rebooting"));

        if (appearance)    appearance->flush();

        if (configStore)   configStore->flush();

        if (appStateStore) appStateStore->flush();
    });
    ArduinoOTA.onError([](ota_error_t error) {
        DEBUG_IF(DEBUG_FLASHER, D_PRINTF("[OTA] error %u\n", error));
    });
    ArduinoOTA.begin();
    DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[OTA] ArduinoOTA ready"));
}

void setupWiFi()
{
    WiFi.mode(WIFI_STA);

    webServer = new AsyncWebServer(SERVER_PORT);
    DefaultHeaders::Instance().addHeader("Connection", "close");
    // Ensure the DNS server is started on the standard DNS port 53
    // pointing all traffic to the ESP8266 AP IP (192.168.4.1)
    wifiManager = new AsyncWiFiManager(webServer, &dns);

    webServer->begin();

    websocketServer = new WebsocketServer(webServer);
    websocketHandler = new WebsocketHandler(websocketServer, panelsController, animScheduler);

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

    logBootDiagnostics();

    LNPanelsInitializer.configure({ .sdaPinNo = IIC_SDA_PIN,
                                    .sclPinNo = IIC_SCL_PIN,
                                    .edgePinNo = INITIALIZER_EDGE_PIN_NO,
                                    .intPinNo = INITIALIZER_EDGE_INTERRUPT_PIN_NO });
    LNPanelsInitializer.start();

    pinMode(LED_PIN, OUTPUT);
    // digitalWrite(LED_PIN, LOW);

    pinMode(PANELS_POWER_PIN, OUTPUT);
    DEBUG_IF(DEBUG_INIT, D_PRINTLN("reseting panels power..."));
    digitalWrite(PANELS_POWER_PIN, LOW);
    delay(100);
    digitalWrite(PANELS_POWER_PIN, HIGH);
    DEBUG_IF(DEBUG_INIT, D_PRINTLN("waiting for panels to boot"));
    delay(500);
    DEBUG_IF(DEBUG_INIT, D_PRINTLN("Initializing..."));

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

                configStore = new Lightnet::ConfigurationStore();
                configStore->load();

                appStateStore = new Lightnet::AppStateStore();
                appStateStore->load();

                {
                    bool initialIsOn = true;

                    switch (configStore->powerStateOnBoot()) {
                        case Lightnet::POWER_ALWAYS_OFF: initialIsOn = false;
                            break;
                        case Lightnet::POWER_LAST_STATE: initialIsOn = appStateStore->isOn();
                            break;
                        default:                         initialIsOn = true;
                            break;
                    }

                    appStateStore->setIsOn(initialIsOn);
                }

                #if DEMO_MODE
                    initDemos(*animService, *sceneStore, *scenePlayer, *animScheduler,
                              *panelsController, LNPanelsInitializer);
                #endif

                setupWiFi();

                appearanceServer = new Lightnet::AppearanceServer(*webServer, *appearance, *paletteStore);
                appearanceServer->begin();
                paletteServer = new Lightnet::PaletteServer(*webServer, *paletteStore, *appearance);
                paletteServer->begin();
                sceneServer = new Lightnet::SceneServer(*webServer, *scenePlayer, *animService, *appStateStore);
                sceneServer->begin();
                animServer = new Lightnet::AnimationServer(*webServer, *animService, *animScheduler, *appearance, *appStateStore);
                animServer->begin();
                panelServer = new Lightnet::PanelServer(*webServer, *panelsController);
                panelServer->begin();
                configServer = new Lightnet::ConfigurationServer(*webServer, *configStore);
                configServer->begin();
                stateServer = new Lightnet::StateServer(*webServer,
                                                        *appStateStore,
                                                        *panelsController,
                                                        *animService,
                                                        *animScheduler,
                                                        *appearance);
                stateServer->begin();

                DEBUG_IF(DEBUG_INIT, D_PRINTLN("Initialization complete"));
                break;

            case 1:
                #ifdef ARDUINO_ARCH_ESP8266
                    MDNS.update();
                #endif
                ArduinoOTA.handle();

                if (serialFwReceiver) serialFwReceiver->run();

                if (panelFlasher) panelFlasher->run();

                websocketServer->cleanup();

                DEBUG_BLOCK({
                // Track heap over time to catch fragmentation-driven resets.
                static uint32_t lastHeapLogMs = 0;
                uint32_t now = millis();

                if ((uint32_t)(now - lastHeapLogMs) >= 10000) {
                    lastHeapLogMs = now;
                    Serial.print("[HEAP] free: ");
                    Serial.print(ESP.getFreeHeap());
                    #ifdef ARDUINO_ARCH_ESP8266
                        Serial.print(" frag%: ");
                        Serial.print(ESP.getHeapFragmentation());
                        Serial.print(" maxBlock: ");
                        Serial.print(ESP.getMaxFreeBlockSize());
                    #endif
                    Serial.println();
                }
            });

                if (!panelFlasher || !panelFlasher->isActive()) {
                    websocketHandler->handleIncommingMessages();

                    if (animScheduler && appStateStore->isOn()) animScheduler->tick(millis());

                    if (scenePlayer && appStateStore->isOn())   scenePlayer->tick(millis());

                    if (appearance)    appearance->tick(millis());

                    if (configStore)   configStore->tick(millis());

                    if (appStateStore) appStateStore->tick(millis());

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
                    #if DEMO_MODE
                        runDemos();
                    #endif
                }

                break;
        }
    }
}

#endif
