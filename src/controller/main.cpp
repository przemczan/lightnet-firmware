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
// Hold only references (to the global LNBus / LNPanelsInitializer / LNTrunkTransport), so
// static-init order across TUs is irrelevant — they're not dereferenced until runtime.
//
// activeSink is picked at compile time, not runtime: under SIM_MODE, sim panels only ever
// respond to LightnetBus-routed commands (LightnetBusSim.cpp -> SimPanelManager), so the scene
// engine and PanelsController must keep using ControllerPacketSink/LNBus there. Every command
// (color, on/off, configuration, fetchState, OTA) goes over the relay trunk via
// ControllerRelayPacketSink (see PanelsController.hpp / ControllerRelayPacketSink.hpp).
// LNBus only still exists as the SIM_MODE transport.
#ifdef SIM_MODE
    Lightnet::ControllerPacketSink activeSink(LNBus);

#else
    Lightnet::ControllerRelayPacketSink activeSink(LNTrunkTransport);

#endif
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

// OTA only exists on real hardware
#ifndef SIM_MODE
    RelayBootloaderClient *relayBootloaderClient = nullptr;
    PanelFlasher *panelFlasher     = nullptr;
    FirmwareUpdateServer *fwUpdateServer   = nullptr;
    SerialFirmwareReceiver *serialFwReceiver = nullptr;

#endif

#ifdef LIGHTNET_MQTT
    Lightnet::MqttConfigStore *mqttConfigStore = nullptr;
    Lightnet::MqttService *mqttService     = nullptr;
    Lightnet::MqttServer *mqttServer      = nullptr;

#endif

// Task-watchdog culprit capture. The TWDT panic backtrace goes to the ROM console (UART0),
// which the S2 Mini doesn't even break out — over USB CDC a watchdog reset looks like a silent
// reboot. This hook (a weak symbol in esp_system, called from the TWDT ISR before the panic)
// snapshots the name of the task that was running — after 5 s of continuous starvation, that IS
// the task hogging the core — into RTC noinit RAM, which survives everything short of a power
// cycle, so logBootDiagnostics() can print it on the next boot.
RTC_NOINIT_ATTR static char wdtHogTaskName[configMAX_TASK_NAME_LEN];
RTC_NOINIT_ATTR static uint32_t wdtHogMarker;
static const uint32_t WDT_HOG_MARKER_VALID = 0x57444748;  // "WDGH"

extern "C" void esp_task_wdt_isr_user_handler(void)
{
    const char *name = pcTaskGetName(NULL);

    for (size_t i = 0; i < sizeof(wdtHogTaskName) - 1; i++) {
        wdtHogTaskName[i] = name[i];

        if (name[i] == '\0') break;
    }

    wdtHogTaskName[sizeof(wdtHogTaskName) - 1] = '\0';
    wdtHogMarker = WDT_HOG_MARKER_VALID;
}

static const char *resetReasonName(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

// Always-on (not gated by DEBUG) so rare production resets can be diagnosed
// from the serial log: the reset reason is printed once at every boot.
void logBootDiagnostics()
{
    esp_reset_reason_t reason = esp_reset_reason();

    Serial.println();
    Serial.print("[BOOT] reset reason: ");
    Serial.print((int)reason);
    Serial.print(" (");
    Serial.print(resetReasonName(reason));
    Serial.println(")");

    if (wdtHogMarker == WDT_HOG_MARKER_VALID) {
        Serial.print("[BOOT] TWDT hog task (ISR snapshot): ");
        Serial.println(wdtHogTaskName);
        wdtHogMarker = 0;
    }

    Serial.print("[BOOT] free heap / minFree / maxAlloc: ");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" / ");
    Serial.print(ESP.getMinFreeHeap());
    Serial.print(" / ");
    Serial.println(ESP.getMaxAllocHeap());
}

