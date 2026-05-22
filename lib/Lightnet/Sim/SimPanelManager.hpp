#pragma once
#ifdef SIM_MODE

#include "SimPanel.hpp"

#ifndef SIM_PANELS_COUNT
#define SIM_PANELS_COUNT 4
#endif

class SimPanelManager {
public:
    SimPanelManager();

    void dispatch(uint8_t address, const void* data, uint8_t size);
    void dispatchAll(const void* data, uint8_t size);
    void tick();

private:
    SimPanel panels[SIM_PANELS_COUNT];
};

extern SimPanelManager SimPanels;

#endif  // SIM_MODE
