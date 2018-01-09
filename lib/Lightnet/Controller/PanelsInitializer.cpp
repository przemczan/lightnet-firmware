#include "PanelsInitializer.hpp"

void PanelsInitializer::start()
{
    LNBus.begin(Protocol::CONTROLLER_ADDRESS);
    LNBus.setOnPacketRequested((LightnetBus::onPacketRequested_t)&PanelsInitializer::onPacketRequested);
    LNBus.setOnPacketReceived((LightnetBus::onPacketReceived_t)&PanelsInitializer::onPacketReceived);
}

void PanelsInitializer::doInitialize()
{
}

void PanelsInitializer::onPacketReceived(Protocol::PacketMeta *packet)
{
}

void PanelsInitializer::onPacketRequested()
{
}

List<PanelsInitializer::Panel *> *PanelsInitializer::getPanels()
{
    return &this->panels;
}

PanelsInitializer LNPanelsInitializer;
