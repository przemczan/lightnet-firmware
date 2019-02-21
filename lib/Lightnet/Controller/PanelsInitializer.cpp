#include "PanelsInitializer.hpp"

PanelsInitializer::PanelsInitializer()
{
    this->pollBuffer = (uint8_t *)malloc(POLL_BUFFER_SIZE);
    this->nextPolling = millis();
    this->panels = new List<Panel *>();
}

PanelsInitializer::~PanelsInitializer()
{
    free(this->pollBuffer);

    if (this->pingEdge) {
        delete this->pingEdge;
    }

    delete this->panels;
}

void PanelsInitializer::configure(configuration_t config)
{
    this->config = config;
}

void PanelsInitializer::start()
{
    pinMode(this->config.intPinNo, INPUT);
    attachInterrupt(digitalPinToInterrupt(this->config.intPinNo), PanelsInitializer::onInterrupt, CHANGE);

    this->pingEdge = new LightnetPanelEdge(this->config.edgePinNo);
    this->pingEdge->setBootTimeout(BOOT_TIMEOUT_MS);

    LNBus.begin(this->config.sdaPinNo, this->config.sclPinNo);

    this->lastActiveEdge = NULL;
    this->lastPacketType = 0;
}

void PanelsInitializer::boot()
{
    if (this->pingEdge->isReady()) {
        return;
    }

    this->pingEdge->boot();

    if (this->pingEdge->getState() == LightnetPanelEdge::STATE_BOOTING) {
        if (millis() > this->nextPolling) {
            this->poll();

            this->nextPolling = millis() + POLL_INTERVAL_US;
        }
    }
}

void PanelsInitializer::updateEdgeState()
{
    this->pingEdge->readBusState();
}

void PanelsInitializer::poll()
{
    Protocol::PacketInitializationPoll pollPacket;

    pollPacket.panelIndex = this->currentPanelIndex;
    PRINTKV("[POLL] polling panel", pollPacket.panelIndex);

    uint8_t error = LNBus.sendPacketWithResponse(
        Protocol::POLLING_ADDRESS,
        &pollPacket,
        sizeof(pollPacket),
        Protocol::PACKET_INITIALIZATION_POLL,
        this->pollBuffer,
        sizeof(Protocol::PacketRegisterEdge)
    );

    if (error) {
        PRINTKV("[POLL] error", error);
        return;
    }

    this->onPacketResponded((Protocol::PacketMeta *)this->pollBuffer);
}

void PanelsInitializer::registerEdge(Protocol::PacketRegisterEdge *packet)
{
    Panel *panel = this->getPanelByIndex(packet->panelIndex);

    if (!panel) {
        return this->registerPanel(packet);
    }

    PRINTLN3("[REGISTER] edge", packet->panelIndex, packet->edgeIndex);

    Edge *edge = new Edge(panel, packet->edgeIndex);

    panel->edges->push(edge);

    this->lastActiveEdge = edge;
}

void PanelsInitializer::registerPanel(Protocol::PacketRegisterEdge *packet)
{
    if (!packet->panelIndex) {
      PRINTLN("[ERROR] Got panel with index = 0.");
    }
    PRINTLN3("[REGISTER] panel", packet->panelIndex, packet->edgeIndex);

    Panel *panel = new Panel(packet->panelIndex);
    Edge *edge = new Edge(panel, this->lastActiveEdge, packet->edgeIndex);

    panel->edges->push(edge);
    this->panels->push(panel);

    if (this->lastActiveEdge) {
        this->lastActiveEdge->connectedEdge = edge;
    }

    this->lastActiveEdge = edge;
    this->currentPanelIndex++;
}

void PanelsInitializer::onPacketResponded(Protocol::PacketMeta *packetMeta)
{
    this->lastPacketType = packetMeta->header.type;

    switch (this->lastPacketType)
    {
        case Protocol::PACKET_REGISTER_EDGE:
            this->registerEdge((Protocol::PacketRegisterEdge *)packetMeta);
            this->sendRegisterAck();
            break;
    }
}

void PanelsInitializer::sendRegisterAck()
{
    Protocol::PacketMeta ackPacket;
    LNBus.sendPacket(
        Protocol::POLLING_ADDRESS,
        &ackPacket,
        sizeof(ackPacket),
        Protocol::PACKET_REGISTER_EDGE_ACK,
        true
    );
}

List<Panel *> * PanelsInitializer::getPanels()
{
    return this->panels;
}

void PanelsInitializer::onInterrupt()
{
    LNPanelsInitializer.updateEdgeState();
}

bool PanelsInitializer::isReady()
{
    return this->pingEdge->isReady();
}

Panel *PanelsInitializer::getPanelByIndex(uint16_t panelIndex)
{
    uint16_t index = this->panels->getSize();
    while (index--) {
        if (this->panels->get(index)->index == panelIndex) {
            return this->panels->get(index);
        }
    }

    return NULL;
}

PanelsInitializer LNPanelsInitializer;
