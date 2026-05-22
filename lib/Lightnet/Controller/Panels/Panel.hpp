#pragma once

#include <Arduino.h>
#include "Edge.hpp"
#include "../../Utils/List.hpp"

class Panel
{
    public:
        uint8_t index;
        List<Edge *> *edges;

        Panel(uint8_t _index);

        ~Panel();
};
