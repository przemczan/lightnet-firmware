#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER
#ifdef DEMO_ENABLED

extern uint8_t DEMO_PANELS;
extern uint8_t demoPanelAddrs[30];

// Declared in main.cpp — polls ArduinoOTA and SerialFirmwareReceiver for ms
// milliseconds so the main loop stays responsive during demo animation delays.
void backgroundTick(uint32_t ms);

void runDemos();

#endif // DEMO_ENABLED
#endif // LIGHTNET_TARGET_CONTROLLER
