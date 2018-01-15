#include "PanelsInitializer.hpp"

volatile uint8_t PanelsInitializer::lastPacketType;
List<PanelsInitializer::Panel *> PanelsInitializer::panels;
volatile PanelsInitializer::Panel PanelsInitializer::lastPanel;

void PanelsInitializer::start(uint8_t edgePinNo)
{
    memset((void *)&PanelsInitializer::lastPanel, 0, sizeof(PanelsInitializer::lastPanel));
    PanelsInitializer::lastPacketType = 0;

    LNBus.begin(Protocol::CONTROLLER_ADDRESS);
    LNBus.setOnPacketRequested((LightnetBus::onPacketRequested_t)&PanelsInitializer::onPacketRequested);
    LNBus.setOnPacketReceived((LightnetBus::onPacketReceived_t)&PanelsInitializer::onPacketReceived);

    PanelsInitializer::edge = new LightnetPanelEdge(edgePinNo);
}

void PanelsInitializer::doInitialize()
{
    this->edge->boot();
}

void PanelsInitializer::updateEdgeState()
{
    this->edge->readBusState();
}

void PanelsInitializer::onPacketReceived(Protocol::PacketMeta *packetMeta)
{
    PanelsInitializer::lastPacketType = packetMeta->header.type;

    switch (packetMeta->header.type)
    {
        case Protocol::PACKET_REGISTER_PANEL:
            Protocol::RegisterPanel *packet = (Protocol::RegisterPanel *)packetMeta;

            lastPanel.id++;
            lastPanel.edgesNumber = packet->edgesNumber;
            lastPanel.parentEdge = packet->parentEdge;

            Panel *panel = new Panel();
            memcpy(panel, (void *)&lastPanel, sizeof(panel));
            panels.push(panel);

            PRINTLN4("New panel [id,parent,edges]:", panel->id, panel->parentEdge, panel->edgesNumber);
            break;
    }
}

void PanelsInitializer::onPacketRequested()
{
    switch (PanelsInitializer::lastPacketType)
    {
        case Protocol::PACKET_REGISTER_PANEL:
            if (PanelsInitializer::lastPanel.id) {
                LNBus.respondToRegisterPanel(PanelsInitializer::lastPanel.id);
            }
            break;
    }
}

List<PanelsInitializer::Panel *> *PanelsInitializer::getPanels()
{
    return &PanelsInitializer::panels;
}

uint8_t PanelsInitializer::isReady()
{
    return this->edge->isReady();
}

void PanelsInitializer::startMastering()
{
    LNBus.end();
    LNBus.begin();
}

PanelsInitializer LNPanelsInitializer;
