#pragma once

#include "PanelList.hpp"

class PanelsInitializer
{
    private:
        volatile uint8_t lastPacketType = 0;
        PanelList *panelList;
        volatile Panel lastPanel;

    public:
        void PanelsInitializer(PanelList *_panelList);
        void doInitialize();
}
