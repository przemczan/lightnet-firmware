#include "PanelsInitializer.hpp"

PanelsInitializer::PanelsInitializer()
{
    this->pollBuffer = (uint8_t *)malloc(POLL_BUFFER_SIZE);
    this->nextPolling = micros();
    this->panels = new List<Panel *>();
}

PanelsInitializer::~PanelsInitializer()
{
    free(this->pollBuffer);
}

void PanelsInitializer::start(uint8_t edgePinNo)
{
    LNBus.begin();

    this->pingEdge = new LightnetPanelEdge(edgePinNo);

    lastActiveEdge = NULL;
    lastPacketType = 0;
}

void PanelsInitializer::doInitialize()
{
    this->pingEdge->boot();

    if (micros() > this->nextPolling) {
        this->poll();

        this->nextPolling = micros() + POLL_INTERVAL_US;
    }
}

void PanelsInitializer::updateEdgeState()
{
    this->pingEdge->readBusState();
}

void PanelsInitializer::poll()
{
    Protocol::PacketInitializationPoll pollPacket;

    pollPacket.panelIndex= this->lastActiveEdge ? this->lastActiveEdge->panel->index : 0;

    uint8_t error = LNBus.sendPacket(
        Protocol::POLLING_ADDRESS,
        &pollPacket,
        sizeof(pollPacket),
        Protocol::PACKET_INITIALIZATION_POLL
    );

    if (error) {
        return;
    }

    uint8_t size = LNBus.requestData(
        Protocol::POLLING_ADDRESS,
        this->pollBuffer,
        PanelsInitializer::POLL_BUFFER_SIZE
    );

    if (!size) {
        return;
    }

    if (Protocol::validatePacket(this->pollBuffer, size) == 0) {
        this->onPacketReceived((Protocol::PacketMeta *)this->pollBuffer);
    }
}

void PanelsInitializer::registerEdge(Protocol::PacketRegisterEdge *packet)
{
    Panel *panel = panels->get(packet->panelIndex);

    if (!panel) {
        return this->registerPanel(packet);
    }

    Edge *edge = new Edge(panel, packet->edgeIndex);

    panel->edges->push(edge);

    this->lastActiveEdge = edge;
}

void PanelsInitializer::registerPanel(Protocol::PacketRegisterEdge *packet)
{
    Panel *panel = new Panel(packet->panelIndex);
    Edge *edge = new Edge(panel, this->lastActiveEdge, packet->edgeIndex);

    panel->edges->push(edge);
    this->panels->push(panel);

    if (this->lastActiveEdge) {
        this->lastActiveEdge->connectedEdge = edge;
    }
}

void PanelsInitializer::onPacketReceived(Protocol::PacketMeta *packetMeta)
{
    this->lastPacketType = packetMeta->header.type;

    if (this->isReady()) {
    } else {
        switch (this->lastPacketType)
        {
            case Protocol::PACKET_REGISTER_EDGE:
                LNPanelsInitializer.registerEdge((Protocol::PacketRegisterEdge *)packetMeta);
                break;
        }
    }
}

uint8_t PanelsInitializer::isReady()
{
    return this->pingEdge->isReady();
}

List<Panel *> * PanelsInitializer::getPanels()
{
    return this->panels;
}

PanelsInitializer LNPanelsInitializer;
