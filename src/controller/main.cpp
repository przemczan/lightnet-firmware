#ifdef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

uint8_t state = 0;
DNSServer dns;
PanelsController *panelsController;
AsyncWebServer *webServer;
AsyncWiFiManager *wifiManager;
WebsocketServer *websocketServer;
WebsocketHandler *websocketHandler;
PacketMirror *packetMirror = nullptr;
Lightnet::AppStateBroadcaster *appStateBroadcaster = nullptr;
Lightnet::MainLoopQueue *mainLoopQueue = nullptr;

// LightnetBus::onPacketSent is a plain function pointer, so it can't capture.
// Forward captured packets to the (global) mirror once it exists.
static void mirrorOutboundPacket(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size)
{
    if (packetMirror) {
        packetMirror->capture(address, packet, size);
    }
}

// Flush mirrored packets to WS clients, capped at ~30fps so a full runner frame's per-panel
// updates coalesce into one frame. Exposed (MirrorService.hpp) so blocking sections such as the
// demos can keep the preview streaming while the main loop is busy.
void serviceMirror()
{
    if (!packetMirror || !websocketServer) {
        return;
    }

    // Send the animation state snapshot to any client that just enabled mirroring,
    // outside the 33 ms throttle so it fires on the very next main-loop tick.
    uint32_t newClient = websocketServer->getAndClearPendingSnapshotClientId();

    if (newClient) {
        packetMirror->flushSnapshotTo(websocketServer, newClient);
    }

    static uint32_t lastMirrorFlushMs = 0;
    uint32_t now = millis();

    if ((uint32_t)(now - lastMirrorFlushMs) >= 33) {
        lastMirrorFlushMs = now;
        packetMirror->flushTo(websocketServer);
    }
}

// Bus/topology seams between the shared scene engine and the controller hardware.
// Hold only references (to the global LNBus / LNPanelsInitializer), so static-init order
// across TUs is irrelevant — they're not dereferenced until runtime.
Lightnet::ControllerPacketSink controllerPacketSink(LNBus);
Lightnet::PanelsTopologyProvider panelsTopologyProvider(LNPanelsInitializer);

Lightnet::AnimationScheduler *animScheduler    = nullptr;
Lightnet::PaletteRepository *paletteStore = nullptr;
Lightnet::AppearanceService *appearance      = nullptr;
Lightnet::SceneStore *sceneStore       = nullptr;
Lightnet::ScenePlayer *scenePlayer      = nullptr;
Lightnet::ScenesService *animService      = nullptr;
Lightnet::AppearanceServer *appearanceServer = nullptr;
Lightnet::PaletteServer *paletteServer    = nullptr;
Lightnet::SceneServer *sceneServer      = nullptr;
Lightnet::AnimationServer *animServer       = nullptr;
Lightnet::PanelServer *panelServer      = nullptr;
Lightnet::ConfigurationStore *configStore    = nullptr;
Lightnet::AppStateStore *appStateStore       = nullptr;
Lightnet::ConfigurationServer *configServer  = nullptr;
Lightnet::StateServer *stateServer           = nullptr;
Lightnet::TopologyConfigStore *topologyConfig = nullptr;
TwibootClient *twibootClient    = nullptr;
PanelFlasher *panelFlasher     = nullptr;
FirmwareUpdateServer *fwUpdateServer   = nullptr;
SerialFirmwareReceiver *serialFwReceiver = nullptr;

#ifdef LIGHTNET_MQTT
    Lightnet::MqttConfigStore *mqttConfigStore = nullptr;
    Lightnet::MqttService *mqttService     = nullptr;
    Lightnet::MqttServer *mqttServer      = nullptr;

#endif

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

#ifdef ARDUINO_ARCH_ESP8266
    // Kept alive for the lifetime of the program so the callback stays registered.
    WiFiEventHandler mdnsGotIPHandler;
#endif

