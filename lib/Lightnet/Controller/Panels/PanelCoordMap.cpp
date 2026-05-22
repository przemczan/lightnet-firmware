#include "PanelCoordMap.hpp"
#include "Arduino.h"

namespace Lightnet {
    PanelCoordMap::PanelCoordMap(uint8_t _maxEntries)
        : count(0), maxEntries(_maxEntries)
    {
        coords = new PanelCoord[maxEntries];
        memset(coords, 0, sizeof(PanelCoord) * maxEntries);
    }

    PanelCoordMap::~PanelCoordMap()
    {
        delete[] coords;
    }

    void PanelCoordMap::setCoord(uint8_t panelIndex, int16_t x, int16_t y)
    {
        int idx = findIndex(panelIndex);

        if (idx >= 0) {
            // Update existing
            coords[idx].x = x;
            coords[idx].y = y;
        } else if (count < maxEntries) {
            // Add new
            coords[count].panelIndex = panelIndex;
            coords[count].x = x;
            coords[count].y = y;
            count++;
        }
    }

    bool PanelCoordMap::getCoord(uint8_t panelIndex, int16_t& x, int16_t& y) const
    {
        int idx = findIndex(panelIndex);

        if (idx < 0) {
            return false;
        }

        x = coords[idx].x;
        y = coords[idx].y;

        return true;
    }

    int PanelCoordMap::findIndex(uint8_t panelIndex) const
    {
        for (uint8_t i = 0; i < count; i++) {
            if (coords[i].panelIndex == panelIndex) {
                return i;
            }
        }

        return -1;
    }
}  // namespace Lightnet