void setupMDNS()
{
    char buffer[20];

    sprintf(buffer, "lightnet-%08X", (uint32_t)ESP.getEfuseMac());

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

    static const uint8_t SELF_TEST_GROUP = 1;

    Lightnet::emitLinearSweep(
        *animScheduler,
        SELF_TEST_GROUP,
        addrs,
        panelCount,
        /*durationMs=*/ 1000, /*width=*/
        3,                                  /*rippleOriginIndex=*/
        0,
        Protocol::ColorRGB{ 255, 255, 255 },
        Lightnet::LinearSweepKind::Wave
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

    sprintf(buffer, "lightnet-%08X", (uint32_t)ESP.getEfuseMac());

    ArduinoOTA.setHostname(buffer);
    ArduinoOTA.onStart(
        []() {
        DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[OTA] controller update starting"));
    }
    );
    ArduinoOTA.onEnd(
        []() {
        DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[OTA] controller update done — rebooting"));

        if (appearance)    appearance->flush();

        if (configStore)   configStore->flush();

        if (appStateStore) appStateStore->flush();
    }
    );
    ArduinoOTA.onError(
        [](ota_error_t error) {
        DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[OTA] error %u", error));
    }
    );
    ArduinoOTA.begin();
    DEBUG_IF(DEBUG_FLASHER, D_PRINTLN("[OTA] ArduinoOTA ready"));
}

void setupWiFi()
{
    WiFi.mode(WIFI_STA);

    webServer = new AsyncWebServer(SERVER_PORT);
    DefaultHeaders::Instance().addHeader("Connection", "close");
    // Ensure the DNS server is started on the standard DNS port 53
    // pointing all traffic to the AP IP (192.168.4.1)
    wifiManager = new AsyncWiFiManager(webServer, &dns);

    webServer->begin();

    websocketServer = new WebsocketServer(webServer);
    websocketHandler = new WebsocketHandler(websocketServer, panelsController, animScheduler);

    // Deferred-execution queue: HTTP handlers post their packet-emitting work here so it
    // runs on the main loop (drained in case 1), keeping all capture() calls single-task.
    mainLoopQueue = new Lightnet::MainLoopQueue();

    // Mirror outbound animation/color packets to WebSocket clients for live preview. Hooked onto
    // whichever transport actually carries traffic (see activeSink's own comment) — the relay
    // sink on real hardware, LNBus under SIM_MODE.
    packetMirror = new PacketMirror();
    packetMirror->setServer(websocketServer);  // enables flush-on-overflow in capture()
    #ifdef SIM_MODE
        LNBus.setOnPacketSent(mirrorOutboundPacket);
    #else
        activeSink.setOnPacketSent(mirrorOutboundPacket);
    #endif

    wifiManager->setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);

    char apName[32];

    sprintf(apName, "Lightnet-Controller-%08X", (uint32_t)ESP.getEfuseMac());

    // This will block for 30 seconds if it can't connect.
    // If you want it non-blocking, you'd need to use startConfigPortal() instead.
    if (!wifiManager->autoConnect(apName)) {
        Serial.println("Failed to connect and hit timeout");
    }

    // mDNS must be started only once the station has an IP — starting it before the
    // connection leaves the responder bound to no address, a likely cause of
    // intermittent `.local` failures.
    setupMDNS();

    setupOTA();

    #ifndef SIM_MODE
        relayBootloaderClient = new RelayBootloaderClient(activeSink);
        panelFlasher = new PanelFlasher(panelsController, &LNPanelsInitializer, relayBootloaderClient);
        fwUpdateServer   = new FirmwareUpdateServer(webServer, panelFlasher);
        serialFwReceiver = new SerialFirmwareReceiver(panelFlasher);
    #endif
}

void setup()
{
    Serial.setRxBufferSize(1024);
    #ifdef SIM_SERIAL_BAUD
        Serial.begin(SIM_SERIAL_BAUD);
    #else
        Serial.begin(57600);
    #endif

    Serial.setDebugOutput(true);

    #if ARDUINO_USB_CDC_ON_BOOT
        // With a host terminal attached, each CDC write blocks up to the TX timeout waiting
        // for the host to drain the endpoint, stalling the main loop and breaking relay UART
        // timing (with no host attached, writes drop immediately). Zero timeout makes debug
        // output lossy-but-nonblocking in both cases.
        Serial.setTxTimeoutMs(0);

        // Native USB CDC drops all writes until the host asserts DTR, and the app starts
        // running as soon as flashing finishes, independent of whether a monitor has
        // reattached to the new port yet. Block briefly on DTR so the earliest boot
        // diagnostics aren't lost to that race; give up after the timeout so a unit with no
        // monitor attached still boots normally. availableForWrite() is the DTR proxy --
        // operator bool additionally requires RTS, which the monitor deliberately never
        // asserts (see platformio.ini).
        unsigned long serialWaitStart = millis();

        while (Serial.availableForWrite() == 0 && millis() - serialWaitStart < 3000) {
            delay(10);
        }

    #else
        delay(250);
    #endif

    logBootDiagnostics();

    pinMode(LED_PIN, OUTPUT);
    // digitalWrite(LED_PIN, LOW);

    // Panels must be powered (and their relay UART listening) before the discovery probe below
    // is sent -- it's a one-shot send with no retry, so firing it before any panel can hear it
    // means discovery times out empty even with a panel attached.
    pinMode(PANELS_POWER_PIN, OUTPUT);
    digitalWrite(PANELS_POWER_PIN, LOW);
    delay(150);
    digitalWrite(PANELS_POWER_PIN, HIGH);
    DEBUG_IF(DEBUG_INIT, D_PRINTLN("waiting for panels to boot"));
    delay(500);
    DEBUG_IF(DEBUG_INIT, D_PRINTLN("Initializing..."));

    LNPanelsInitializer.configure(
        // Baud comes from src/controller.config.hpp -- see Panel/LightnetPanel.cpp's own
        // EdgeUartTransport::begin() comment for why the default is lower than the hardware
        // redesign plan's original 1Mbps assumption.
        { .trunkRxPin = CONTROLLER_TRUNK_RX_PIN,
          .trunkTxPin = CONTROLLER_TRUNK_TX_PIN,
          .trunkOutputEnablePin = CONTROLLER_TRUNK_OE_PIN,
          .trunkBaud = LIGHTNET_TRUNK_BAUD }
    );
    LNPanelsInitializer.start();

    #ifdef SIM_MODE
        panelsController = new PanelsController(activeSink);
    #else
        panelsController = new PanelsController(activeSink, activeSink);
    #endif

    digitalWrite(LED_PIN, HIGH);
}

