#ifdef SIM_MODE

#include "../Controller/Panels/PanelsInitializer.hpp"
#include "../Controller/Panels/Panel.hpp"
#include "../Controller/Panels/Edge.hpp"
#include "../Controller/Topology/PanelGraph.hpp"
#include "../Controller/Topology/PanelGeometry.hpp"
#include "../Utils/List.hpp"
#include <Arduino.h>

#ifndef SIM_PANELS_COUNT
    #define SIM_PANELS_COUNT 4
#endif

#define SIM_EDGES_PER_PANEL 3

static bool simReady = false;

// One candidate attachment point for the next panel: connect its `childEdge` connector to
// the `parentEdge` connector of the already-placed panel at `parentSlot`.
struct SimPlacementCandidate {
    uint8_t parentSlot, parentEdge, childEdge;
};

PanelsInitializer::PanelsInitializer()
    : lastActiveEdge(nullptr), lastPacketType(0), pingEdge(nullptr),
    pullBuffer(nullptr), nextPulling(0), currentPanelIndex(1),
    interruptPinNo(0), nextPanelToSend(0), nextPanelEdgeToSend(0)
{
    panels = new List<Panel *>();
}

PanelsInitializer::~PanelsInitializer()
{
    for (uint16_t i = 0; i < panels->getSize(); i++) {
        delete panels->get(i);
    }

    delete panels;
}

void PanelsInitializer::configure(configuration_t)
{
}

void PanelsInitializer::start()
{
    for (uint8_t i = 1; i <= SIM_PANELS_COUNT; i++) {
        Panel *p = new Panel(i);

        for (uint8_t e = 0; e < SIM_EDGES_PER_PANEL; e++) {
            p->edges->push(new Edge(p, e));
        }

        panels->push(p);
    }

    // Build a random spanning tree, attaching panels one at a time. Each new panel's
    // (parent, parent-connector, own-connector) is chosen — among all still-free
    // combinations, in random order — by checking via PanelGeometry (the exact layout math
    // the controller renders with) that its polygon doesn't overlap any already-placed one.
    // Already-placed panels never move (new panels only attach as leaves, and the layout
    // anchor is the lowest panel index = panel 1), so a placement validated here stays valid
    // for the rest of the build. `static`: PanelGraph/PanelGeometry hold ~100-panel-sized
    // arrays — too large for the ESP8266 stack (see CLAUDE.md "ESP8266 heap not stack");
    // start() runs once at boot.
    randomSeed(micros());

    using namespace Lightnet;

    static uint8_t indices[SIM_PANELS_COUNT];
    static uint8_t edgeCounts[SIM_PANELS_COUNT];
    static TopoLink links[SIM_PANELS_COUNT];
    static bool edgeUsed[SIM_PANELS_COUNT][SIM_EDGES_PER_PANEL];
    static SimPlacementCandidate cands[SIM_PANELS_COUNT * SIM_EDGES_PER_PANEL * SIM_EDGES_PER_PANEL];
    static PanelGraph graph;
    static PanelGeometry geometry;

    for (uint8_t s = 0; s < SIM_PANELS_COUNT; s++) {
        indices[s]    = (uint8_t)(s + 1);
        edgeCounts[s] = SIM_EDGES_PER_PANEL;

        for (uint8_t e = 0; e < SIM_EDGES_PER_PANEL; e++) edgeUsed[s][e] = false;
    }

    uint8_t linkCount = 0;

    for (uint8_t i = 1; i < SIM_PANELS_COUNT; i++) {
        uint8_t nc = 0;

        for (uint8_t j = 0; j < i; j++) {
            for (uint8_t pe = 0; pe < SIM_EDGES_PER_PANEL; pe++) {
                if (edgeUsed[j][pe]) continue;

                for (uint8_t ce = 0; ce < SIM_EDGES_PER_PANEL; ce++) {
                    cands[nc++] = SimPlacementCandidate{ j, pe, ce };
                }
            }
        }

        // Fisher-Yates shuffle so the search doesn't always favour the same parent/orientation.
        for (uint8_t k = nc; k > 1; k--) {
            uint8_t r = (uint8_t)random(0, k);
            SimPlacementCandidate tmp = cands[k - 1];

            cands[k - 1] = cands[r];
            cands[r]     = tmp;
        }

        int16_t accepted = -1;

        for (uint8_t c = 0; c < nc; c++) {
            links[linkCount] = TopoLink{
                indices[cands[c].parentSlot], cands[c].parentEdge,
                indices[i], cands[c].childEdge
            };

            graph.build(indices, (uint8_t)(i + 1), links, (uint8_t)(linkCount + 1));
            geometry.build(graph, edgeCounts, 0);

            float newVx[SIM_EDGES_PER_PANEL], newVy[SIM_EDGES_PER_PANEL];
            uint8_t newN;

            geometry.worldVertsOf(indices[i], newVx, newVy, newN);

            bool overlap = false;

            for (uint8_t s = 0; s < i && !overlap; s++) {
                float vx[SIM_EDGES_PER_PANEL], vy[SIM_EDGES_PER_PANEL];
                uint8_t n;

                geometry.worldVertsOf(indices[s], vx, vy, n);
                overlap = convexPolygonsOverlap(newVx, newVy, newN, vx, vy, n);
            }

            if (!overlap) {
                accepted = (int16_t)c;
                break;
            }
        }

        if (accepted < 0) {
            // Best effort: every orientation collided (shouldn't happen — triangles leave
            // plenty of free parent/edge/orientation combinations). Keep the last one tried.
            accepted = (int16_t)(nc - 1);
            Serial.printf("[SIM] panel %u: no overlap-free placement found, using closest fit\n", indices[i]);
        }

        edgeUsed[cands[accepted].parentSlot][cands[accepted].parentEdge] = true;
        edgeUsed[i][cands[accepted].childEdge]                           = true;
        linkCount++;
    }

    for (uint8_t k = 0; k < linkCount; k++) {
        Panel *parent = getPanelByIndex(links[k].panelA);
        Panel *child  = getPanelByIndex(links[k].panelB);

        Edge *pe = parent->edges->get(links[k].edgeA);
        Edge *ce = child->edges->get(links[k].edgeB);

        pe->connectedEdge = ce;
        ce->connectedEdge = pe;
    }

    simReady = true;
    Serial.printf("[SIM] %u virtual panels registered, %u edges each\n", SIM_PANELS_COUNT, SIM_EDGES_PER_PANEL);
}

void PanelsInitializer::boot()
{
}

bool PanelsInitializer::isFinished()
{
    return simReady;
}

void PanelsInitializer::updateEdgeState()
{
}

List<Panel *> * PanelsInitializer::getPanels()
{
    return panels;
}

Panel * PanelsInitializer::getPanelByIndex(uint16_t index)
{
    for (uint16_t i = 0; i < panels->getSize(); i++) {
        if (panels->get(i)->index == index) return panels->get(i);
    }

    return nullptr;
}

// Private stubs — never called in sim mode
void PanelsInitializer::registerPanel(Protocol::PacketRegisterEdge *)
{
}

void PanelsInitializer::registerEdge(Protocol::PacketRegisterEdge *)
{
}

void PanelsInitializer::pull()
{
}

void PanelsInitializer::onPacketResponded(Protocol::PacketMeta *)
{
}

void PanelsInitializer::sendRegisterAck()
{
}

void PanelsInitializer::onInterrupt()
{
}

PanelsInitializer LNPanelsInitializer;

#endif  // SIM_MODE
