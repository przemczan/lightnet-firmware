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

    this->state = STATE_BOOTING;
}

void PanelsInitializer::doInitialize()
{
    switch (this->state) {
        case STATE_BOOTING:
            this->boot();
            break;

        case STATE_BOOTING_FINISHED:
            this->endBoot();
            break;

        case STATE_READY:
            break;
    }
}

void PanelsInitializer::boot()
{
    this->pingEdge->boot();

    if (this->pingEdge->getState() == LightnetPanelEdge::STATE_BOOTING) {
        if (millis() > this->nextPolling) {
            this->poll();

            this->nextPolling = millis() + POLL_INTERVAL_US;
        }
    }

    if (this->pingEdge->isReady()) {
        this->state = STATE_BOOTING_FINISHED;
    }
}

void PanelsInitializer::endBoot()
{
    LNBus.end();

    detachInterrupt(digitalPinToInterrupt(this->config.intPinNo));
    LNBus.setOnPacketReceived(PanelsInitializer::onPacketReceivedService);
    LNBus.setOnPacketRequested(PanelsInitializer::onPacketRequestedService);

    LNBus.begin(this->config.sdaPinNo, this->config.sclPinNo, Protocol::POLLING_ADDRESS);
    this->state = STATE_READY;
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

    uint8_t error = LNBus.sendPacket(
        Protocol::POLLING_ADDRESS,
        &pollPacket,
        sizeof(pollPacket),
        Protocol::PACKET_INITIALIZATION_POLL
    );

    if (error) {
        PRINTKV("[POLL] error", error);
        return;
    }

    uint8_t size = LNBus.requestData(
        Protocol::POLLING_ADDRESS,
        this->pollBuffer,
        sizeof(Protocol::PacketRegisterEdge)
    );

    if (!size) {
        PRINTLN("[POLL] no data");
        return;
    }

    if (Protocol::validatePacket(this->pollBuffer, size) == 0) {
        this->onPacketResponded((Protocol::PacketMeta *)this->pollBuffer);
    }
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
        Protocol::PACKET_REGISTER_EDGE_ACK
    );
}

List<Panel *> * PanelsInitializer::getPanels()
{
    return this->panels;
}

void PanelsInitializer::onPacketReceived(Protocol::PacketMeta *packet, int size)
{
    Panel *panel = NULL;
    Edge *edge = NULL;

    PRINTKV("[DRIVER] packet", packet->header.type);

    switch (packet->header.type) {
        case Protocol::PACKET_GET_FIRST_PANEL_EDGE_INFO:
            this->nextPanelToSend = 0;
            this->nextPanelEdgeToSend = 0;
            break;

        case Protocol::PACKET_GET_NEXT_PANEL_EDGE_INFO:
            panel = this->panels->get(this->nextPanelToSend);

            if (panel) {
                edge = panel->edges->get(++this->nextPanelEdgeToSend);
            }

            if (!edge) {
                this->nextPanelToSend++;
                this->nextPanelEdgeToSend = 0;
            }
            break;
    }
}

void PanelsInitializer::onPacketRequested()
{
    PRINTLN("[DRIVER] request");

    Panel *panel = this->panels->get(this->nextPanelToSend);

    if (!panel) {
        return this->respondWithEmtyPanelInfo();
    }

    Edge *edge = panel->edges->get(this->nextPanelEdgeToSend);

    if (!edge) {
        return this->respondWithEmtyPanelInfo();
    }

    PRINTLN("[DRIVER] responding with panel info");

    Protocol::PacketPanelEdgeInfo panelInfo;

    panelInfo.panelIndex = panel->index;
    panelInfo.edgeIndex = edge->index;
    panelInfo.connectedPanelIndex = edge->connectedEdge ? edge->connectedEdge->panel->index : 0;

    LNBus.sendResponsePacket(&panelInfo, sizeof(panelInfo), Protocol::PACKET_PANEL_EDGE_INFO);
}

void PanelsInitializer::respondWithEmtyPanelInfo()
{
    PRINTLN("[DRIVER] responding with empty info");

    Protocol::PacketPanelEdgeInfo panelInfo;

    panelInfo.panelIndex = 0;
    panelInfo.edgeIndex = 0;
    panelInfo.connectedPanelIndex = 0;

    LNBus.sendResponsePacket(&panelInfo, sizeof(panelInfo), Protocol::PACKET_PANEL_EDGE_INFO);
}

void PanelsInitializer::onInterrupt()
{
    LNPanelsInitializer.updateEdgeState();
}

void PanelsInitializer::onPacketReceivedService(Protocol::PacketMeta *packet, int size)
{
    LNPanelsInitializer.onPacketReceived(packet, size);
}

void PanelsInitializer::onPacketRequestedService()
{
    LNPanelsInitializer.onPacketRequested();
}

bool PanelsInitializer::isReady()
{
    return this->state == STATE_READY;
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
