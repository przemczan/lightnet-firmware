#pragma once

#include "../../Common/Protocol.hpp"
#include "../../Utils/List.hpp"
#include "Panel.hpp"

// Real (non-SIM) discovery runs over the relay trunk — see PanelsInitializer.cpp. The SIM_MODE
// body (Sim/PanelsInitializerSim.cpp) fabricates a random tree directly and never touches these
// types at all, so they're excluded from that build entirely.
#ifndef SIM_MODE
    #include "../Relay/ControllerEdgeTransport.hpp"
    #include "../Relay/ControllerDiscoveryService.hpp"
#endif

class PanelsInitializer
{
    typedef struct {
        uint8_t  trunkRxPin;
        uint8_t  trunkTxPin;
        uint8_t  trunkOutputEnablePin;
        uint32_t trunkBaud;
    } configuration_t;

    public:
        PanelsInitializer();
        ~PanelsInitializer();
        void start();
        bool isFinished();

        List<Panel *> *getPanels();

        Panel *getPanelByIndex(Lightnet::PanelIndex panelIndex);
        void configure(configuration_t config);
        void boot();

    private:
        List<Panel *> *panels;
        configuration_t config;

        #ifndef SIM_MODE
            ControllerDiscoveryService discoveryService;  // wraps the shared LNTrunkTransport
            bool treeConverted;

            void convertDiscoveredTreeToPanels();
        #endif
};

extern PanelsInitializer LNPanelsInitializer;
