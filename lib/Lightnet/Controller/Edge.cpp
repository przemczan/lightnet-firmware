#include "Edge.hpp"

Edge::Edge(Panel *_panel, Edge *_connectedEdge, uint8_t _index):
    panel(_panel),
    connectedEdge(_connectedEdge),
    index(_index)
{
}

Edge::Edge(Panel *_panel, uint8_t _index):
    panel(_panel),
    connectedEdge(NULL),
    index(_index)
{
}
