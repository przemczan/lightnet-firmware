#include "Edge.hpp"

Edge::Edge(Panel *_panel, Edge *_connectedEdge, uint8_t _index) :
    index(_index),
    connectedEdge(_connectedEdge),
    panel(_panel)
{
}

Edge::Edge(Panel *_panel, uint8_t _index) :
    index(_index),
    connectedEdge(NULL),
    panel(_panel)
{
}