void loop()
{
    LNPanelsInitializer.boot();

    if (!LNPanelsInitializer.isFinished()) {
        static unsigned long lastWaitingLog = 0;

        if (millis() - lastWaitingLog >= 2000) {
            DEBUG_IF(DEBUG_INIT, D_PRINTLN("waiting for panel discovery to complete..."));
            lastWaitingLog = millis();
        }
    }

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

                animScheduler = new Lightnet::AnimationScheduler(activeSink);
                animScheduler->initialize();

                selfTest();

                // Filesystem mounted before WiFi so PaletteStore/AppearanceStore
                // can read /data/palettes.db and /config/ before the captive portal blocks.
                Lightnet::Fs::begin();

                // Ensure /config exists before the stores below write into it. LittleFS won't
                // create a file whose parent directory is missing, so on a fresh filesystem every
                // /config/*.json write would fail without this. Idempotent.
                Lightnet::Fs::mkdir("/config");

                paletteStore = new Lightnet::PaletteRepository();
                paletteStore->ensureSeeded();
                appearance   = new Lightnet::AppearanceService(*animScheduler, *paletteStore);
                appearance->loadAndApply();

                sceneStore  = new Lightnet::SceneStore();
                sceneStore->compactIfFragmented();
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
                    initDemos(
                        *animService,
                        *sceneStore,
                        *scenePlayer,
                        *animScheduler,
                        *panelsController,
                        LNPanelsInitializer
                    );
                #endif

                setupWiFi();

                appearanceServer = new Lightnet::AppearanceServer(*webServer, *appearance, *paletteStore, *animService, *mainLoopQueue);
                appearanceServer->begin();
                paletteServer = new Lightnet::PaletteServer(*webServer, *paletteStore, *appearance);
                paletteServer->begin();
                sceneServer = new Lightnet::SceneServer(
                    *webServer,
                    *sceneStore,
                    *scenePlayer,
                    *animService,
                    *appStateStore,
                    *appearance,
                    *mainLoopQueue
                );
                sceneServer->begin();
                animServer = new Lightnet::AnimationServer(
                    *webServer,
                    *animService,
                    *animScheduler,
                    *appearance,
                    *appStateStore,
                    *mainLoopQueue
                );
                animServer->begin();
                panelServer = new Lightnet::PanelServer(*webServer, *panelsController, *mainLoopQueue);
                panelServer->begin();
                configServer = new Lightnet::ConfigurationServer(*webServer, *configStore, *topologyConfig, *scenePlayer, *mainLoopQueue);
                configServer->begin();
                stateServer = new Lightnet::StateServer(
                    *webServer,
                    *appStateStore,
                    *panelsController,
                    *animService,
                    *animScheduler,
                    *appearance,
                    *mainLoopQueue,
                    packetMirror
                );

                stateServer->begin();

                appStateBroadcaster = new Lightnet::AppStateBroadcaster(
                    *websocketServer,
                    *appStateStore,
                    *animService
                );

                #ifdef LIGHTNET_MQTT
                    mqttService = new Lightnet::MqttService(
                        *mqttConfigStore,
                        *appStateStore,
                        *appearance,
                        *animService,
                        *sceneStore,
                        *panelsController,
                        LNPanelsInitializer,
                        *animScheduler,
                        *mainLoopQueue,
                        packetMirror
                    );
                    mqttService->begin();
                    mqttServer = new Lightnet::MqttServer(*webServer, *mqttConfigStore, *mqttService);
                    mqttServer->begin();
                #endif

                DEBUG_IF(DEBUG_INIT, D_PRINTLN("Initialization complete"));
                break;

            case 1:
                ArduinoOTA.handle();

                #ifndef SIM_MODE

                    if (serialFwReceiver) serialFwReceiver->run();

                    if (panelFlasher) panelFlasher->run();

                #endif

                websocketServer->cleanup();

                DEBUG_BLOCK(
            {
                // Track heap over time to catch fragmentation-driven resets.
                static uint32_t lastHeapLogMs = 0;
                uint32_t now = millis();

                if ((uint32_t)(now - lastHeapLogMs) >= 1000) {
                    lastHeapLogMs = now;
                    Serial.print("[HEAP] free: ");
                    Serial.print(ESP.getFreeHeap());
                    Serial.println();
                }
            });

                #ifndef SIM_MODE

                    if (!panelFlasher || !panelFlasher->isActive()) {
                #endif
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
                #ifndef SIM_MODE
        }

                #endif

                break;
        }
    }
}

#endif
