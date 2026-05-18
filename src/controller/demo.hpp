#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER
#ifdef DEMO_ENABLED

extern uint8_t DEMO_PANELS;
extern uint8_t demoPanelAddrs[30];

void runDemos();

#endif // DEMO_ENABLED
#endif // LIGHTNET_TARGET_CONTROLLER
