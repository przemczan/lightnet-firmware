#ifdef SIM_MODE

#include "SimPanelManager.hpp"

SimPanelManager::SimPanelManager()
{
    for (uint8_t i = 0; i < SIM_PANELS_COUNT; i++) {
        panels[i].setIndex(i + 1);  // panel indices 1..N
    }
}

void SimPanelManager::dispatch(uint8_t address, const void *data, uint8_t size)
{
    for (uint8_t i = 0; i < SIM_PANELS_COUNT; i++) {
        if (panels[i].getIndex() == address) {
            panels[i].handlePacket(data, size);

            return;
        }
    }
}

void SimPanelManager::dispatchAll(const void *data, uint8_t size)
{
    for (uint8_t i = 0; i < SIM_PANELS_COUNT; i++) {
        panels[i].handlePacket(data, size);
    }
}

void SimPanelManager::tick()
{
    for (uint8_t i = 0; i < SIM_PANELS_COUNT; i++) {
        panels[i].tick();
    }
}

SimPanelManager SimPanels;

#endif  // SIM_MODE
