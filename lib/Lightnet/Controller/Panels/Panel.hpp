#pragma once

#include <Arduino.h>
#include "Edge.hpp"
#include "../../Utils/List.hpp"
#include "../../Core/Common/LightnetConfig.hpp"

class Panel
{
    public:
        Lightnet::PanelIndex index;
        List<Edge *> *edges;

        Panel(Lightnet::PanelIndex _index);

        ~Panel();
};