void setupMDNS()
{
    char buffer[20];

    #ifdef ARDUINO_ARCH_ESP8266
        sprintf(buffer, "lightnet-%04X", ESP.getChipId());
    #else
        sprintf(buffer, "lightnet-%08X", (uint32_t)ESP.getEfuseMac());
    #endif

    MDNS.begin(&buffer[0]);
    MDNS.addService("lightnet", "tcp", SERVER_PORT);

    #ifdef ARDUINO_ARCH_ESP8266
        // The ESP8266 mDNS responder silently stops answering after a STA
        // reconnect (it loses its 224.0.0.251 multicast/IGMP membership), so
        // `lightnet-XXXX.local` resolution dies while the device keeps running.
        // Re-announce every time the station re-acquires an IP. (ESP32's mDNS
        // task handles this itself, so this is not needed there.)
        mdnsGotIPHandler = WiFi.onStationModeGotIP(
            [](const WiFiEventStationModeGotIP &) {
        MDNS.notifyAPChange();
    });
    #endif
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

    static const uint8_t SELF_TEST_GROUP = 1;

    Lightnet::emitLinearSweep(
        *animScheduler, SELF_TEST_GROUP, addrs, panelCount,
        /*durationMs=*/ 1000, /*width=*/ 3, /*rippleOriginIndex=*/ 0,
        Protocol::ColorRGB{ 255, 255, 255 }, Lightnet::LinearSweepKind::Wave
    );

    delay(1050);

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
        DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[OTA] error %u", error));
    });
    ArduinoOTA.begin();
    DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[OTA] ArduinoOTA ready"));
}

#ifdef LIGHTNET_MQTT
    static void onMqttPortalSaved()
    {
        if (mqttService) mqttService->notifyConfigChanged();
    }

#endif

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

    // Deferred-execution queue: HTTP handlers post their packet-emitting work here so it
    // runs on the main loop (drained in case 1), keeping all capture() calls single-task.
    mainLoopQueue = new Lightnet::MainLoopQueue();

    // Mirror outbound animation/color packets to WebSocket clients for live preview.
    packetMirror = new PacketMirror();
    packetMirror->setServer(websocketServer);  // enables flush-on-overflow in capture()
    LNBus.setOnPacketSent(mirrorOutboundPacket);

    wifiManager->setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);

    #ifdef LIGHTNET_MQTT

        if (mqttConfigStore) {
            Lightnet::mqttPortalSetup(wifiManager, mqttConfigStore, onMqttPortalSaved);
        }

    #endif

    // This will block for 30 seconds if it can't connect.
    // If you want it non-blocking, you'd need to use startConfigPortal() instead.
    if (!wifiManager->autoConnect("Lightnet-Controller")) {
        Serial.println("Failed to connect and hit timeout");
    }

    // mDNS must be started only once the station has an IP. Starting it before
    // the connection (as it was before) leaves the ESP8266 responder bound to
    // no address, which is a likely cause of intermittent `.local` failures.
    setupMDNS();

    setupOTA();

    twibootClient    = new TwibootClient();
    panelFlasher     = new PanelFlasher(panelsController, &LNPanelsInitializer, twibootClient);
    fwUpdateServer   = new FirmwareUpdateServer(webServer, panelFlasher);
    serialFwReceiver = new SerialFirmwareReceiver(panelFlasher);
}

void setup()
{
    Serial.setRxBufferSize(1024);
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
        // Only poll when not connected: on ESP32 the socket is never opened if
        // WiFi connected directly (no portal), so calling parsePacket() on it
        // returns EBADF (errno 9) every iteration, flooding the log.
        if (wifiManager != nullptr && WiFi.status() != WL_CONNECTED) {
            dns.processNextRequest();
        }

        switch (state) {
            case 0:
                delay(500);

                state = 1;

                sendConfiguration();

                animScheduler = new Lightnet::AnimationScheduler(controllerPacketSink);
                animScheduler->initialize();

                selfTest();

                // Filesystem mounted before WiFi so PaletteStore/AppearanceStore
                // can read /data/palettes.db and /config/ before the captive portal blocks.
                Lightnet::Fs::begin();

                // Ensure /config exists before the stores below write into it. ESP32's LittleFS
                // (unlike ESP8266's) won't create a file whose parent directory is missing, so on
                // a fresh filesystem every /config/*.json write would fail without this. Idempotent.
                Lightnet::Fs::mkdir("/config");

                paletteStore = new Lightnet::PaletteRepository();
                paletteStore->ensureSeeded();
                appearance   = new Lightnet::AppearanceService(*animScheduler, *paletteStore);
                appearance->loadAndApply();

                sceneStore  = new Lightnet::SceneStore();
                scenePlayer = new Lightnet::ScenePlayer(*animScheduler, *paletteStore, panelsTopologyProvider);
                animService = new Lightnet::ScenesService(*sceneStore, *scenePlayer);

                // Per-device topology config: logical root used by scene selectors.
                topologyConfig = new Lightnet::TopologyConfigStore();
                topologyConfig->load();
                scenePlayer->setLogicalRoot(topologyConfig->logicalRoot(), millis());

                configStore = new Lightnet::ConfigurationStore();
                configStore->load();

                appStateStore = new Lightnet::AppStateStore();
                appStateStore->load();

                #ifdef LIGHTNET_MQTT
                    mqttConfigStore = new Lightnet::MqttConfigStore();
                    mqttConfigStore->load();
                #endif

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

                appearanceServer = new Lightnet::AppearanceServer(*webServer, *appearance, *paletteStore, *animService, *mainLoopQueue);
                appearanceServer->begin();
                paletteServer = new Lightnet::PaletteServer(*webServer, *paletteStore, *appearance);
                paletteServer->begin();
                sceneServer = new Lightnet::SceneServer(*webServer, *sceneStore, *scenePlayer, *animService, *appStateStore, *appearance,
                                                        *mainLoopQueue);
                sceneServer->begin();
                animServer = new Lightnet::AnimationServer(*webServer,
                                                           *animService,
                                                           *animScheduler,
                                                           *appearance,
                                                           *appStateStore,
                                                           *mainLoopQueue);
                animServer->begin();
                panelServer = new Lightnet::PanelServer(*webServer, *panelsController, *mainLoopQueue);
                panelServer->begin();
                configServer = new Lightnet::ConfigurationServer(*webServer, *configStore, *topologyConfig, *scenePlayer, *mainLoopQueue);
                configServer->begin();
                stateServer = new Lightnet::StateServer(*webServer,
                                                        *appStateStore,
                                                        *panelsController,
                                                        *animService,
                                                        *animScheduler,
                                                        *appearance,
                                                        *mainLoopQueue,
                                                        packetMirror);
                stateServer->begin();

                appStateBroadcaster = new Lightnet::AppStateBroadcaster(
                    *websocketServer, *appStateStore, *animService);

                #ifdef LIGHTNET_MQTT
                    mqttService = new Lightnet::MqttService(
                        *mqttConfigStore, *appStateStore, *appearance, *animService, *sceneStore,
                        *panelsController, LNPanelsInitializer, *animScheduler, *mainLoopQueue, packetMirror
                    );
                    mqttService->begin();
                    mqttServer = new Lightnet::MqttServer(*webServer, *mqttConfigStore, *mqttService);
                    mqttServer->begin();
                #endif

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

                if ((uint32_t)(now - lastHeapLogMs) >= 1000) {
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

                    // Run work deferred by HTTP handlers (scene play, power, appearance, …)
                    // on the main loop so all packet emission stays single-task.
                    if (mainLoopQueue) mainLoopQueue->drain();

                    if (scenePlayer && appStateStore->isOn())   scenePlayer->tick(millis());

                    if (appearance)    appearance->tick(millis());

                    if (configStore)   configStore->tick(millis());

                    if (appStateStore) appStateStore->tick(millis());

                    if (appStateBroadcaster) appStateBroadcaster->tick();

                    #ifdef LIGHTNET_MQTT

                        if (mqttService) mqttService->tick(millis());

                    #endif

                    serviceMirror();

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
